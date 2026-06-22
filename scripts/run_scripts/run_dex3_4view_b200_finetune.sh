#!/usr/bin/env bash
set -euo pipefail

export WANDB_MODE=${WANDB_MODE:-offline}

two_view_run_root=${two_view_run_root:-/NHNHOME/WORKSPACE/chan/runs/unifolm_vla/dex3_2view_b200_batch8_full_100k}
pretrained_checkpoint=${pretrained_checkpoint:-${two_view_run_root}/final_model/pytorch_model.pt}

if [ ! -f "${pretrained_checkpoint}" ]; then
  echo "Pretrained 2-view checkpoint not found: ${pretrained_checkpoint}"
  exit 2
fi

base_vlm=${base_vlm:-/NHNHOME/WORKSPACE/chan/checkpoints/UnifoLM-VLM-Base} \
oxe_data_root=${oxe_data_root:-/NHNHOME/WORKSPACE/chan/datasets/dex3_rlds} \
run_root_dir=${run_root_dir:-/NHNHOME/WORKSPACE/chan/runs/unifolm_vla} \
run_id=${run_id:-dex3_4view_b200_batch4_finetune_20k} \
data_mix=Unitree_Dex3_4view_task \
num_processes=2 \
per_device_batch_size=${per_device_batch_size:-4} \
gradient_accumulation_steps=1 \
mixed_precision=bf16 \
max_train_steps=${max_train_steps:-20000} \
num_warmup_steps=${num_warmup_steps:-1000} \
logging_frequency=${logging_frequency:-100} \
eval_interval=${eval_interval:-1000} \
save_interval=${save_interval:-2500} \
save_final_model=${save_final_model:-True} \
action_model_learning_rate=${action_model_learning_rate:-3e-5} \
use_wrist_image=True \
pretrained_checkpoint=${pretrained_checkpoint} \
scripts/run_scripts/run_unifolm_vla_train_dex3.sh
