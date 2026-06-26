# g1_ws

Minimal onboard low-level bridge workspace for Unitree G1 29-DOF with Dex3 hands.

This folder is intended for the Jetson board on the real G1 robot. It only
builds the low-level DDS bridge that can publish motor commands to `rt/lowcmd`,
subscribe to `rt/lowstate` / `rt/secondary_imu`, and initialize Dex3 left/right
hand DDS channels.

It does not include policy inference, planner code, ONNX Runtime, TensorRT, or
CUDA. The policy network is expected to run on a separate Jetson AGX later. In
this v1, the action transport is intentionally not implemented: the executable
uses `NullActionSource`, so it is a connectivity and safety smoke test that
keeps a damping fallback command active instead of commanding motion.
Dex3 commands also default to timeout-stop mode, so this v1 does not command
finger position motion.

## What This Builds

- `g1_lowlevel_bridge`: G1 onboard process.
- Unitree DDS initialization with `ChannelFactory::Init(0, <interface>)`.
- `rt/lowstate` and `rt/secondary_imu` subscribers.
- `rt/lowcmd` publisher.
- Dex3 left/right hand command publishers.
- Dex3 left/right hand state subscribers.
- 500 Hz command writer thread.
- 50 Hz control/update thread.
- `ActionSource` interface with `NullActionSource` and optional LCM action input.
- Optional LCM policy action packets with body action, Dex3 open/close flags, and
  Dex3 joint-position fields.

## Copying To The Robot

Recommended: clone the full `29_dof_g1_deploy` repository to the G1 Jetson.
The repository includes `thirdparty/unitree_sdk2`, and this workspace uses that
SDK path by default.

If you only copy this folder, also copy Unitree SDK2 and pass its path to CMake:

```bash
cmake -S . -B build -DUNITREE_SDK2_PATH=/path/to/unitree_sdk2
```

The default expected layout is:

```text
29_dof_g1_deploy/
├── g1_ws/
└── thirdparty/
    └── unitree_sdk2/
```

## Install Build Tools

On the G1 Jetson:

```bash
sudo apt update
sudo apt install -y build-essential cmake git
```

If Unitree SDK2 dependencies are missing, install the same dependencies used by
your Unitree SDK2 setup. This minimal package does not require TensorRT, ONNX
Runtime, or CUDA.

## Find The Robot Network Interface

The real G1 network interface is usually the one with a `192.168.123.x` address:

```bash
ip -4 addr
```

Example interface names are `enP8p1s0`, `eth0`, or similar.

## Build

From the repository root on the G1 Jetson:

```bash
cd g1_ws
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

If `unitree_sdk2` is not in the default relative location:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DUNITREE_SDK2_PATH=/path/to/unitree_sdk2
cmake --build build -j$(nproc)
```

## Preserving Build Directories

CMake build outputs are isolated by build directory. To test edited code without
overwriting an existing build, configure a new build directory instead of
reusing `build`:

```bash
cmake -S . -B /tmp/g1_ws_joint_test_build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/g1_ws_joint_test_build -j$(nproc)
```

Run the test binary from that directory:

```bash
/tmp/g1_ws_joint_test_build/g1_lowlevel_bridge enP8p1s0
```

The original `build/` directory remains untouched. You can keep multiple build
directories, for example `build_original`, `build_joint_test`, or temporary
builds under `/tmp`, and switch between their binaries directly.

## Run A Smoke Test

Keep the robot safely supported or suspended for the first test.

```bash
./build/g1_lowlevel_bridge enP8p1s0
```

Replace `enP8p1s0` with the interface found from `ip -4 addr`.

Expected behavior without an external action source:

- The program prints the first valid `LowState` message.
- It reports `LowState OK` periodically.
- It initializes Dex3 left/right hand DDS channels.
- If Dex3 is connected, it prints first left/right `HandState` receipt.
- It does not command walking, standing, or any policy motion.
- It does not command Dex3 finger open/close motion.
- It keeps a damping fallback command active after LowState is available.
- Press `Ctrl-C` to stop; the bridge sends damping commands briefly before exit.

