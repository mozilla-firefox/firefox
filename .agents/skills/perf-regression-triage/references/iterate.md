# Iterating to a fix

The loop is: hypothesis, minimal change, narrow try push, check. Each round takes hours of
wall clock and real CI capacity, so the value is almost entirely in making each round
answer one clean question.

## Before the first round

Have these written down:

- The confirmed delta per test, from `confirm.md`. This is the number the fix must clear.
- One or two **target tests** — the most-regressed subtests from the alert. These are the
  signal you iterate against.
- A specific hypothesis about the mechanism, from the Before/After profiles (via the
  **profiler-analysis** skill) and the patch diff.

Iterating without a mechanism hypothesis turns into guess-and-push, which is the single
most expensive failure mode here.

## The iteration push

Deliberately not the confirmation command. Narrow hard:

For a fix that changes compiled code, a directional iteration can use a faster non-PGO
source build:

```
./mach try perf --alert <ALERT_ID> \
  --tests <target-test> \
  --platforms linux \
  --non-pgo \
  --no-artifact \
  --rebuild 6 \
  -m "bug <BUG> perf fix attempt N: <one-line hypothesis>"
```

If the fix needs no compilation, use `--artifact` and omit `--non-pgo`. That keeps the
shippable PGO binary and avoids a source build.

What each narrowing buys, and what it costs:

- `--tests <target>` — cuts the task count sharply. Cost: you stop seeing collateral
  movement in the other regressed subtests.
- `--platforms linux` — one platform instead of all. Cost: platform-specific behaviour is
  invisible. Use the platform the alert fired on.
- `--non-pgo` — noticeably faster source builds for compiled changes. Cost: different
  optimization configuration, so treat the result as **directional only**. A fix that
  works here still has to be proven on shippable. Use `--no-artifact` so the build includes
  the compiled fix.
- `--rebuild 5` or `6` — deliberately thinner than the confirmation run's 10. Enough to see
  whether a change moved the number at all, which is the only question a round needs to
  answer. Cost: a marginal result here is not trustworthy on its own, so do not treat a
  small improvement as a fix until the unnarrowed run below confirms it at 10.

Put the hypothesis in `-m`. Three attempts later it is the only thing that tells you what
each push was testing.

Every push still needs the user's explicit approval.

## Between rounds

- Change **one thing** per round. Two changes in one push means an ambiguous result and a
  wasted round.
- If a round shows no movement, suspect the hypothesis before suspecting the measurement —
  especially if the change was small and the noise band is wide.
- Prefer changes that restore the old behaviour on the hot path over changes that add new
  optimization. Reverting a fast path you removed is easy to reason about; a new
  optimization needs its own justification and its own review.
- If three rounds produce nothing, stop pushing and go back to the profiles. Consider
  whether the honest answer is a partial fix or an accepted tradeoff — see
  `close-out.md`.
- Want a profile of your own attempt rather than the ones in the bug: add `--profile` to
  the push, then analyze it through **profiler-analysis**.

## Final verification

A narrowed `--non-pgo` result is not sufficient to close the bug. Once a round looks good,
run the confirmation command again, unnarrowed, with the fix applied:

```
./mach try perf --alert <ALERT_ID> --rebuild 10 --no-artifact \
  -m "bug <BUG> perf fix verification"
```

Verification has to run the fix on the same shippable/PGO configuration the alert fired
on. Keep `--no-artifact` when the fix touches compiled code: an artifact build would leave
the fix out. For a fix needing no compilation, use `--artifact` in the command above.
See the configuration section in `confirm.md`.

This must show the full alert set back at or near the base numbers. Check specifically that
no other subtest in the set got worse — a fix that trades one speedometer subtest for
another is not a fix, and the perf sheriffs will catch it.

Link that PerfCompare result in the bug and on the review. It is the evidence the fix
works, and it is what lets a reviewer approve a perf-motivated change quickly.
