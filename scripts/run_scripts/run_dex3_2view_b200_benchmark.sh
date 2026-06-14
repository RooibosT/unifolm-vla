#!/usr/bin/env bash
set -euo pipefail

export WANDB_MODE=${WANDB_MODE:-offline}

base_vlm=${base_vlm:-/NHNHOME/WORKSPACE/chan/checkpoints/UnifoLM-VLM-Base} \
oxe_data_root=${oxe_data_root:-/NHNHOME/WORKSPACE/chan/datasets/dex3_rlds} \
run_root_dir=${run_root_dir:-/NHNHOME/WORKSPACE/chan/runs/unifolm_vla} \
run_id=${run_id:-dex3_2view_b200_batch8_benchmark} \
data_mix=Unitree_Dex3_all_task \
num_processes=2 \
per_device_batch_size=${per_device_batch_size:-8} \
gradient_accumulation_steps=1 \
mixed_precision=bf16 \
max_train_steps=${max_train_steps:-20} \
num_warmup_steps=${num_warmup_steps:-5} \
logging_frequency=${logging_frequency:-5} \
eval_interval=${eval_interval:-10} \
save_interval=${save_interval:-1000000} \
save_final_model=${save_final_model:-False} \
use_wrist_image=False \
scripts/run_scripts/run_unifolm_vla_train_dex3.sh
