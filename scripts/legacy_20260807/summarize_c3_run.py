#!/usr/bin/env python3
import json
from collections import Counter
from pathlib import Path


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("run_dirs", nargs="+", type=Path)
    args = parser.parse_args()
    for run_dir in args.run_dirs:
        counts = Counter()
        for path in sorted(run_dir.glob("telemetry_drone_*.jsonl")):
            for raw in path.read_text(encoding="utf-8").splitlines():
                counts[json.loads(raw).get("event")] += 1
        print(run_dir, dict(counts))


if __name__ == "__main__":
    main()
