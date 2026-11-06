---
name: bug-filing
description: File a Bugzilla bug for Firefox/Gecko work, draft a bug summary and description, or attach a file to an existing bug. Use when the user asks to file a bug, wants a bug summary/description for a change, needs to report a defect/enhancement/task, or wants a file (e.g. a screenshot or testcase) attached to a bug. Determines the product and component with `mach file-info`, drafts the summary and description, and opens a prefilled enter_bug.cgi form in the browser for the user to submit.
allowed-tools:
  - Bash(./mach file-info:*)
  - Bash(python3 .agents/skills/bug-filing/file-bug.py:*)
  - Bash(python3 .claude/skills/bug-filing/file-bug.py:*)
---

# Filing a bug

## Overview

File a Mozilla bug by opening a prefilled Bugzilla "enter bug" form in the user's
browser. The skill picks the right component automatically, drafts a well-formatted
summary and description, and hands the form to the user to submit, so the bug is
created under their own account with a chance to review and adjust.

## Workflow

1. **Determine the product and component.** Run
   `./mach file-info bugzilla-component <file>` on a representative file the change
   touches. The output is `Product :: Component`; split on `::` for the `product`
   and `component` URL params.
   - If it returns `UNKNOWN`, pick the closest sensible component rather than
     leaving it blank.

2. **Choose the bug type** (per Mozilla's task/defect/enhancement guide):
   - `defect` — shipping software is not behaving as expected: a regression, a crash, an error.
   - `enhancement` — a new feature or function, or changing how an existing feature behaves.
   - `task` — change a configuration, update a parameter, or refactor existing code;
     engineering changes with no user-facing behavior change (tooling, tests, docs, build).

3. **Draft the summary and description.**
   - **Shape the summary to the bug type chosen above.** In all three cases the
     mechanism, the file list, and any counts belong in the description, not the
     summary.
     - `defect` - the observable symptom, in the terms someone hitting it would
       use ("Restored tabs come back with blank titles"). Not the suspected
       cause and not the fix: either can turn out wrong, and the symptom is what
       people search for.
     - `enhancement` - the capability wanted ("Allow pinned tabs to be restored
       per window").
     - `task` - the action to take ("Use `declareLazy` in the remaining
       sessionstore modules").
   - Cut what goes stale or reads as code in prose: a count ("Six modules..."),
     a helper's namespace prefix (`declareLazy`, not `XPCOMUtils.declareLazy`),
     glob shorthand (`lazy.*`).
   - Use ordinary words, and never a term coined for the occasion. Where the
     project has no established name for the thing, describe it instead.
   - Match a sibling bug's shape when one exists, so the two are findable
     together, and let any trailing clause carry only what distinguishes this
     one.
   - In the description, wrap code identifiers (function, variable, class, file,
     pref, and flag names) in backticks so they render as code in the filed bug.
   - For a test-failure bug, include a link to the test's dashboard:
     `https://tests.firefox.dev/test.html?test=<path>`.

4. **Open the prefilled form.** Don't print the draft or ask for approval first: the
   user reviews it in the form, and nothing is filed until they submit it. Printing
   it as well would put a second copy of the draft in your context, re-sent with
   every later request in the session. If the user asked only for a summary and
   description, print those instead and stop.

   A request to file a bug, in any wording, means opening this form. Never create
   the bug yourself, even with a Bugzilla API key at hand: the form is the user's
   only chance to review the text before it goes public under their name.
   Attaching a file to an existing bug is the one post you make yourself, and
   only once the user approves it (see below).

   Run the helper script with each Bugzilla
   `enter_bug.cgi` field as a `field=value` argument; it URL-encodes the values and
   opens the form in the browser (cross-platform, so Linux, macOS, and Windows all work):
   ```
   python3 .agents/skills/bug-filing/file-bug.py 'product=<P>' 'component=<C>' \
       'bug_type=<T>' 'short_desc=<summary>' 'comment=<description>'
   ```
   - Single-quote every `field=value` argument.
   - Write `short_desc` and `comment` as plain text, with markdown backticks around
     code identifiers; the script does all the encoding.
   - Any form field works, so add more as the bug needs them, e.g. `blocked=<bug>`
     (blocks), `dependson=<bug>` (depends on), or `see_also=<url>`.

   The script opens the form and prints a one-line confirmation. **Never paste the
   URL into your reply**: a prefilled form carries the whole description
   percent-encoded in its query string, which puts the URL past the length at which
   a terminal stops linkifying it, so the reader gets an unclickable wall of `%20`.
   It also costs you: those characters stay resident in your context and are re-sent
   with every later request in the session. Report the summary and the component
   instead - and don't compress the description to shorten the URL, which nobody
   was meant to read.

   Tell the user they can edit the form before submitting it, or ask you for
   changes; for changes, run the script again with the revised draft. Once they
   submit the form, they provide the bug number.

## Attaching a file to an existing bug

A form cannot prefill a file input, so attaching goes through the REST API:
```
python3 .agents/skills/bug-filing/attach-to-bug.py --bug <N> --file <path> \
    --summary '<attachment description>' --comment-file <path>
```
It reads the API key from `api_key` under `[bugzilla.mozilla.org]` in
`~/.config/python-bugzilla/bugzillarc`, and prints the new attachment's URL.
`--comment '<text>'` takes the comment inline, for short text without quotes.
`--help` lists the other options, e.g. `--file-name`.

Run it with `--dry-run` first, which prints the payload without posting it.
The dry run fails when no key is configured, but never checks the key against
Bugzilla, so a bad key shows up only as an HTTP 401 on the real run.
Show the user that output, name the bug, and wait for their approval before
the real run: the attachment is public and permanent.

## Notes

- Filing a bug only opens a prefilled form; the skill never submits the bug
  itself. It posts only an attachment, after the user approves the dry run.
