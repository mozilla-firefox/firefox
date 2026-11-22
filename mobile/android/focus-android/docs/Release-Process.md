# Release Process

## Creating a new Release Branch

1. Create a branch name with the format `releases_v[version]` (for example: `releases_v87.0`).
2. Pin the Android Components version to the final release version with the format `[version].0.0`.

   For example:

   | FILE               | CURRENT VERSION    | RELEASE VERSION |
   |--------------------|--------------------|-----------------|
   | AndroidComponents  | 73.0.20210223143117| 73.0.0          |

   For reference, the AC release checklist can be found at <https://mozac.org/contributing/release-checklist>.
   Instead of updating the AC version manually, you could also wait for automation to update the [ac version](https://github.com/mozilla-mobile/focus-android/actions/workflows/update-ac.yml).
3. In the GitHub UI, create a branch on the upstream with the format `releases_v[version]`.
4. Push the `releases_v[version]` branch as a PR with the commit message "Set version to [version]" and commit into the upstream `releases_v[version]` branch.
5. Create a new [milestone](https://github.com/mozilla-mobile/focus-android/milestones) for the next release and close the existing milestone.
6. Announce the new `releases_v[version]` branch on Slack in #releaseduty-mobile.

Automation should take over after you land your PR into the upstream `releases_v[version]` branch. You can verify by clicking on the branch in the UI, and looking for the green/yellow dot that will list links to the running build tasks.

## After the Beta release

   Update the `versionName` from `build.gradle(Module:focus-android.app)` to the next Focus beta release version.This should be the `[version + 1]`.
   For example, if the release cut was for `90`, we're looking to prepare the latest nightly for `91`.
   For example:

   | FILE               | CURRENT VERSION    | RELEASE VERSION |
   |--------------------|--------------------|-----------------|
   | build.gradle       | 90                 | 91              |

## Renew telemetry

After the Beta cut, another task is to renew/remove all soon to expire telemetry probes. What we're looking for is to create a list of telemetry that will expire in `[release_version add 2]`. See [Firefox Release Calendar](https://wiki.mozilla.org/Release_Management/Calendar) for the current Release version. There is a script that will help with finding these soon to expire telemetry.

1. Use the helper `python3 tools/data_renewal_generate.py release_version+2` to detected and generate files that will help create the following files:

- `[release_version add 2]`_expiry_list.csv
- `[release_version add 2]`_renewal_request.txt

2. Upload the `[release_version add 2]`_expiry_list.csv to Google sheet in this [shared Google Drive](https://drive.google.com/drive/folders/1_ertMvn59eE9JmN721RqOjW6nNtxq9oS?usp=sharing) and contact product to review. For each telemetry listed answer decide for:

- Renew the metric (for how long? Add 12 versions?)
- Choose not to renew (but not delete)
- Choose to remove the metric
- Renew the metric and set to never expire (this should only be for business critical metrics)

3. Note that `metrics.yaml` is also modified.  Once the review is over, continue to modify `metrics.yaml` to match the decision made in the Google sheet.  Make sure to add the PR link and if the telemetry never expires, add the email of the owner as contact.
4. Create a PR for review.  Modify `[release_version add 2]`_renewal_request.txt and paste it to the PR for data review.  Make sure to add the answers for:

- When will this collection now expire?
- Why was the initial period of collection insufficient?

## Ask for Help

If you run into any problems, please ask any questions on Slack in #releaseduty-mobile.

References:

- <https://github.com/mozilla-mobile/focus-android/tree/releases_v96.0>
- <https://github.com/mozilla-mobile/focus-android/pull/6012>
