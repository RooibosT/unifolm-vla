from __future__ import annotations

import struct

POLICY_ACTION_MAGIC = 0x31414347
ROBOT_STATE_MAGIC = 0x31545347
PROTOCOL_VERSION_V3 = 3
ACTION_FLAG_EMERGENCY_STOP = 1 << 0
ACTION_FLAG_DEX3_ABSOLUTE_Q = 1 << 5
STATE_FLAG_HAS_LEFT_HAND_Q = 1 << 1
STATE_FLAG_HAS_RIGHT_HAND_Q = 1 << 2
DEX3_ACTION_DIM = 28

POLICY_ACTION_V3_FORMAT = "<IHHQq28f"
POLICY_ACTION_V3_SIZE = struct.calcsize(POLICY_ACTION_V3_FORMAT)

ROBOT_STATE_FORMAT = "<IHHQQqB4f3f3f4f3f3f29f29f29f7f7f7f7f7f7f"
ROBOT_STATE_SIZE = struct.calcsize(ROBOT_STATE_FORMAT)


def pack_policy_action_v3(
    sequence: int,
    send_time_us: int,
    q_target: list[float],
    emergency_stop: bool = False,
) -> bytes:
    if len(q_target) != DEX3_ACTION_DIM:
        raise ValueError(f"q_target must be {DEX3_ACTION_DIM}D, got {len(q_target)}")
    flags = ACTION_FLAG_DEX3_ABSOLUTE_Q
    if emergency_stop:
        flags |= ACTION_FLAG_EMERGENCY_STOP
    return struct.pack(
        POLICY_ACTION_V3_FORMAT,
        POLICY_ACTION_MAGIC,
        PROTOCOL_VERSION_V3,
        flags,
        sequence,
        send_time_us,
        *[float(v) for v in q_target],
    )
