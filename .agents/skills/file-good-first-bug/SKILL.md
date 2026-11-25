---
name: file-good-first-bug
description: Use this skill when the user wants to file good-first-bugs in Bugzilla for Firefox. A good-first-bug is a narrow, self-contained, low-risk task scoped so a first-time contributor can land it without deep context. Sources include lint warnings, small typos, dead-code removal, mechanical refactors, docs conversions, or any other small task the user points at. Opens a prefilled `enter_bug.cgi` form per bug in the user's browser for them to submit. Trigger on phrases like "file a good first bug", "open good first bugs for X", "create a good-first-bug from this".
---

# Filing good first bugs

## What is a good first bug

A bug a newcomer can pick up cold and finish in one short patch. The hallmarks:

- **Narrow scope** - one file or one tiny area. Multiple newcomers can work in parallel without conflict.
- **Self-contained** - the bug body contains everything: what to change, where to find it, how to reproduce / verify, and links to the contribution quickref. The contributor should not need to ask follow-up questions.
- **Low risk** - fix is mechanical or obvious. No design decisions, no API redesign, no cross-module reasoning.
- **Verifiable** - there's a clear signal the patch is correct (a lint that stops firing, a test that passes, a doc that renders, a warning that disappears).

Linter output is just one *source* of such tasks. Other good sources:

- A small typo or wording fix in code/docs the user spotted
- Removing one piece of dead code the user identified
- Converting one RST doc page to Markdown (or similar mechanical migration)
- Replacing one deprecated API call with its successor in a single file
- Adding a missing test for one small behavior
- Renaming one symbol consistently within a contained module

If the task touches many files, requires API design, or needs deep domain knowledge - it's **not** a good first bug. Push back on the user before filing.

## Workflow

1. **Understand the task source.** The user will point at something: a file, a lint output, a typo, a paragraph of docs, a deprecated call site. Read enough to confirm the work is genuinely narrow.

2. **Decide on bug shape.** Prefer **one bug per file / per atomic task**. Combine only if the user explicitly asks. Multiple small bugs let newcomers work in parallel and is the established Firefox precedent (e.g. bug 2031381).

3. **Propose to the user before filing.** Use `AskUserQuestion` to confirm scope and which items to file. Never generate bug URLs without explicit approval - filing bugs is user-visible.

4. **Open a prefilled `enter_bug.cgi` form per bug.** There is no MCP tool to create Bugzilla bugs - the user submits each one from the form. Use the URL builder below, which opens it in their browser.

5. **Report one line per bug** - its title and where it went (product/component). **Never paste the URL into your reply.** Terminal linkification gives up past some length, and a prefilled form's URL clears that threshold: it carries the whole bug body percent-encoded in its query string, which ran to 2413 and 2635 characters for two ordinary good-first-bugs. Past the threshold the reader gets an unclickable wall of `%20`. It costs you as well as the reader: those characters stay resident in your context and are re-sent with every later request in the session, which is also why the script no longer prints the URL - having it in front of you is what invites pasting it. Do **not** respond by writing a terser bug body - a good-first-bug has to be self-sufficient, and that is worth more than a URL a reader was never meant to click. The script already put the form in front of the user.

## Choosing product/component

Most Firefox good-first-bugs go to **Developer Infrastructure / Lint and Formatting** when they come from lint output, since that's the team that owns the trackers. For other sources, route to the component that owns the affected code:

- Docs conversions → the component of the docs (often `Developer Infrastructure / Source Documentation`)
- Code typos / dead-code → the component that owns the file
- API call-site updates → the component using the API

**Ask the user** if you're unsure - don't guess.

## Tracker bug

Good-first-bugs typically `blocks:` a tracker bug so the metawork is visible. Known trackers:

- clippy warnings → `1361342`
- ruff warnings → `1968295`
- Coverity issues → `1230156`
- clang-based static analysis → `712350`
- other linters → ask the user

For non-lint bugs there may or may not be a tracker. Ask the user; if there isn't one, omit `blocked`.

## Whiteboard

Set the Bugzilla whiteboard (`status_whiteboard`) to `[lang=LANGUAGE]` so newcomers can filter good-first-bugs by language. Use the full language name:

- Rust → `[lang=rust]`
- Python → `[lang=python]`
- JavaScript → `[lang=js]`
- C++ → `[lang=c++]`
- HTML / CSS → `[lang=html]` / `[lang=css]`
- Markdown / docs → `[lang=md]`

Pick the language of the file the contributor will actually edit. If a bug spans languages, list both: `[lang=rust][lang=python]`.

## Mentor

Always set a mentor (`bug_mentors`) on the bug, defaulting to the bug filer. Without a mentor the bug won't show up in some good-first-bug dashboards, and a newcomer's comments or questions may be lost because nobody is clearly responsible for answering them.

