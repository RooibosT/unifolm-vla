#!/usr/bin/env bash
set -euo pipefail

DEX3_SRC=${DEX3_SRC:-/NHNHOME/WORKSPACE/chan/datasets/dex3_lerobot}
DEX3_HDF5=${DEX3_HDF5:-/NHNHOME/WORKSPACE/chan/datasets/dex3_hdf5}
DEX3_SHARDS=${DEX3_SHARDS:-4}
DEX3_COMPRESSION=${DEX3_COMPRESSION:-lzf}
DEX3_DRY_RUN=${DEX3_DRY_RUN:-0}

declare -a DATASETS=(
  "g1_dex3_block_stacking|unitreerobotics/G1_Dex3_BlockStacking_Dataset|G1_Dex3_BlockStacking_Dataset"
  "g1_dex3_camera_packaging|unitreerobotics/G1_Dex3_CameraPackaging_Dataset|G1_Dex3_CameraPackaging_Dataset"
  "g1_dex3_grasp_square|unitreerobotics/G1_Dex3_GraspSquare_Dataset|G1_Dex3_GraspSquare_Dataset"
  "g1_dex3_object_placement|unitreerobotics/G1_Dex3_ObjectPlacement_Dataset|G1_Dex3_ObjectPlacement_Dataset"
  "g1_dex3_pick_apple|unitreerobotics/G1_Dex3_PickApple_Dataset|G1_Dex3_PickApple_Dataset"
  "g1_dex3_pick_bottle|unitreerobotics/G1_Dex3_PickBottle_Dataset|G1_Dex3_PickBottle_Dataset"
  "g1_dex3_pick_charger|unitreerobotics/G1_Dex3_PickCharger_Dataset|G1_Dex3_PickCharger_Dataset"
  "g1_dex3_pick_doll|unitreerobotics/G1_Dex3_PickDoll_Dataset|G1_Dex3_PickDoll_Dataset"
  "g1_dex3_pick_gum|unitreerobotics/G1_Dex3_PickGum_Dataset|G1_Dex3_PickGum_Dataset"
  "g1_dex3_pick_snack|unitreerobotics/G1_Dex3_PickSnack_Dataset|G1_Dex3_PickSnack_Dataset"
  "g1_dex3_pick_tissue|unitreerobotics/G1_Dex3_PickTissue_Dataset|G1_Dex3_PickTissue_Dataset"
  "g1_dex3_pouring|unitreerobotics/G1_Dex3_Pouring_Dataset|G1_Dex3_Pouring_Dataset"
  "g1_dex3_toasted_bread|unitreerobotics/G1_Dex3_ToastedBread_Dataset|G1_Dex3_ToastedBread_Dataset"
)

count_episodes() {
  local dataset_root=$1
  python - "$dataset_root" <<'PY'
import sys
import json
from pathlib import Path

root = Path(sys.argv[1])
info_path = root / "meta" / "info.json"
if info_path.exists():
    with open(info_path, "r") as f:
        total_episodes = json.load(f).get("total_episodes")
    if total_episodes is not None:
        print(total_episodes)
        raise SystemExit(0)

episodes_jsonl = root / "meta" / "episodes.jsonl"
if episodes_jsonl.exists():
    with open(episodes_jsonl, "r") as f:
        print(sum(1 for line in f if line.strip()))
else:
    raise FileNotFoundError(f"No episode metadata found under {root / 'meta'}")
PY
}

count_hdf5() {
  local output_dir=$1
  find "$output_dir" -maxdepth 1 -name "*.hdf5" 2>/dev/null | wc -l
}

for item in "${DATASETS[@]}"; do
  IFS="|" read -r dataset_name repo_id folder_name <<< "$item"
  dataset_root="$DEX3_SRC/$folder_name"
  output_dir="$DEX3_HDF5/$dataset_name"
  log_dir="$output_dir/logs"

  if [[ ! -d "$dataset_root" ]]; then
    echo "Missing dataset root, skipping: $dataset_root"
    continue
  fi

  mkdir -p "$output_dir" "$log_dir"
  episode_count=$(count_episodes "$dataset_root")
  hdf5_count=$(count_hdf5 "$output_dir" | tr -d " ")

  if (( hdf5_count >= episode_count )); then
    echo "Skipping $dataset_name: $hdf5_count/$episode_count HDF5 files already exist"
    continue
  fi

  shard_size=$(( (episode_count + DEX3_SHARDS - 1) / DEX3_SHARDS ))
  echo "Converting $dataset_name: episodes=$episode_count existing=$hdf5_count shards=$DEX3_SHARDS compression=$DEX3_COMPRESSION"

  for (( shard=0; shard<DEX3_SHARDS; shard++ )); do
    start=$(( shard * shard_size ))
    if (( start >= episode_count )); then
      continue
    fi
    max_episodes=$shard_size
    log_path="$log_dir/convert_${dataset_name}_shard_${shard}.log"

    if [[ "$DEX3_DRY_RUN" == "1" ]]; then
      echo "  shard=$shard start=$start max_episodes=$max_episodes log=$log_path"
    else
      python prepare_data/convert_lerobot_to_hdf5.py \
        --mode dex3 \
        --repo_id "$repo_id" \
        --data_path "$dataset_root" \
        --target_path "$output_dir" \
        --start_episode "$start" \
        --max_episodes "$max_episodes" \
        --hdf5_compression "$DEX3_COMPRESSION" \
        > "$log_path" 2>&1 &
    fi
  done

  if [[ "$DEX3_DRY_RUN" != "1" ]]; then
    wait
  fi
  echo "Finished $dataset_name: $(count_hdf5 "$output_dir" | tr -d " ")/$episode_count HDF5 files"
done
