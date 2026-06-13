from typing import Iterator, Tuple, Any

import os
import h5py
import glob
import numpy as np
os.environ["CUDA_VISIBLE_DEVICES"] = "-1" 
import tensorflow as tf
import tensorflow_datasets as tfds
import sys
# Add the directory containing this file to sys.path for imports
_current_dir = os.path.dirname(os.path.abspath(__file__))
if _current_dir not in sys.path:
    sys.path.insert(0, _current_dir)
from conversion_utils import MultiThreadedDatasetBuilder

RLDS_SCHEMA = os.environ.get("UNIFOLM_RLDS_SCHEMA", "g1").lower()
HDF5_GLOB = os.environ.get("UNIFOLM_RLDS_HDF5_GLOB", "/path/to/save/the/converted/data/directory/*.hdf5")


def _use_dex3_schema() -> bool:
    return RLDS_SCHEMA == "dex3"


def _read_language(raw_dataset):
    raw_value = raw_dataset[()] if raw_dataset.shape == () else raw_dataset[0]
    if isinstance(raw_value, bytes):
        return raw_value.decode("utf-8")
    return str(raw_value)


def _read_camera_or_zeros(images_group, camera_name: str, reference_images):
    if camera_name in images_group:
        return images_group[camera_name][:]
    return np.zeros_like(reference_images)


def batch_pose17_to_pose23(actions):
    """
    actions: (T, 17)
    output:  (T, 23)
    """
    actions = np.asarray(actions, dtype=float)
    T = actions.shape[0]

    # Split
    L_xyz = actions[:, 0:3]
    L_rpy = actions[:, 3:6]
    R_xyz = actions[:, 6:9]
    R_rpy = actions[:, 9:12]
    waist5 = actions[:, 12:17]

    # Convert RPY→6D (vectorized)
    def rpy_to_6d_batch(rpy):
        roll, pitch, yaw = rpy[:,0], rpy[:,1], rpy[:,2]
        cr, sr = np.cos(roll), np.sin(roll)
        cp, sp = np.cos(pitch), np.sin(pitch)
        cy, sy = np.cos(yaw), np.sin(yaw)

        # Column1
        col1 = np.stack([cy*cp,
                         sy*cp,
                         -sp], axis=1)

        # Column2
        col2 = np.stack([
            cy*sp*sr - sy*cr,
            sy*sp*sr + cy*cr,
            cp*sr
        ], axis=1)

        return np.concatenate([col1, col2], axis=1)

    L_6d = rpy_to_6d_batch(L_rpy)
    R_6d = rpy_to_6d_batch(R_rpy)

    # Output
    return np.concatenate([L_xyz, L_6d, R_xyz, R_6d, waist5], axis=1)

