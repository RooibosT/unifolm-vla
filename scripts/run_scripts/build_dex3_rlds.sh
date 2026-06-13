#!/usr/bin/env bash
set -euo pipefail

DEX3_HDF5=${DEX3_HDF5:-/NHNHOME/WORKSPACE/chan/datasets/dex3_hdf5}
DEX3_RLDS=${DEX3_RLDS:-/NHNHOME/WORKSPACE/chan/datasets/dex3_rlds}
DEX3_RLDS_TMP=${DEX3_RLDS_TMP:-/tmp/unifolm_dex3_rlds_builders}
DEX3_RLDS_WORKERS=${DEX3_RLDS_WORKERS:-4}
DEX3_RLDS_MAX_PATHS=${DEX3_RLDS_MAX_PATHS:-4}
DEX3_RLDS_OVERWRITE=${DEX3_RLDS_OVERWRITE:-0}
DEX3_RLDS_DRY_RUN=${DEX3_RLDS_DRY_RUN:-0}
DEX3_RLDS_MAX_EXAMPLES=${DEX3_RLDS_MAX_EXAMPLES:-}

declare -a DATASETS=(
  "g1_dex3_block_stacking"
  "g1_dex3_camera_packaging"
  "g1_dex3_grasp_square"
  "g1_dex3_object_placement"
  "g1_dex3_pick_apple"
  "g1_dex3_pick_bottle"
  "g1_dex3_pick_charger"
  "g1_dex3_pick_doll"
  "g1_dex3_pick_gum"
  "g1_dex3_pick_snack"
  "g1_dex3_pick_tissue"
  "g1_dex3_pouring"
  "g1_dex3_toasted_bread"
)

if [[ -n "${DEX3_RLDS_DATASETS:-}" ]]; then
  read -r -a DATASETS <<< "$DEX3_RLDS_DATASETS"
fi

prepare_builder() {
  local dataset_name=$1
  local builder_dir="$DEX3_RLDS_TMP/$dataset_name"

  rm -rf "$builder_dir"
  mkdir -p "$builder_dir"
  cp prepare_data/hdf5_to_rlds/rlds_dataset/conversion_utils.py "$builder_dir/conversion_utils.py"
  touch "$builder_dir/__init__.py"

  python - "$dataset_name" "$DEX3_RLDS_WORKERS" "$DEX3_RLDS_MAX_PATHS" "$builder_dir/$dataset_name.py" <<'PY'
import sys
from pathlib import Path

dataset_name, workers, max_paths, output_path = sys.argv[1], sys.argv[2], sys.argv[3], Path(sys.argv[4])
src = Path("prepare_data/hdf5_to_rlds/rlds_dataset/rlds_dataset.py").read_text()
src = src.replace("class rlds_dataset(MultiThreadedDatasetBuilder):", f"class {dataset_name}(MultiThreadedDatasetBuilder):")
src = src.replace("N_WORKERS = 8", f"N_WORKERS = {int(workers)}")
src = src.replace("MAX_PATHS_IN_MEMORY = 8", f"MAX_PATHS_IN_MEMORY = {int(max_paths)}")
output_path.write_text(src)
PY

  echo "$builder_dir"
}

mkdir -p "$DEX3_RLDS" "$DEX3_RLDS_TMP"

for dataset_name in "${DATASETS[@]}"; do
  hdf5_glob="$DEX3_HDF5/$dataset_name/*.hdf5"
  hdf5_count=$(find "$DEX3_HDF5/$dataset_name" -maxdepth 1 -name "*.hdf5" 2>/dev/null | wc -l | tr -d " ")
  if (( hdf5_count == 0 )); then
    echo "Missing HDF5 files, skipping: $dataset_name"
    continue
  fi

  output_info="$DEX3_RLDS/$dataset_name/1.0.0/dataset_info.json"
  if [[ "$DEX3_RLDS_OVERWRITE" != "1" && -f "$output_info" ]]; then
    echo "Skipping $dataset_name: RLDS already exists at $output_info"
    continue
  fi

  builder_dir=$(prepare_builder "$dataset_name")
  log_dir="$DEX3_RLDS/logs"
  mkdir -p "$log_dir"
  log_path="$log_dir/build_${dataset_name}.log"

  cmd=(
    tfds build "$builder_dir"
    --data_dir "$DEX3_RLDS"
  )
  if [[ "$DEX3_RLDS_OVERWRITE" == "1" ]]; then
    cmd+=(--overwrite)
  fi
  if [[ -n "$DEX3_RLDS_MAX_EXAMPLES" ]]; then
    cmd+=(--max_examples_per_split "$DEX3_RLDS_MAX_EXAMPLES")
  fi

  echo "Building $dataset_name: hdf5=$hdf5_count workers=$DEX3_RLDS_WORKERS max_paths=$DEX3_RLDS_MAX_PATHS"
  echo "  log=$log_path"
  if [[ "$DEX3_RLDS_DRY_RUN" == "1" ]]; then
    echo "  UNIFOLM_RLDS_SCHEMA=dex3 UNIFOLM_RLDS_HDF5_GLOB=$hdf5_glob ${cmd[*]}"
    continue
  fi

  UNIFOLM_RLDS_SCHEMA=dex3 \
  UNIFOLM_RLDS_HDF5_GLOB="$hdf5_glob" \
  "${cmd[@]}" > "$log_path" 2>&1

  echo "Finished $dataset_name"
done
