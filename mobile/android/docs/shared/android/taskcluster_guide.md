# Taskcluster guide

This guide is designed to help you get started with [Taskcluster](https://firefox-ci-tc.services.mozilla.com/), the Continuous Integration system we use, and to introduce you to some intermediate topics.

* [Getting Started](#getting-started)
* [FAQ and HOWTOs](#faq-and-howtos)

## Getting Started

Are you totally new to Taskcluster or Taskgraph? Check out this [3-minute-video](https://johanlorenzo.github.io/blog/2019/10/24/taskgraph-is-now-deployed-to-the-biggest-mozilla-mobile-projects.html). If you want an overview of Taskgraph, you can read the blog post attached to it.

If you have any questions regarding Taskcluster or Taskgraph, feel free to join #releaseduty-mobile on Mozilla's Slack instance.

## FAQ and HOWTOs

### How do I test some changes I make under the `taskcluster/` folder?

The easy way: create a PR and you will get results in minutes. If you want to test your changes locally, have a look at the [README of Taskgraph](https://github.com/taskcluster/taskgraph/blob/main/README.rst).

### How do I see test results?

* On PRs: the status badge will let you know whether it is green, running, or broken.
* On the master branch: Treeherder is a great tool to have an overview. Here are the links for:
  * [fenix](https://treeherder.mozilla.org/#/jobs?repo=fenix)
  * [android-components](https://treeherder.mozilla.org/#/jobs?repo=android-components)
  * [reference-browser](https://treeherder.mozilla.org/#/jobs?repo=reference-browser)
* More generally: the status badge attached to each commit on Github will point you to each task run. For instance, here is a link to one of Fenix's release branch: <https://github.com/mozilla-mobile/fenix/commits/releases/v5.0>

### Why is my PR taking much longer than usual? What is this toolchain task?

Major mobile projects now cache external dependencies in a Taskcluster artifact. We implemented this solution because some maven repositories couldn't keep up with the load we required from them. This also puts us one step closer to having reproducible builds.
The cache gets rebuilt if [any of these files](https://github.com/mozilla-mobile/fenix/blob/7974a5c77c82915ce64faa6c403c0feadd7d6580/taskcluster/ci/toolchain/android.yml#L43-L47) change. Otherwise, build tasks just reuse the right cache: taskgraph is smart enough to know which existing one to use, we call this kind of tasks `toolchain`.
Sadly, there is no way to ask gradle to just download dependencies - we have to **compile everything**. This is why such task can be really long. We will address the slowness of fenix in <https://github.com/mozilla-mobile/fenix/issues/10910>.

### How do I find the latest nightly?

If you are interested in just getting the latest APKs, use these links:

* [fenix](https://firefox-ci-tc.services.mozilla.com/tasks/index/mobile.v2.fenix.nightly.latest) (choose the architecture you are interested in)
* [android-components](https://firefox-ci-tc.services.mozilla.com/tasks/index/mobile.v2.android-components.nightly.latest) (choose the component you are interested in)
* [reference-browser](https://firefox-ci-tc.services.mozilla.com/tasks/index/mobile.v2.reference-browser.nightly.latest) (choose the architecture you are interested in)

If you want to understand why something is wrong with nightly:

1. click on one of the aforementioned Treeherder links
1. type `nightly` in the search bar at the top right corner and press `Enter` => this will filter out any non-nightly job
1. check if there are any non-green jobs. If none are displayed, do not hesitate to load more results by clicking on one of the `get next` buttons.

### How do I rerun jobs?

* On pull requests: just close and reopen the PR, the full task graph will be retriggered. (You cannot rerun individual tasks while [bug 1641282](https://bugzilla.mozilla.org/show_bug.cgi?id=1641282) is not fixed)
* Otherwise: on the taskcluster UI, when looking at a task, go to the bottom right corner and select "Rerun". If it doesn't work, please contact the Release Engineering team (#releaseduty-mobile on Slack)

If you hesitate between reruns and retriggers, choose `rerun` first, then `retrigger` if the former failed.

### How do I run a staging nightly/beta/release on PRs?

It is easy! Just create a new file called `try_task_config.json` at the root of the repository and it will change what graph taskgraph generates.

⚠️ Do not forget to remove this file before merging your patch.

#### Nightly

```json
{
    "parameters": {
        "optimize_target_tasks": true,
        "target_tasks_method": "nightly"
    },
    "version": 2
}
```

will generate a nightly graph on your PR.

#### Beta

```json
{
    "parameters": {
        "optimize_target_tasks": true,
        "release_type": "beta",
        "target_tasks_method": "release"
    },
    "version": 2
}
```

#### Release

```json
{
    "parameters": {
        "optimize_target_tasks": true,
        "release_type": "release",
        "target_tasks_method": "release"
    },
    "version": 2
}
```

#### More generally

You can know what `target_tasks_method` to provide by looking at the `@_target_task()` sections in `target_tasks.py`. E.g: [target_tasks.py in Fenix](https://github.com/mozilla-mobile/fenix/blob/824dedb19588a9052b03ad162155c62ecd08e316/taskcluster/fenix_taskgraph/target_tasks.py#L29).

### Why don't PRs open by external contributors get Tasckluster jobs?

Long story short: It's because of a limitation of the Taskcluster security model. More details in this [Bugzilla bug](https://bugzilla.mozilla.org/show_bug.cgi?id=1534764).
2 workarounds:
  a. A person with write access on the repo opens a 2nd PRs with the same patch. This will trigger TC as usual.
  b. If the repository has bors enabled, first make sure the PR doesn't do anything suspicious, then comment `bors try` on the PR. [Bors](https://bors.tech/) will run Taskcluster jobs on an integration branch.

### How do I get secrets/tokens baked into an APK?

We use Taskcluster secrets to store secrets and tokens that will be injected into APKs. See [this pull request](https://github.com/mozilla-mobile/fenix/pull/7772) as an example. Steps are:

1. Implement a similar code based on the PR mentioned just before.
1. Tell a member of the Release Engineering team you would like to add a new secret.
1. Securely send the secret by following [one of these methods](https://mana.mozilla.org/wiki/display/SVCOPS/Sharing+a+secret+with+a+coworker)
1. For Releng:
    1. Go to <https://firefox-ci-tc.services.mozilla.com/secrets/?search=fenix>
    1. Edit existing secrets to include the new key and the new (unencrypted) value. Key is the [first value of this tuple](https://github.com/mozilla-mobile/fenix/pull/7772/files#diff-99b25bd5c02b53ff6a6260f416305b52533dfd3f70e0c2627a561f375cab3339R50). Value is the secret it self. You usually want to edit the `nightly`, `beta`, and `release` secrets. `nightly-simulation` should be edited too but with dummy values.
