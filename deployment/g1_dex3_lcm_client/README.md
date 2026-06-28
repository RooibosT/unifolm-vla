# Thor G1 Dex3 LCM Deploy Client

This module runs on Thor and connects:

```text
Teleimager camera stream + G1_ROBOT_STATE
  -> VLA policy server /act
  -> G1_POLICY_ACTION V3
```

## Dependencies

Install the UnifoLM-VLA environment first. The deploy client additionally needs:

```bash
pip install lcm pyzmq logging_mp opencv-python pyyaml json_numpy
```

The client imports Teleimager from the vendored source tree by default:

```text
external/g1/29_dof_g1_deploy/thirdparty/teleimager/src
```

## Dry Run

Dry run performs camera receive, LCM state receive, policy inference, action
post-processing, and logging. It does not publish motor actions.

```bash
PYTHONPATH=deployment python -m g1_dex3_lcm_client \
  --image-host <G1_PC2_IP> \
  --task-name g1_dex3_block_stacking \
  --instruction "block stacking" \
  --policy-url http://127.0.0.1:8777/act
```

## Publish Actions

Only use this after bridge dry-run, image receive, state receive, and policy
inference have been verified.

```bash
PYTHONPATH=deployment python -m g1_dex3_lcm_client \
  --image-host <G1_PC2_IP> \
  --task-name g1_dex3_block_stacking \
  --instruction "block stacking" \
  --policy-url http://127.0.0.1:8777/act \
  --execute-actions
```

## Image Mapping

The default assumption is:

```text
teleimager head left half  -> image_primary
teleimager head right half -> image_secondary
```

Use `--primary-view right` if the camera order needs to be swapped.

## Action Chunking

The client publishes at `--control-hz` and does not call policy every control
tick. It consumes the current `[25, 28]` chunk step by step, then requests the
next chunk when `--replan-steps` steps remain. The default is:

```text
control_hz = 10
replan_steps = 8
max_hold_steps = 2
policy_timeout_s = 2.0
```

If policy inference is slower than the remaining horizon, the client holds the
last absolute `q_target[28]` for at most `--max-hold-steps` publish ticks. Once
that limit is reached it stops publishing actions, allowing the G1 bridge's
action timeout to switch to damping. Verify that `--control-hz` matches the
action dt used when the Dex3 checkpoint was trained.

## Robot Posture Assumption

The G1 bridge V3 absolute-q path commands the 14 arm joints and Dex3 hands from
the policy action. Legs and waist are held at the latest LowState joint
positions with the configured bridge gains. Use real motor execution only when
the robot is in a stable fixed/supported posture, or after replacing that hold
behavior with the intended balance/stand controller integration.
