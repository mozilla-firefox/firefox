# Sprint Process

Focus follows a 2-week sprint cycle with 6-week milestone releases. Dot-releases with bugfixes are released every two weeks. Our upcoming train schedule is [here](https://wiki.mozilla.org/Mobile/Focus/Android/Train_Schedule).

## Issue naming and labels

### Labels

Priority labels are based on the [Bugzilla triage process][triage priority] and set during triage to determine when they'll be worked on:

* `P1`: Issues for the current 2-week sprint.
  * [Open engineering issues](https://github.com/mozilla-mobile/focus-android/issues?utf8=%E2%9C%93&q=is%3Aissue%20is%3Aopen%20label%3AP1%20NOT%20%5Bux%5D%20in%3Atitle%20)
  * [Open UX issues](https://github.com/mozilla-mobile/focus-android/issues?utf8=%E2%9C%93&q=is%3Aissue%20is%3Aopen%20label%3AP1%20ux%20in%3Atitle%20)
* `P2`: Issues for the 6-week milestone release.
  * [Open engineering issues](https://github.com/mozilla-mobile/focus-android/issues?utf8=%E2%9C%93&q=is%3Aissue%20is%3Aopen%20label%3AP2%20NOT%20%5Bux%5D%20in%3Atitle%20)
  * [Open UX issues](https://github.com/mozilla-mobile/focus-android/issues?utf8=%E2%9C%93&q=is%3Aissue%20is%3Aopen%20label%3AP2%20ux%20in%3Atitle%20)
* `P3`: Backlog
* `P5`: Will not fix but will accept a patch

Other labels:

* `addressed`: Label for excluding items from triage. Should be used for [meta] items.

### Issue Prefixes

* `[meta]`: larger issues that need to be broken down, into a `[breakdown]` issue and issues for its smaller parts. This should include a checklist of all the issues (including the `[breakdown]` issue).

  These do NOT get a P* label, but should be in a milestone.
* `[breakdown]`: issue to track the work of breaking down a larger bug

## Triage - Weekly

* ([Link](https://github.com/mozilla-mobile/focus-android/issues?q=is%3Aissue+is%3Aopen+-label%3AP1+-label%3AP2+-label%3AP3+-label%3AP4+-label%3AP5+-label%3Aaddressed+-label%3Ablocked+sort%3Aupdated-desc+no%3Amilestone)) Assign priority labels (P1, P2, ...) to bugs without priority labels or the `addressed` label
* ([Link](https://github.com/mozilla-mobile/focus-android/issues?utf8=%E2%9C%93&q=is%3Aissue+is%3Aopen+label%3AP1+no%3Amilestone+)) Ensure all P1 labels are assigned a milestone
* ([Link](https://github.com/mozilla-mobile/focus-android/issues?utf8=%E2%9C%93&q=is%3Aissue+is%3Aopen+label%3AP2+no%3Amilestone+)) Ensure all P2 labels are assigned a milestone

## Weekly Bug management (alternating)

### Sprint planning

Deciding what goes into a sprint, promote P2 issues to P1. After every release, this meeting is for deciding what goes into the upcoming milestone - add issues to the milestone and set them as P2.

### Backlog grooming

Handling Triage overflow, adding to the contributor bug lists, looking at milestone lists.

[triage priority]: https://wiki.mozilla.org/Bugmasters/Process/Triage#Weekly_or_More_Frequently_.28depending_on_the_component.29

## Monthly Roadmap Planning

Plan and prioritize features for upcoming milestones - an update will be emailed out.

Plan and facilitate workweeks
release schedule
trying to work with things without proper training
too much observing
