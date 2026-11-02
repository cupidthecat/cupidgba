#!/usr/bin/env python3
# @file check_comment_spacing.py
# @brief Keep standalone C and Python comments attached to the code below them.
"""Keep standalone C and Python comments attached to the code below them."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import io
from pathlib import Path
import re
import subprocess
import tokenize

C_TOKENS = re.compile(
    r'"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'|'
    r"(?P<block>/\*[\s\S]*?\*/)|(?P<line>//[^\r\n]*)"
)
BLANK_AFTER = re.compile(r"([ \t]*\r?\n)(?:[ \t]*\r?\n)+")


def comments(text, language):
    if language == "python":
        offsets = [0]
        for line in text.splitlines(keepends=True):
            offsets.append(offsets[-1] + len(line))
        for token in tokenize.generate_tokens(io.StringIO(text).readline):
            if token.type == tokenize.COMMENT:
                yield offsets[token.start[0] - 1] + token.start[1], offsets[
                    token.end[0] - 1
                ] + token.end[1]
    else:
        for match in C_TOKENS.finditer(text):
            if match.lastgroup in ("block", "line"):
                # Splicing this physical line could turn following code into
                # part of a continued C line comment. Leave that line intact.
                if match.lastgroup == "line" and match.group().rstrip().endswith(("\\", "??/")):
                    continue
                yield match.start(), match.end()


def gaps(text, language="c"):
    found = []
    for start, end in comments(text, language):
        line_start = text.rfind("\n", 0, start) + 1
        if text[line_start:start].strip():
            continue  # A trailing comment belongs to the preceding statement.
        after = BLANK_AFTER.match(text, end)
        if after and text[after.end() :].strip():
            # Preserve the comment's own line ending, removing only empty
            # physical lines between that standalone comment and its code.
            found.append((end + len(after.group(1)), after.end(), text.count("\n", 0, start) + 1))
    return found


def close_gaps(text, language="c"):
    for start, end, _ in reversed(gaps(text, language)):
        text = text[:start] + text[end:]
    return text


def supported(name):
    return name.endswith((".c", ".h", ".py")) or (
        name.startswith("tests/") and name.endswith(".inc")
    )


def source_paths(root):
    if (root / ".git").exists():
        try:
            names = (
                subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
            )
        except FileNotFoundError as error:
            raise ValueError(
                "Git is required to enumerate a checkout; install Git on PATH"
            ) from error
    else:
        # Source archives need the same checks without a Git installation.
        names = [
            path.relative_to(root).as_posix()
            for folder in ("src", "include", "tests", "tools")
            for path in (root / folder).rglob("*")
            if path.is_file()
        ]
        names += [path.name for path in root.iterdir() if path.is_file()]
    return sorted(name for name in names if supported(name))


def check_files(root, names, fix=False):
    failures = []
    for name in names:
        path = root / name
        if not path.resolve().is_relative_to(root.resolve()):
            raise ValueError(f"Source path leaves the repository: {name}")
        text = path.read_bytes().decode("utf-8")
        language = "python" if name.endswith(".py") else "c"
        detached = gaps(text, language)
        if not detached:
            continue
        if fix:
            path.write_bytes(close_gaps(text, language).encode("utf-8"))
            print(f"Fixed {name}: {len(detached)} comment gaps")
        else:
            failures += [
                f"{name}:{line}: blank line between a standalone comment and its code"
                for _, _, line in detached
            ]
    if failures:
        raise ValueError("\n".join(failures))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix", action="store_true", help="Remove detached-comment blank lines")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    try:
        names = source_paths(root)
        if not names:
            raise ValueError("No C or Python source files found")
        check_files(root, names, args.fix)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    print(f"PASS comment spacing: {len(names)} source, header and fixture files")


if __name__ == "__main__":
    main()
