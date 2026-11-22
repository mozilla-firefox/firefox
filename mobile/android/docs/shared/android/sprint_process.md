# APT sprint process

Projects created by the Android Product Team typically ship a major release every six weeks and ship a minor release twice between major releases (every two weeks). For example, if v2.0 is released on week 0, v2.1 will be released on week 2, v2.2 will be released on week 4, and v3.0 will be released on week 6. Emergency bug fixes can be released more frequently. Projects will typically link to their release schedule (example: [focus-android](https://wiki.mozilla.org/Mobile/Focus/Android/Train_Schedule)).

Development is broken down into two week-long chunks of time called sprints: we track the work for these sprints in GitHub milestones (example: [focus-android][milestone example]). We have several planning meetings to identify what should go into each release and thus into each sprint.

## Sprint planning meetings

### Roadmap (monthly)

The product team plans and prioritizes the project roadmap: the general project plan over the next few months. These meetings will:

- At a high level, decide which features go into which releases
- Schedule team work weeks
- Modify the release schedule (if necessary)

Updates from this meeting will be emailed to the full team.

### Triage (weekly)

The full team will go through newly filed issues to:

- Assign priority labels (P1, P2, ...) to bugs without priority labels or the `addressed` label
- Ensure all P1 labels are assigned to a milestone
- Ensure all P2 labels are assigned to a milestone

Each project should link to its own triage queries (example: [focus-android](https://github.com/mozilla-mobile/focus-android/issues?q=is%3Aissue+is%3Aopen+-label%3AP1+-label%3AP2+-label%3AP3+-label%3AP4+-label%3AP5+-label%3Aaddressed+-label%3Ablocked+sort%3Aupdated-desc+no%3Amilestone)).

For more information on priority labels, [see below](#categorizing-issues).

### Weekly bug planning (weekly)

The full team meets to manage bugs. This meeting time-slot alternates between two separate agendas:

#### Sprint planning

Held before a sprint begins, in this meeting we:

- Decide what goes into the current sprint by promoting P2 issues to P1
- Decide what goes into the next sprint by adding issues to the upcoming milestone and setting them as P2

#### Backlog grooming

Held mid-sprint, in this meeting we:

- Handle triage overflow
- Add to the contributor bug lists (e.g. `help wanted`)
- Check in on current sprint progress
- Look over the upcoming sprint

## Categorizing issues

We have several conventions for categorizing our issues.

### Labels

We label issues with their priority: these priorities are based on the [Bugzilla triage process][triage priority].

- `P1`: issues for the current 2-week sprint
- `P2`: issues for the 6-week milestone release
- `P3`: Backlog
- `P5`: Will not fix but will accept a patch

Other labels:

- `addressed`: label for excluding items from triage. Should be used for [meta] items.

### Issue Prefixes

In the issue title, we may prefix the name to categorize the issues.

- `[meta]`: larger issues that need to be broken down, into a `[breakdown]` issue and issues for its smaller parts. This should include a checklist of all the issues (including the `[breakdown]` issue).

  These do NOT get a P* label, but should be in a milestone.
- `[breakdown]`: issue to track the work of breaking down a larger bug

[triage priority]: https://wiki.mozilla.org/Bugmasters/Process/Triage#Weekly_or_More_Frequently_.28depending_on_the_component.29
[milestone example]: https://github.com/mozilla-mobile/focus-android/milestone/30?closed=1