def _generate_examples(paths) -> Iterator[Tuple[str, Any]]:
    """Yields episodes for list of data paths."""
    # the line below needs to be *inside* generate_examples so that each worker creates it's own model
    # creating one shared model outside this function would cause a deadlock

    def _parse_example(episode_path):
        # Load raw data
        with h5py.File(episode_path, "r") as F:

            actions = F['action'][:]
            states = F['observations']["qpos"][:]
            is_dex3_episode = _use_dex3_schema() or (actions.shape[-1] == 28 and states.shape[-1] == 28)
            if not is_dex3_episode and "ee_qpos" in F['observations']:
                ee_states = F['observations']["ee_qpos"][:]
                ee_states_6d = batch_pose17_to_pose23(ee_states)
            if not is_dex3_episode and "ee_action" in F:
                ee_actions = F["ee_action"][:]
                ee_actions_6d = batch_pose17_to_pose23(ee_actions)
            images_group = F['observations']["images"]
            images_left_top = images_group["cam_left_high"][:]
            images_right_top = images_group["cam_right_high"][:]
            images_left_wrist = _read_camera_or_zeros(images_group, "cam_left_wrist", images_left_top)
            images_right_wrist = _read_camera_or_zeros(images_group, "cam_right_wrist", images_right_top)
            
            language_instruction = _read_language(F['language_raw'])

        episode = []
        for i in range(actions.shape[0]):
            step = {
                'observation': {
                    'image_left_top': images_left_top[i],
                    'image_right_top': images_right_top[i],
                    'image_left_wrist': images_left_wrist[i],
                    'image_right_wrist': images_right_wrist[i],
                    'state': np.asarray(states[i], np.float32),
                },
                'action': np.asarray(actions[i], dtype=np.float32),
                'discount': 1.0,
                'is_first': i == 0,
                'is_last': i == (actions.shape[0] - 1),
                'is_terminal': i == (actions.shape[0] - 1),
                'language_instruction': language_instruction,
            }
            if not is_dex3_episode:
                step['observation']['ee_state'] = np.asarray(ee_states[i], np.float32)
                step['observation']['ee_state_6d'] = np.asarray(ee_states_6d[i], np.float32)
                step['ee_action'] = np.asarray(ee_actions[i], dtype=np.float32)
                step['ee_action_6d'] = np.asarray(ee_actions_6d[i], dtype=np.float32)
            episode.append(step)

        # Create output data sample
        sample = {
            'steps': episode,
            'episode_metadata': {
                'file_path': episode_path
            }
        }

        # If you want to skip an example for whatever reason, simply return None
        return episode_path, sample

    # For smallish datasets, use single-thread parsing
    for sample in paths:
        ret = _parse_example(sample)
        yield ret


