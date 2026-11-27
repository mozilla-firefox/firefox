---
name: desktop-theme
description: "A Firefox::Theme or Toolkit::Themes bug; investigating a CSS issue; writing/reviewing a CSS or design-token patch; taking before/after screenshots."
---

# Desktop theme

Run every step you have the tools for, in a review too. Without a build, a browser or `./mach`, skip only the steps that need what is missing and keep going from the diff and the source, which show which rule wins and which layer or token a declaration reads. Where a finding rests on a number only a skipped step can produce (e.g. a contrast ratio, a resolved color, a size), flag it and ask the author for that measurement rather than inferring it.

Skipping a step never skips the documents it points to. In a review, a subject skill's "before you probe, edit, add or remove X, read Y" also means "before reviewing a diff that does X, read Y": the documents hold the checks the diff is reviewed against.

Whenever you write or review CSS, read `docs/code-quality/coding-style/css_guidelines.md` and treat it as a checklist: check the diff against each of its rules that a subject skill below doesn't already cover, and cite the heading of the section each rule comes from.

Also load every subject skill that matches the task before you go further, even when the report already names the rule or token or hints at a solution:

- `desktop-theme-colors`: color - high contrast, forced colors, prefers-contrast, dark mode, system colors; a border, shadow, focus ring or icon that vanishes; states like hover/selected/focus that don't look distinct; a color right in chrome but wrong in an about: page or vice versa.
- `desktop-theme-layout`: CSS or markup that moves, sizes or clips a box - overflow, scrollbars, centering, RTL, fractional scaling, font size, density, zoom, popovers, anchors; whether a change moves or resizes anything.
- `desktop-theme-tokens`: design tokens and CSS variables - picking one for a hard-coded value, `*.tokens.json` edits, Nova/Figma overrides, buildtokens, value-neutral diffs; one undefined or ineffective on a surface; finding the revision that changed a token's value.
- `desktop-theme-screenshots`: before/after screenshots; pixel-diffing a pair to prove a change visually neutral; shooting a hover or open-menu state; sampling a reporter's image; showing a hit area, focus target or stacking order. Capturing and diffing need a build; sampling a reporter's image needs only `./mach`.
