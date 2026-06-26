# G1 Onboard LCM Split Steps

This README is for the code that must run on the real G1 onboard computer.

## What Was Added

The onboard bridge now has optional LCM support:

```text
G1_ROBOT_STATE    G1 onboard -> AGX
G1_POLICY_ACTION  AGX -> G1 onboard
```

Relevant files:

```text
g1_ws/include/lcm_protocol.hpp
g1_ws/include/lcm_action_source.hpp
g1_ws/include/lcm_robot_state_publisher.hpp
g1_ws/src/g1_lowlevel_bridge.cpp
g1_ws/CMakeLists.txt
```

LCM is optional at build time. If LCM is not installed, the original null-action
smoke test still builds. If LCM is installed, the binary gains LCM action/state
support.

## Install LCM On The Robot

```bash
sudo apt update
sudo apt install -y liblcm-dev
```

## Rebuild

Use a clean build directory so CMake detects LCM:

```bash
cd ~/29_dof_g1_deploy/g1_ws
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

During configure, confirm CMake prints something like:

```text
LCM found: /usr/lib/.../liblcm.so
```

## Dry-Run Test

Start with the robot supported and e-stop operator ready:

```bash
./build/g1_lowlevel_bridge eth0 \
  --action-source lcm \
  --publish-lcm-state \
  --dry-run-actions
```

Expected logs:

```text
[lcm-state] Publishing channel: G1_ROBOT_STATE
[lcm-action] Subscribed channel: G1_POLICY_ACTION
[bridge] Dry-run actions enabled; received actions will not move motors.
[bridge] First LowState received. G1 type: ...
```

Then run the AGX smoke sender. The robot should print:

```text
[lcm-action] First action received. seq=0
[bridge] Dry-run action received seq=...
```

Because `--dry-run-actions` is enabled, the robot keeps damping command active.

## Explicit LCM URL

If AGX and G1 do not discover each other, set the same LCM URL on both sides:

```bash
export LCM_DEFAULT_URL="udpm://239.255.76.67:7667?ttl=1"
```

Or pass it directly:

```bash
./build/g1_lowlevel_bridge eth0 \
  --action-source lcm \
  --publish-lcm-state \
  --dry-run-actions \
  --lcm-url "udpm://239.255.76.67:7667?ttl=1"
```

## Real Action Enable

Only remove `--dry-run-actions` after:

- AGX receives `G1_ROBOT_STATE` at the expected rate.
- G1 receives `G1_POLICY_ACTION` with monotonically increasing sequence.
- Action timeout behavior has been observed.
- The robot is suspended or physically supported.
- The AGX process does not publish `rt/lowcmd` directly.

Real action mode:

```bash
./build/g1_lowlevel_bridge eth0 \
  --action-source lcm \
  --publish-lcm-state
```

If action packets stop or become stale, the bridge falls back to damping.

## Safety Boundary

AGX sends raw policy `action[29]` only. The robot bridge applies:

```text
q_target[i] = default_angles[i]
            + action[isaaclab_to_mujoco[i]] * g1_action_scale[i]
```

Do not send q targets or gains from AGX in this first split mode.
