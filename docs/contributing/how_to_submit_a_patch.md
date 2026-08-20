(how-to-submit-a-patch)=

# How to submit a patch

Submitting a patch, getting it reviewed, and committed to the Firefox
source tree involves several steps. This article explains how.

:::{note}
We are also providing a {ref}`Firefox Contributors Quick Reference <Firefox Contributors' Quick Reference>` for contributors.
:::

The process of submission is illustrated by the following diagram, and
each step is detailed below:

```{mermaid}
graph TD;
    Preparation --> Bugzilla[(Bugzilla)];
    Bugzilla[(Bugzilla)] --> Git[(Git)];
    Git[(Git)] --> c[Working on a patch];
    c[Working on a patch] --> Testing;
    Testing --> c[Working on a patch];
    Testing --> e[Submit the patch];
    e[Submit the patch] --> Phabricator[(Phabricator)];
    Phabricator[(Phabricator)] --> d[Getting Reviews];
    d[Getting Reviews] -- Addressing Review comment --> c[Working on a patch];
    d[Getting Reviews] --> Lando[(Lando)];
    Lando[(Lando)] --> h[Push the change];
    h[Push the change] --> Git2[(Git)];
    style Bugzilla fill:#e1f5ff,stroke:#0366d6
    style Git fill:#e1f5ff,stroke:#0366d6
    style Phabricator fill:#e1f5ff,stroke:#0366d6
    style Lando fill:#e1f5ff,stroke:#0366d6
    style Git2 fill:#e1f5ff,stroke:#0366d6
```

## Preparation

