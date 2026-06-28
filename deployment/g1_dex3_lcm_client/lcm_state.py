from __future__ import annotations

import struct
import threading
import time
from dataclasses import dataclass
from typing import Optional

import numpy as np

from .lcm_protocol import (
    ROBOT_STATE_FORMAT,
    ROBOT_STATE_MAGIC,
    ROBOT_STATE_SIZE,
    STATE_FLAG_HAS_LEFT_HAND_Q,
    STATE_FLAG_HAS_RIGHT_HAND_Q,
)


@dataclass
class RobotState:
    sequence: int
    send_time_us: int
    q: np.ndarray
    left_hand_q: np.ndarray
    right_hand_q: np.ndarray
    received_at: float

    def dex3_state_28(self) -> np.ndarray:
        return np.concatenate(
            [
                self.q[15:22],
                self.left_hand_q,
                self.q[22:29],
                self.right_hand_q,
            ]
        ).astype(np.float32)


def decode_robot_state_packet(data: bytes) -> RobotState:
    if len(data) != ROBOT_STATE_SIZE:
        raise ValueError(
            f"RobotStatePacket size mismatch: got {len(data)}, expected {ROBOT_STATE_SIZE}"
        )
    unpacked = struct.unpack(ROBOT_STATE_FORMAT, data)
    magic = unpacked[0]
    if magic != ROBOT_STATE_MAGIC:
        raise ValueError(f"Bad RobotStatePacket magic: 0x{magic:x}")
    flags = unpacked[2]
    if (flags & STATE_FLAG_HAS_LEFT_HAND_Q) == 0 or (flags & STATE_FLAG_HAS_RIGHT_HAND_Q) == 0:
        raise ValueError("RobotStatePacket is missing Dex3 hand state flags")

    sequence = unpacked[3]
    send_time_us = unpacked[5]
    idx = 7
    idx += 4 + 3 + 3 + 4 + 3 + 3
    q = np.asarray(unpacked[idx : idx + 29], dtype=np.float32)
    idx += 29
    idx += 29  # dq
    idx += 29  # tau_est
    left_hand_q = np.asarray(unpacked[idx : idx + 7], dtype=np.float32)
    idx += 7
    idx += 7  # left dq
    idx += 7  # left tau
    right_hand_q = np.asarray(unpacked[idx : idx + 7], dtype=np.float32)

    return RobotState(
        sequence=sequence,
        send_time_us=send_time_us,
        q=q,
        left_hand_q=left_hand_q,
        right_hand_q=right_hand_q,
        received_at=time.monotonic(),
    )


class LcmStateReceiver:
    def __init__(self, lcm_url: str, channel: str):
        try:
            import lcm  # type: ignore
        except ImportError as exc:
            raise RuntimeError("Python lcm package is required for LcmStateReceiver") from exc
        self._lcm_mod = lcm
        self._lcm = lcm.LCM(lcm_url)
        self._channel = channel
        self._latest: Optional[RobotState] = None
        self._lock = threading.Lock()
        self._running = False
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        self._lcm.subscribe(self._channel, self._on_message)
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=1.0)

    def latest(self, max_age_s: float = 0.5) -> Optional[RobotState]:
        with self._lock:
            state = self._latest
        if state is None or time.monotonic() - state.received_at > max_age_s:
            return None
        return state

    def _on_message(self, _channel: str, data: bytes) -> None:
        try:
            state = decode_robot_state_packet(data)
        except ValueError:
            return
        with self._lock:
            self._latest = state

    def _loop(self) -> None:
        while self._running:
            self._lcm.handle_timeout(100)
