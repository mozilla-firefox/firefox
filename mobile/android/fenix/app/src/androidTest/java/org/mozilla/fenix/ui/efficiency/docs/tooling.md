# Helper scripts (run by hand)

Optional accelerators for the conversion workflow. They're plain Python/bash — **run them directly**; you do
not need the agent or the `effwatch` bridge for solo work (the bridge only exists so an agent can drive these
on your machine). They live in the team `testops-tools` repo (staged today from
`ui-test-modernization/tools/`). Most take `--json` for machine-readable output; omit it for readable text.

| Script                   | When                                  | Run it                                                                                                                                                                                                                       |
| ------------------------ | ------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **effscaffold**          | picking/understanding a test (gate 1) | `python3 effscaffold.py <Class.method>` — prints legacy body, TestRail id, whether it's already converted, the robots + their selector lines, and which screens are already modeled.                                         |
| **effdump / ScreenDump** | discovering real handles (gate 2)     | Dump a screen's elements in all three layers (Compose / Espresso / UIAutomator) so you author selectors from ground truth. On a nav failure the harness auto-dumps: `adb logcat -c` then `adb logcat -d -s EffScreenDump:I`. |
| **effcheck**             | static pre-flight (gate 5)            | `python3 effcheck.py --app-root <fenix>/app/src/main --eff-root <…>/ui/efficiency <File.kt> …` — resolution, empty nav paths, inline selectors, missing verbs, test-class boilerplate. Exit ≠ 0 = fix something.             |
| **effbuild**             | reading a build log                   | `./gradlew … 2>&1 \| python3 effbuild.py` — one-line verdict + only the real compile errors.                                                                                                                                 |
| **effpretty**            | reading a run (gate 6)                | `python3 effpretty.py view <logfile>` (or `capture` live via adb) — renders the structured `Eff` log so you read signal, not noise.                                                                                          |
| **effverify**            | the done-gate (gate 6)                | `python3 effverify.py <batchdir> <TestName> …` — confirms the named test actually ran, wasn't skipped, and its run is 0-failed. Exit 0 = truly passed.                                                                       |

## Running a test by hand

Fenix in the Firefox repository builds+runs through `./mach`, not `./gradlew`, from the repo root. To build and run
one instrumented efficiency test on a connected device:

```
./mach gradle fenix:connectedDebugAndroidTest \
  -Pandroid.testInstrumentationRunnerArguments.class=org.mozilla.fenix.ui.efficiency.tests.<Class>#<method>
```

Then read the logcat through `effpretty`, and gate with `effverify`. (The `effloop.sh` / `effwatch.sh` scripts
just wrap this so an agent can trigger it remotely — for solo work, the command above is enough.)

## Notes

- **Secrets:** `effbug` (Bugzilla) reads `BUGZILLA_API_KEY` from your env/`.eff.env`; you won't need it for
  pure authoring, only when filing bugs.
- **moz-phab has no `--dry-run`** — it's interactive (prints the commit list and prompts before creating
  revisions). Bound a stack with `moz-phab submit <first-new-commit>` so it can't touch already-landed base
  commits.
