---
name: profiler-analysis
description: |
  Analyze Firefox performance profiles using the profiler-cli CLI tool. Trigger when given a profiler.firefox.com or share.firefox.dev link, a local profile path, or when the user wants to investigate an issue in a Firefox profile. Always use this skill instead of WebFetch for Firefox profiler URLs; WebFetch only retrieves the profiler UI's HTML shell and cannot access profile data, whereas profiler-cli downloads and parses the actual profile into a local daemon that supports structured queries over stacks, markers, threads, and samples. Not for capturing a new profile: if no profile exists yet and one has to be recorded use profile-capture instead.
argument-hint: "[profile path or URL]"
allowed-tools:
  - Bash(profiler-cli:*)
  - Bash(searchfox-cli:*)
  - Bash(jq:*)
  - Read
  - Grep
  - Glob
---

# Analyzing a Firefox profile

You are helping a Mozilla Firefox engineer analyze a Firefox performance profile using the `profiler-cli` CLI tool. The user works on Firefox and has familiarity with the browser's internals, so you can use Firefox-specific terminology freely (e.g. Gecko, SpiderMonkey, Necko, content/parent process split, PBackground, etc.) without explaining it.

## How to Help the User

Before doing anything else, verify `profiler-cli` is installed by running `profiler-cli --version`. If the command is not found, STOP immediately: tell the user to install it by running `./mach bootstrap` (answering yes to the agentic coding tools question), and to then restart the agent from a new terminal, since the current shell's PATH won't include the directory bootstrap installs it to. Then end your turn. Do NOT attempt any workaround. In particular, do not download profiles from Taskcluster or artifact URLs, do not fetch them with WebFetch, and do not parse profile JSON by hand. `profiler-cli` is the only supported way to load and analyze a profile, and there is no acceptable fallback.

A profile from a local build or from the Firefox MCP is unsymbolicated. Load it with `profiler-cli load --with-samply <path>`. This needs profiler-cli 0.10.0 or newer; if it is older, tell the user to run `./mach bootstrap` (and `npm uninstall -g @firefox-devtools/profiler-cli` if they installed it with npm before, since that copy can shadow the bootstrapped one) and stop.

When invoked:

1. Run `profiler-cli guide` first and read all of it, unpiped: no `head`, `tail`, or `grep`. It is the command reference, and the analysis patterns come late in the output.
2. Load the profile into a named session and pass `--session <id>` to every later command, so you never touch a session the user started.
   Never `stop --all`, and never stop a session you did not start. The profile comes from `$ARGUMENTS`, or from the conversation when a link or path was pasted there.
3. Walk through the analysis interactively. Run commands and interpret the output.
4. Suggest next steps based on what the output reveals.
5. When output is large or complex, highlight the most actionable findings.

The case studies below are snapshots of past investigations, not a command reference: they predate some commands the guide documents, so treat a walkthrough's commands as one route rather than the full set, and use the guide output for the full list of commands.

If the URL is a `profiler.firefox.com/from-file/...` or `profiler.firefox.com/from-browser/...` link, stop and tell the user it cannot be loaded. These URLs store the profile data locally in the browser tab and are not accessible to anyone else. Ask the user to either upload the profile using the share button in the Firefox Profiler UI (which produces a `share.firefox.dev` or `profiler.firefox.com/public/...` link), or pass a local file path to the profile JSON directly.

Do not print commands for the user to run, execute them and interpret the results.

Once there is no more useful information left in the profile to look at, run `profiler-cli stop --session <id>` to shut down the background daemon process (it persists beyond individual commands and must be explicitly stopped to free the port and memory), then present the findings.

## Accuracy: Never Present Guesses as Facts

Profiles invite plausible-sounding causal stories the data does not support. Engineers act on your conclusions, so a confident wrong guess wastes their time and erodes trust.

- **Separate observation from inference.** State what the data shows (sample counts, marker durations, frames, thread states) as fact; treat root cause, "why it's slow", what a symbol does, and causal links as inference until validated.
- **Validate before asserting, and back claims with data.** Confirm inferences first via `searchfox-cli`, marker payloads, or sample counts, and quantify (e.g. "4200 of 5000 main-thread samples").
- **Label what you can't validate.** Say "hypothesis" or "guessing from the symbol name" plainly; never dress a guess as a conclusion, and give the user the concrete next step to confirm or refute it.
- **Report inconclusive results as inconclusive** rather than inventing a tidy explanation; say plainly when the data doesn't support a conclusion.

## Case Studies

The `references/` directory in this skill contains real profiling investigations. Each scenario has two files:

- **Narrative file** (e.g. `macos-extensions-hang-infinite-recursion.md`): first-person walkthrough explaining the reasoning and what was found. Read it when you need the reasoning rather than the commands, for example when writing findings up for a bug.
- **profiler-cli companion file** (e.g. `macos-extensions-hang-infinite-recursion-cli.md`): step-by-step `profiler-cli` commands with real output and annotations. These are the most useful for calibrating your approach.

Before starting an analysis, use `Glob` to list available case studies, then read the `-cli.md` file that most closely matches the current scenario:

- **Lock contention / mutex blocking during startup**: `firefox-macos-startup-font-initialization-cli.md`
- **Test timeout / idle hang (main thread parked in a wait primitive, deadlocked on an event or message that never fires — often a cross-process race)**: `windows-test-timeout-cross-process-race-cli.md`
- **Hang or jank (extension JS spinning at 100% CPU)**: `macos-extensions-hang-infinite-recursion-cli.md`
- **Hang or jank getting worse over time, IPC I/O thread blocked, long IPCIn marker durations**: `macos-ipc-blob-url-accumulation-cli.md`
- **Android startup performance**: `simpleperf-non-rooted-firefox-startup-cli.md`
- **Android / simpleperf profiles**: `simpleperf-non-rooted-fenix-sync-history-fetch-cli.md`
- **Resource usage (CPU/memory over time), marker-only profiles with no samples**: `resource-usage-linux-fat-aar-build-cli.md`
- **Network or I/O issues**: `macos-network-pr-bad-descriptor-error-cli.md`
- **NSS/TLS deadlock freezing networking**: `macos-network-nss-doh-deadlock-cli.md`
- **Broken stack walking**: `windows-broken-stackwalk-jpeg-avx2-cli.md`

If the scenario is unclear, read `firefox-macos-startup-font-initialization-cli.md` as a general-purpose baseline.

The list above is for calibration only. The actual problem may be novel or not covered by any case study. Use the examples to understand the investigation approach, not to constrain what you look for or which commands you use.
