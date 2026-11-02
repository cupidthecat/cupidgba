# C source style

Use the repository's `.clang-format` for C source, headers and C fixture
includes. [Building](building.md#c-formatting) explains the pinned formatter
and the check used by CI. Assembly includes keep their assembler syntax.

Give each C source/header, Python script, assembly source and source include
a file header near the top. Use `@file` with the actual filename and `@brief`
with a concrete description of its role. Add short bullets when timing,
ownership or several responsibilities need explanation. Keep the description
consistent with the current code and its verification limits.

C files and C includes use a `/** ... */` block. Python uses `# @file` and
`# @brief` comments, preserving its shebang and module docstring. Assembly
uses its accepted comment syntax. Keep existing license notices intact.
Generated includes emit their headers from the generator too; retain exact
fixture reconstruction checks and verify that assembled bytes stay unchanged.

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