The mentor is a **Bugzilla account** email, which the URL builder resolves from the filer's Bugzilla API key when you omit `--mentor`. Do not infer it from an address you already have: a Bugzilla account frequently uses one of its own (a `+bmo` alias is common), and a corporate address is not evidence about it. A guessed address puts a stranger on the hook for a newcomer's questions, and one that is no Bugzilla account at all makes `enter_bug.cgi` reject the submission.

Pass `--mentor` when the mentor is someone other than the filer, and when the builder reports that it cannot resolve the address - most contributors have no python-bugzilla config for it to read. In that case ask the filer for it. Substituting an address you happen to have is never the answer.

## Comment body template

The body must be self-sufficient. Adapt this skeleton:

```
Filing as a good first bug to learn workflows.

<one short paragraph: what to do and where>

<optional: warning list / code snippet / current vs. desired>

Link to the code:
<a permalink from `searchfox-cli -q <term> -p <path> --permalink`; when it prints nothing, https://searchfox.org/firefox-main/source/<path> with no line number>

To verify the fix:

<the exact command(s) the contributor should run, e.g.>
./mach lint -W -l clippy <path>
./mach test <path>
./mach doc --no-serve --no-open

<optional: brief note about why the change is desired>

Tutorial to contribute:
https://firefox-source-docs.mozilla.org/contributing/contribution_quickref.html
https://firefox-source-docs.mozilla.org/contributing/stack_quickref.html

Please don't ask for the bug to be assigned. It will be automatically assigned to the first patch.
```

If it is a more complex bug, please use the `good-next-bug` keyword instead (`--keywords good-next-bug`), and in the message, request the contributor to ask for the bug to be assigned.

The last paragraph (about auto-assignment) is canonical - keep it verbatim.

For a `good-next-bug`, replace that last paragraph with:

```
Please ask for the bug to be assigned to you before starting work on it, and wait until it is assigned before submitting a patch.
```

## URL builder

Use the helper script `scripts/build_url.py` (path relative to this skill file) to open each prefilled `enter_bug.cgi` form:

```bash
./scripts/build_url.py "<title>" "<comment>" --tracker 1361342 --lang rust
```

It opens the form in the user's default browser and prints a one-line confirmation. `--print-url` prints the URL instead, for a headless host - and if you use it, hand the user the URL by writing it to a file rather than into your reply, for the length reason in step 5. Run it with `--help` for the full list of options (`--product`, `--component`, `--tracker`, `--keywords`, `--lang`, `--mentor`). It can also be imported and its `build_url()` function called directly, which stays offline and omits the mentor unless you pass one; the command line resolves it (see [Mentor](#mentor)).

## Example: lint warnings (canonical case)

This is the pattern bug 2031381 established. For each candidate file with 1-3 trivial warnings:

- Title: `Fix clippy warnings in <path>`
- Body lists the warnings, links searchfox at the first warning line, gives the `./mach lint -W -l clippy <path>` reproducer.
- Product/Component: `Developer Infrastructure / Lint and Formatting`
- Blocks: `1361342`

**Avoid these lints in good-first-bugs** unless the user explicitly accepts the extra scope:

- `clippy::too_many_arguments` - needs API refactoring
- `clippy::missing_safety_doc` - needs writing real Safety docs
- `clippy::type_complexity` - needs designing a `type` alias

Quick grouping of warnings by file:

```bash
awk '/^\// { f=$0; next } /warning/ { c[f]++; lines[f]=lines[f] "\n" $0 }
     END { for (f in c) if (c[f] <= 3) print c[f]"\t"f lines[f] }' clippy.txt | sort -n
```

## Example: typo fix

- Title: `Fix typo "recieve" in <path>`
- Body: quote the offending line(s), point at searchfox, give the corrected wording.
- Component: whatever owns `<path>`.
- No tracker unless the user names one.

## Example: docs conversion

- Title: `Convert <path> from RST to Markdown`
- Body: link to the existing RST file, explain the target is `.md` with the same content, point to a recent landed conversion as a reference, give `./mach doc --no-serve --no-open` as the verification command.
- Component: `Developer Infrastructure / Source Documentation`.

## Reminders

- **One bug per atomic task** unless the user opts into a combined bug.
- **Always set a mentor, and never hand-write the address** - let the builder resolve it, or ask the filer. Without a mentor the bug is invisible in some dashboards and a newcomer's questions go unanswered.
- **Don't auto-assign** - the template explicitly tells contributors not to ask.
- **More complex bugs get `good-next-bug`** - and their body asks the contributor to request assignment instead.
- **Never paste a prefilled URL into your reply** - it is long enough to defeat terminal linkification. Let the script open the form, and don't trim the bug body to shorten it.
- **Push back on scope** that's too big, too vague, or requires design decisions - those are not good-first-bugs.
- **Match the component to the work** - don't dump everything under Lint and Formatting if the bug isn't a lint bug.
