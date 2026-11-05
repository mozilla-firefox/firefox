---
name: firefox-commits
description: Guidelines to follow when committing, amending commits, or updating a submitted Phabricator revision
---

# Skill commit message / committing

```
Bug xxxxx - What has been fixed. r?reviewerA,blockingreviewerB!,#reviewergroup

A small number of lines, optional.

Differential Revision: https://phabricator.services.mozilla.com/DNNNNNN
```

`moz-phab submit` adds the `Differential Revision:` line. Write only the lines
above it, removing such a line will create a new review request, lose comment
history and confuse everybody.

Commit messages in the Firefox project are written with a very specific format,
including the bug number and reviewer as the first line, more explanations if
needed in the body of the commit message. If they have been submitted for review
already, the final line includes the review identifier / URL. Coding agents have
been seen inventing bug numbers. This doesn't help anybody: the patch ends up
attached to an unrelated bug. Take the bug number from a neighboring related
patch if in a series, or from the bug the user mentioned (the `moz` MCP can
fetch it to check that it matches the change), or simply ask the user if they
have one or if they would like to create one. When the user confirms there must
be no bug, use `No bug - ` as the prefix.

This format is not only expected by Mozilla developers, it is used by numerous
bits of tooling, be it CI, Phabricator that we use for review, searchfox.org to
auto-link bug numbers, etc., and so it is beneficial for all parties to respect
it.

Use the `find-reviewer` skill to determine the reviewer list.

Large patches are frowned upon by reviewers. The `stack-reorganize` skill can
help splitting things up.

## Modifying a commit message

When modifying a commit message after it has been submitted, it is critical to
leave the last line untouched, or the tooling will create a separate review
request on Phabricator, losing comments and confusing the reviewer. That line is
`Differential Revision: https://phabricator.services.mozilla.com/DNNNNNN`, and
`moz-phab submit` adds it. `git commit --amend -m`, `jj describe -m` and
`jj squash -m` replace the whole message, so the new message has to include
that line. When only the code changes, `git commit --amend --no-edit` keeps the
message as is.

The commit message explains why the change is needed: the diff already shows
what has changed. In some cases it can be important to mention why a particular
route was taken instead of another solution. This is useful for future readers
of the code to understand the decision. What is needed to understand the code
itself belongs in a code comment, next to the code.

## Content of the message

It is however critical to not be too verbose. Terseness is extremely
important, because a wall of text will not be read. It is perfectly appropriate
to have single line commit message for simple changes, such as updating a
library to a new upstream revision, updating Web Platform tests after
implementing or fixing a feature (non exhaustive). Longer commit messages aren't
necessarily better. Try push links, performance numbers and the reasoning that
led to the fix belong in a bug comment, where the discussion continues and
where reviewers ask for more data; the commit message is final and links expire.

The commit message is also important to help the reviewer. Whether they read
the commit message or the patch first, they check that the two match and that
the patch is otherwise correct in various respects. When the reviewer proposes
a commit message, use their wording as is: they know which facts matter to them.

## Security bugs

For a patch on a security-sensitive bug, the first line describes what the code
now does in neutral terms, and there is no body: the explanation goes in the
bug. People watch check-ins, so the message leaves out the nature of the issue
(use-after-free, overflow, bypass), the word security, and the sec-approver's
name. See `docs/bug-mgmt/processes/fixing-security-bugs.md`.

## Addressing review comments on a commit in a stack

Fold the change into the commit it belongs to, so its message and its
`Differential Revision:` line stay as they are. With git:

```
git commit --fixup=<rev> <files>
GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash <rev>~1
```

This gives `<rev>` and every commit above it a new hash, and the old hash still
points to the code without the fix. Look `<rev>` up again with
`git log --oneline` before using it. With jj, `jj squash --into <rev> <files>`,
and refer to the commit by its change ID, which stays the same.

To update only that revision on Phabricator, pass `--single`:

```
moz-phab submit --single <rev> -y
```

Without `--single`, `moz-phab submit <rev>` takes `<rev>` as the start of a
range that runs to the current commit and leaves `<rev>` itself out. It uploads
new diffs for the commits above it and leaves the one that changed untouched.
After submitting, fetch the revision with the `moz` MCP and check that its diff
contains the fix.
