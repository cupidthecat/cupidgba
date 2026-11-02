# @file test_comment_spacing.py
# @brief Preserve source bytes except empty lines after standalone comments.
"""Preserve source bytes except empty lines after standalone comments."""

# SPDX-License-Identifier: GPL-3.0-or-later
import ast
import contextlib
import io
import unittest
from pathlib import Path
import tempfile
from unittest.mock import patch

from check_comment_spacing import check_files, close_gaps, gaps, source_paths


class CommentSpacing(unittest.TestCase):
    def test_c_comments_attach_to_the_following_definition(self):
        for comment in [
            "// Explanation.",
            "/* Explanation. */",
            "/**\n * @brief Explanation.\n */",
        ]:
            source = "void previous(void) {}\n\n" + comment + "\n\n\nvoid function(void) {}\n"
            expected = "void previous(void) {}\n\n" + comment + "\nvoid function(void) {}\n"
            self.assertEqual(close_gaps(source), expected)
            self.assertEqual(len(gaps(source)), 1)
            self.assertEqual(gaps(expected), [])

    def test_comment_groups_and_preprocessor_code(self):
        source = "/* Section. */\n\n/** Definition. */\n\n#define VALUE 1\n"
        self.assertEqual(
            close_gaps(source), "/* Section. */\n/** Definition. */\n#define VALUE 1\n"
        )

    def test_license_text_and_internal_comment_paragraphs_stay_intact(self):
        comment = "/* Copyright example\n *\n * License paragraph.\n */"
        self.assertEqual(
            close_gaps(comment + '\n\n#include "test.h"\n'), comment + '\n#include "test.h"\n'
        )

    def test_trailing_comments_and_end_of_file_remain_separate(self):
        for source in [
            "int x; /* Explains x. */\n\nint y;\n",
            "int x; // Explains x.\n\nint y;\n",
            "int x; /* Trailing\n * explanation. */\n\nint y;\n",
            "/* Footer. */\n\n",
            "// Footer.\n\n",
        ]:
            self.assertEqual(close_gaps(source), source)

    def test_quoted_comment_markers_are_not_comments(self):
        source = "const char *text = \"/* not a comment */\\n\\n\";\n\nchar c = '/';\n"
        self.assertEqual(close_gaps(source), source)
        source = 'const char *text = "escaped \\" // text";\n\nint x;\n'
        self.assertEqual(close_gaps(source), source)

    def test_continued_line_comments_and_macros_are_not_spliced(self):
        for source in [
            "// Continued ??/\n\nint x;\n",
            "// Continued \\\n\nint x;\n",
            "/* Macro. */ \\\n\nint x;\n",
            "#define X \\\n    /* Macro. */ \\\n\nint x;\n",
        ]:
            self.assertEqual(close_gaps(source), source)

    def test_crlf_and_indentation_are_preserved(self):
        source = "    /* Local. */\r\n \t\r\n    int x;\r\n"
        self.assertEqual(close_gaps(source), "    /* Local. */\r\n    int x;\r\n")

    def test_python_comments_attach_without_changing_the_syntax_tree(self):
        source = "# SPDX-License-Identifier: example\n\nimport sys\n\n\n# Explanation.\n\ndef f():\n    # Local.\n\n    return sys.version\n"
        fixed = close_gaps(source, "python")
        self.assertNotIn("# Explanation.\n\n", fixed)
        self.assertNotIn("# Local.\n\n", fixed)
        self.assertEqual(ast.dump(ast.parse(source)), ast.dump(ast.parse(fixed)))
        self.assertEqual(close_gaps(fixed, "python"), fixed)

    def test_python_strings_and_trailing_comments_are_unchanged(self):
        source = 'text = """# Data\n\nnot code"""\nvalue = 1  # Explains value.\n\nother = 2\n'
        self.assertEqual(close_gaps(source, "python"), source)


class SourceEnumeration(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    @patch("check_comment_spacing.subprocess.check_output")
    def test_git_inventory_keeps_paths_with_spaces(self, command):
        (self.root / ".git").mkdir()
        command.return_value = b"src/file name.c\0README.md\0include/test.h\0tests/check.py\0"
        self.assertEqual(
            source_paths(self.root), ["include/test.h", "src/file name.c", "tests/check.py"]
        )
        command.assert_called_once_with(["git", "ls-files", "-z"], cwd=self.root)

    @patch("check_comment_spacing.subprocess.check_output", side_effect=FileNotFoundError)
    def test_missing_git_is_an_explicit_failure(self, command):
        (self.root / ".git").mkdir()
        with self.assertRaisesRegex(ValueError, "Git is required"):
            source_paths(self.root)
        command.assert_called_once()

    @patch("check_comment_spacing.subprocess.check_output")
    def test_source_archive_is_checked_without_git(self, command):
        for name in ["src/file.c", "include/test.h", "tests/check.py", ".local-research/private.c"]:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                "# Note.\nimport sys\n" if name.endswith(".py") else "/* Note. */\n\nint x;\n"
            )
        names = source_paths(self.root)
        self.assertEqual(names, ["include/test.h", "src/file.c", "tests/check.py"])
        with self.assertRaisesRegex(ValueError, "blank line"):
            check_files(self.root, names)
        with contextlib.redirect_stdout(io.StringIO()):
            check_files(self.root, names, fix=True)
        check_files(self.root, names)
        self.assertEqual((self.root / "src/file.c").read_text(), "/* Note. */\nint x;\n")
        command.assert_not_called()

    def test_paths_outside_the_source_root_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "leaves the repository"):
            check_files(self.root, ["../outside.c"], fix=True)


if __name__ == "__main__":
    unittest.main()
