"""
Script lerobot to h5.
# --repo-id     Your unique repo ID on Hugging Face Hub
# --output_dir  Save path to h5 file

python unitree_lerobot/utils/convert_lerobot_to_h5.py \
    --repo-id 401_g1_robot_shortest_150/bag_lerobot \
    --root /home/jiang/datasets/unitree_vla/lerobot2.0_format_dataset/401_g1_robot_shortest_150/bag_lerobot \
    --output_dir /home/jiang/datasets/unitree_vla/hdf5_format_dataset/filter_401_g1/bag
"""

import os
import cv2
import h5py
import json
import av
import argparse
import numpy as np
import pandas as pd
from tqdm import tqdm
from pathlib import Path
from collections import defaultdict
from lerobot.datasets.lerobot_dataset import LeRobotDataset


class LeRobotDataProcessor:
    DEX3_IMAGE_KEYS = [
        "observation.images.cam_left_high",
        "observation.images.cam_right_high",
        "observation.images.cam_left_wrist",
        "observation.images.cam_right_wrist",
    ]

    def __init__(
        self,
        repo_id: str,
        root: str = None,
        image_dtype: str = "to_unit8",
        mode: str = "g1",
        tolerance_s: float = 1e-4,
        video_backend: str = "pyav",
    ) -> None:
        self.image_dtype = image_dtype
        self.mode = mode
        self.root = Path(root) if root is not None else None
        if self.mode == "dex3":
            self._load_dex3_local_dataset()
            return

        self.dataset = LeRobotDataset(
            repo_id=repo_id,
            root=root,
            video_backend=video_backend,
            tolerance_s=tolerance_s,
        )
        self.num_episodes = self.dataset.num_episodes

    def _load_dex3_local_dataset(self):
        if self.root is None:
            raise ValueError("Dex3 conversion requires --data_path pointing to a local LeRobot dataset.")

        info_path = self.root / "meta" / "info.json"
        episodes_path = self.root / "meta" / "episodes.jsonl"
        tasks_path = self.root / "meta" / "tasks.jsonl"
        if not info_path.exists() or not episodes_path.exists() or not tasks_path.exists():
            raise FileNotFoundError(f"Missing Dex3 metadata under {self.root / 'meta'}")

        with open(info_path, "r") as f:
            self.dex3_info = json.load(f)

        self.dex3_tasks = {}
        with open(tasks_path, "r") as f:
            for line in f:
                if line.strip():
                    item = json.loads(line)
                    self.dex3_tasks[int(item["task_index"])] = item["task"]

        self.dex3_episodes = []
        with open(episodes_path, "r") as f:
            for line in f:
                if line.strip():
                    self.dex3_episodes.append(json.loads(line))
        self.num_episodes = len(self.dex3_episodes)

        parquet_paths = sorted((self.root / "data").glob("**/episode_*.parquet"))
        if not parquet_paths:
            parquet_paths = sorted((self.root / "data").glob("**/*.parquet"))
        if not parquet_paths:
            raise FileNotFoundError(f"No parquet files found under {self.root / 'data'}")
        self.dex3_df = pd.concat((pd.read_parquet(path) for path in parquet_paths), ignore_index=True)
        self.dex3_groups = {
            int(ep_idx): group.sort_values("frame_index")
            for ep_idx, group in self.dex3_df.groupby("episode_index", sort=True)
        }

    @staticmethod
    def _to_numpy(value):
        if hasattr(value, "detach"):
            value = value.detach().cpu()
        if hasattr(value, "numpy"):
            value = value.numpy()
        return np.asarray(value)

    def _format_image(self, value):
        value = self._to_numpy(value)
        if value.ndim == 3 and value.shape[0] in (1, 3, 4):
            value = np.transpose(value, (1, 2, 0))
        if value.dtype != np.uint8:
            if np.issubdtype(value.dtype, np.floating) and np.max(value) <= 1.0:
                value = value * 255
            value = np.clip(value, 0, 255).astype(np.uint8)

        if self.image_dtype == "to_unit8":
            return value
        if self.image_dtype == "to_bytes":
            success, encoded_img = cv2.imencode(".jpg", value, [cv2.IMWRITE_JPEG_QUALITY, 100])
            if not success:
                raise ValueError("Image encoding failed")
            return np.void(encoded_img.tobytes())
        raise ValueError(f"Unsupported image dtype: {self.image_dtype}")

    def _dex3_video_path(self, camera_key: str, episode_index: int) -> Path:
        chunk_size = int(self.dex3_info.get("chunks_size", 1000))
        chunk_idx = episode_index // chunk_size
        path = self.root / "videos" / f"chunk-{chunk_idx:03d}" / camera_key / f"episode_{episode_index:06d}.mp4"
        if not path.exists():
            raise FileNotFoundError(f"Missing Dex3 video file: {path}")
        return path

    def _decode_video_rgb(self, video_path: Path) -> list[np.ndarray]:
        frames = []
        with av.open(str(video_path)) as container:
            for frame in container.decode(video=0):
                frames.append(frame.to_ndarray(format="rgb24"))
        if not frames:
            raise ValueError(f"No frames decoded from {video_path}")
        return frames

    def _process_dex3_episode(self, episode_index: int) -> dict:
        if episode_index not in self.dex3_groups:
            raise KeyError(f"Episode {episode_index} not found in Dex3 parquet data")

        rows = self.dex3_groups[episode_index]
        states = np.stack(rows["observation.state"].map(lambda x: np.asarray(x, dtype=np.float32)).to_numpy())
        actions = np.stack(rows["action"].map(lambda x: np.asarray(x, dtype=np.float32)).to_numpy())
        if states.shape[-1] != 28 or actions.shape[-1] != 28:
            raise ValueError(f"Dex3 expects 28D state/action, got state={states.shape}, action={actions.shape}")

        episode_length = actions.shape[0]
        cameras = {}
        for camera_key in self.DEX3_IMAGE_KEYS:
            camera_name = camera_key.split(".")[-1]
            frames = self._decode_video_rgb(self._dex3_video_path(camera_key, episode_index))
            if len(frames) < episode_length:
                raise ValueError(
                    f"Video {camera_key} episode {episode_index} has {len(frames)} frames, "
                    f"but parquet has {episode_length} rows"
                )
            cameras[camera_name] = np.asarray(frames[:episode_length], dtype=np.uint8)

        task_index = int(rows["task_index"].iloc[0])
        task = self.dex3_tasks.get(task_index, "")
        cam_height, cam_width = next(iter(cameras.values())).shape[1:3]

        return {
            "state": states,
            "action": actions,
            "cameras": cameras,
            "task": task,
            "episode_length": episode_length,
            "episode_index": episode_index,
            "data_cfg": {
                "camera_names": list(cameras.keys()),
                "cam_height": cam_height,
                "cam_width": cam_width,
                "state_dim": states.shape[-1],
                "action_dim": actions.shape[-1],
            },
        }

    def process_episode(self, episode_index: int) -> dict:
        """Process a single episode to extract camera images, state, and action."""
        if self.mode == "dex3":
            return self._process_dex3_episode(episode_index)

        from_idx = self.dataset.episode_data_index["from"][episode_index].item()
        to_idx = self.dataset.episode_data_index["to"][episode_index].item()

        episode = defaultdict(list)
        cameras = defaultdict(list)
        task = ""

        for step_idx in tqdm(
            range(from_idx, to_idx), desc=f"Episode {episode_index}", position=1, leave=False, dynamic_ncols=True
        ):

            step = self.dataset[step_idx]
            if self.mode == "dex3":
                image_dict = {
                    key.split(".")[-1]: self._format_image(step[key])
                    for key in self.DEX3_IMAGE_KEYS
                    if key in step
                }
                missing = {key.split(".")[-1] for key in self.DEX3_IMAGE_KEYS} - set(image_dict)
                if missing:
                    raise KeyError(f"Episode {episode_index} step {step_idx} is missing Dex3 cameras: {sorted(missing)}")

                for key, value in image_dict.items():
                    cameras[key].append(value)

                state = self._to_numpy(step["observation.state"]).astype(np.float32)
                action = self._to_numpy(step["action"]).astype(np.float32)
                if state.shape[-1] != 28 or action.shape[-1] != 28:
                    raise ValueError(
                        f"Dex3 expects 28D state/action, got state={state.shape}, action={action.shape}"
                    )

                episode["state"].append(state)
                episode["action"].append(action)
                task = step.get("task", task)
                continue

            image_dict = {
                key.split(".")[2]: np.transpose(
                    (value.numpy() * 255).astype(np.uint8), (1, 2, 0)
                )
                for key, value in step.items()
                if key.startswith("observation.image") and len(key.split(".")) >= 3
            }


            for key, value in image_dict.items():
                if self.image_dtype == "to_unit8":
                    cameras[key].append(value)
                elif self.image_dtype == "to_bytes":
                    success, encoded_img = cv2.imencode(".jpg", value, [cv2.IMWRITE_JPEG_QUALITY, 100])
                    if not success:
                        raise ValueError(f"Image encoding failed for key: {key}")
                    cameras[key].append(np.void(encoded_img.tobytes()))

            cam_height, cam_width = next(iter(image_dict.values())).shape[:2]

            obs_left_gripper = step['observation.left_gripper'].unsqueeze(0)
            obs_right_gripper = step['observation.right_gripper'].unsqueeze(0)
            action_left_gripper = step['action.left_gripper'].unsqueeze(0)
            action_right_gripper = step['action.right_gripper'].unsqueeze(0)


            state_list = [step['observation.left_arm'], step['observation.right_arm'], obs_right_gripper, obs_left_gripper, step['observation.body'][12:15]]
            state = np.concatenate(state_list)

            ee_state_list = [step['observation.left_ee'], step['observation.right_ee'], obs_right_gripper, obs_left_gripper, step['observation.body'][12:15]]
            ee_state = np.concatenate(ee_state_list)

            action_list = [step['action.left_arm'], step['action.right_arm'], action_right_gripper, action_left_gripper, step['action.body'][3:6]]
            action = np.concatenate(action_list)

            ee_action_list = [step['action.left_ee'], step['action.right_ee'], action_right_gripper, action_left_gripper, step['action.body'][3:6]]
            ee_action = np.concatenate(ee_action_list)
            
            episode["state"].append(state)
            episode["action"].append(action)
            episode["ee_state"].append(ee_state)
            episode['ee_action'].append(ee_action)

        episode["cameras"] = cameras
        episode["task"] = task if self.mode == "dex3" else step["task"]
        episode["episode_length"] = to_idx - from_idx

        # Data configuration for later use
        if self.mode == "dex3":
            cam_height, cam_width = next(iter(cameras.values()))[0].shape[:2]
            episode["data_cfg"] = {
                "camera_names": list(cameras.keys()),
                "cam_height": cam_height,
                "cam_width": cam_width,
                "state_dim": np.squeeze(np.asarray(episode["state"][0]).shape),
                "action_dim": np.squeeze(np.asarray(episode["action"][0]).shape),
            }
        else:
            episode["data_cfg"] = {
                "camera_names": list(image_dict.keys()),
                "cam_height": cam_height,
                "cam_width": cam_width,
                "state_dim": np.squeeze(state.shape),
                "ee_state_dim": np.squeeze(ee_state.shape),
                "action_dim": np.squeeze(action.shape),
                "ee_action_dim": np.squeeze(ee_action.shape),
            }
        episode["episode_index"] = episode_index

        return episode


