# Patch uplifting rules

This page describes the rules applied by the Release Team to uplift (aka
backport) a patch from a development branch to a more stable branch. For
example, taking a patch that fixes a bug in Nightly and applying it to the Beta
branch. The [release tracking rules](https://wiki.mozilla.org/Release_Management/Tracking_rules)
page may also be helpful for understanding how release management makes
decisions.

All [regular guidelines](/contributing/how_to_submit_a_patch.md) for changes
apply.

## General

Before you request, ask "does this actually need an uplift?" With a 2-week
cadence, the next major release is rarely more than 14 days away. Most fixes
could simply ride the next train. Request an uplift only when the cost of
waiting one cycle is genuinely unacceptable (see the criteria and examples
below).

Prefer disabling over uplifting. If the issue can be mitigated by turning a
feature off via a pref or feature flag, do that instead of uplifting a fix.
Disabling is lower-risk and can often be done remotely without an uplift.

## Requesting Uplift via Phabricator

See [requesting an uplift](requesting_an_uplift.md) for how to request an uplift
via Phabricator.

## Guidelines on approval comments for Beta and Release

A form is provided to add information in a comment for the uplift request.

- \[User impact if declined/Reason for urgency\]: In addition to the STR (steps
  to reproduce) reported in the bug, explain on a deeper level how an end user
  would be impacted with/without your change. State explicitly why this cannot
  wait for the next train.
- \[Code covered by automated testing?\] Yes/No/Unknown
- \[Fix verified in Nightly?\] Was the fix verified in a nightly build, e.g. by
  the reporter or QA?
- \[Needs manual test from QE?\] Yes/No
  - \[Steps to reproduce for manual QE testing\] Either point to an existing
    comment listing STR or elaborate here directly.
- \[List of other uplifts needed\] If this patch depends on other changes that
  are not present on the target branch, list them here (and request approval in
  the other bugs as well)
- \[Risk associated with taking this patch\]: Low/Medium/High
- \[Explanation of risk level\]
  - When saying something is "low", "medium", or "high" risk please justify
  - E.g.: Low risk because its a one line CSS change impacting only settings
    page
  - E.g.: Medium, given the code complexity and integration with other areas of
    code that might be impacted. Expect regressions in areas like...
  - E.g.: Risky, given the complex nature the bake time we have have on central
    or other branches. High rate of fallouts/regression. Could be mitigated by
    more manual testing in areas or running some targeted test cases
    - If the issue could be worked around by disabling a certain feature, or
      with a different, less-risky, patch for uplift, mention this here —
      "disable instead of uplift" is the expected default.
- \[String changes made/needed?\] Please answer this as "none" if no string
  changes were made. String changes need to be approved by a l10n driver since
  they impact the work of localization teams.
- \[Is Android affected?\] Does this fix also impact Android applications?
  [GeckoView](https://wiki.mozilla.org/Mobile/GeckoView) Meaning as well as a
  desktop release, Release Management should create an
  [android-components](https://mozac.org/) build and follow up with
  Fenix/Focus.

## Risk vs Severity

An important question for any uplift is: how severe is the issue, and how risky
is the fix? We uplift low-risk fixes for high-severity issues. We do not uplift
low-severity issues, high-risk changes, or new features — those ride the next
train or wait behind a pref.

- [Severity](/bug-mgmt/guides/severity.md) = user impact: how many users are
  affected and how badly (crash, data loss, broken top site, security exposure)
  vs. cosmetic or rare edge case.
- "Risk" = the chance the patch itself introduces a new regression: size of the
  change, component risk, bake time, and test coverage.

Rule of thumb:

- High severity + low risk -> We would uplift. The ideal candidate (e.g. a
  one-line fix for a high-volume crash).
- High severity + high risk -> This is case by case. Strongly prefer disabling
  the feature or a smaller/safer fix. Uplift the risky fix only if there is no
  safer mitigation and it genuinely can't wait.
- Low severity + low risk -> We would not uplift, and it can ride the train.
- Low severity + high risk -> We would not uplift, and it can ride the train.
- New feature / feature enablement -> We would not uplift, regardless of
  severity or risk. It can be gated behind a pref and ride the train.

Examples:

We would uplift:

- A one-line null-check that fixes a high-volume startup crash. (high severity,
  low risk)
- A small, well-tested fix for a functional regression breaking a top-1000 site
  for many users. (high severity, low risk)
- Flipping a pref to disable a broken feature. (high severity, minimal risk)

Would not uplift:

- A fix for a cosmetic glitch or an edge case affecting very few users. (low
  severity)
- A large JS-engine refactor to fix a moderate bug. (high risk — take bake time
  or find a smaller fix)
- Enabling or shipping a new feature. (new feature — ride the train or gate
  behind a pref)

## Firefox (Desktop and Mobile)

### Beta Uplift (approval-mozilla-beta)

- Must be landed on firefox-main, or reason given for direct-to-branch uplift
- Ideally reproducible by QA so easily verified
- Has 'baked' on firefox-main and demonstrated decrease in crash or
  reproducibility
- No string changes, unless approved by l10n driver
- Cannot reasonably wait for the next train, AND cannot be mitigated by
  disabling a feature via pref/flag

Changes can be:

- Performance improvement (proven, need real numbers)
- Top-crashers
- Recent regressions

The closer to the release the more careful an uplift should be done. With
2-week cycles, the whole Beta window is short, so scrutiny ramps quickly.

### Release Uplift (approval-mozilla-release)

Some issues are bad enough that we don't want to wait until the next major
release. We do major releases every 2 weeks. We also have one planned dot
release in between each release. Most fixes can wait that long. Candidates for
uplift to Release are either drivers of a dot release or narrow "ridealongs". If
it would be a release blocker, it might be a good dot release driver.

Since the next major release is only two weeks out, the ridealong bar is high —
if it can wait, then it should wait.

Examples include:

- Major top crash (above or near the level for oom crashes). Provide evidence of
  impact.
- High volume startup crash.
- Security issue that doesn't need a chemspill.
- Functional regression with broad impact (confirmed by telemetry or high volume
  of SUMO / user reports).
- A problem in a major feature that materially breaks it for many users.

While uplifts are generally not the preferred way to ship new feature work, it
is understood that there are times when business needs to do so justify the
required effort. Our release process is designed to have the flexibility to
accommodate these requests, though in general they need to be handled on a case
by case basis to determine the suitability. Teams are encouraged to reach out to
Release Management in the #release-coordination channel on Slack or @relman so
their specific needs can be assessed. Factors that will need to be taken into
account include:

- Size and scope of patches to be uplifted
- QA availability to test prior to shipping and during development
- Engineering resources to resolve any conflicts between different development
  branches
- String additions/changes which may impact available locales

To uplift to release without relman approval, your change must be a part of a
known-issue respin or NPOTB (not part of the build) config changes needed to
support build infra

### ESR Uplift (approval-mozilla-esrXX)

We do a ESR minor release every 2 weeks. These releases do not include new
features and are reserved for stability/security fixes only. ESR uplift requests
follow a similar uplift request process as Beta/Release.
