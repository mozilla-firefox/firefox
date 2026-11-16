# How To Contribute Code To Firefox

The whole process can be a bit long, and it might take time to get things right.
If at any point you are stuck, please don't hesitate to ask at [https://chat.mozilla.org](https://chat.mozilla.org)
in the [#introduction](https://chat.mozilla.org/#/room/#introduction:mozilla.org) channel.
Additionally, here are some etiquette tips to help when reaching out:

- Please don't ask to ask a question, post your question with the relevant context and someone will be able to help when they have time.

- Use public facing channels to ask your questions instead of direct messaging folks.

  - Other people get to learn from your question and there's a higher chance your question will get answered quickly since there are many people in the #introduction room.

- Your question may not be answered immediately, this is expected! If you are not getting feedback after an hour or so, feel free to repost the question.

  - Sometimes messages get skimmed over or notifications are lost in the sea of other things, so it's normal to repost your question in this case.

- Please search through the recent scrollback of your relevant channels to see if your question has been asked and/or answered already.

  - Most issues with setup have been experienced before, so there's a good possibility that your question has already been answered recently.

We make changes to Firefox by writing patches, testing them and pushing them into "the tree", the
term we use for all the code in the
[firefox repository](https://github.com/mozilla-firefox/firefox). Let's get started.

Please see the {ref}`Firefox Contributors Quick Reference <firefox-contributors-quick-reference>` for simple check list.

## Find a bug we've identified as a good fit for contributors

With millions of bugs filed in Bugzilla, it can be hard to know
where to start, so we've created these bug categories to make getting
involved a little easier:

- [Codetribute](https://codetribute.mozilla.org/) - our site for
  finding bugs that are mentored, some are good first bugs, some are
  slightly harder. Your mentor will help guide you with the bug fix and
  through the submission and landing process.
- [Good First Bugs](https://bugzilla.mozilla.org/buglist.cgi?keywords=good-first-bug&resolution=---)
  \- are the best way to take your first steps into the Mozilla
  ecosystem. They're all about small changes, sometimes as little as a
  few lines, but they're a great way to learn about setting up your
  development environment, navigating Bugzilla, and making
  contributions to the Mozilla codebase.

:::{note}
If you're new, start with bugs with the `good-first-bug` keyword; after a few
of those you can move on to bugs with the
[good-next-bug](https://bugzilla.mozilla.org/buglist.cgi?keywords=good-next-bug&resolution=---)
keyword or that have a
[mentor assigned](https://bugzilla.mozilla.org/buglist.cgi?resolution=---&f1=bug_mentor&o1=isnotempty&keywords=good-first-bug%2C%20good-next-bug&keywords_type=nowords).
:::

## How to select a good bug and get it assigned to you

The first step is to find an unassigned bug and express interest in fixing it.
Even with no assignee, it is polite to check if someone has recently commented
that they're looking at fixing the issue. If you find a bug that isn't assigned,
keep in mind that the triage owner may not have time to mentor that bug, or to
review patches.

If no one has expressed interest before you, comment on the bug to ask to be
assigned and wait for a reply before submitting a patch. If you don't receive a
response within a few days, you can set a needinfo request for the mentor or triage owner,
but keep in mind that responses may not be immediate: people may be
on vacation or very busy, and don't work on weekends.

A bug having been filed, especially years ago, doesn't mean it is still relevant
or that we'd accept patches for it. Oftentimes it is important to get
context on the bug, and to understand whether it needs UX or product support,
or if the triage owner or mentor has an opinion on how to implement it.

If the bug is about a defect, first confirm that it is still reproducible. Bugs
can become stale, and the issue may already have been fixed by another patch
that doesn't reference the bug.

In the age of AI tooling, it is increasingly easy to generate a patch without
having any of this context. This puts a large amount of responsibility on
reviewers, and often on UX and product partners, and every new feature, setting
or added logic increases the maintenance burden of Firefox.

:::{warning}
Patches for bugs where the triage owner or mentor hasn't agreed to the change,
or for feature requests without Product/UX sign-off, may be declined. A large
portion of our roadmap is planned in collaboration with our Product and UX
teams, and not all changes can be accepted.
:::

(good-first-bug-guide)=

## Quality expectations for patches

To help reviewers give useful feedback and keep the code maintainable, patches
should meet the following criteria before being submitted for review:

- The change is scoped and should solve one issue clearly, without unrelated
  refactors or drive-by changes.
- The code is maintainable and consistent, following the project's existing
  patterns, naming, structure, and {doc}`style rules </code-quality/coding-style/index>`.
  Utilize our build tool {doc}`/mach/index`, its
  {doc}`linting </code-quality/lint/usage>`,
  {doc}`static analysis </code-quality/static-analysis/index>`, and other
  {doc}`code checking features </code-quality/index>` to help with some of this.
- The description should explain what changed and why, and the diff should be
  small and clear enough for a reviewer to understand without reconstructing
  intent.
- Code should build cleanly, pass relevant checks, and include testing or
  manual verification steps appropriate to the change.

:::{warning}
Partial, experimental, or AI-generated changes that the author cannot explain,
validate, or finish are unlikely to be approved. See our
{doc}`AI coding policy </contributing/ai-coding>`.
:::

{ref}`Our Developer Guide and its parent document <working-on-firefox>`
contains additional information, such as the
{ref}`reviewer checklist <reviewer-checklist>`, how to submit patches, and code
review guidelines, which should be read before submitting a patch.

## Getting your code reviewed

Once you fix the bug, you can advance to having your code reviewed.
Mozilla uses
[Phabricator](https://moz-conduit.readthedocs.io/en/latest/phabricator-user.html)
for code review.

If you have a mentored bug, ask your mentor first: they will review the
patch, or can easily find out who should. Otherwise, see
{doc}`/contributing/reviews` for how to pick a reviewer or a review group, the
`r=` commit message syntax, and what to do when a review is not
happening.

### Following up and responding

For most new contributors, and even for long-time Mozillians, the first
review of your patch will be "Requested Changes" (or an "r-" in
Bugzilla). This does not mean you've done bad work. There is more work
to do before the code can be merged into the tree. Your patch may need
some changes - perhaps minor, perhaps major - and your reviewer will
give you some guidance on what needs to be done next.

This is an important process, so don't be discouraged! With our
long-lived codebase, and hundreds of millions of users, the care and
attention helping contributors bring good patches is the cornerstone of
the Mozilla project. Make any changes your reviewer seeks; if you're
unsure how, be sure to ask!

See {ref}`addressing-review-comments` for how to update and resubmit
your patch.

## Getting code into Firefox

Once your patch has been accepted, it is ready to go. Before it can be
merged into the tree, your patch will need to complete a successful run
through our {ref}`try server <pushing-to-try>`,
making sure there are no unexpected regressions. If you don't have try
server access already, your mentor, or the person who reviewed your
patch, will be able to help.

Ask the reviewer to land the patch for you.
For more details, see {ref}`push-a-change`

## Do it all again

Thank you. You've fixed your very first bug, and the Open Web is
stronger for it. But don't stop now.

There is plenty more to do. Your mentor might suggest a new bug for you
to work on, or you can
{ref}`find one that interests you <good-first-bug-guide>`.

As you fix more bugs, you can request more access to Mozilla
infrastructure so you can push to try and eventually land your own code.
See {doc}`/contributing/levelling-up`.

## More information

- [A beginner's guide to SpiderMonkey, Mozilla's Javascript
  engine](https://wiki.mozilla.org/JavaScript:New_to_SpiderMonkey)