class H5Writer:
    def __init__(self, output_dir: Path, compression: str = "gzip", gzip_level: int = 1) -> None:
        self.output_dir = output_dir
        self.compression = compression
        self.gzip_level = gzip_level
        os.makedirs(output_dir, exist_ok=True)

    def _compression_kwargs(self):
        if self.compression == "none":
            return {}
        if self.compression == "gzip":
            return {"compression": "gzip", "compression_opts": self.gzip_level}
        return {"compression": self.compression}

    def write_to_h5(self, episode: dict) -> None:
        """Write episode data to HDF5 file."""

        episode_length = episode["episode_length"]
        episode_index = episode["episode_index"]
        state = episode["state"]
        action = episode["action"]
        qvel = np.zeros_like(episode["state"])
        cameras = episode["cameras"]
        task = episode["task"]
        data_cfg = episode["data_cfg"]

        # Prepare data dictionary
        data_dict = {
            "/observations/qpos": np.asarray(state),
            "/observations/qvel": np.asarray(qvel),
            "/action": np.asarray(action),
            **{f"/observations/images/{k}": np.asarray(v) for k, v in cameras.items()},
        }
        if "ee_state" in episode:
            data_dict["/observations/ee_qpos"] = np.asarray(episode["ee_state"])
        if "ee_action" in episode:
            data_dict["ee_action"] = np.asarray(episode["ee_action"])

        h5_path = os.path.join(self.output_dir, f"episode_{episode_index}.hdf5")

        with h5py.File(h5_path, "w", rdcc_nbytes=1024**2 * 2, libver="latest") as root:
            # Set attributes
            root.attrs["sim"] = False

            # Create datasets
            obs = root.create_group("observations")
            image = obs.create_group("images")
            compression_kwargs = self._compression_kwargs()

            # Write camera images
            for cam_name, images in cameras.items():
                image.create_dataset(
                    cam_name,
                    shape=(episode_length, data_cfg["cam_height"], data_cfg["cam_width"], 3),
                    dtype="uint8",
                    chunks=(1, data_cfg["cam_height"], data_cfg["cam_width"], 3),
                    **compression_kwargs,
                )
                # root[f'/observations/images/{cam_name}'][...] = images

            # Write state and action data
            obs.create_dataset("qpos", (episode_length, data_cfg["state_dim"]), dtype="float32", **compression_kwargs)
            obs.create_dataset("qvel", (episode_length, data_cfg["state_dim"]), dtype="float32", **compression_kwargs)
            root.create_dataset("action", (episode_length, data_cfg["action_dim"]), dtype="float32", **compression_kwargs)
            if "ee_state_dim" in data_cfg:
                obs.create_dataset("ee_qpos", (episode_length, data_cfg["ee_state_dim"]), dtype="float32", **compression_kwargs)
            if "ee_action_dim" in data_cfg:
                root.create_dataset("ee_action", (episode_length, data_cfg["ee_action_dim"]), dtype="float32", **compression_kwargs)
            # Write metadata
            root.create_dataset("is_edited", (1,), dtype="uint8")
            substep_reasonings = root.create_dataset(
                "substep_reasonings", (episode_length,), dtype=h5py.string_dtype(encoding="utf-8"), compression="gzip"
            )
            root.create_dataset("language_raw", data=task)
            substep_reasonings[:] = [task] * episode_length

            # Write additional data
            for name, array in data_dict.items():
                root[name][...] = array



