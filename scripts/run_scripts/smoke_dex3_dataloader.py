#!/usr/bin/env python
import argparse
from pathlib import Path

import numpy as np
import torch
from torch.nn.utils.rnn import pad_sequence
from torch.utils.data import DataLoader

from unifolm_vla.rlds_dataloader.datasets.datasets import RLDSBatchTransform, RLDSDataset


class RawBatchTransform:
    def __call__(self, rlds_batch):
        obs = rlds_batch["observation"]
        dataset_name = rlds_batch["dataset_name"]
        if isinstance(dataset_name, np.ndarray):
            dataset_name = dataset_name[0]
        if isinstance(dataset_name, bytes):
            dataset_name = dataset_name.decode("utf-8")

        language = rlds_batch["task"]["language_instruction"]
        if isinstance(language, np.ndarray):
            language = language[0]
        if isinstance(language, bytes):
            language = language.decode("utf-8")

        out = {
            "dataset_name": dataset_name,
            "language": language,
            "action": rlds_batch["action"],
            "state": obs.get("proprio"),
        }
        for key in ("image_primary", "image_secondary", "image_left_wrist", "image_right_wrist"):
            if key in obs:
                out[key] = obs[key]
        return out


def collate_processor_batch(inputs, processor):
    batch = {}
    input_ids = [example["input_ids"].squeeze(0) for example in inputs]
    batch["input_ids"] = pad_sequence(
        input_ids,
        batch_first=True,
        padding_value=processor.tokenizer.pad_token_id,
    )
    batch["attention_mask"] = batch["input_ids"].ne(processor.tokenizer.pad_token_id)
    batch["action"] = torch.tensor(np.squeeze(np.stack([example["actions"] for example in inputs])))
    batch["state"] = (
        torch.tensor(np.squeeze(np.stack([example["proprio"] for example in inputs])))
        if inputs[0].get("proprio") is not None
        else None
    )
    if "pixel_values" in inputs[0]:
        batch["pixel_values"] = torch.cat([example["pixel_values"] for example in inputs], dim=0)
    if "image_grid_thw" in inputs[0]:
        batch["image_grid_thw"] = torch.cat([example["image_grid_thw"] for example in inputs], dim=0)
    return batch


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data_root_dir", default="/NHNHOME/WORKSPACE/chan/datasets/dex3_rlds")
    parser.add_argument("--data_mix", default="g1_dex3_pick_charger")
    parser.add_argument("--batch_size", type=int, default=2)
    parser.add_argument("--shuffle_buffer_size", type=int, default=32)
    parser.add_argument("--window_size", type=int, default=1)
    parser.add_argument("--num_batches", type=int, default=1)
    parser.add_argument("--use_wrist_image", action="store_true")
    parser.add_argument("--processor_path", default=None)
    args = parser.parse_args()

    if args.processor_path:
        from transformers import AutoProcessor

        processor = AutoProcessor.from_pretrained(args.processor_path)
        processor.tokenizer.padding_side = "left"
        transform = RLDSBatchTransform(
            processor=processor,
            use_wrist_image=args.use_wrist_image,
            use_proprio=True,
        )
        collate_fn = lambda examples: collate_processor_batch(examples, processor)
    else:
        processor = None
        transform = RawBatchTransform()
        collate_fn = None

    dataset = RLDSDataset(
        Path(args.data_root_dir),
        args.data_mix,
        transform,
        resize_resolution=(224, 224),
        shuffle_buffer_size=args.shuffle_buffer_size,
        image_aug=False,
        window_size=args.window_size,
    )
    print(f"dataset_length={len(dataset)}")
    print(f"statistics_keys={list(dataset.dataset_statistics)[:3]}")

    loader = DataLoader(
        dataset,
        batch_size=args.batch_size,
        collate_fn=collate_fn,
        num_workers=0,
    )

    for batch_idx, batch in zip(range(args.num_batches), loader):
        print(f"\nBATCH {batch_idx}")
        if processor is None:
            print("dataset_name:", batch["dataset_name"])
            print("language:", batch["language"])
            print("action:", tuple(batch["action"].shape), batch["action"].dtype)
            print("state:", tuple(batch["state"].shape), batch["state"].dtype)
            for key in ("image_primary", "image_secondary", "image_left_wrist", "image_right_wrist"):
                if key in batch:
                    print(f"{key}:", tuple(batch[key].shape), batch[key].dtype)
            assert batch["action"].shape[-2:] == (25, 28), batch["action"].shape
            assert batch["state"].shape[-1] == 28, batch["state"].shape
        else:
            print("input_ids:", tuple(batch["input_ids"].shape), batch["input_ids"].dtype)
            print("attention_mask:", tuple(batch["attention_mask"].shape), batch["attention_mask"].dtype)
            print("action:", tuple(batch["action"].shape), batch["action"].dtype)
            print("state:", tuple(batch["state"].shape), batch["state"].dtype)
            print("pixel_values:", tuple(batch["pixel_values"].shape), batch["pixel_values"].dtype)
            print("image_grid_thw:", tuple(batch["image_grid_thw"].shape), batch["image_grid_thw"].dtype)
            assert batch["action"].shape[-2:] == (25, 28), batch["action"].shape
            assert batch["state"].shape[-1] == 28, batch["state"].shape
            assert batch["pixel_values"].numel() > 0
            assert batch["image_grid_thw"].numel() > 0

    print("\nDEX3_DATALOADER_SMOKE_OK")


if __name__ == "__main__":
    main()
