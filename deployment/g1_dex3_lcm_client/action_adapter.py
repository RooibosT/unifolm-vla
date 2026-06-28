from __future__ import annotations

from dataclasses import dataclass
from typing import List, Tuple

import numpy as np

POLICY_CHUNK_SHAPE = (25, 28)


@dataclass
class _Chunk:
    start_step: int
    actions: np.ndarray


class ActionAdapter:
    def __init__(self, use_temporal_ensemble: bool = True, temporal_ensemble_k: float = 0.01):
        self._use_temporal_ensemble = use_temporal_ensemble
        self._k = temporal_ensemble_k
        self._step = 0
        self._chunks: List[_Chunk] = []

    def add_chunk(self, action_chunk: np.ndarray) -> None:
        chunk = np.asarray(action_chunk, dtype=np.float32)
        if chunk.shape != POLICY_CHUNK_SHAPE:
            raise ValueError(f"Expected policy action shape [25, 28], got {chunk.shape}")

        if not self._use_temporal_ensemble:
            self._chunks = [_Chunk(start_step=self._step, actions=chunk)]
            return

        self._chunks.append(_Chunk(start_step=self._step, actions=chunk))
        self._prune()

    def has_action(self) -> bool:
        self._prune()
        return bool(self._chunks)

    def steps_until_empty(self) -> int:
        self._prune()
        if not self._chunks:
            return 0
        return max(c.actions.shape[0] - (self._step - c.start_step) for c in self._chunks)

    def next_action(self) -> np.ndarray:
        self._prune()
        if not self._chunks:
            raise RuntimeError("No valid action chunk is available")

        if not self._use_temporal_ensemble:
            chunk = self._chunks[-1]
            age = self._step - chunk.start_step
            selected = chunk.actions[age]
            self._step += 1
            return selected.astype(np.float32)

        candidates: List[Tuple[float, np.ndarray]] = []
        for c in self._chunks:
            age = self._step - c.start_step
            if 0 <= age < c.actions.shape[0]:
                candidates.append((np.exp(-self._k * age), c.actions[age]))
        if not candidates:
            raise RuntimeError("No valid action remains after pruning")
        else:
            weights = np.asarray([w for w, _ in candidates], dtype=np.float32)
            actions = np.stack([a for _, a in candidates], axis=0)
            weights = weights / np.sum(weights)
            selected = np.sum(actions * weights[:, None], axis=0)

        self._step += 1
        return selected.astype(np.float32)

    def select(self, action_chunk: np.ndarray) -> np.ndarray:
        """Compatibility helper for tests and one-shot callers."""
        self.add_chunk(action_chunk)
        return self.next_action()

    def _prune(self) -> None:
        self._chunks = [
            c for c in self._chunks if 0 <= self._step - c.start_step < c.actions.shape[0]
        ]
