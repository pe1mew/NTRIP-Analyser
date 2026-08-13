# Doxygen belongs in headers, not in the .c file

In NTRIP-Analyser, Doxygen documentation goes in the **header**, not in
the implementation file. A function declared in a `.h` carries its
`/** ... */` contract there and nowhere else.

**Why:** Doxygen merges a declaration's block with a definition's block,
so documenting both produces duplicated output and spurious warnings —
`ParseMountTable` was reported as having eight parameters when it takes
four, purely because the header and `gui_parsers.c` each documented the
same four. Beyond the tooling, the header is the contract a caller
reads; the implementation is where it is carried out.

**How to apply:** When adding or moving documentation, put the `/** */`
block on the declaration in the header. In the `.c`, use a plain `/* */`
comment for implementation notes that genuinely belong there (bit
layouts, algorithm choices, why a branch exists). File-local `static`
functions have no header, so their Doxygen blocks stay in the `.c` —
the rule is about avoiding duplication, not about stripping comments.

A Doxyfile at the repository root turns this into an enforced check:
`WARN_IF_DOC_ERROR` reports the merge conflicts.
