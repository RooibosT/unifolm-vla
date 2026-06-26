# 29_dof_g1_deploy

Unitree G1 29-DOF onboard low-level bridge with Dex3 hand DDS channels.

이 repository는 **G1 로봇 onboard Jetson에서 29DOF body low-level 제어 bridge와 Dex3 양손 DDS 채널만 빌드/실행**하기 위한 최소 패키지입니다. `policy network`, `planner`, `ONNX Runtime`, `TensorRT`, `CUDA`는 포함하지 않습니다. 외부 Jetson AGX에서 policy를 실행하고, 나중에 통신 방식이 정해지면 이 bridge에 action receiver만 추가하는 구조입니다.

현재 v1은 `NullActionSource`를 사용합니다. 즉, policy action을 받지 않으며 로봇을 걷게 하거나 자세를 만들지 않습니다. 실제 로봇에서는 `rt/lowstate` 수신, `rt/secondary_imu` 수신, `rt/lowcmd` 송신, Dex3 `HandState` 수신 경로와 안전 fallback만 확인하는 smoke test 용도입니다. Dex3 손은 기본적으로 timeout-stop 명령을 보내므로 손가락 position motion을 만들지 않습니다.

## 폴더 구조

```text
29_dof_g1_deploy/
├── g1_ws/                         # 빌드/실행할 low-level bridge 코드
│   ├── CMakeLists.txt
│   ├── include/
│   └── src/g1_lowlevel_bridge.cpp
└── thirdparty/
    └── unitree_sdk2/              # 이 repo에 포함된 Unitree SDK2
```

`unitree_sdk2`를 같이 포함했기 때문에, 로봇에서 이 repository만 `git clone`하면 바로 `g1_ws`를 빌드할 수 있습니다.

## 1. 로봇에서 repository 받기

G1 onboard Jetson에서 작업합니다.

```bash
cd ~
git clone https://github.com/MinCheol6322/29_dof_g1_deploy.git
cd 29_dof_g1_deploy
```

이미 clone한 폴더가 있다면:

```bash
cd ~/29_dof_g1_deploy
git pull
```

## 2. 빌드 도구 설치

처음 한 번만 설치하면 됩니다.

```bash
sudo apt update
sudo apt install -y build-essential cmake git
```

이 minimal bridge는 TensorRT, ONNX Runtime, CUDA를 요구하지 않습니다.

## 3. 로봇 네트워크 인터페이스 확인

G1 low-level DDS 통신에 사용할 network interface를 찾습니다.

```bash
ip -4 addr
```

보통 `192.168.123.x` 대역 IP가 붙은 interface가 G1 robot network입니다. 예시는 다음과 같습니다.

```text
enP8p1s0
eth0
```

아래 실행 예시에서는 `enP8p1s0`를 사용하지만, 실제 로봇에서 확인한 interface 이름으로 바꿔야 합니다.

## 4. g1_ws 빌드

