# Firefox for Android Team Processes

## Communication channels

For internal Firefox Android communication start off in #mobile-android-team.

Dedicated channels on Matrix work as well:

* [#Fenix](https://matrix.to/#/#fenix:mozilla.org) - Fenix development discussions.
* [#android-components](https://matrix.to/#/#android-components:mozilla.org) - shared core components for our mobile browsers/projects.
* [#geckoview](https://matrix.to/#/#geckoview:mozilla.org) - working on the Android layer of Gecko that interfaces with Android Components and Fenix.

## Design Feasibility

Who: Design lead, designer, engineering lead, engineer, product lead, product manager
Purpose: meet any time we start design for a new feature (esp large ones) to discuss:

* Overall concept and user goals
* Where the feature fits within the Fenix system
* Any existing technical constraints or dependencies (on Android OS, or other Firefox Mobile teams like GV or A-C)
* Alignment on user stories

Design Handoff
Who: designer, engineer, product manager, QA lead
Purpose: before engineering sprint for a feature starts to discuss:

* Overall purpose of feature and how it relates to or interacts with existing features
* Discuss expectations for functionality
* Discuss visual and interactive design elements
* Negotiate scope and discuss changes if tasks are too large for a single sprint

## Triage

* The team triages Bugzilla issues asynchronously. This involves reviewing all new issues (bugs, crashes, and feature requests) to determine whether they should be addressed in MVP or added to backlog for future sprints.

### Process

* Add appropriate labels for [features](https://github.com/mozilla-mobile/fenix/labels?q=Feature)
* Add to appropriate Projects ([1](https://github.com/orgs/mozilla-mobile/projects), [2](https://github.com/mozilla-mobile/fenix/projects))
* Note any urgent issues and tag the Product Team when needed

## Sprint Planning

### Sprint pre-planning

* Build sprint plan based on what is ready (UX/dependencies), priority, and complexity.

### During Sprint Planning

* Assign stories/bugs to sprint up to capacity, and resolve any open questions.
* Size stories as needed. Most sizing is happening asynchronously.
* Go over UX Sprint Plan

## Backlog Grooming

* During Backlog Grooming, review and all user stories assigned to Backlog that have not been sized.
* ...

## During Sprint

* Engineers will label stories as “in progress” as they work begin work on them, and assign story to themselves.
* IF a user story has a UX component that needs review, when it is ready for review, engineer will add a screenshot/gif and @mention the designer in the user story. Add the [needs:ux-review](https://github.com/mozilla-mobile/fenix/labels/needs%3AUX-review) label to the PR.
* When a PR has been merged in, the Merger verify that Milestone this code will ship in to the issue. (Careful around soft code freeze! The release may have already been cut, which affects the current milestone.)
* Engineers will label stories with [eng:qa:needed](https://github.com/mozilla-mobile/fenix/labels/eng%3Aqa%3Aneeded) when the ticket is ready to be tested.
* QA will label the story as [eng:qa:verified](https://github.com/mozilla-mobile/fenix/labels/eng%3Aqa%3Averified) and close the ticket (which will move it to the “Done” column).
* If a ticket becomes blocked and will miss the sprint, engineers will label it “waiting” and notify the Product Team with the reason. This issue will be moved back to the appropriate backlog.
* Engineers will remove the “waiting” label and re-apply the appropriate label (“in progress,” “QA needed”).

## UX Review

IF a user story has a UX component that needs review, when it is ready for review:

* Consider hopping on a call to do a ‘desk check’ with the Designer*
* Engineer will add a gif/screenshot/apk (as applicable to the issue)
* Engineer will @mention the designer in the user story and ping the Designer on Slack, and add the `needs:UX-feedback` label

  UX designer reviews the component (consider hopping on a call to work through minor changes). Communication is key to here between designer and engineer in how they want to go through edits.

  We will also go through UX review during Sprint Demos.

## Copy Review

IF a user story has a String that needs a review, when it is ready for review:

* Raise an issue in Fenix as feature request.
* Add the needs:strings label.
* Add the screenshot where the string will be applied.
* Add detail explanation of the use case.
* Comment asking the UX designer to review.

## Engineering review

Use tags on open PRs to show which part of the process it is on. Some notable ones:

1. [needs:review](https://github.com/mozilla-mobile/fenix/labels/needs%3Areview) - PR that needs a review. Anyone should jump on any reviews with this label and help out with reviews. Thanks in advance.
2. [pr:needs-ac](https://github.com/mozilla-mobile/fenix/labels/needs%3Aac) - PR that is waiting for a AC bump. Typically, we use “waiting”, but this provides us with a bit more context.
3. [pr:approved](https://github.com/mozilla-mobile/fenix/labels/pr%3Aapproved) - PR that has been approved. This one is a bit easier to parse visually compare to the Approved in the GitHub summary
4. [pr:waiting-for-authors](https://github.com/mozilla-mobile/fenix/labels/pr%3Awaiting-for-authors) - PR that has been approved and awaiting any changes before they can land. Usually a PR might be approved, but has not been landed because it is waiting for followup changes.

## QA

* Engineers will label stories as [eng:qa:needed](https://github.com/mozilla-mobile/fenix/labels/eng%3Aqa%3Aneeded) when the ticket is ready to be tested (which will move the ticket to the ‘Ready for QA’ column’).
* QA will review the ticket and determine whether it can be manually tested. If no QA is needed, QA will close the ticket and move it to the ‘Done’ column.

IF a defect is found:

* a comment is added to the story @mentioning the engineer
* the defect is linked as a dependency for the story
* The QA Needed label is removed.
* The related story is set to ‘In Progress’.
* When the defect has been fixed, the Engineer will add the ‘QA Needed’ label to the story again.
* When all critical defects have been fixed, QA will label the story as “QA verified” and close the ticket (which will move it to the “Done” column).

## Testing Performance Impact of Code Changes

* The Performance team wrote this up for reference: <https://wiki.mozilla.org/Performance/Fenix/Performance_reviews>

## Accessibility

* During design reviews, we should ask ourselves if custom UI is necessary to accomplish the desired behavior. Oftentimes, out of the box android UI has accessibility and localization built in, whereas with custom pieces we need to build it ourselves.
* For new features that have UI elements that are unique to Fenix or have user interaction, test them personally with TalkBack (just as you would test to make sure the feature works before submitting a patch)
* Before submitting a patch, run the Accessibility Scanner over the screens affected to ensure no new issues are introduced (and post a screenshot of your results)
* QA should test tickets that have “eng:qa:needed” with accessibility services (like TalkBack) enabled and only mark them as “eng:qa:verified” if these services work well with the new feature
* As mentioned in the ticket, QA team will be adding accessibility checks to UI tests over time
* Remember: the earlier we catch issues in the process (closer to dev coding or UX designing) the less work it is for everyone involved! So please be diligent about going through these checks before submitting a patch.

## Templates

* We've created templates for new issues with instruction on how to fill them out based on their request/nature [here](https://github.com/mozilla-mobile/fenix/issues/new/choose)

More details in [link to Process Doc & Flowchart](https://docs.google.com/document/d/1w_6G4uCfQjyBh0ilQZKz3G-0IvBhzExlg80kaJrBA3c/)

## Adding Locales

The completion rate of different locales can be seen on [Pontoon](https://pontoon.mozilla.org/projects/android-l10n/).
While the project is in Preview stage, all locales will be the same between Firefox Preview and Firefox Preview Nightly.
Before we do our next large, milestone release, we'll update the Release locales to match the ones approved by L10N to have reached sufficient localization completion.
