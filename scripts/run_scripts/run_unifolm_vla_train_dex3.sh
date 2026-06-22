if [ -z "${NCCL_SOCKET_IFNAME:-}" ]; then
  if ip link show eth0 >/dev/null 2>&1; then
    export NCCL_SOCKET_IFNAME=eth0
  elif ip link show ib0 >/dev/null 2>&1; then
    export NCCL_SOCKET_IFNAME=ib0
  else
    export NCCL_SOCKET_IFNAME=lo
  fi
fi

export TORCH_NCCL_BLOCKING_WAIT=${TORCH_NCCL_BLOCKING_WAIT:-1}
export TORCH_NCCL_ASYNC_ERROR_HANDLING=${TORCH_NCCL_ASYNC_ERROR_HANDLING:-1}
export NCCL_TIMEOUT=${NCCL_TIMEOUT:-1000}

# Build Dex3 RLDS datasets with:
#   UNIFOLM_RLDS_SCHEMA=dex3 \
#   UNIFOLM_RLDS_HDF5_GLOB=/path/to/hdf5/*.hdf5 \
#   tfds build --data_dir /path/to/rlds_root

Framework_name=unifolm_vla
base_vlm=${base_vlm:-/path/to/your/UnifoLM-VLM-Base}
model_type=qwen2_5_vl
freeze_module_list=${freeze_module_list:-qwen_vl_interface}
window_size=${window_size:-1}

oxe_data_root=${oxe_data_root:-/path/to/your/dex3_rlds_root}
data_mix=${data_mix:-Unitree_Dex3_all_task}

run_root_dir=${run_root_dir:-/path/to/your/run_root_dir}
run_id=${run_id:-dex3_28d_action_head}
num_processes=${num_processes:-1}
per_device_batch_size=${per_device_batch_size:-2}
max_train_steps=${max_train_steps:-20000}
gradient_accumulation_steps=${gradient_accumulation_steps:-1}
mixed_precision=${mixed_precision:-bf16}
use_wrist_image=${use_wrist_image:-False}
use_proprio=${use_proprio:-True}
shuffle_buffer_size=${shuffle_buffer_size:-10000}
save_interval=${save_interval:-5000}
eval_interval=${eval_interval:-500}
logging_frequency=${logging_frequency:-100}
num_warmup_steps=${num_warmup_steps:-1000}
base_learning_rate=${base_learning_rate:-1e-5}
action_model_learning_rate=${action_model_learning_rate:-1e-4}
save_final_model=${save_final_model:-True}
pretrained_checkpoint=${pretrained_checkpoint:-}
reload_modules=${reload_modules:-}

pretrained_args=()
if [ -n "${pretrained_checkpoint}" ]; then
  pretrained_args+=(--trainer.pretrained_checkpoint "${pretrained_checkpoint}")
fi
if [ -n "${reload_modules}" ]; then
  pretrained_args+=(--trainer.reload_modules "${reload_modules}")
fi

if [ "${gradient_accumulation_steps}" -ne 1 ]; then
  echo "gradient_accumulation_steps must be 1 with the current Accelerate + DeepSpeed ZeRO-2 training loop."
  echo "Increase per_device_batch_size or num_processes to raise the global batch size."
  exit 2
fi

output_dir=${run_root_dir}/${run_id}
mkdir -p ${output_dir}
cp $0 ${output_dir}/

accelerate launch \
  --config_file src/unifolm_vla/config/deepseeds/deepspeed_zero2.yaml \
  --num_processes ${num_processes} \
  --mixed_precision ${mixed_precision} \
  --gradient_accumulation_steps ${gradient_accumulation_steps} \
  src/unifolm_vla/training/train_unifolm_vla.py \
  --config_yaml ./src/unifolm_vla/config/training/unifolm_vla_train.yaml \
  --framework.framework_py ${Framework_name} \
  --framework.qwenvl.base_vlm ${base_vlm} \
  --framework.qwenvl.model_type ${model_type} \
  --framework.action_model.action_dim 28 \
  --framework.action_model.state_dim 28 \
  --framework.action_model.action_horizon 25 \
  --framework.action_model.future_action_window_size 24 \
  --datasets.vla_data.data_root_dir ${oxe_data_root} \
  --datasets.vla_data.data_mix ${data_mix} \
  --datasets.vla_data.window_size ${window_size} \
  --datasets.vla_data.per_device_batch_size ${per_device_batch_size} \
  --trainer.freeze_modules ${freeze_module_list} \
  --trainer.max_train_steps ${max_train_steps} \
  --trainer.gradient_accumulation_steps ${gradient_accumulation_steps} \
  --trainer.shuffle_buffer_size ${shuffle_buffer_size} \
  --trainer.save_interval ${save_interval} \
  --trainer.use_wrist_image ${use_wrist_image} \
  --trainer.use_proprio ${use_proprio} \
  --trainer.logging_frequency ${logging_frequency} \
  --trainer.eval_interval ${eval_interval} \
  --trainer.num_warmup_steps ${num_warmup_steps} \
  --trainer.learning_rate.base ${base_learning_rate} \
  --trainer.learning_rate.action_model ${action_model_learning_rate} \
  --trainer.save_final_model ${save_final_model} \
  --run_root_dir ${run_root_dir} \
  --run_id ${run_id} \
  --wandb_project ${wandb_project:-vla_dex3} \
  --wandb_entity ${wandb_entity:-your_wandb_entity} \
  "${pretrained_args[@]}"
