# C source style

Use the repository's `.clang-format` for C source, headers and C fixture
includes. [Building](building.md#c-formatting) explains the pinned formatter
and the check used by CI. Assembly includes keep their assembler syntax.

Use block comments consistently:

- `/* ... */` explains implementation choices, invariants and test intent.
- `/** ... */` documents files, APIs and named documentation sections. Keep
  existing `@file`, `@brief`, `@param` and related tags with their definitions.
- `/**< ... */` documents a structure member beside its declaration.

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
