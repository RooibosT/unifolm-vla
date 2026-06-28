from __future__ import annotations

import urllib.request

import json_numpy
import numpy as np

json_numpy.patch()


class PolicyClient:
    def __init__(self, policy_url: str, timeout_s: float = 2.0):
        self._policy_url = policy_url
        self._timeout_s = timeout_s

    def act(
        self,
        image_primary: np.ndarray,
        image_secondary: np.ndarray,
        state: np.ndarray,
        instruction: str,
        task_name: str,
    ) -> np.ndarray:
        payload = {
            "observations": [
                {
                    "image_primary": image_primary,
                    "image_secondary": image_secondary,
                    "state": np.asarray(state, dtype=np.float32),
                    "instruction": instruction,
                    "task_name": task_name,
                }
            ]
        }
        data = json_numpy.dumps(payload).encode("utf-8")
        request = urllib.request.Request(
            self._policy_url,
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(request, timeout=self._timeout_s) as response:
            body = response.read().decode("utf-8")
        action = json_numpy.loads(body)
        return np.asarray(action, dtype=np.float32)
