#!/usr/bin/env python3
"""Report concrete CWXEditor module coverage and fail on inconsistent pairs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


GENERATED_MARKER = "Generated from the DMD semantic AST"
PLACEHOLDER_MARKERS = (
    "cwxeditor::port::",
    "cwxeditor::port::Type<",
    "cwxeditor::port::Return<",
    "cwxeditor::port::Base<",
    "cwxeditor_src/lib/d_compat.hpp",
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    inventory = json.loads((root / "inventory.json").read_text(encoding="utf-8"))
    concrete: list[str] = []
    generated: list[str] = []
    invalid: list[str] = []

    for item in inventory["files"]:
        header = root / item["hpp"]
        source = root / item["cpp"]
        if not header.is_file() or not source.is_file():
            invalid.append(f"missing pair: {item['d_source']}")
            continue
        header_text = header.read_text(encoding="utf-8")
        source_text = source.read_text(encoding="utf-8")
        text = header_text + "\n" + source_text
        has_generated_marker = GENERATED_MARKER in header_text
        placeholders = [marker for marker in PLACEHOLDER_MARKERS if marker in text]
        if has_generated_marker:
            generated.append(item["d_source"])
        else:
            concrete.append(item["d_source"])
            if placeholders:
                invalid.append(
                    f"concrete pair retains placeholders: {item['d_source']}: "
                    + ", ".join(placeholders)
                )

    print(f"runtime modules: {len(inventory['files'])}")
    print(f"concrete modules: {len(concrete)}")
    print(f"generated catalog modules: {len(generated)}")
    for message in invalid:
        print(f"ERROR: {message}")

    if invalid or (args.require_complete and generated):
        if args.require_complete:
            for module in generated:
                print(f"PENDING: {module}")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
