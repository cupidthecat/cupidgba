#!/usr/bin/env python3
# @file      check_file_headers.py
# @author    cupidthecat
# @brief     Validate leading file documentation and GPL notices in owned project sources.
# @date      2026-11-16
#
# @copyright Copyright (c) 2026 cupidthecat
#
# This file is part of CupidGBA.
#
# CupidGBA is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# CupidGBA is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CupidGBA.  If not, see <https://gnu.org>.
"""Validate leading file documentation and GPL notices in owned project sources."""

# SPDX-License-Identifier: GPL-3.0-or-later
from datetime import date
from pathlib import Path, PurePosixPath
import re
import subprocess


SOURCE_SUFFIXES = {".c", ".h", ".py", ".s", ".inc", ".cmake"}
BUILD_FILES = {"CMakeLists.txt", "Makefile", ".clang-format"}
OWNED_DIRS = {"src", "include", "tests", "tools"}
EXCLUDED_DIRS = {".git", ".local-research", "third_party", "third-party", "vendor", "build"}
NOTICE = """@copyright Copyright (c) 2026 cupidthecat

This file is part of CupidGBA.

CupidGBA is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

CupidGBA is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with CupidGBA.  If not, see <https://gnu.org>.""".splitlines()
TAG = re.compile(r"^@(file|author|brief|date|copyright)\s+(.+)$")


def supported(name):
    path = PurePosixPath(name)
    if not path.parts or path.is_absolute() or ".." in path.parts:
        return False
    if any(part in EXCLUDED_DIRS or part.startswith(("build-", "cmake-build-"))
           for part in path.parts[:-1]):
        return False
    if name in BUILD_FILES:
        return True
    if path.parts[0] == ".github":
        return path.suffix.lower() in {".yml", ".yaml"} | SOURCE_SUFFIXES
    return (len(path.parts) == 1 or path.parts[0] in OWNED_DIRS) and (
        path.suffix.lower() in SOURCE_SUFFIXES
    )


def source_paths(root):
    if (root / ".git").exists():
        try:
            names = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode(
                "utf-8"
            ).split("\0")
        except FileNotFoundError as error:
            raise ValueError("Git is required to enumerate this checkout") from error
        except subprocess.CalledProcessError as error:
            raise ValueError(f"Git source inventory failed: {error}") from error
    else:
        names = [
            path.relative_to(root).as_posix()
            for folder in ("src", "include", "tests", "tools", ".github")
            for path in (root / folder).rglob("*")
            if path.is_file()
        ]
        names.extend(path.name for path in root.iterdir() if path.is_file())
    return sorted(name for name in names if supported(name))


def _next_line(text, start):
    end = text.find("\n", start)
    return len(text) if end == -1 else end + 1


def _leading_header(text, name):
    """Read the first actual comment; source text after code cannot supply a header."""
    position = 1 if text.startswith("\ufeff") else 0
    suffix = PurePosixPath(name).suffix.lower()
    if suffix == ".py" and text.startswith("#!", position):
        position = _next_line(text, position)
    c_style = suffix in {".c", ".h", ".s", ".inc"}
    if suffix in {".c", ".h"}:
        markers = ("//",)
    elif suffix in {".s", ".inc"}:
        markers = ("@", "#", "//")
    else:
        markers = ("#",)
    while position < len(text):
        line_end = _next_line(text, position)
        line = text[position:line_end].strip()
        if not line:
            position = line_end
        elif ((c_style and re.fullmatch(r"/\*\s*SPDX-License-Identifier:\s*[^*\n]+\*/", line))
              or (any(line.startswith(f"{mark} SPDX-License-Identifier:") for mark in markers)
                  and re.fullmatch(r"(?://|#|@) SPDX-License-Identifier: [^\r\n]+", line))):
            position = line_end
        else:
            break

    if c_style and text.startswith("/*", position):
        end = text.find("*/", position + 2)
        if end == -1:
            return []
        body = text[position + 2:end]
        lines = [re.sub(r"^\s*\*\s?", "", line).rstrip() for line in body.splitlines()]
        while lines and not lines[0]:
            lines.pop(0)
        return lines

    first = text[position:_next_line(text, position)].lstrip()
    marker = next((mark for mark in markers if first.startswith(mark)), None)
    if marker is None:
        return []
    lines = []
    for line in text[position:].splitlines():
        stripped = line.lstrip()
        if not stripped:
            break
        if not stripped.startswith(marker):
            break
        lines.append(stripped[len(marker):].removeprefix(" ").rstrip())
    return lines


def header_errors(text, name):
    lines = _leading_header(text, name)
    first_tag = TAG.fullmatch(lines[0]) if lines else None
    if not first_tag or first_tag.group(1) != "file":
        return ["missing leading @file comment"]
    tags = {}
    for line in lines:
        match = TAG.fullmatch(line)
        if match:
            tags.setdefault(match.group(1), []).append(match.group(2))

    errors = []
    for key in ("file", "author", "brief", "date", "copyright"):
        if len(tags.get(key, ())) != 1:
            errors.append(f"expected one @{key} tag")
    if tags.get("file") and tags["file"][0] != PurePosixPath(name).name:
        errors.append(f"@file is {tags['file'][0]!r}, expected {PurePosixPath(name).name!r}")
    if tags.get("author") and tags["author"][0] != "cupidthecat":
        errors.append("@author must be cupidthecat")
    if tags.get("brief"):
        brief = tags["brief"][0]
        if (not re.search(r"[A-Za-z]", brief)
                or re.search(r"\b(TODO|TBD|FIXME|placeholder)\b", brief, re.IGNORECASE)):
            errors.append("@brief must describe this file concretely")
    if tags.get("date"):
        value = tags["date"][0]
        try:
            if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", value):
                raise ValueError(value)
            date.fromisoformat(value)
        except ValueError:
            errors.append("@date must be a valid YYYY-MM-DD date")
    if tags.get("copyright") and tags["copyright"][0] != NOTICE[0].split(" ", 1)[1]:
        errors.append("@copyright must match the CupidGBA template")
    license_start = lines.index(NOTICE[0]) if NOTICE[0] in lines else -1
    if license_start == -1 or lines[license_start:license_start + len(NOTICE)] != NOTICE:
        errors.append("missing or altered GPLv3-or-later notice")
    return errors


def check_files(root, names):
    failures = []
    resolved_root = root.resolve()
    for name in names:
        path = (root / name).resolve()
        if not path.is_relative_to(resolved_root):
            raise ValueError(f"Source path leaves the repository: {name}")
        try:
            source = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            failures.append(f"{name}: expected UTF-8 text")
            continue
        failures.extend(f"{name}: {message}" for message in header_errors(source, name))
    if failures:
        raise ValueError("\n".join(failures))


def main():
    root = Path(__file__).resolve().parents[2]
    try:
        names = source_paths(root)
        if not names:
            raise ValueError("No owned source files found")
        check_files(root, names)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    print(f"PASS file headers: {len(names)} owned source, fixture and build files")


if __name__ == "__main__":
    main()
