# Working with stack of patches Quick Reference

Working on Firefox, we strongly recommend working with stack of patches.
Patches should be small and could be landed in the order used to push them.
This also helps to breakdown the work for different reviewers.

As it can be complex for newcomers, this documentation explains the
various commands.

:::{note}
This page documents the Git workflow, which is the default. If you
prefer Jujutsu, see {doc}`jujutsu` for the equivalent
commands.
:::

In Phabricator, the stack can be seen in the `Revision Contents` section.
The top of the stack (most recent change) is first in the list.

This is also sometimes called "stack of revisions", "stack of commits" or "series of commits".

**Example:**

```{image} img/example-stack.png
```

For the overall quick reference guide, see the {doc}`Firefox Contributors Quick Reference <contribution_quickref>`

For background on why Firefox uses stacks and how the Phabricator workflow differs from GitHub, see {doc}`phabricator_vs_github`.

For how to shape a series so that each patch reviews well - what to front-load, where to draw the commit boundaries, when to split across bugs - see {doc}`reviewable-patch-series`.

## Visualize the stack

```shell
git log
git log --graph --oneline --all
```

## Merge two patches

It can happen that, instead of updating a patch, a new revision is
created on Phabricator. For this, merge the patches locally:

```shell
# Replace "pick" by "squash" or "fixup"
git rebase -i
```

Then, push to Phabricator and abandon the old change.

## Submitting the first patch on the stack

There are times when you are working on multiple patches and
just want to submit the first one. For this, you can use:

```shell
moz-phab submit .
# or
moz-phab submit -s HEAD
```

To submit the last X patches:

```shell
# This example will submit the last 3 patches
moz-phab submit HEAD~3
```

## Reorder the stack

Sometimes, we want to change the order the patches in the stack.
Fortunately, VCS support this easily.

```shell
# In the editor, just move the patches to the line below/above to
# reorder commits.
# Remove everything if you want to cancel the operation.
git rebase -i
```

## Make a change on a patch at the beginning of the stack

In some cases, the reviewer is asking for a change at the bottom of the stack (ie not at the top).
So, a simple `git commit --amend` would not work.

In such case, the following approach can be used:

```shell
# Creates a new commit on top of the stack, marked as a fixup for
# the target commit. It does not move or squash anything yet.
git commit --fixup <hash of the commit>

# Replays the stack from just before the target commit, automatically
# squashing the fixup commit into it along the way.
git rebase -i --autosquash <hash of the commit>^
```

## Removing patches in the stack

To remove a patch in the stack:

```shell
# Replace "pick" by "drop"
# Or simply remove the line for this commit
git rebase -i
```

## Rebasing the stack

As the codebase moves fast, it can be necessary to pull changes from
the `main` branch before landing the changes.

```shell
git remote update
git rebase origin/main
```

(stack-quickref-re-applying-a-patch)=

## Re-applying a patch to update it

If you still have the commit for that revision in your working directory
(for example, you never switched away from it), you don't need to re-apply
anything: just edit the files and amend the existing commit.

If the commit is *not* in your working directory anymore (e.g. you pulled
new changes, switched branches, or are working from a fresh clone), you
first need to re-apply it:

```shell
moz-phab patch D<revision_id>

# Or you can use the URL of the revision on Phabricator
moz-phab patch https://phabricator.services.mozilla.com/D<revision_id>
```

By default this creates the patch on top of the commit it was originally
based on, which may not be where your local `HEAD` is. To instead apply
it directly on top of your current `HEAD` (i.e. rebase it there), use
`--apply-to here`:

```shell
moz-phab patch D<revision_id> --apply-to here
```

## Reorganizing the stack in Phabricator

```shell
moz-phab reorg [start_rev] [end_rev]
```

allows you to reorganize the stack in Phabricator.

If you've changed the local stack by adding, removing or moving the commits around, you need to change the parent/child relation of the revisions in Phabricator.

```shell
moz-phab reorg
```

command will compare the stack, display what will be changed and ask for permission before taking any action.

:::{note}
Note that reviewbot will not restart the analysis.
:::
