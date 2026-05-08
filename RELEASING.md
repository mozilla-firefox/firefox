# Releasing Robowolf

GitHub Releases only, for now. The release workflow at
`.github/workflows/release.yml` builds a signed APK and attaches it to a
GitHub Release whenever a tag matching `v*` is pushed.

## TL;DR

```bash
# After merging a clean upstream-sync PR, cut a release:
git checkout privacy-features
git pull origin privacy-features
git tag -a v$(date -u +%Y.%m.%d) -m "Release $(date -u +%Y.%m.%d)"
git push origin --tags
```

The release workflow takes over from there: builds the APK, signs it,
publishes a GitHub Release.

## One-time setup

### 1. Generate a release keystore

You need a JKS keystore for signing release APKs. **Generate it once, keep
it safe forever** — losing it means future releases can't update existing
installs (Android requires the same signing key for upgrades).

```bash
keytool -genkeypair \
  -alias robowolf \
  -keyalg RSA \
  -keysize 4096 \
  -validity 10950 \
  -keystore robowolf-release.jks \
  -storepass 'CHOOSE_A_STRONG_PASSWORD' \
  -keypass 'CHOOSE_A_STRONG_PASSWORD' \
  -dname "CN=Robowolf, O=B-Project, C=IT"
```

Use a long, unique password. You won't enter it interactively after this —
GitHub Secrets does.

**Back up `robowolf-release.jks` somewhere outside this repo** (a password
manager attachment, an offline drive, or both). Do not commit it.

### 2. Encode the keystore for GitHub Secrets

```bash
base64 -w 0 robowolf-release.jks > robowolf-release.jks.b64
```

(On macOS, use `base64 -i robowolf-release.jks > robowolf-release.jks.b64`.)

### 3. Add four GitHub Secrets

GitHub web UI → Settings → Secrets and variables → Actions → New
repository secret:

| Secret name                 | Value                                                  |
| --------------------------- | ------------------------------------------------------ |
| `SIGNING_KEYSTORE_BASE64`   | Paste the contents of `robowolf-release.jks.b64`.      |
| `SIGNING_KEYSTORE_PASSWORD` | The store password from step 1.                        |
| `SIGNING_KEY_ALIAS`         | `robowolf`                                             |
| `SIGNING_KEY_PASSWORD`      | The key password from step 1 (same as store, usually). |

After adding all four, **delete the local `.b64` file** — the secret is
safely in GitHub now.

### 4. (Recommended) Pin Actions permissions

GitHub Settings → Actions → General → Workflow permissions → "Read and
write permissions" so the release workflow can create releases.

## Cutting a release

### Auto: tag-driven

Push any tag that starts with `v`. The most natural scheme is
date-based:

```bash
git tag -a v$(date -u +%Y.%m.%d) -m "Release $(date -u +%Y.%m.%d)"
git push origin --tags
```

The Actions tab shows the build progress (~30–60 minutes for a fresh
gradle cache; ~10–15 with cache). When done, the release appears at
`https://github.com/<you>/robowolf/releases`.

### Manual: workflow_dispatch

GitHub Actions tab → "Release" → Run workflow → enter a version like
`v2026.05.07`. The workflow creates the tag if it doesn't exist, then
builds and publishes.

## What gets built

The workflow runs `./gradlew :app:assembleRelease`. Fenix's release build
type:

- `applicationId = org.mozilla.firefox` ⚠️ — see "Things to fix" below.
- `minifyEnabled = true`, `shrinkResources = true` (R8 + ProGuard).
- ABI splits enabled — you'll get separate APKs per architecture
  (`armeabi-v7a`, `arm64-v8a`, `x86_64`). All of them are uploaded.
- Signed with the Robowolf keystore (not Mozilla's).

Verify any APK locally before publishing:

```bash
$ANDROID_HOME/build-tools/<version>/apksigner verify --print-certs robowolf-arm64-v2026.05.07.apk
```

The certificate fingerprint must match the one you generated in step 1.

## Things to fix before public distribution

The first release will work for sideload, but a few items still bite:

1. **`applicationId = org.mozilla.firefox`** in
   `mobile/android/fenix/app/build.gradle` line 161. This collides with
   the official Firefox app's package name on devices that have both.
   Change to e.g. `it.bproject.robowolf` so installs co-exist. This also
   matches Mozilla's trademark policy.

2. **Adjust SDK** is still pulled in via gradle deps even though we
   disabled it at runtime. F-Droid won't accept this; for GitHub-only
   distribution it's fine but worth removing eventually.

3. **The other 121 locale `strings.xml` files** still say "Firefox". For
   any locale you support publicly, run the same bulk replace we did for
   English and Italian.

## Chaining release with the upstream sync

The two workflows are intentionally decoupled — `upstream-sync.yml` opens
a PR, `release.yml` fires on tag push. To chain them:

- **Manual chain (recommended):** review and merge the sync PR, then push
  a date-tag. You retain veto power on every release.
- **Auto chain:** add a third tiny workflow that fires on `push` to
  `privacy-features` and tags `v$(date -u +%Y.%m.%d)`. Every merged sync
  becomes a release. Risky if a regression slips into a sync PR; only
  enable once you have CI tests that gate merges.

## When a release fails

Common failure modes and fixes:

- **`SIGNING_KEYSTORE_BASE64 secret is not set`** → step 3 above wasn't
  done, or the secret is on the wrong scope (organization vs repository).
- **Gradle OOM** → bump `org.gradle.jvmargs=-Xmx4g` in
  `gradle.properties` or run on a larger runner.
- **`./mach environment` fails** → workflow runs on a fresh runner with no
  spaces in the path; this should not occur in CI even though it does
  locally.
- **APK rejected by Android during install** → mismatched signing key.
  Verify the keystore in the secret matches the one used for the prior
  release. Never rotate the signing key after publishing.

## Bigger questions later

- **F-Droid (official):** submit a metadata PR to `gitlab.com/fdroid/fdroiddata`
  with a recipe pointing at this repo. F-Droid then builds and signs from
  source on every tag. Removes the keystore secret management from your
  side.
- **Custom F-Droid repo:** run `fdroidserver` somewhere; rsync the APK +
  index from CI. Useful for nightly channels.
- **Play Store:** would need a Google Play developer account and an
  internal-testing track. Not on the roadmap yet.
