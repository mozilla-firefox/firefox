# Frontend Code Review Best Practices

## Introduction

Code reviews help ensure that the code solves a specific problem, doesn’t have bugs or regress other functionality, and has adequate test coverage. It also provides an opportunity to ensure correct documentation is added, whether through code comments or documentation of changes. The process of reviewing code also helps spread knowledge about how the code works amongst developers, both as a patch author and as a reviewer.

A review is required before code or tests are added to Mozilla’s repository. Once approved, code is merged to autoland and once tests pass, sheriffs will merge it with other commits to the `main` branch. This guide outlines a set of standards for both patch author and reviewer.

## Intended audience and outcomes

This document is primarily intended for engineers on the Firefox Desktop Front end team. The aim of this guide is to align the team around an agreed set of expectations for patch reviews that will reduce confusion, prevent misunderstandings, help land patches more efficiently and to help onboard new team members.

As an engineer, you’ll be in the position of being both a patch author and a reviewer, so having a good understanding of the expectations of both roles is recommended.

## What is expected from a frontend code review?

The sections below outline some of the high level expectations when reviewing code. See also the [Technologies and Specifics sections for more details on what to look for that are specific to Frontend code](#technologies-and-specifics).

### The basics

A code review and automation will check if this patch:

* applies cleanly to the `main` branch and can be built.
* has a {ref}`good commit message <commit-message>` that describes the changes as well as the reason for them where that is not obvious.
  * Adding a longer description in a commit message will be used as the summary in Phabricator.
* fixes the issue at hand.
* has automated test coverage where appropriate. [Exceptions to this are covered by using the test-exception-\* tags in Phabricator](/testing/testing-policy/index.md#exceptions). Here are some common exceptions in frontend where \[new\] test coverage isn’t needed:
  * style/image/l10n-only changes
  * Code that interacts directly with the OS in ways we can’t mock in automated tests.
  * Code that requires very specific external circumstances (OS settings, wall clock time, actions by other programs, etc.) we can’t mock reliably.
  * Refactorings of existing code (where existing test coverage exists)
  * Removing code
* passes linting and/or automated tests.

**If any of these is missing/broken, a reviewer is likely to stop then and there.** Before submitting patches, code authors should ensure they have these covered.

### Is this necessary and sufficient?

Two useful ways of looking at patches are “is this necessary?” and “is this sufficient?”:

* **Necessary**: are all the changes in the patch required? It’s common to find other issues when fixing a bug, but code not necessary to fix this specific bug should be moved to a separate bug. Reviewers will usually flag up:
  * Unrelated code changes
  * Changing unrelated whitespace etc. (mind your editor’s autoformat)
  * Unnecessarily complex or hacky fixes
* **Sufficient**: does this fix the issue completely? That is, have we looked at edge cases and is the patch/patchset definitely fixing all of the issue, not just parts of it or a symptom of it?
  * This also means **considering what is not in the patch**. So e.g. when renaming a method, patch authors and reviewers should not just check if all the replacements in the patch are correct, but also if all occurrences were actually touched in the patch (searchfox should make this relatively straightforward, or you can apply the patch locally and use `grep`/`ack`/`ag` etc).

## Expectations for the Reviewer

### Progress > Perfection

As noted above, the context for the patch should be provided by the author, however it’s important to try to strike a balance between ensuring we maintain high standards and being able to make progress. If you identify issues that are routinely cropping up that could be covered by automated linting, please file issues for new rules to be added. This helps us to ensure that the time engineers dedicate to review is primarily spent on efficiently ensuring a change addresses what it sets out to and is both a maintainable and safe addition to Firefox.

### Language and tone in review comments

* Take the time to thank and point out good code changes.
* Focus on the code, not the patch author (avoid wording that sounds like it’s criticizing the author, for example “this doesn’t make sense, why did you do it this way?” versus “I’m curious why you took this approach. Could you please provide some context?”).
* Framing comments as questions rather than statements can help create an environment conducive to collaboration. Rather than “This doesn’t work when `bar` is `undefined`”, “Could you please check if this code is properly handling the case of `bar` being `undefined`?” .
* Using “please” and “what do you think?” goes a long way in making others feel like colleagues, and not subordinates. As a reviewer, double-check your comments. Assume the code author has spent more time thinking about this part of the code than you have and might actually be right, even if you originally thought something was wrong.
* Be clear about what changes are required to be changed in order to land the patch versus those that are optional.
* For things that aren’t [nits](#what-is-a-nit) or trivially understood to be improvements, explain *why* you are requesting a change. Understanding the reasoning behind requested changes helps make sure that you and the patch author share an understanding of what the patch is doing, and helps the patch author learn.
* Minimize the number of passes required for review and addressing review comments. Where possible provide your review comments in a single pass rather than multiple iterations. This helps to reduce the number of cycles needed to get something through review and any associated context switching for both the reviewer and the patch author. In a case where you don't have time for a full review, mention that your review is currently incomplete and set expectations for when it will be so the author can plan accordingly.
* When reviewing code on behalf of additional review groups added to a patch, your review should primarily focus on the areas that the review-group is responsible for or interfaces that your group is using. However, if you do spot a non-trivial issue that falls outside this during review, then it would be reasonable to flag it. .

### Turnaround time for reviews

* Aim for a response within 1 business day. This need not be a full review, but could be a comment setting expectations about when you’ll be able to review the patch, pointing to other folks who might be able to review it sooner or would be more appropriate to review the patch.
* If you’re explicitly asked for review and can’t get to it due to time constraints, it’s always helpful to set expectations for the patch author.

## Expectations for the patch author

### Communication

There is often more than one way to address an issue, and reviewers and patch authors may disagree about the “right” approach to a bug. What is “complex” or “hacky” to one person may be “correctly architectured” or “efficient” to another. If when working on a bug, you’re unsure about what the “right” approach is (e.g. “just add a nullcheck here and move on” vs. “refactor the calling code so we don’t ever pass null”), talk to your prospective reviewer or other people familiar with the code before deciding, to avoid repeated work when your reviewer suggests an alternative, perhaps because they have other context you weren’t aware of, or to fit the general architecture of the component you’re changing.

When you haven’t talked to your reviewer because you think the approach you chose is obviously correct, it is usually still helpful to outline your reasoning/choices in the extended commit message, a phabricator comment, and/or bugzilla. It doesn’t need to be an essay, but ideally the reviewer should be able to understand the patch and its reasoning from reading the patch and commit message.

Make use of the commit message to offer both a brief, and where necessary a longer explanation of what the change is doing. Reviewers often review dozens of patches each week. Ensuring that the patch contains all the information they need to review the change helps speed up reviews and avoids confusion about what a patch is trying to achieve.

If the patch needs a shorter turnaround time of the review cycle due to a release or uplift deadline (which should also be reflected in the bug priority and severity that’s been set), you should ping blocking reviewers on slack or matrix to raise awareness of this deadline and to confirm they can review your patch as soon as possible. If they are unable to do so, you’ll need to find another reviewer who can. Where possible ask the existing reviewer for a suggestion, or ask someone else in the same review group. If that’s not possible, ask for input from your team/project lead or manager.

When making changes to revisions, using the ‘plan-changes’ action in Phabricator [“removes revisions from reviewers’ queues, meaning that they will no longer be visible under “Ready to Review” on their “Active Revisions” dashboards, until a new diff is uploaded”](https://moz-conduit.readthedocs.io/en/latest/phabricator-user.html#other-revision-actions)

When making non-trivial changes to patches which are already on Phabricator, use `--message` (or `-m`) with your `moz-phab` submission to explain what you’re changing.

Leave questions or comments in Phabricator about things you were not sure about. Your reviewer might be able to offer answers, or you might have stumbled on deeper underlying problems that need addressing elsewhere. Either way, everyone learns.

### Addressing review comments

Except for very small patches it is extremely common for a reviewer to request changes to a patch after their first review. Reviewers are expected to be courteous and clear about what they’re asking you to change and why. While reviewers *do* have the final sign-off, code review should be considered to be a conversation. Reviewers are not expected to be perfect. If you don’t understand what a reviewer has asked or disagree with their suggestions then respond with your reasoning or ask for clarification. Usually this happens in the code review itself but it is not uncommon to talk with the reviewer directly where that is faster. In this case it is helpful to add a summary of your discussion to the review comment in Phabricator for future code historians.

If you have requested reviews from multiple reviewers and one of them requests changes, the patch is removed from all reviewers' queues until you re-submit the patch with the requested changes, questions for clarification or comments.

Once you’ve addressed everything the reviewer requested, push your patch up for review again and expect that the reviewer will be verifying that you changed everything requested. To help save unnecessary review iterations, where possible avoid putting the patch back into review until you've made all the necessary changes.

Occasionally the reviewer may ask for a small number of changes but still approve the patch. You are expected to make those changes before the patch can land, the reviewer is just indicating that they trust you to make those changes and don’t need to re-review the patch. If those changes turn out to be larger than the reviewer would have reasonably expected then you should re-request review.

You’re within your rights to suggest that some review feedback can be fixed in follow-up bugs and patches, so long as you ensure to get those follow-ups filed in Bugzilla and that they’re addressed in a timely manner. This is particularly true for in-progress features that are being held to Nightly. Features still being developed and held to Nightly should not be expected to land in a perfect state all in one patch.

### Code quality

Make sure before submitting a patch that it compiles cleanly and that the tests included all pass.

Consider re-reading the patch you’ve just submitted to Phabricator once you submit it the first time. Often Phabricator’s clear diff view will highlight things to you that you might have missed, especially if you’ve worked on the patch a long time. It can also help highlight changes that aren’t immediately obvious to someone reading the patch for the first time, and might need an explanation.

Avoid whitespace or other trivial changes to code that you don’t need to change to accomplish your goal. Making other, unrelated changes makes the patch harder to review, and makes it difficult to later to find the origin of a change.

Several small patches are easier to review than one monolith that changes lots of files.

Talk to your peers and prospective reviewer(s) before starting big refactors. Firefox’s code base is huge and refactors carry risk, so reviewers are wary of big changes like that and they are usually difficult to review.

## Technologies and Specifics

This section covers some frontend-specific aspects of patches that reviewers are likely to look at.

### JS/DOM

* Use existing components to implement feature designs. Do not reimplement custom button/card/toggle/… styling based on the design and vanilla HTML, as it doesn’t scale, is hard to maintain, and will likely miss edge-cases for accessibility, RTL, etc.
  * The inverse is also true: not every new bit of code you work on has to be factored out as a generic, reusable component immediately. When you are the first/only consumer of a bit of code, it is usually not possible to predict what aspects of your code will need to be “generic”, where API boundaries should be, and so on. Trying to do it immediately is doomed to failure, so don’t spend time on it - save it for when the second potential consumer comes along.
* Lines of code is a flawed metric, and we’re not code golfing - but don’t write several 10-line functions when 2 lines of inline code will do. Balance brevity and readability appropriately.
* Use DOM properties over attributes where possible.
  * Where it isn’t, `toggleAttribute` and `classList.toggle` are often helpful to avoid repetitive if/else blocks.

### Styling and CSS, and SVGs

* Use existing components/classes to implement feature designs (see JS/DOM section).
* Keep in mind all CSS needs to work in RTL languages. Use logical properties (`margin-inline-start` and friends) rather than physical ones (`margin-left`).
  * See [Firefox RTL (right-to-left) guidelines](/code-quality/coding-style/rtl_guidelines.md) for more detailed information.
* See the [Accessibility](#accessibility) section on use of colours, HCM, etc.
* See detailed [CSS authoring guidelines](/code-quality/coding-style/css_guidelines.md).
* See the [Firefox SVG Guidelines](/code-quality/coding-style/svg_guidelines.md).

### Localization

* For anything user-facing, use localised strings. **Do NOT** hardcode English strings in markup.
* For new code, use fluent. For modifying older code, use what the old code uses (but consider switching to fluent if it’s straightforward, as it provides better translation primitives in other languages).
* When writing experimental features that need strings for en-US only that are not final yet, use fluent and put the ftl file in a `content` rather than `locale` directory and package it accordingly. When strings are final, move them to a regular `locale` directory and include them as normal but make sure to do so outside string freeze and while allowing reasonable time for our (largely volunteer) localisers to submit translations - don’t just dump dozens of strings into `locale` a day before string freeze.
* If the meaning of a string changes, or \[in fluent\] you add/remove attributes, **you must update the message identifier**.
* More detailed [fluent review guidelines](/l10n/fluent/review.md#guidelines-for-working-with-fluent-files) are available separately.

### Accessibility

* Write semantic HTML.
* Anything mouse-accessible should be keyboard-accessible or have a keyboard-accessible equivalent.
* Ensure things work in high contrast mode and that there is sufficient colour contrast between foreground and background items in non-high-contrast mode and with different themes.
* Any images that **aren’t purely decorative** (e.g. toolbar button icons where there is no visible text label) need an `alt` attribute or `aria-label` or similar to give it an accessible name, as well as a tooltip for sighted users who might not understand icons.
* Any images that **are** purely **decorative** (e.g. illustrations next to text) should be implemented using one of these options:
  * CSS background images, which have no representation in the accessibility tree
  * HTML `img` elements that use `role=presentation` to remove them from the accessibility tree, and do not have an alt attribute.
* If implementing animations or transitions, do so behind a `prefers-reduced-motion` media query to ensure people who have epilepsy or other motion-triggered sensitivities are not hurt by our attempts to make things beautiful.
* The first rule of ARIA is “don’t use ARIA” (see also “Write semantic HTML”) - but it can be a tool where semantic markup is insufficient (e.g. live regions, complex labeling structures).

### Performance

* Avoid [style](/performance/bestpractices.md#detecting-and-avoiding-synchronous-style-flushes) and [layout](/performance/bestpractices.md#detecting-and-avoiding-synchronous-reflow) flushes.
* Use `requestIdleCallback` and/or workers for anything on the startup path and/or CPU or disk-intensive work that doesn’t actually need to happen immediately.
* Use `IOUtils` to do async File IO, rather than nsIFile for synchronous access.
* Keep complexity in mind and use reasonable data structures
  * e.g. use a Set rather than arrays if they’re big, unordered, and you need to look items up in the collection
  * Bear in mind that some collections/loops may be reasonably sized in the common case, but have pathological edge cases (that person with 3652 tabs is looking at you. You know who I mean\!). You shouldn’t need to jump through too many hoops, but use memoization or association (which are O(1) or O(log n)) rather than “just” looping through the list (which is O(n)).
* Other than that, don’t over-optimize things unless the code is known to be hot/performance-sensitive.
  * As an example, we don’t, as a rule, use `for (var i; i < ary.length; i++)` style “raw” array loops, instead preferring the more readable `for (let item of ary)`, even though that is slightly less performant.

### Security

* When in doubt, ask the security team, and/or `#security` on slack.
* Take care when communicating between processes or with web content. Specifically:
  * We can’t trust web content.
  * We can’t trust web content processes (!). Any information you get via IPC (JSActor messages or similar) could be completely bogus. Use information from the parent process (the CanonicalBrowsingContext and so on) to make security decisions wherever possible.
  * As a corollary, if you have code that wants to navigate or interact with web content, do it entirely within the content process, rather than passing e.g. URIs or similar information to the parent and expecting it to navigate for you. This avoids exposing APIs to (potentially compromised) content processes that let them navigate arbitrarily - the “basic” APIs for that (`document.location` and so on) will be secured by Gecko itself, and we want to reuse that infrastructure as much as possible.
  * Do not load untrusted content (ie content from the web) in the parent process.
* XSS and markup related advice:
  * Avoid inline event handlers wherever possible.
  * All `about:` pages must have a Content Security Policy (CSP). Use one that prevents inline styles and inline script.
  * Do not use `innerHTML`.
  * Gecko has built in sanitizer APIs - if you have to deal with untrusted HTML, see [nsIParserUtils](https://searchfox.org/firefox-main/source/parser/html/nsIParserUtils.idl). Do NOT hand-roll sanitizing inputs.
* Other vectors to take into account:
  * URLs are malleable and not a good way of identifying an origin. Use nsIPrincipal objects. You’ll need this to deal correctly with `blob` URIs, `about:blank`, and data URIs.
  * We can’t assume that for `something.foo.sometld`, `something` is a subdomain of `foo`, or that `foo.sometld` is the “top” domain. Some TLDs (`.co.uk` and `.org.uk` and so on are an obvious example, but `github.io` and `s3.dualstack.ap-northeast-1.amazonaws.com`, too\!) have individually controlled subdomains. Use [nsIEffectiveTLDService](https://searchfox.org/firefox-main/source/netwerk/dns/nsIEffectiveTLDService.idl) to work out what the “top” domain is. Do not use manual string manipulation.
  * Spoofing: where an attacker tries to pretend to the user they’re on a different website, usually by overlaying content or markup over the address bar or other trusted UI, or using full screen or similar tricks.
  * Clickjacking/keyjacking: where an attacker encourages users to click or hit keys repeatedly (“win $50 if you [hook a duck](https://en.wikipedia.org/wiki/Hook-a-duck) by clicking this button really fast”), and uses that to bypass security warnings. Typical defence: delay enabling buttons on security-critical dialogs - there’s even a [helper](https://searchfox.org/mozilla-central/rev/d00845a44a8d1bf3f472ff36fd3f22a03af30a76/toolkit/components/prompts/src/PromptUtils.sys.mjs#49-61) for this.
  * Crypto: do not make up your own cryptography, random number generator, password/key management or similar. Talk to experts (some of them work at Mozilla)\!

### Testing

* All patches must have automated tests where possible and practical.
  * When writing tests, be sure to see that you’ve seen your test fail. This helps to avoid scenarios where a test always passes even when it shouldn’t.
  * There are some patches where it may be appropriate not to have tests, particularly ones that only touch documentation, styling (CSS/images), or test-only changes.
  * For situations where you can’t write tests, such as with features that interact with the operating system in ways we can’t verify automatically, leave a comment explaining why to the reviewer. It is likely going to be the first question they ask and they should know when one of these situations applies or may know alternate ways to write the test.
* If you’ve pushed to try, leaving a link to the try push on Phabricator can help avoid duplicating effort (Note: reviewers are not expected to create try pushes by default). If there are unclear test failures that may or may not be intermittent, your reviewer can help work out what is happening.

### Documentation

* Ideally, code should be readable without explanation. Where documentation is required, use code comments for individual pieces of code, and the extended commit message (i.e. the second/third/nth lines of the commit message) to describe the high-level goal of your change.
* A guideline for code comments is to add them in cases where they provide additional context for *why* a change is being made, but to avoid adding them if the comment only restates *what* the code is doing.
* If the reviewer asks for explanations, this is often a sign that the code should be simplified, it needs more code comments, or the approach in the patch is not right. Prefer code and/or comment changes over communicating only in Phabricator - so that future readers of the code have the same context.
* Ensure that any existing Firefox Source Docs material is also updated to reflect the change.

## Commandeering patches

Patches should only be commandeered by agreement with the original patch author or if the author cannot be expected to respond in a timely fashion (on vacation or sick leave or no longer active in the project).

If you do need to commandeer a patch you can preserve the original author with `git commit --amend --author="Other Person <person@mozilla.com>"` when amending the original commit.

## Community Participation Guidelines

Please remember to always abide by [the Community Participation Guidelines](https://www.mozilla.org/en-US/about/governance/policies/participation/).

Be generous and inclusive. Always assume good intentions, even where there is a disagreement during the course of the review.

## FAQ

### What is a Nit?

A nit is a minor defect. This could be a typo or a small issue. These are usually the kind of things that would *not result in a “request for changes”*, but they should be called out and it’s expected that they would be addressed by the patch author before the patch lands.

### How do I file a bug for new lint rules?

[If you have an idea for an eslint (JS) or stylelint (CSS) rule you can file a bug here](https://bugzilla.mozilla.org/enter_bug.cgi?product=Developer%20Infrastructure&component=Lint%20and%20Formatting) if a more specific component isn’t a better fit.

## Further Reading

* [Code review antipatterns](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/code-review-antipatterns/) an article by Simon Tatham outlining common anti-patterns in code review.
