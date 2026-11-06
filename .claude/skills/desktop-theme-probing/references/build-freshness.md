# Confirming the build a probe measures

`SKILL.md` §2 has the restart rule; this file has the stale-build signatures
and what each kind of edit needs before the next launch. A stale build reads as
a clean measurement of the old code, or as a launch failure that looks like a
probe bug.

## 1. Signatures and repairs

- **`Missing chrome or resource URL: chrome://…`** in the browser log, with a
  Marionette timeout in the probe, on the first launch after the tree moved to
  another base (e.g. a rebase, another branch). The objdir builds its chrome
  registry from the jar manifests as a copy, so it still lists the old base's
  files. Launch first and rebuild only on this signature: `./mach build faster`,
  after `touch <objdir>/CLOBBER` when `[ CLOBBER -nt <objdir>/CLOBBER ]` says
  the CLOBBER moved. A moved CLOBBER alone is no reason to rebuild: the tree
  launches fine while the registry still matches. A checkout rewrites CLOBBER
  with a new mtime whenever its content differs between the two revisions, so
  after switching the test reads moved even for an objdir already good for the
  new revision; `touch <objdir>/CLOBBER` once it is (e.g. right after
  `./mach artifact install`).
- **`Missing chrome or resource URL: moz-src:///…` that survives `build
  faster`**, or a UniFFI argument-count mismatch in a service's failed init
  (`SearchService: #init: failure`): the objdir's downloaded binary was built at
  another revision than the checkout, and `build faster` never replaces it. Run
  `./mach artifact install` at the checked-out revision, then `./mach build
  faster`. From JS, a service with a failed init looks like one the realm lacks,
  so read the browser log before concluding either.
- **BiDi answers `unsupported operation` or "navigation not allowed"** to the
  devtools MCP: the binary is older than the MCP client expects, as after an
  `artifact install` of an older build. Use the test harness (`SKILL.md` §3) or
  a newer build.

## 2. What is live at the next launch

- A plain jar.mn entry (e.g. CSS, JS, `.mjs`) is either a symlink into the
  source tree, live at the next launch with no rebuild, or a copy that needs
  `./mach build faster` after each edit; the host's platform file, section
  PATHS, says which.
- An entry marked `*` in its jar.mn is a preprocessed copy: `browser.xhtml` with
  every `.inc.xhtml` it `#include`s, and the other preprocessed `.xhtml` (e.g.
  aboutDialog, pageInfo, webext-panels). It needs `./mach build faster` after
  every edit, on every platform. The theme jar.mn files have no `*` entries.
- A baseline placed by path (`SKILL.md` §6) follows the same rules.
- A New Tab change absent at runtime may be a stale bundle left in the source
  tree, which shadows the objdir copy. New Tab's docs,
  `browser/extensions/newtab/docs/index.md` "Bundles left in the source tree",
  say which commands leave one and how to clear it.