```bash
cd ~/29_dof_g1_deploy/g1_ws
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

빌드가 성공하면 실행 파일이 생성됩니다.

```bash
ls -lh build/g1_lowlevel_bridge
```

도움말 확인:

```bash
./build/g1_lowlevel_bridge --help
```

## 5. 실제 로봇 smoke test

처음 테스트는 반드시 로봇을 매달거나 물리적으로 지지한 상태에서 진행하세요. 바닥에 세워 놓고 바로 테스트하지 마세요.

짧게 10초만 실행:

```bash
timeout 10s ./build/g1_lowlevel_bridge enP8p1s0
```

`enP8p1s0`는 3단계에서 찾은 interface 이름으로 바꿉니다.

정상 로그 예시:

```text
[bridge] Initializing Unitree DDS on interface: enP8p1s0
[dex3] Initialized left/right hand publishers and subscribers.
[bridge] Started. Action source: null
[bridge] v1 uses NullActionSource, so it will not command motion.
[bridge] Dex3 left/right hands are included in safe timeout-stop mode.
[bridge] First LowState received. G1 type: ...
```

조금 더 오래 실행하면 다음과 같은 로그가 주기적으로 나옵니다.

```text
[bridge] LowState OK count=500 pelvis_rpy=[...]
```

Dex3 hand state가 정상으로 들어오면 다음 로그도 확인됩니다.

```text
[dex3] First left HandState received on rt/lf/dex3/left/state
[dex3] First right HandState received on rt/lf/dex3/right/state
```

이 브리지는 호환성을 위해 hand state topic을 두 종류 모두 subscribe합니다.

```text
left state:  rt/lf/dex3/left/state,  rt/dex3/left/state
right state: rt/lf/dex3/right/state, rt/dex3/right/state
left cmd:    rt/dex3/left/cmd
right cmd:   rt/dex3/right/cmd
```

종료는 `Ctrl-C`로 합니다.

```text
[bridge] Stopping. Sending damping command before exit.
```

## 6. 정상/비정상 판단

정상 상태:

- `First LowState received`가 출력됩니다.
- `LowState OK` 로그가 주기적으로 출력됩니다.
- Dex3가 연결되어 있으면 left/right `HandState received` 로그가 출력됩니다.
- 로봇이 걷거나 특정 자세로 움직이지 않습니다.
- Dex3 손가락이 열림/닫힘 position motion을 하지 않습니다.
- 관절은 damping command 때문에 약간 저항감을 가질 수 있습니다.
- `Ctrl-C` 종료 시 damping command를 짧게 보낸 뒤 종료합니다.

비정상 상태:

- `First LowState received`가 나오지 않습니다.
- Dex3가 장착되어 있는데 left/right `HandState received`가 나오지 않습니다.
- `LowState CRC error`가 계속 증가합니다.
- 로봇 관절이 갑자기 움직입니다.
- 손가락이 의도하지 않게 움직입니다.
- 기존 Unitree motion controller와 충돌하는 느낌이 있습니다.
- motor fault, 과열, 이상한 소음이 발생합니다.

비정상 상태가 보이면 즉시 e-stop을 누르고 프로세스를 종료하세요.

## 7. 안전 체크리스트

실제 로봇에서 실행 전 확인:

- 로봇이 하네스/스탠드 등으로 지지되어 있습니다.
- 비상정지 버튼을 누를 사람이 바로 옆에 있습니다.
- 주변에 사람이 없습니다.
- G1 network interface를 정확히 골랐습니다.
- 기존 high-level motion app이 동시에 명령을 보내지 않습니다.
- 이 v1에는 action receiver가 없으므로, policy motion이 나와서는 안 됩니다.

현재 timeout/fallback 정책:

- `LowState`가 없으면 command publish를 시작하지 않습니다.
- `LowState`가 stale이면 damping fallback으로 전환합니다.
- `NullActionSource`라서 action이 없으면 damping fallback을 유지합니다.
- stale action이 들어오는 미래 구현에서도 damping fallback으로 전환하게 되어 있습니다.
- Dex3는 기본 timeout-stop command를 유지합니다.
- `Ctrl-C` 종료 시 damping command를 짧게 보낸 뒤 종료합니다.

## 8. 다음 단계

이 smoke test가 통과한 뒤에만 AGX policy board와의 통신을 추가하세요. 나중에 구현할 action receiver는 `g1_ws/include/action_source.hpp`의 `ActionSource` 인터페이스를 구현하면 됩니다.

외부 policy board가 보낼 값은 raw policy `action[29]`를 기본으로 가정합니다. 로봇 onboard bridge가 기존 deploy 코드와 같은 변환을 담당합니다.

```text
q_target[i] = default_angles[i]
            + action[isaaclab_to_mujoco[i]] * g1_action_scale[i]
dq_target[i] = 0
tau_ff[i] = 0
kp[i] = kps[i]
kd[i] = kds[i]
```

이 변환을 onboard에 두는 이유는 joint ordering, action scale, gains, timeout, emergency fallback을 로봇 쪽 safety boundary 안에 유지하기 위해서입니다.
