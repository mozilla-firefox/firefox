
==Periodic File Updates==

This docker image examines the in-tree files for HSTS preload data, HPKP pinning and blocklisting, and
will produce a diff for each necessary to update the in-tree files.

If given a conduit API token, it will also use the arcanist client to submit the commits for review.


==Quick Start==

```sh
docker build -t hsts-local --no-cache --rm .

docker run -v /path/to/firefox:/builds/worker/checkouts/gecko -e GECKO_PATH=/builds/worker/checkouts/gecko -e TASKCLUSTER_ROOT_URL=https://firefox-ci-tc.services.mozilla.com -e DO_HSTS=1 -e DO_HPKP=1 -e BRANCH="mozilla-central" -e USE_MOZILLA_CENTRAL=1 hsts-local
```

HSTS checks will only be run if the `DO_HSTS` environment variable is set.
Likewise for `DO_HPKP` and the HPKP checks. Environment variables are used
rather than command line arguments to make constructing taskcluster tasks
easier.

To prevent a full build when landing with Phabricator, set the `DONTBUILD`
environment variable.

==Background==

These scripts have been moved from
`https://hg.mozilla.org/build/tools/scripts/periodic_file_updates/` and
`security/manager/tools/` in the main repos.

==HSTS Checks==

`scripts/getHSTSPreloadList.js` will examine the current contents of
nsSTSPreloadList.inc in the task's checkout, add in the mandatory
hosts, and those from the Chromium source, and check them all to see if their
SSL configuration is valid, and whether or not they have the
Strict-Transport-Security header set with an appropriate `max-age`.

This javascript has been modified to use async calls to improve performance.

==HPKP Checks==

`scripts/genHPKPStaticPins.js` will ensure the list of pinned public keys are
up to date.

==Example Taskcluster Task==

https://firefox-ci-tc.services.mozilla.com/tasks/create/

```yaml
provisionerId: aws-provisioner-v1
workerType: gecko-1-b-linux
retries: 0
created: '2018-02-07T14:45:57.347Z'
deadline: '2018-02-07T17:45:57.348Z'
expires: '2019-02-07T17:45:57.348Z'
scopes: []
payload:
  image: srfraser/hsts1
  maxRunTime: 1800
  artifacts:
    public/build/nsSTSPreloadList.diff:
      path: /home/worker/artifacts/nsSTSPreloadList.diff
      expires: '2019-02-07T13:57:35.448Z'
      type: file
    public/build/StaticHPKPins.h.diff:
      path: /home/worker/artifacts/StaticHPKPins.h.diff
      expires: '2019-02-07T13:57:35.448Z'
      type: file
    public/build/blocklist.diff:
      path: /home/worker/artifacts/blocklist.diff
      expires: '2019-02-07T13:57:35.448Z'
      type: file
  env:
    DO_HSTS: 1
    DO_HPKP: 1
    BRANCH: mozilla-central
    USE_MOZILLA_CENTRAL: 1
    REVIEWERS: catlee
metadata:
  name: Periodic updates testing
  description: Produce diffs for HSTS and HPKP in-tree files.
  owner: sfraser@mozilla.com
  source: 'https://firefox-ci-tc.services.mozilla.com/tasks/create'
tags: {}
extra:
  treeherder:
    jobKind: test
    machine:
      platform: linux64
    tier: 1
    symbol: 'hsts'

```