Dex3 topics used by this bridge:

```text
left cmd:     rt/dex3/left/cmd
right cmd:    rt/dex3/right/cmd
left state:   rt/lf/dex3/left/state and rt/dex3/left/state
right state:  rt/lf/dex3/right/state and rt/dex3/right/state
```

Expected Dex3 logs:

```text
[dex3] Initialized left/right hand publishers and subscribers.
[dex3] First left HandState received on rt/lf/dex3/left/state
[dex3] First right HandState received on rt/lf/dex3/right/state
```

## Safety Checklist

Before running on the physical robot:

- Keep the robot suspended or physically supported for initial tests.
- Keep an operator ready at the robot emergency stop.
- Confirm the network interface is the G1 interface, not Wi-Fi or another LAN.
- Confirm `LowState` is received before enabling an external action source.
- Confirm Dex3 `HandState` is received before enabling hand position commands;
  without state feedback, hand smoothing cannot use the measured current pose.
- Keep `--dry-run-actions` enabled until the AGX sender sequence, packet rate,
  packet version, timeout behavior, and emergency-stop behavior are verified.
- Start Dex3 joint-position tests with small values before sending full-range
  finger targets.

Timeout behavior:

- No `LowState`: the bridge does not publish commands.
- Stale `LowState`: intended behavior is damping fallback.
- No action from `ActionSource`: the bridge keeps the current fallback command.
- Stale action: intended behavior is damping fallback, but the timeout check is
  currently disabled in code and must be restored before unattended teleoperation.
- Dex3 commands: timeout-stop by default and on shutdown; during LCM action mode,
  v1 open/close flags or v2 joint-position fields can command the hands.
- `Ctrl-C`: the bridge sends damping commands briefly and exits.

## LCM Action Receiver

The bridge can receive external actions through LCM when built with `liblcm-dev`
and started with `--action-source lcm`. The body command remains raw policy
`action[29]`.

Dex3 hand commands are included in the LCM action protocol:

- v1 action packet: body `action[29]` plus binary hand open/close flags.
- v2 action packet: body `action[29]` plus optional `left_hand_q[7]` and
  `right_hand_q[7]` joint-position arrays.
- If a v2 packet sets `kActionFlagHasLeftHandQ` or
  `kActionFlagHasRightHandQ`, the corresponding joint array is used.
- If the joint-position flag is absent, the bridge falls back to the v1-style
  open/close flags.

Dex3 joint order is:

```text
[thumb_0, thumb_1, thumb_2, middle_0, middle_1, index_0, index_1]
```

Dex3 joint targets are clamped to per-hand limits and published through the
500 Hz command writer loop. If Dex3 state is being received, each publish clamps
per-joint motion to a maximum delta of `0.25 rad` from the latest measured hand
state to avoid sudden jumps.

The onboard bridge owns the safety-critical conversion:

```text
q_target[i] = default_angles[i]
            + action[isaaclab_to_mujoco[i]] * g1_action_scale[i]
dq_target[i] = 0
tau_ff[i] = 0
kp[i] = kps[i]
kd[i] = kds[i]
```

Keeping this conversion onboard ensures joint ordering, scaling, gains, timeout
handling, and emergency fallback remain on the robot-side safety boundary.

## LCM Split Mode

This workspace can be built with optional LCM support. Install LCM before
configuring CMake:

```bash
sudo apt update
sudo apt install -y liblcm-dev
```

Then rebuild from a clean build directory:

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Dry-run AGX split mode:

```bash
./build/g1_lowlevel_bridge eth0 \
  --action-source lcm \
  --publish-lcm-state \
  --dry-run-actions
```

Channels:

```text
G1_ROBOT_STATE    G1 onboard -> AGX
G1_POLICY_ACTION  AGX -> G1 onboard
```

Use `--lcm-url` if the AGX/G1 network needs an explicit LCM URL. Keep
`--dry-run-actions` enabled until the AGX sender sequence, packet rate, and
timeout behavior are verified.
