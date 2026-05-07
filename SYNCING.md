# Keeping the Robowolf fork synced with upstream Firefox

## TL;DR

A GitHub Actions workflow at `.github/workflows/upstream-sync.yml` runs every
Monday at 06:00 UTC. It fetches `mozilla-firefox/firefox`, fast-forwards
`origin/main` to track upstream, merges `upstream/main` into a fresh
`sync/upstream-YYYY-MM-DD` branch off `privacy-features`, and opens a PR.
You review and merge that PR by hand.

## Branch layout

| Branch             | Role                                                                  |
| ------------------ | --------------------------------------------------------------------- |
| `main`             | **Pristine mirror** of `mozilla-firefox/firefox` `main`. Never edit.  |
| `privacy-features` | The actual Robowolf fork branch. All debloat lives here.              |
| `sync/upstream-*`  | Auto-generated weekly sync branches. Each becomes a PR.               |

## One-time setup

Two prerequisites must be satisfied before the cron-driven sync will fire:

### 1. The workflow file must be on the default branch

GitHub schedules `cron` triggers only from workflows that exist on the
repository's default branch. You have two options:

**Option A — Make `privacy-features` the default branch (recommended):**

  1. GitHub web UI → Settings → Branches → Default branch → switch to
     `privacy-features`.
  2. The workflow file is already on `privacy-features`, so cron will fire
     starting next scheduled slot.

**Option B — Cherry-pick the workflow file onto `main`:**

  ```bash
  git checkout main
  git checkout privacy-features -- .github/workflows/upstream-sync.yml
  git commit -m "ci: add upstream-sync workflow"
  git push origin main
  ```

  This puts a Robowolf-specific file onto your pristine mirror branch, which
  is unusual; option A is cleaner.

### 2. The workflow needs permission to push to `main` and create PRs

This is set inside the workflow file (`permissions: contents: write,
pull-requests: write`) and uses the default `GITHUB_TOKEN`. If `main` is a
protected branch, the fast-forward step will silently skip
(`continue-on-error: true`) and you'll need to use the GitHub web UI's
"Sync fork" button manually for that step. The PR-creation step is
unaffected.

## What the weekly run does

```
upstream/main ──┐
                ├──► origin/main           (fast-forward mirror)
                │
                └──► sync/upstream-YYYY-MM-DD
                       │
                       └─ merge ─► PR ──► privacy-features  (you review)
```

1. Checkout `privacy-features` with full history.
2. Fetch `mozilla-firefox/firefox` `main` with `--filter=blob:none` so the
   runner doesn't need to download the full blob history (~3 GB → ~600 MB).
3. Fast-forward `origin/main` to `upstream/main`. Skipped if the push fails
   due to branch protection.
4. Create a fresh `sync/upstream-YYYY-MM-DD` branch off `privacy-features`.
5. Merge `upstream/main` into it.
   - **No conflicts:** push, open PR titled `Upstream sync YYYY-MM-DD`.
   - **Conflicts:** commit conflict markers verbatim, push, open PR titled
     `[CONFLICTS] Upstream sync YYYY-MM-DD` with the `sync-conflict` label.

## Reviewing a clean PR

```bash
git fetch origin
git checkout privacy-features
git merge --ff-only origin/sync/upstream-YYYY-MM-DD
git push origin privacy-features
```

Or use the GitHub UI's "Merge pull request" button (a regular merge commit
is fine — the merge commit on the sync branch is already the merge of
upstream into your fork).

## Resolving a conflict PR

The bot committed conflict markers verbatim so the diff is reviewable. Pull
the branch, resolve, and merge:

```bash
git fetch origin
git checkout sync/upstream-YYYY-MM-DD
# Resolve the files with conflict markers (search for `<<<<<<<`).
# Most conflicts will be in:
#   - mobile/android/fenix/app/src/main/java/org/mozilla/fenix/utils/Settings.kt
#   - mobile/android/fenix/app/src/main/java/org/mozilla/fenix/FeatureFlags.kt
#   - mobile/android/fenix/app/src/main/res/values/strings.xml
#   - mobile/android/fenix/app/src/main/res/values/static_strings.xml
git add -A
git commit --amend --no-edit
git push origin sync/upstream-YYYY-MM-DD --force-with-lease
```

Then merge the PR.

## Manual sync

Trigger ad-hoc from GitHub Actions tab → Sync upstream Firefox → Run
workflow. Or locally:

```bash
git remote add upstream https://github.com/mozilla-firefox/firefox.git   # one-time
git fetch upstream main --filter=blob:none

git checkout privacy-features
git checkout -b sync/manual-$(date -u +%Y-%m-%d)
git merge upstream/main --no-edit -m "chore: manual upstream sync"
# Resolve conflicts if any
git push origin sync/manual-...
gh pr create --base privacy-features --title "Manual upstream sync"
```

## Why not auto-merge?

Every category of debloat (`FeatureFlags.kt`, `Settings.kt`, `strings.xml`,
`RobowolfPrivacyPrefs.kt`, the GroupTabStrip files) is in *modified* upstream
files. Mozilla edits these same files frequently. Auto-merge could silently
land an upstream change that re-introduces telemetry, sponsored content, or
onboarding nags — exactly the things this fork exists to remove. Manual
review is cheap insurance.

## What if the merge gets too painful?

If conflict resolution becomes a recurring multi-hour chore, the next
investment is converting Robowolf's modifications into a patch stack
(individual `.patch` files under `robowolf-patches/` plus a re-apply script).
That's how Mull, IronFox, and LibreWolf manage their forks long-term and is
significantly merge-friendlier than a long-lived branch. Tell the bot to
"convert to patch stack" when you're ready.
