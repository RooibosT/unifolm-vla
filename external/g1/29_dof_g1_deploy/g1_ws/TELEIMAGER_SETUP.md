# Teleimager Setup on G1 PC2

This bridge repository vendors Unitree Teleimager under:

```text
external/g1/29_dof_g1_deploy/thirdparty/teleimager
```

Run Teleimager on G1 PC2 to stream camera frames to the Thor deploy client.

## Install

```bash
cd ~/29_dof_g1_deploy/thirdparty/teleimager
conda create -n teleimager python=3.10 -y
conda activate teleimager
sudo apt install -y libusb-1.0-0-dev libturbojpeg-dev
pip install -e ".[server]"
bash setup_uvc.sh
```

## Discover Cameras

```bash
teleimager-server --cf
```

Use the discovered `video_id`, `serial_number`, or `physical_path` values to update:

```text
thirdparty/teleimager/cam_config_server.yaml
```

Default ZMQ ports:

```text
head_camera:        55555
left_wrist_camera:  55556
right_wrist_camera: 55557
config responder:   60000
```

For the Dex3 2-view VLA deploy path, the head camera should publish a binocular
wide image. The Thor client splits the image by width:

```text
left half  -> image_primary
right half -> image_secondary
```

## Start Server

```bash
cd ~/29_dof_g1_deploy/thirdparty/teleimager
conda activate teleimager
teleimager-server
```

Thor can test the stream with:

```bash
PYTHONPATH=deployment python -m g1_dex3_lcm_client.client \
  --image-host <G1_PC2_IP> \
  --task-name g1_dex3_block_stacking \
  --instruction "block stacking" \
  --dry-run
```