Every change to the code is tracked by a bug report
in [bugzilla.mozilla.org](https://bugzilla.mozilla.org/). Without a
bug, code will not be reviewed, and without review, code will not be
accepted. To avoid duplication, [search for an existing
bug](https://bugzilla.mozilla.org/query.cgi?format=specific) about
your change, and only if none exists, file a new one. Most communication
about code changes take place in the associated code
review, so be sure the bug describes the exact problem being solved.

Please verify the bug is for the correct product and component. For more
information, ask questions on the newsgroups, or on the #developers room
on [chat.mozilla.org](https://chat.mozilla.org).

The person working on a bug should be the 'assignee' of that bug in
Bugzilla. If somebody else is currently the assignee of a bug, email
this person to coordinate changes. If the bug is unassigned, leave a
message in the bug's comments, stating that you intend working on it,
and suggest that someone with bug-editing privileges assign it to you.

Some teams wait for new contributors to attach their first patch before
assigning a bug. This makes it available for other contributors, in case
the new contributor is unable to level up to patch creation. By
expressing interest in a bug comment, someone from that team should
guide you through their process.

## Module ownership

All code is supervised by a [module
owner](https://www.mozilla.org/en-US/about/governance/policies/module-ownership/).
This person will be responsible for reviewing and accepting the change.
Before writing your code, determine the module owner, verifying your
proposed change is considered acceptable. They may want to look over any
new user interface (UI review), functions (API review), or testcases for
the proposed change.

If module ownership is not clear, ask on the newsgroups or [on
Matrix](https://chat.mozilla.org). See {ref}`Getting reviews` for the other
ways of identifying a suitable reviewer or review group.

## Working on a patch

Changes to the Firefox source code are presented in the form of a patch.
A patch is a commit to version control. Firefox and related code is
stored in our [git repository](https://github.com/mozilla-firefox/firefox).

Each patch should represent a single complete change, separating
distinct changes into multiple individual patches. If your change
results in a large, complex patch, seek if it can be broken into
[smaller, easy to understand patches representing complete
steps](https://we.phorge.it/book/flavor/article/writing_reviewable_code/#many-small-commits),
applied on top of each other. This makes it easier to review your
changes, [leading to quicker
reviews,](https://groups.google.com/group/mozilla.dev.planning/msg/2f99460f57f776ef?hl=en)
and improved confidence in this review outcome.

Also ensure that your commit message is formatted appropriately. A
simple commit message should look like this:

```
Bug 123456 - Change this thing to work better by doing something. r=reviewers
```

The text of the message should be what you did to fix the bug, not a
description of what the bug was. If it is not obvious why this change is
appropriate, then [explain why in the commit
message](https://we.phorge.it/book/flavor/article/writing_reviewable_code/#write-sensible-commit-messages).
If this does not fit on one line, then leave a blank line and add
further lines for more detail and/or reasoning.

The `r=reviewers` part specifies that `reviewers` should review the patch
and provide feedback before it is integrated into the Firefox codebase. For
choosing reviewers, and the full reviewer syntax, please see
{ref}`Getting reviews`.

You can edit the message of the current commit at any time using
`git commit --amend` or `git rebase -i`.

Also look at our {ref}`Reviewer Checklist` for a list
of best practices for patch content that reviewers will check for or
require.

## Testing

All changes must be tested. In most cases, {ref}`Automated Testing` is required for every
change to the code.

While we desire to have automated tests for all code, we also have a
linter tool which runs static analysis on our JavaScript, for best
practices and common mistakes. See {ref}`ESLint` for more information.

Ensure that your change has not caused regressions, by running the
automated test suite locally, or using the [Mozilla try
server](https://wiki.mozilla.org/Build:TryServer). Module owners, or
developers [on Matrix](https://chat.mozilla.org) may be willing to
submit jobs for those currently without try server privileges.

## Submit the patch

:::{note}
Make sure you rebase your patch on top of the latest build before you
submit to prevent any merge conflicts.
:::

Mozilla uses Phabricator for code review. See the [Mozilla Phabricator
User
Guide](https://moz-conduit.readthedocs.io/en/latest/phabricator-user.html)
for instructions.

Don't be shy in posting partial patches, demonstrating potential
approaches, and asking for preliminary feedback. It is easier for others
to comment, and offer suggestions, when a question is accompanied by
some code.

## Getting reviews for my patch

See the dedicated page {ref}`Getting reviews`

(addressing-review-comments)=

## Addressing review comments

It is unusual for patches to be perfect the first time around. The
reviewer may use the ‘Request Changes’
[action](http://moz-conduit.readthedocs.io/en/latest/phabricator-user.html#reviewing-patches)
and list problems that must be addressed before the patch can be
accepted. Please remember that requesting revisions is not meant to
discourage participation, but rather to encourage the best possible
resolution of a bug. Carefully work through the changes that the
reviewer recommends, attach a new patch, and request review again.

Sometimes a reviewer will grant conditional review with the ‘Accept
Revision’ action but will also indicate minor necessary changes, such as
spelling, or indentation fixes. All recommended corrections should be
made, but a re-review is unnecessary. Make the changes and submit a new
patch. If there is any confusion about the revisions, another review
should be requested.

Sometimes, after a patch is reviewed, but before it can be committed,
someone else makes a conflicting change. If the merge is simple, and
non-invasive, post an updated version of the patch. For all non-trivial
changes, another review is necessary.

If at any point the review process stalls for more than two weeks, see
{ref}`getting-attention`.

In many open source projects, developers will accept patches in an
unfinished state, finish them, and apply the completed code. In
Mozilla's culture, **the reviewer will only review and comment on a
patch**. If a submitter declines to make the revisions, the patch will
sit idle, until someone chooses to take it on.

## Pushing the change

A patch can be pushed (aka. 'landed') after it has been properly
reviewed.

:::{note}
Be sure to build the application with the patch applied. This
ensures it runs as expected, passing automated tests, and/or runs
through the [try
server](https://wiki.mozilla.org/Build:TryServerAsBranch). In the
bug, please also mention you have completed this step.

Submitting untested patches wastes the committer's time, and may burn
the release tree. Please save everyone's time and effort by
completing all necessary verifications.
:::

Ask the reviewer to land the patch for you.
For more details, see {ref}`push-a-change`

[Lando](https://moz-conduit.readthedocs.io/en/latest/lando-user.html) is used
to automatically land your code.

## Regressions

It is possible your code causes functional or performance regressions.
There is a tight
[policy](https://www.mozilla.org/about/governance/policies/regressions/) on
performance regressions, in particular. This means your code may be
dropped, leaving you to fix and resubmit it. Regressions, ultimately
mean the tests you ran before checking in are not comprehensive enough.
A resubmitted patch, or a patch to fix the regression, should be
accompanied by appropriate tests.

After authoring a few patches, consider requesting commit access to the
Mozilla source code. See {ref}`Levelling up`.
