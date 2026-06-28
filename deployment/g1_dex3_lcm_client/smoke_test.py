from __future__ import annotations

import struct

import numpy as np

from .action_adapter import ActionAdapter
from .lcm_protocol import (
    POLICY_ACTION_MAGIC,
    POLICY_ACTION_V3_FORMAT,
    POLICY_ACTION_V3_SIZE,
    PROTOCOL_VERSION_V3,
    ROBOT_STATE_SIZE,
    pack_policy_action_v3,
)
from .teleimager_camera import TeleimagerCamera


def test_policy_action_v3_packet_size() -> None:
    packet = pack_policy_action_v3(1, 2, [0.0] * 28)
    assert len(packet) == POLICY_ACTION_V3_SIZE == 136
    unpacked = struct.unpack(POLICY_ACTION_V3_FORMAT, packet)
    assert unpacked[0] == POLICY_ACTION_MAGIC
    assert unpacked[1] == PROTOCOL_VERSION_V3


def test_robot_state_packet_size() -> None:
    assert ROBOT_STATE_SIZE == 629


def test_camera_split() -> None:
    image = np.zeros((480, 1280, 3), dtype=np.uint8)
    image[:, :640, 0] = 10
    image[:, 640:, 0] = 20
    camera = object.__new__(TeleimagerCamera)
    camera._primary_view = "left"
    primary, secondary = camera._split_head(image)
    assert primary.shape == (480, 640, 3)
    assert secondary.shape == (480, 640, 3)
    assert primary[0, 0, 2] == 10
    assert secondary[0, 0, 2] == 20


def test_action_adapter_shape() -> None:
    adapter = ActionAdapter(use_temporal_ensemble=True)
    action = np.zeros((25, 28), dtype=np.float32)
    adapter.add_chunk(action)
    selected = adapter.next_action()
    assert selected.shape == (28,)
    assert adapter.steps_until_empty() == 24


def test_action_adapter_reuses_chunk() -> None:
    adapter = ActionAdapter(use_temporal_ensemble=False)
    action = np.tile(np.arange(25, dtype=np.float32)[:, None], (1, 28))
    adapter.add_chunk(action)
    assert np.all(adapter.next_action() == 0.0)
    assert np.all(adapter.next_action() == 1.0)


def main() -> None:
    test_policy_action_v3_packet_size()
    test_robot_state_packet_size()
    test_camera_split()
    test_action_adapter_shape()
    test_action_adapter_reuses_chunk()
    print("g1_dex3_lcm_client smoke tests passed")


if __name__ == "__main__":
    main()
