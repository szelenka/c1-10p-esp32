#!/usr/bin/env python3
"""Check resource usage against chopper_limits.h caps. Warn at 80% capacity."""

from __future__ import annotations

import re
from pathlib import Path

from gate_lib import GateReporter

LIMITS_H = Path("main/include/chopper/chopper_limits.h")
NODE_DIR = Path("main/include/chopper/nodes")
SOURCE_DIRS = [Path("main/include/chopper"), Path("main/chopper")]

WARN_THRESHOLD = 0.80

LIMIT_RE = re.compile(r"(MAX_[A-Z_]+)\s*=\s*([0-9]+)")
TOPIC_RE = re.compile(r'"([a-z]+(?:/[a-z_]+)+)"')
TOPIC_CONTEXT_RE = re.compile(
    r"createPublisher<|createSubscription<|addPublisher\(|addSubscription\("
)
SUB_CONTEXT_RE = re.compile(r"createSubscription<")


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def parse_limits() -> dict[str, int]:
    limits: dict[str, int] = {}
    for name, value in LIMIT_RE.findall(read_text(LIMITS_H)):
        limits[name] = int(value)
    return limits


def count_nodes() -> int:
    return len(list(NODE_DIR.glob("*.h")))


def collect_topics_and_subs() -> tuple[set[str], dict[str, int]]:
    topics: set[str] = set()
    subs_per_topic: dict[str, int] = {}
    for root in SOURCE_DIRS:
        for ext in ("*.h", "*.cpp"):
            for path in root.rglob(ext):
                for line in read_text(path).splitlines():
                    stripped = line.lstrip()
                    if stripped.startswith("//") or stripped.startswith("*"):
                        continue
                    if TOPIC_CONTEXT_RE.search(line):
                        for topic in TOPIC_RE.findall(line):
                            topics.add(topic)
                    if SUB_CONTEXT_RE.search(line):
                        for topic in TOPIC_RE.findall(line):
                            subs_per_topic[topic] = subs_per_topic.get(topic, 0) + 1
    return topics, subs_per_topic


def main() -> int:
    reporter = GateReporter("check-capacity")
    limits = parse_limits()

    # Nodes
    if NODE_DIR.is_dir():
        node_count = count_nodes()
        max_nodes = limits.get("MAX_NODES", 32)
        pct = node_count / max_nodes if max_nodes > 0 else 0
        if pct >= 1.0:
            reporter.fail("nodes", str(LIMITS_H), 0,
                          f"Nodes: {node_count}/{max_nodes} ({pct:.0%}) — limit exceeded")
        elif pct >= WARN_THRESHOLD:
            reporter.warn("nodes", str(LIMITS_H), 0,
                          f"Nodes: {node_count}/{max_nodes} ({pct:.0%}) — approaching limit")
        else:
            reporter.pass_("nodes", message=f"Nodes: {node_count}/{max_nodes} ({pct:.0%})")

    # Topics
    topics, subs_per_topic = collect_topics_and_subs()
    max_topics = limits.get("MAX_TOPICS", 32)
    topic_count = len(topics)
    pct = topic_count / max_topics if max_topics > 0 else 0
    if pct >= 1.0:
        reporter.fail("topics", str(LIMITS_H), 0,
                      f"Topics: {topic_count}/{max_topics} ({pct:.0%}) — limit exceeded")
    elif pct >= WARN_THRESHOLD:
        reporter.warn("topics", str(LIMITS_H), 0,
                      f"Topics: {topic_count}/{max_topics} ({pct:.0%}) — approaching limit")
    else:
        reporter.pass_("topics", message=f"Topics: {topic_count}/{max_topics} ({pct:.0%})")

    # Subscribers per topic
    max_subs = limits.get("MAX_SUBSCRIBERS_PER_TOPIC", 8)
    for topic, count in sorted(subs_per_topic.items()):
        pct = count / max_subs if max_subs > 0 else 0
        if pct >= 1.0:
            reporter.fail("subscribers", str(LIMITS_H), 0,
                          f"Subscribers on '{topic}': {count}/{max_subs} ({pct:.0%}) — limit exceeded")
        elif pct >= WARN_THRESHOLD:
            reporter.warn("subscribers", str(LIMITS_H), 0,
                          f"Subscribers on '{topic}': {count}/{max_subs} ({pct:.0%}) — approaching limit")

    return reporter.finish()


if __name__ == "__main__":
    raise SystemExit(main())