class rlds_dataset(MultiThreadedDatasetBuilder):
    """DatasetBuilder for example dataset."""

    VERSION = tfds.core.Version('1.0.0')
    RELEASE_NOTES = {
      '1.0.0': 'Initial release.',
    }
    N_WORKERS = 8            # number of parallel workers for data conversion
    MAX_PATHS_IN_MEMORY = 8  # number of paths converted & stored in memory before writing to disk
                               # -> the higher the faster / more parallel conversion, adjust based on avilable RAM
                               # note that one path may yield multiple episodes and adjust accordingly
    PARSE_FCN = _generate_examples      # handle to parse function from file paths to RLDS episodes

    def _info(self) -> tfds.core.DatasetInfo:
        """Dataset metadata (homepage, citation,...)."""
        if _use_dex3_schema():
            image_feature = lambda doc: tfds.features.Image(
                shape=(480, 640, 3),
                dtype=np.uint8,
                encoding_format='jpeg',
                doc=doc,
            )
            return self.dataset_info_from_configs(
                features=tfds.features.FeaturesDict({
                    'steps': tfds.features.Dataset({
                        'observation': tfds.features.FeaturesDict({
                            'image_left_top': image_feature('Left top camera RGB observation.'),
                            'image_right_top': image_feature('Right top camera RGB observation.'),
                            'image_left_wrist': image_feature('Left wrist camera RGB observation.'),
                            'image_right_wrist': image_feature('Right wrist camera RGB observation.'),
                            'state': tfds.features.Tensor(
                                shape=(28,),
                                dtype=np.float32,
                                doc='G1 Dex3 28D joint and dexterous hand state.',
                            ),
                        }),
                        'action': tfds.features.Tensor(
                            shape=(28,),
                            dtype=np.float32,
                            doc='G1 Dex3 28D absolute joint and dexterous hand action.',
                        ),
                        'discount': tfds.features.Scalar(
                            dtype=np.float32,
                            doc='Discount if provided, default to 1.'
                        ),
                        'is_first': tfds.features.Scalar(
                            dtype=np.bool_,
                            doc='True on first step of the episode.'
                        ),
                        'is_last': tfds.features.Scalar(
                            dtype=np.bool_,
                            doc='True on last step of the episode.'
                        ),
                        'is_terminal': tfds.features.Scalar(
                            dtype=np.bool_,
                            doc='True on last step of the episode if it is a terminal step, True for demos.'
                        ),
                        'language_instruction': tfds.features.Text(
                            doc='Language Instruction.'
                        ),
                    }),
                    'episode_metadata': tfds.features.FeaturesDict({
                        'file_path': tfds.features.Text(
                            doc='Path to the original data file.'
                        ),
                    }),
                }))

        return self.dataset_info_from_configs(
            features=tfds.features.FeaturesDict({
                'steps': tfds.features.Dataset({
                    'observation': tfds.features.FeaturesDict({
                        'image_left_top': tfds.features.Image(
                            shape=(480, 640, 3),
                            dtype=np.uint8,
                            encoding_format='jpeg',
                            doc='Left top camera RGB observation.',
                        ),
                        'image_right_top': tfds.features.Image(
                            shape=(480, 640, 3),
                            dtype=np.uint8,
                            encoding_format='jpeg',
                            doc='Right top camera RGB observation.',
                        ),
                        'image_left_wrist': tfds.features.Image(
                            shape=(480, 640, 3),
                            dtype=np.uint8,
                            encoding_format='jpeg',
                            doc='Left wrist camera RGB observation.',
                        ),
                        'image_right_wrist': tfds.features.Image(
                            shape=(480, 640, 3),
                            dtype=np.uint8,
                            encoding_format='jpeg',
                            doc='Left wrist camera RGB observation.',
                        ),
                        'state': tfds.features.Tensor(
                            shape=(19,),
                            dtype=np.float32,
                            doc='Robot joint state (7D left arm + 7D right arm + 1D left gripper + 1D right gripper + 3D waist).',
                        ),
                        'ee_state': tfds.features.Tensor(
                            shape=(17,),
                            dtype=np.float32,
                            doc='Robot end effector state (6D EEF left arm + 6D EEF right arm + 1D left gripper + 1D right gripper + 3D waist).',
                        ),
                        'ee_state_6d': tfds.features.Tensor(
                            shape=(23,),
                            dtype=np.float32,
                            doc='Robot end effector state (9D EEF left arm + 9D EEF right arm + 1D left gripper + 1D right gripper + 3D waist).',
                        ),
                    }),
                    'action': tfds.features.Tensor(
                        shape=(19,),
                        dtype=np.float32,
                        doc='Robot joint action (7D left arm + 7D right arm + 1D left gripper + 1D right gripper + 3D waist).',
                    ),
                    'ee_action': tfds.features.Tensor(
                        shape=(17,),
                        dtype=np.float32,
                        doc='Robot eef action (6D EEF left arm + 6D EEF right arm + 1D left gripper + 1D right gripper + 3D waist).',
                    ),
                    'ee_action_6d': tfds.features.Tensor(
                        shape=(23,),
                        dtype=np.float32,
                        doc='Robot eef action (9D EEF left arm + 9D EEF right arm + 1D left gripper + 1D right gripper + 3D waist).',
                    ),
                    'discount': tfds.features.Scalar(
                        dtype=np.float32,
                        doc='Discount if provided, default to 1.'
                    ),
                    'is_first': tfds.features.Scalar(
                        dtype=np.bool_,
                        doc='True on first step of the episode.'
                    ),
                    'is_last': tfds.features.Scalar(
                        dtype=np.bool_,
                        doc='True on last step of the episode.'
                    ),
                    'is_terminal': tfds.features.Scalar(
                        dtype=np.bool_,
                        doc='True on last step of the episode if it is a terminal step, True for demos.'
                    ),
                    'language_instruction': tfds.features.Text(
                        doc='Language Instruction.'
                    ),
                }),
                'episode_metadata': tfds.features.FeaturesDict({ 
                    'file_path': tfds.features.Text(
                        doc='Path to the original data file.'
                    ),
                }),
            }))
        
        
        
    def _split_paths(self):
        """Define filepaths for data splits."""
        return {
            'train': glob.glob(HDF5_GLOB),
        }
