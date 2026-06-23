#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# Dex3 2-view full retrain with BOUNDS_Q99 action normalization.
#
# Why a fresh run (not a resume):
#   The action head learns to predict in the *normalized* action space. We just
#   switched G1_DEX3 normalization from BOUNDS (min/max) to BOUNDS_Q99 (q01/q99)
#   in src/unifolm_vla/rlds_dataloader/constants.py, so the old checkpoints are
#   no longer compatible and the head must be retrained from scratch.
#   (q01/q99 are already stored in dataset_statistics.json -> no RLDS rebuild.)
#
# Sufficient training (data coverage):
#   Dataset = Unitree_Dex3_all_task = 13 tasks, 3,152 demos, 2,587,515 transitions.
#   samples_seen = global_batch * max_train_steps
#   epochs       = samples_seen / 2,587,515
#   The previous 100k-step / global-batch-16 run only covered ~0.62 epoch.
#   Defaults below target ~3 epochs:
#     global_batch = per_device_batch_size(16) * num_processes(2) = 32
#     240,000 steps * 32 = 7,680,000 samples  ->  ~2.97 epochs
#   Raising steps/batch costs WALL-CLOCK TIME (and batch costs VRAM); it does not
#   change per-step VRAM otherwise. shuffle_buffer costs host RAM, not VRAM.
# =============================================================================

export WANDB_MODE=${WANDB_MODE:-offline}

base_vlm=${base_vlm:-/NHNHOME/WORKSPACE/chan/checkpoints/UnifoLM-VLM-Base} \
oxe_data_root=${oxe_data_root:-/NHNHOME/WORKSPACE/chan/datasets/dex3_rlds} \
run_root_dir=${run_root_dir:-/NHNHOME/WORKSPACE/chan/runs/unifolm_vla} \
run_id=${run_id:-dex3_2view_b200_full_q99_3ep} \
data_mix=${data_mix:-Unitree_Dex3_all_task} \
num_processes=${num_processes:-2} \
per_device_batch_size=${per_device_batch_size:-16} \
gradient_accumulation_steps=1 \
mixed_precision=bf16 \
max_train_steps=${max_train_steps:-240000} \
num_warmup_steps=${num_warmup_steps:-5000} \
logging_frequency=${logging_frequency:-100} \
eval_interval=${eval_interval:-1000} \
save_interval=${save_interval:-10000} \
save_final_model=${save_final_model:-True} \
shuffle_buffer_size=${shuffle_buffer_size:-50000} \
base_learning_rate=${base_learning_rate:-1e-5} \
action_model_learning_rate=${action_model_learning_rate:-1e-4} \
use_wrist_image=False \
use_proprio=True \
scripts/run_scripts/run_unifolm_vla_train_dex3.sh
