export NCCL_SOCKET_IFNAME=${NCCL_SOCKET_IFNAME:-bond0}
export NCCL_IB_HCA=${NCCL_IB_HCA:-mlx5_2,mlx5_3}
export NCCL_BLOCKING_WAIT=1
export NCCL_ASYNC_ERROR_HANDLING=1
export NCCL_TIMEOUT=1000

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
use_wrist_image=${use_wrist_image:-False}
use_proprio=${use_proprio:-True}

output_dir=${run_root_dir}/${run_id}
mkdir -p ${output_dir}
cp $0 ${output_dir}/

accelerate launch \
  --config_file src/unifolm_vla/config/deepseeds/deepspeed_zero2.yaml \
  --num_processes ${num_processes} \
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
  --trainer.shuffle_buffer_size 10000 \
  --trainer.save_interval 5000 \
  --trainer.use_wrist_image ${use_wrist_image} \
  --trainer.use_proprio ${use_proprio} \
  --trainer.logging_frequency 100 \
  --trainer.eval_interval 500 \
  --trainer.learning_rate.base 1e-5 \
  --trainer.learning_rate.action_model 1e-4 \
  --run_root_dir ${run_root_dir} \
  --run_id ${run_id} \
  --wandb_project ${wandb_project:-vla_dex3} \
  --wandb_entity ${wandb_entity:-your_wandb_entity}
