from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


def default_repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


@dataclass
class ClientConfig:
    policy_url: str
    image_host: str
    image_request_port: int
    lcm_url: str
    state_channel: str
    action_channel: str
    control_hz: float
    task_name: str
    instruction: str
    primary_view: str
    dry_run: bool
    use_temporal_ensemble: bool
    temporal_ensemble_k: float
    replan_steps: int
    max_hold_steps: int
    policy_timeout_s: float
    teleimager_src: Path
    require_policy: bool


def build_argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Thor-side G1 Dex3 VLA deploy client")
    parser.add_argument("--policy-url", default="http://127.0.0.1:8777/act")
    parser.add_argument("--image-host", required=True, help="G1 PC2 image server IP")
    parser.add_argument("--image-request-port", type=int, default=60000)
    parser.add_argument("--lcm-url", default="udpm://239.255.76.67:7667?ttl=1")
    parser.add_argument("--state-channel", default="G1_ROBOT_STATE")
    parser.add_argument("--action-channel", default="G1_POLICY_ACTION")
    parser.add_argument("--control-hz", type=float, default=10.0)
    parser.add_argument("--task-name", required=True)
    parser.add_argument("--instruction", required=True)
    parser.add_argument("--primary-view", choices=("left", "right"), default="left")
    parser.add_argument("--dry-run", action="store_true", default=True)
    parser.add_argument("--execute-actions", action="store_true", help="Publish LCM actions")
    parser.add_argument("--no-temporal-ensemble", action="store_true")
    parser.add_argument("--temporal-ensemble-k", type=float, default=0.01)
    parser.add_argument(
        "--replan-steps",
        type=int,
        default=8,
        help="Request a new policy chunk when this many steps remain in the current chunk.",
    )
    parser.add_argument(
        "--max-hold-steps",
        type=int,
        default=2,
        help="Maximum publish ticks to resend the last q_target while waiting for policy.",
    )
    parser.add_argument(
        "--policy-timeout-s",
        type=float,
        default=2.0,
        help="HTTP timeout for each policy /act request.",
    )
    parser.add_argument(
        "--teleimager-src",
        type=Path,
        default=default_repo_root() / "external/g1/29_dof_g1_deploy/thirdparty/teleimager/src",
    )
    parser.add_argument(
        "--no-require-policy",
        action="store_true",
        help="Continue loop when policy requests fail; useful for connectivity smoke tests.",
    )
    return parser


def parse_args() -> ClientConfig:
    args = build_argparser().parse_args()
    if args.control_hz <= 0:
        raise ValueError("--control-hz must be positive")
    if args.replan_steps < 1:
        raise ValueError("--replan-steps must be at least 1")
    if args.max_hold_steps < 0:
        raise ValueError("--max-hold-steps must be non-negative")
    if args.policy_timeout_s <= 0:
        raise ValueError("--policy-timeout-s must be positive")
    return ClientConfig(
        policy_url=args.policy_url,
        image_host=args.image_host,
        image_request_port=args.image_request_port,
        lcm_url=args.lcm_url,
        state_channel=args.state_channel,
        action_channel=args.action_channel,
        control_hz=args.control_hz,
        task_name=args.task_name,
        instruction=args.instruction,
        primary_view=args.primary_view,
        dry_run=not args.execute_actions,
        use_temporal_ensemble=not args.no_temporal_ensemble,
        temporal_ensemble_k=args.temporal_ensemble_k,
        replan_steps=args.replan_steps,
        max_hold_steps=args.max_hold_steps,
        policy_timeout_s=args.policy_timeout_s,
        teleimager_src=args.teleimager_src,
        require_policy=not args.no_require_policy,
    )