def lerobot_to_h5(
    repo_id: str,
    output_dir: Path,
    root: str = None,
    mode: str = "g1",
    tolerance_s: float = 1e-4,
    video_backend: str = "pyav",
    start_episode: int = 0,
    max_episodes: int | None = None,
    hdf5_compression: str = "gzip",
    gzip_level: int = 1,
) -> None:
    """Main function to process and write LeRobot data to HDF5 format."""

    # Initialize data processor and H5 writer
    data_processor = LeRobotDataProcessor(
        repo_id,
        root,
        image_dtype="to_unit8",
        mode=mode,
        tolerance_s=tolerance_s,
        video_backend=video_backend,
    )  # image_dtype Options: "to_unit8", "to_bytes"
    h5_writer = H5Writer(output_dir, compression=hdf5_compression, gzip_level=gzip_level)

    # Process each episode
    end_episode = data_processor.num_episodes
    if max_episodes is not None:
        end_episode = min(end_episode, start_episode + max_episodes)
    episode_range = range(start_episode, end_episode)

    for episode_index in tqdm(episode_range, desc="Episodes", position=0, dynamic_ncols=True):
        if os.path.exists(os.path.join(output_dir, f"episode_{episode_index}.hdf5")):
            print(f"Episode {episode_index} already exists")
            continue
        episode = data_processor.process_episode(episode_index)
        h5_writer.write_to_h5(episode)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--data_path", type=str, default="")
    parser.add_argument("--target_path", type=str, default="")
    parser.add_argument("--repo_id", type=str, default=None)
    parser.add_argument("--mode", choices=["g1", "dex3"], default="g1")
    parser.add_argument("--tolerance_s", type=float, default=None)
    parser.add_argument("--video_backend", choices=["pyav", "video_reader", "torchcodec"], default=None)
    parser.add_argument("--start_episode", type=int, default=0)
    parser.add_argument("--max_episodes", type=int, default=None)
    parser.add_argument("--hdf5_compression", choices=["gzip", "lzf", "none"], default=None)
    parser.add_argument("--gzip_level", type=int, default=1)
    args = parser.parse_args()
    repo_id = args.repo_id or os.path.basename(args.data_path)
    tolerance_s = args.tolerance_s if args.tolerance_s is not None else (1000.0 if args.mode == "dex3" else 1e-4)
    video_backend = args.video_backend or "pyav"
    hdf5_compression = args.hdf5_compression or ("lzf" if args.mode == "dex3" else "gzip")
    root_path = args.data_path
    output_dir = args.target_path
    lerobot_to_h5(
        repo_id,
        output_dir,
        root_path,
        mode=args.mode,
        tolerance_s=tolerance_s,
        video_backend=video_backend,
        start_episode=args.start_episode,
        max_episodes=args.max_episodes,
        hdf5_compression=hdf5_compression,
        gzip_level=args.gzip_level,
    )
