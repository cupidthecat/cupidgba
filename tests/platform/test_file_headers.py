#!/usr/bin/env python3
# @file      test_file_headers.py
# @author    cupidthecat
# @brief     Exercise owned-header parsing, malformed notices and source inventory selection.
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
"""Exercise owned-header parsing, malformed notices and source inventory selection."""

# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from check_file_headers import NOTICE, check_files, header_errors, source_paths


def sample_header(name, style="block"):
    lines = [f"@file      {name}", "@author    cupidthecat",
             "@brief     Validate hardware events and matching register access.",
             "@date      2026-11-16", "", *NOTICE]
    if style == "block":
        return "/**\n" + "".join(" *" + (f" {item}" if item else "") + "\n" for item in lines) + " */\n"
    return "".join(style + (f" {item}" if item else "") + "\n" for item in lines)


class LeadingHeaders(unittest.TestCase):
    def test_actual_c_comment_after_spdx(self):
        for name in ("test.c", "test.h", "test.inc", "test.s"):
            text = "/* SPDX-License-Identifier: GPL-3.0-or-later */\n" + sample_header(name)
            self.assertEqual(header_errors(text + "int value;\n", name), [])
        tabbed = sample_header("test.c").replace("@file      test.c", "@file\ttest.c")
        self.assertEqual(header_errors(tabbed, "test.c"), [])

    def test_python_build_and_assembly_comment_forms(self):
        cases = (
            ("check.py", "#!/usr/bin/env python3\n", "#"),
            ("CHECK.PY", "#!/usr/bin/env python3\n", "#"),
            ("Makefile", "", "#"),
            ("CMakeLists.txt", "", "#"),
            (".clang-format", "", "#"),
            (".github/workflows/ci.yml", "", "#"),
            ("driver.s", "", "@"),
            ("driver.inc", "", "@"),
        )
        for name, prefix, style in cases:
            with self.subTest(name=name):
                self.assertEqual(header_errors(prefix + sample_header(Path(name).name, style)
                                               + "actual_code = True\n", name), [])
        short = sample_header("arm.c").replace(
            "Validate hardware events and matching register access.", "Emulate ARM7TDMI instructions."
        )
        self.assertEqual(header_errors(short, "arm.c"), [])

    def test_tag_and_notice_mismatches_fail(self):
        source = sample_header("sample.c")
        changed = {
            "filename": source.replace("@file      sample.c", "@file      wrong.c"),
            "author": source.replace("@author    cupidthecat", "@author    somebody"),
            "missing brief": source.replace("@brief     Validate hardware events and matching register access.\n", ""),
            "placeholder": source.replace("Validate hardware events and matching register access.", "TODO"),
            "date format": source.replace("2026-11-16", "11/16/2026"),
            "invalid date": source.replace("2026-11-16", "2026-02-30"),
            "copyright": source.replace("Copyright (c) 2026 cupidthecat", "Copyright (c) 2026 Other"),
            "truncated license": source.replace("WITHOUT ANY WARRANTY", "WITH WARRANTY"),
        }
        for case, text in changed.items():
            with self.subTest(case=case):
                self.assertTrue(header_errors(text, "sample.c"))

    def test_code_or_literal_before_comment_cannot_supply_header(self):
        header = sample_header("sample.c")
        self.assertIn("missing leading @file",
                      header_errors('const char *text = "' + header.replace("\n", "\\n")
                                    + '";\n' + header, "sample.c")[0])
        self.assertIn("missing leading @file", header_errors("int value;\n" + header, "sample.c")[0])
        self.assertIn("missing leading @file", header_errors("/* Notes. */\n" + header, "sample.c")[0])
        python_header = sample_header("script.py", "#")
        self.assertIn("missing leading @file",
                      header_errors('text = """\n' + python_header + '"""\n', "script.py")[0])

    def test_invalid_comment_syntax_and_same_line_spdx_bypass(self):
        c_header = sample_header("sample.c")
        python_header = sample_header("script.py", "#")
        self.assertIn("missing leading @file",
                      header_errors(sample_header("script.py"), "script.py")[0])
        self.assertIn("missing leading @file",
                      header_errors(sample_header("sample.c", "#"), "sample.c")[0])
        self.assertIn("missing leading @file",
                      header_errors(sample_header("sample.c", "@"), "sample.c")[0])
        self.assertIn("missing leading @file", header_errors(
            "/* SPDX-License-Identifier: GPL-3.0-or-later */ int value;\n" + c_header,
            "sample.c")[0])
        self.assertIn("missing leading @file", header_errors(
            "int value;\n" + python_header, "script.py")[0])


class SourceInventory(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    @patch("check_file_headers.subprocess.check_output")
    def test_git_inventory_keeps_owned_build_and_fixture_files(self, command):
        (self.root / ".git").mkdir()
        command.return_value = (
            b"CMakeLists.txt\0Makefile\0.clang-format\0"
            b".github/workflows/ci.yml\0tests/roms/probe.json\0"
            b"tests/roms/check_probe.py\0src/arm/test.c\0"
            b"external-checkout/internal.c\0.local-research/private.c\0"
        )
        self.assertEqual(source_paths(self.root), [
            ".clang-format", ".github/workflows/ci.yml", "CMakeLists.txt",
            "Makefile", "src/arm/test.c", "tests/roms/check_probe.py",
        ])
        command.assert_called_once_with(["git", "ls-files", "-z"], cwd=self.root)

    def test_archive_inventory_ignores_reference_and_build_folders(self):
        paths = (
            "src/core/file.c", "include/cupid/config.h", "tests/roms/check_probe.py",
            "tests/gba/fixture.inc", "tools/gen.s", "CMakeLists.txt", "Makefile",
            ".clang-format", ".github/actions/download/action.yml", "standalone.py",
            "external-checkout/src/dummy.c", "other-repository/src/dummy.c",
            "src/build-temporary/dummy.c", "tests/third-party/dummy.c",
            ".local-research/private.py", "tests/roms/config.json",
        )
        for name in paths:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("sample")
        self.assertEqual(source_paths(self.root), sorted(paths[:10]))

    def test_validation_fails_without_modifying_inputs(self):
        path = self.root / "sample.c"
        source = sample_header("other.c")
        path.write_text(source)
        with self.assertRaisesRegex(ValueError, "sample.c: @file"):
            check_files(self.root, ["sample.c"])
        self.assertEqual(path.read_text(), source)
        with self.assertRaisesRegex(ValueError, "leaves the repository"):
            check_files(self.root, ["../outside.c"])


if __name__ == "__main__":
    unittest.main()
