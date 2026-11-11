---
name: android-retrace
description: Decode (retrace) an R8-optimised or obfuscated Fenix or Focus Java stack trace, e.g. from Socorro, the Google Play Console, logcat or a test run, by fetching the matching mapping.txt and running retrace.
---

# Retracing Android stack traces

Nightly, beta and release builds of Fenix and Focus are optimised by R8, and Fenix is also obfuscated. Their Java stack traces have renamed library classes (`qnf$b`, `clg`), `R8$$SyntheticClass` frames, missing inlined frames and wrong line numbers. A trace whose Fenix frames look readable still needs retracing: its line numbers are wrong too.

Use `.agents/skills/android-retrace/retrace.py` with the trace saved to a file. Pick the mapping source from what the user has:
- A Socorro crash: the crash's Details tab shows two IDs. Ask for the **Build ID**, which is the 14-digit Gecko build ID (e.g. `20261111091117`). The separate "Application Build ID" is the app's version code, which is only needed for the Play Console fallback below. Then run `python3 .agents/skills/android-retrace/retrace.py --build-id <id> [--product focus] trace.txt`. It finds the shipped app bundle's build on Taskcluster and downloads its `mapping.txt` (about 220 MB, cached in `artifacts/retrace/`).
- A Treeherder build job (`B`, `Bf`, `AAB`): `--task-id <taskId> trace.txt`.
- An `.aab`, e.g. from the Google Play Console: `--aab <file> trace.txt`.
- A `mapping.txt`: `--mapping <file> trace.txt`.

If `--build-id` fails (builds before bug 2086064 published no mapping, and Taskcluster artifacts expire), have the user download the bundle from the Google Play Console: Test and release > Latest releases and bundles > All app bundles, find the bundle whose version code equals Socorro's "Application Build ID", and download the original file; then use `--aab`.

Strip logcat prefixes (`E AndroidRuntime:` and timestamps) from the trace first. When reporting the result, point out restored inlined frames and corrected line numbers. The mapping must come from the exact build that crashed; a mapping from a different build gives a plausible but wrong trace.

The script's tests run with `./mach python --virtualenv python-test .agents/skills/android-retrace/tests/test_retrace.py`.
