from __future__ import annotations

import time

import numpy as np

from .lcm_protocol import pack_policy_action_v3


class LcmActionPublisher:
    def __init__(self, lcm_url: str, channel: str):
        try:
            import lcm  # type: ignore
        except ImportError as exc:
            raise RuntimeError("Python lcm package is required for LcmActionPublisher") from exc
        self._lcm = lcm.LCM(lcm_url)
        self._channel = channel
        self._sequence = 1

    def publish(self, q_target: np.ndarray, emergency_stop: bool = False) -> int:
        q = np.asarray(q_target, dtype=np.float32).reshape(-1)
        send_time_us = int(time.time() * 1_000_000)
        packet = pack_policy_action_v3(
            sequence=self._sequence,
            send_time_us=send_time_us,
            q_target=q.tolist(),
            emergency_stop=emergency_stop,
        )
        self._lcm.publish(self._channel, packet)
        sequence = self._sequence
        self._sequence += 1
        return sequence
