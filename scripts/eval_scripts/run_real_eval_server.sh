python deployment/model_server/run_real_eval_server.py \
    --ckpt_path /NHNHOME/WORKSPACE/chan/runs/unifolm_vla/dex3_4view_b200_batch8_finetune_20k/final_model/pytorch_model.pt \
    --port 8777 \
    --unnorm_key g1_stack_block \
    --vlm_pretrained_path /NHNHOME/WORKSPACE/chan/hf_cache/hub/models--unitreerobotics--UnifoLM-VLM-Base