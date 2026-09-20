#!/usr/bin/env python3
"""Phase 6.C (design-review.md section 7, Phase 6 / section 8's "CPU
conformance" row): diff the normalized golden-scenario summaries produced by
Testing/Cxx/TestCrossBackendGoldenScenario.cpp's KINETO and ITT legs
(`cross_backend_golden.json`, one per build) for exact agreement on event
identities and counts.

KINETO and ITT cannot coexist in one process (mutually exclusive per
build), so this never runs in-process -- it is a small, standalone
post-build diff over two JSON files, matching Scripts/check_architecture.py's
own "cheap, purpose-built check" style.
"""

import argparse
import json
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kineto_json", help="cross_backend_golden.json from the KINETO leg")
    parser.add_argument("itt_json", help="cross_backend_golden.json from the ITT leg")
    args = parser.parse_args()

    with open(args.kineto_json, encoding="utf-8") as handle:
        kineto = json.load(handle)
    with open(args.itt_json, encoding="utf-8") as handle:
        itt = json.load(handle)

    if kineto == itt:
        print(f"OK: KINETO and ITT agree on {len(kineto)} golden_* event identities/counts")
        for name, count in sorted(kineto.items()):
            print(f"  - {name}: {count}")
        return 0

    print("MISMATCH: KINETO and ITT golden-scenario summaries disagree", file=sys.stderr)
    names = sorted(set(kineto) | set(itt))
    for name in names:
        k = kineto.get(name)
        i = itt.get(name)
        if k != i:
            print(f"  {name}: kineto={k!r} itt={i!r}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
