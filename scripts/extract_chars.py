#!/usr/bin/env python3
"""extract_chars.py

Extract unique non-whitespace characters from a text file and write them
to an output file (one line, characters concatenated). By default reads
`data/font test.txt` and writes `scripts/chars_input.txt`.

Usage:
  python scripts/extract_chars.py [input_file] [output_file]

Options:
    By default the output is sorted by Unicode codepoint. Use
    `--preserve-order` to keep the first-seen order instead.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import sys


def extract_unique_chars(text: str, keep_whitespace: bool = False) -> str:
    seen = set()
    ordered = []
    for ch in text:
        if not keep_whitespace and ch.isspace():
            continue
        if ch not in seen:
            seen.add(ch)
            ordered.append(ch)
    return "".join(ordered)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Extract unique characters from a file"
    )
    parser.add_argument(
        "input",
        nargs="?",
        default="data/font test.txt",
        help="Input text file (default: data/font test.txt)",
    )
    parser.add_argument(
        "output",
        nargs="?",
        default="scripts/chars_input.txt",
        help="Output file to write characters (default: scripts/chars_input.txt)",
    )
    parser.add_argument(
        "--preserve-order",
        action="store_true",
        help="Preserve first-seen order instead of sorting (default: sort)",
    )
    parser.add_argument(
        "--keep-whitespace",
        action="store_true",
        help="Include whitespace characters (space, tab, etc.) in output",
    )
    args = parser.parse_args(argv)

    input_path = Path(args.input)
    output_path = Path(args.output)

    if not input_path.exists():
        print(f"Error: input file '{input_path}' not found", file=sys.stderr)
        return 2

    text = input_path.read_text(encoding="utf-8")

    # By default we sort the unique characters. Use --preserve-order to keep
    # the original first-seen ordering.
    if not args.preserve_order:
        chars = sorted(
            {ch for ch in text if (args.keep_whitespace or not ch.isspace())}
        )
        out = "".join(chars)
    else:
        out = extract_unique_chars(text, keep_whitespace=args.keep_whitespace)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(out + "\n", encoding="utf-8")

    print(f"Wrote {len(out)} characters to '{output_path}'")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
