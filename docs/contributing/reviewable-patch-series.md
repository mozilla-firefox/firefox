# Making a patch series reviewable

## Overview

In the Firefox code base, and in many other Mozilla code bases, commits are reviewed individually. There is no "review squashed series" workflow. Many individual small commits are easier to understand than one large commit. Submitting small commits for review helps catch bugs during review. And in the case that a regression does get introduced, having a fully working product after every commit makes it easy to bisect which change caused the bug or performance regression.

:::{note}
If you work with an AI agent, these in-tree skills can rework existing commits along the lines of this page: {doc}`stack-reorganize </ai-agent-tools/skills/stack-reorganize/SKILL>` restructures a whole stack, and {doc}`stack-split-commit </ai-agent-tools/skills/stack-split-commit/SKILL>` splits one large commit.
:::

## Goals

- Smoothe out the landing process by avoiding known sources of review and landing friction.
- Reduce cognitive load during review, so that it's easy for the reviewer to spot bugs, and to understand the impact of a change.
- Every "prefix" of the patch series should leave the world in a meaningful valid state: the tree builds, lints, and passes tests after every commit, not just at the tip.
- Every patch should be an incremental improvement that makes sense to a reviewer in isolation.
- Every patch should look "natural" and not depend on later work to justify its existence.
- No forward references: an earlier patch's code and comments can't mention a concept that only a later patch introduces.

## Sources of review friction

- Unclear value proposition, if the patch only looks natural once combined with other work in the series
- Mixing of unrelated concerns, e.g. functionality changes and cleanup, or changes made by a tool (e.g. global search and replace + fmt) and other changes
- Messy intermediate states that gets cleaned up in a later commit
- Large number of reviewers on a single patch, if the patch touches files across multiple directories with distinct review responsibilities

## Strategies to minimize friction

- "Land-early nuggets": Often, a larger change will include small tweaks which are orthogonal to the goal of the larger change, but which are non-controversial and which make sense to adopt even if the larger change is rejected. Find those nuggets of value and pull them out into patches that go *before* the larger work - they can be reviewed quickly and will reduce the amount of rebasing that has to happen for the larger change if that one is stuck in review for a while.
- Refactor first, then change behavior: In the process of writing a patch, the main focus is usually on a certain behavior change, and then sometimes some cleanup is done afterwards. But during review, it's usually better to do the "cleanup" / refactor first, in such a way that the actual behavior change that follows will look very natural.
- Predict reviewer response: Do a review of each patch yourself, and predict what a reviewer would say about it. Then shift things around until you think that the reviewer will have nothing to complain about - or at least until they could only disagree with the effect of the patch / the proposed change, and not with the mechanics of the implementation.
- Put behavior changes front-and-center: In patches which change behavior, minimize distractions from unrelated changes.

## Multiple bugs

Sometimes it can make sense to split the series across multiple Bugzilla bugs. Here, the term "bug" is used loosely in the sense of "patch subseries container" / "unit of landing".

Another approach is to put all patches on the same bug first, and move them out as needed for individual landings. More concretely: As the reviews come in, the developer can move a "fully-reviewed prefix" of the patch series into a different bug and land them there, while the remaining patches stay in the original bug where they wait for the rest of the reviews to come in. The goal is to have one landing per bug. With Phabricator, patches can be moved across bugs without losing the review status.

## Trade-offs

Some of the goals above pull in opposite directions. This section acknowledges that there are some judgment calls involved.

For example, there are often multiple options when making a change to a widely-used API:

- Option 1: In the same commit, change both the API as well as all consumers of that API across all directories. Good: atomic change, passes build, no messy intermediate state. Bad: large patch, large number of reviewers
- Option 2: One commit for the API change, multiple commits (grouped by reviewer) for updating consumers. Good: Small patch, low number of reviewers per patch. Bad: Intermediate state fails to build
- Option 3: One commit makes the API change but keeps a compatibility stub so that existing callers still work, and then multiple patches convert various directories, with a final patch removing the compat stub. Good: Small patch, low number of reviewers per patch, passes build. Bad: somewhat messy intermediate state with compat stub, risk that compat stub stays around indefinitely.

Option 2 should be avoided. Prefer option 1 here in most cases, unless there are so many consumers that updating them all in a single commit is impractical. Option 3 becomes more attractive if the intermediate state still appears somewhat natural.

Going overboard on patch splitting can backfire: Too many individual patches can be a drag to review. Depending on the situation it can be a good idea to group similar changes into the same commit. If a refactoring looks non-sensical without the associated behavior change, it can make sense to do both in the same commit.

## What should be moved to the front of the series?

- Front-load non-controversial improvements.
- Front-load risk: If the entire larger change is doomed if one of its pieces doesn't work out, and if that deal-breaker piece can be validated independently, it can make sense to get just that piece reviewed and landed first. Then it can go through Nightly testing while the specifics of the rest are still being discussed.
- Front-load changes which don't change behavior but which, by being separate, improve the clarity of upcoming behavior change patches.

## Examples

### Example from responsive design mode patches (bug 1978145)

Initial order:

- A: Remove resizer offset and browser border.
- B: Add rudimentary toolbar-on-top support.
- C: Add a browser bottom cover to hide the part that's pushed offscreen when the top toolbar is visible.
- D: Move mouse motion detection to the parent, and add snap animation when the mouse button is lifted.
- E: Improve the visuals of the RDM dynamic toolbar.
- F: Add an .rdm-screen-box element which, unlike the browser, doesn't move, and make it render the shadow. Also bring back the border as another shadow.

After reorganization:

- L: Make RDM element sizes and positions easier to reason about.
  - Combined from parts of A and F, retains the border so that visuals don't change
- M: Simplify RDM resizer positioning in the presence of zoom, by introducing a scaled wrapper element.
  - New patch which only became obvious during reorganization.
- N: Use a .dynamic-toolbar-enabled class instead of .style.visibility.
  - Extracted from B
- O: Make .rdm-dynamic-toolbar look more like an actual toolbar.
  - Extracted from E
- P: Increase DYNAMIC_TOOLBAR_MAX_HEIGHT to 50px, because a 50px toolbar looks more realistic.
  - Extracted from E
- Q: Make the RDM dynamic toolbar snap to fully-visible or fully-hidden when the mouse is released.
  - Mostly D, maybe with some parts from other patches
- R: Add support for toolbar-on-top to RDM's dynamic toolbar mode.
  - Combined from parts of B, C, and F

Clear responsibility per patch, behavior changes are individual small patches, toolbar-on-top mode (including "browser bottom cover" workaround) isn't a concern until the last patch.
