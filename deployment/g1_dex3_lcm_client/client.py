from __future__ import annotations

from concurrent.futures import Future, ThreadPoolExecutor
import logging
import time
from typing import Optional

import numpy as np

from .action_adapter import ActionAdapter
from .config import ClientConfig, parse_args
from .lcm_action import LcmActionPublisher
from .lcm_state import LcmStateReceiver
from .policy_client import PolicyClient
from .teleimager_camera import TeleimagerCamera


def _future_done(future: Future) -> bool:
    return future.done()


def run(config: ClientConfig) -> None:
    logging.info("Starting G1 Dex3 deploy client. dry_run=%s", config.dry_run)
    camera = TeleimagerCamera(
        host=config.image_host,
        request_port=config.image_request_port,
        teleimager_src=config.teleimager_src,
        primary_view=config.primary_view,
    )
    state_receiver = LcmStateReceiver(config.lcm_url, config.state_channel)
    state_receiver.start()
    policy = PolicyClient(config.policy_url, timeout_s=config.policy_timeout_s)
    action_adapter = ActionAdapter(config.use_temporal_ensemble, config.temporal_ensemble_k)
    publisher = (
        None if config.dry_run else LcmActionPublisher(config.lcm_url, config.action_channel)
    )

    period = 1.0 / config.control_hz
    pending_policy: Optional[Future] = None
    last_q_target: Optional[np.ndarray] = None
    hold_count = 0
    try:
        with ThreadPoolExecutor(max_workers=1) as executor:
            while True:
                loop_start = time.monotonic()
                robot_state = state_receiver.latest()

                if pending_policy is not None and _future_done(pending_policy):
                    try:
                        action_adapter.add_chunk(pending_policy.result())
                        logging.info(
                            "received policy chunk; remaining_steps=%d",
                            action_adapter.steps_until_empty(),
                        )
                    except Exception:
                        logging.exception("Policy request failed")
                        if config.require_policy:
                            raise
                    finally:
                        pending_policy = None

                should_request_policy = pending_policy is None and (
                    not action_adapter.has_action()
                    or action_adapter.steps_until_empty() <= config.replan_steps
                )
                if should_request_policy:
                    if robot_state is None:
                        logging.warning("No fresh robot state; cannot request policy chunk")
                    else:
                        try:
                            image_primary, image_secondary = camera.get_primary_secondary()
                            state_28 = robot_state.dex3_state_28()
                        except Exception:
                            logging.exception("Failed to build policy observation")
                            if config.require_policy:
                                raise
                        else:
                            pending_policy = executor.submit(
                                policy.act,
                                image_primary=image_primary,
                                image_secondary=image_secondary,
                                state=state_28,
                                instruction=config.instruction,
                                task_name=config.task_name,
                            )
                            logging.debug(
                                "submitted policy request at state_seq=%d", robot_state.sequence
                            )

                if action_adapter.has_action():
                    q_target = action_adapter.next_action()
                    last_q_target = q_target.copy()
                    hold_count = 0
                elif last_q_target is not None and pending_policy is not None:
                    if hold_count >= config.max_hold_steps:
                        if hold_count == config.max_hold_steps:
                            logging.error(
                                "Max hold steps reached; pausing action publish so bridge timeout can damp"
                            )
                        hold_count += 1
                        elapsed = time.monotonic() - loop_start
                        if elapsed < period:
                            time.sleep(period - elapsed)
                        continue
                    q_target = last_q_target.copy()
                    hold_count += 1
                    logging.warning(
                        "Holding last q_target while waiting for next policy chunk (%d/%d)",
                        hold_count,
                        config.max_hold_steps,
                    )
                else:
                    elapsed = time.monotonic() - loop_start
                    if elapsed < period:
                        time.sleep(period - elapsed)
                    continue

                if not np.all(np.isfinite(q_target)):
                    raise ValueError("Policy produced non-finite q_target")

                state_seq = robot_state.sequence if robot_state is not None else -1
                if publisher is None:
                    logging.info(
                        "dry-run action shape=%s min=%.4f max=%.4f state_seq=%d",
                        q_target.shape,
                        float(np.min(q_target)),
                        float(np.max(q_target)),
                        state_seq,
                    )
                else:
                    seq = publisher.publish(q_target)
                    logging.info("published action seq=%d state_seq=%d", seq, state_seq)

                elapsed = time.monotonic() - loop_start
                if elapsed < period:
                    time.sleep(period - elapsed)
    finally:
        state_receiver.stop()
        camera.close()


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    run(parse_args())


if __name__ == "__main__":
    main()
