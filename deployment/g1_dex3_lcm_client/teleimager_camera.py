from __future__ import annotations

import sys
import time
from pathlib import Path
from typing import Tuple

import numpy as np


class TeleimagerCamera:
    def __init__(
        self, host: str, request_port: int, teleimager_src: Path, primary_view: str = "left"
    ):
        if primary_view not in {"left", "right"}:
            raise ValueError("primary_view must be 'left' or 'right'")
        sys.path.insert(0, str(teleimager_src))
        from teleimager.image_client import ImageClient  # type: ignore

        self._client = ImageClient(host=host, request_port=request_port)
        self._primary_view = primary_view

    def close(self) -> None:
        self._client.close()

    def get_primary_secondary(self, timeout_s: float = 2.0) -> Tuple[np.ndarray, np.ndarray]:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            head, _fps = self._client.get_head_frame()
            if head is not None:
                return self._split_head(head)
            time.sleep(0.01)
        raise TimeoutError("Timed out waiting for teleimager head frame")

    def _split_head(self, head: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        if head.ndim != 3 or head.shape[2] != 3:
            raise ValueError(f"Expected HWC BGR/RGB image, got shape {head.shape}")
        if head.shape[1] % 2 != 0:
            raise ValueError(f"Binocular head image width must be even, got {head.shape[1]}")
        mid = head.shape[1] // 2
        left = head[:, :mid, ::-1]
        right = head[:, mid:, ::-1]
        if self._primary_view == "left":
            return left.copy(), right.copy()
        return right.copy(), left.copy()
