#!/usr/bin/env python3
"""Phase 6.D (design-review.md section 7, Phase 6): CI must distinguish
"verified on real hardware" from "silently skipped everywhere" for
real-hardware-gated tests, rather than relying on a human reading the raw
log -- a GTEST_SKIP() exits 0 exactly like a real pass.

Parses a GoogleTest JUnit XML report (written via the GTEST_OUTPUT
environment variable) for one test suite and reports how many of its cases
ran for real versus were skipped. Exits non-zero only on a malformed/missing
report -- this is a visibility tool, not a new pass/fail gate, matching the
"toolkit-only builds test compilation, not GPU capture" framing: a CUDA
toolkit-only CI runner is expected to skip every real-hardware case, and
that is not itself a failure.
"""

from __future__ import annotations

import argparse
import os
import sys
import xml.etree.ElementTree as ET


def find_suite(root: ET.Element, suite_name: str) -> ET.Element | None:
    for suite in root.iter("testsuite"):
        if suite.get("name") == suite_name:
            return suite
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xml_path", help="Path to a GoogleTest JUnit XML report")
    parser.add_argument(
        "--suite",
        default="GpuRealHardware",
        help="Test suite name to report on (default: GpuRealHardware)",
    )
    parser.add_argument(
        "--summary-file",
        default=os.environ.get("GITHUB_STEP_SUMMARY"),
        help="Optional Markdown file to append the summary to (defaults to "
        "$GITHUB_STEP_SUMMARY when set)",
    )
    args = parser.parse_args()

    if not os.path.isfile(args.xml_path):
        print(f"error: no such report: {args.xml_path}", file=sys.stderr)
        return 1

    tree = ET.parse(args.xml_path)
    suite = find_suite(tree.getroot(), args.suite)
    if suite is None:
        print(f"error: no <testsuite name=\"{args.suite}\"> in {args.xml_path}", file=sys.stderr)
        return 1

    total = int(suite.get("tests", "0"))
    skipped = int(suite.get("skipped", "0"))
    failed = int(suite.get("failures", "0")) + int(suite.get("errors", "0"))
    ran_for_real = total - skipped

    if ran_for_real > 0:
        headline = f"{ran_for_real}/{total} `{args.suite}` tests ran on real hardware"
    else:
        headline = (
            f"0/{total} `{args.suite}` tests ran on real hardware "
            f"(toolkit-only: every case skipped cleanly)"
        )
    if failed:
        headline += f", {failed} FAILED"

    print(headline)
    for case in suite.iter("testcase"):
        name = case.get("name", "?")
        if case.find("skipped") is not None:
            state = "skipped"
        elif case.find("failure") is not None or case.find("error") is not None:
            state = "FAILED"
        else:
            state = "ran"
        print(f"  - {args.suite}.{name}: {state}")

    if args.summary_file:
        with open(args.summary_file, "a", encoding="utf-8") as handle:
            handle.write(f"### {args.suite} real-hardware status\n\n{headline}\n\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
