# C source style

Use the repository's `.clang-format` for C source, headers and C fixture
includes. [Building](building.md#c-formatting) explains the pinned formatter
and the check used by CI. Assembly includes keep their assembler syntax.

Give each C source/header, Python script, assembly source and source include
a file header at the top, after a required shebang or an existing SPDX notice.
Use `@file` with the actual filename, `@author` with `cupidthecat`, `@brief`
with a concrete description of its role, `@date` in `YYYY-MM-DD` form and the
CupidGBA copyright and license notice below. Keep descriptions consistent with
the current code and its verification limits. Preserve useful bullets about
timing, ownership and other responsibilities between the metadata and license.

```c
/**
 * @file      module.h
 * @author    cupidthecat
 * @brief     Describe this file's interface or implementation.
 * @date      2026-11-16
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
```

C files and C includes use a `/** ... */` block. Python uses `#` comments for
the same fields and notice, preserving its shebang and module docstring. ARM
assembly and assembly includes use `@` comments. Keep existing SPDX identifiers
and license notices intact, including the source notices collected in `LICENSE`.
Generated includes emit their complete headers from the generator too. Keep
their dates fixed in the templates so rebuilding does not change the output
with the host clock. Retain exact fixture reconstruction checks and verify
that assembled bytes stay unchanged.

Build scripts, workflow files and `.clang-format` use the same fields in `#`
comments. JSON fixture manifests retain their valid JSON syntax without comments.

Run `python3 tests/platform/check_file_headers.py` to check leading metadata and
the complete license notice. The check covers tracked project source, fixtures
and build configuration, with support for source archives. It reports missing
or malformed headers without rewriting files. CI runs it before the Linux GCC
and Windows builds and through the registered CTest suite. Review descriptions
when changing behavior; the check cannot determine whether prose matches code.

Use block comments consistently:

- `/* ... */` explains implementation choices, invariants and test intent.
- `/** ... */` documents files, APIs and named documentation sections. Keep
  existing `@file`, `@brief`, `@param` and related tags with their definitions.
- `/**< ... */` documents a structure member beside its declaration.

Put a standalone comment directly above the declaration or statement it describes.
Do not leave a blank line between the comment and the code. When a section needs
separation, put the blank line before its comment instead. A trailing comment
stays attached to the statement on its own line; it does not describe the next
statement.

`python3 tests/platform/check_comment_spacing.py` checks C and Python comments.
Use `--fix` to remove existing gaps, then run the normal formatter. The check
preserves comment text, string contents and continued C macro/comment lines.
CI runs it alongside the formatter and as part of the regression suite.

For longer comments, use a leading `*` on each continuation line. Write complete
sentences when explaining behavior. Short labels beside constants can remain
phrases. Explain timing, ownership and hardware constraints rather than
restating an obvious assignment. Update a comment when its behavior changes;
do not remove a useful explanation just to shorten a file.

Keep required license and copyright notices intact. Reference research belongs
in private working directories; public comments describe CupidGBA's behavior
and its verified limits.

Place subsystem implementation and tests in matching folders. Shared memory
feedback belongs under `src/gba/memory/` and `tests/gba/memory/`, for example.
Refactor a subsystem in steps and run its regressions after each structural
change. Generated observations retain their values and their reconstruction
checks when formatting changes.
