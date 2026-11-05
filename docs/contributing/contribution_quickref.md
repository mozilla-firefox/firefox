(firefox-contributors-quick-reference)=

# Firefox Contributors' Quick Reference

Some parts of this process, including cloning and compiling, can take a long time even on modern hardware.
If at any point you get stuck, please don't hesitate to ask at [https://chat.mozilla.org](https://chat.mozilla.org)
in the [#introduction](https://chat.mozilla.org/#/room/#introduction:mozilla.org) channel.

Don’t hesitate to look at the {doc}`Getting Set Up To Work On The Firefox Codebase </setup/index>` for a more detailed tutorial.

## Before you start

Please register and create your account for

[Bugzilla](https://bugzilla.mozilla.org/) : web-based general-purpose bug tracking system.
To register with Phabricator, make sure you enable Two-Factor Authentication (My Profile >> Edit Profile & Preferences >> Two-Factor Authentication) in Bugzilla.

[Phabricator](https://phabricator.services.mozilla.com/): web-based software development collaboration tools, mainly for code review.
Please obtain an API Token (Settings >> Conduit API Tokens)

## Windows dependencies

1. You need a {ref}`supported version of Windows<tier-1-hosts>`.

2. Download the [MozillaBuild Package.](https://ftp.mozilla.org/pub/mozilla/libraries/win32/MozillaBuildSetup-Latest.exe) Installation directory should be:

   ```text
   c:\mozilla-build\
   ```

3. Before moving on to the next steps, make sure to fulfill the {doc}`Windows prerequisites </setup/windows_build>`

:::{note}
All the commands of this tutorial must be run in the shell provided with the MozillaBuild Package (start-shell.bat)
:::

{doc}`More information on building Firefox on Windows </setup/windows_build>`

## Bootstrap a copy of the Firefox source code

You can download the source code and have Firefox automatically download and install the other dependencies it needs. The below command as per your Operating System, will download a lot of data (years of Firefox history!) then guide you through the interactive setup process.

Downloading can take from 40 minutes to two hours (depending on your connection) and the repository should be less than 5GB (~ 20GB after the build).

The **default options are recommended**.
If you're not planning to write C++ or Rust code, select {doc}`Artifact Mode <build/artifact_builds>`
and follow the instructions at the end of the bootstrap for creating a mozconfig file.

### To Setup Firefox On Windows

```shell
cd c:/
mkdir mozilla-source
cd mozilla-source
wget https://raw.githubusercontent.com/mozilla-firefox/firefox/refs/heads/main/python/mozboot/bin/bootstrap.py

python3 bootstrap.py
```

More information on {doc}`building Firefox for Windows </setup/windows_build>`.

### To Setup Firefox On macOS and Linux

```shell
curl -L https://raw.githubusercontent.com/mozilla-firefox/firefox/refs/heads/main/python/mozboot/bin/bootstrap.py -O

python3 bootstrap.py
```

More information on {doc}`building Firefox for Linux </setup/linux_build>` and {doc}`building Firefox for MacOS </setup/macos_build>`.

### To Setup Firefox for Android

```shell
curl -L https://raw.githubusercontent.com/mozilla-firefox/firefox/refs/heads/main/python/mozboot/bin/bootstrap.py -O

python3 bootstrap.py
```

More information on {doc}`building Firefox for Android </mobile/android/index>`

## To set up your editor

:::{note}
Visual Studio Code is the recommended editor for Firefox development.
Not because it is better than the other editors but because we decided to
focus our energy on a single editor.
:::

Setting up your editor is an important part of the contributing process. Having
linting and other features integrated, saves you time and will help with reducing
build and reviews cycles.

See our {doc}`editor page for more information about how to set up your favorite editor <editor>`.

## More advanced git configuration

See the {doc}`Git tips and tricks page <git-tricks>` to use a personal fork, and for other useful git configurations.

## To build & run

Once the System is bootstrapped, run:

```shell
cd firefox
./mach build
```

which will check for dependencies and start the build.
This will take a while; a few minutes to a few hours depending on your hardware.

:::{note}
If you build Firefox often, add `ac_add_options \-\-with-ccache=sccache` to .mozconfig.
sccache will significantly speed up your builds by caching compilation results.
The Firefox build system will download sccache automatically.
:::

:::{note}
The default build is a compiled build with optimizations. Check out the
{doc}`mozconfig file documentation </setup/configuring_build_options>`
to see other build options. If you don't plan to change C++ or Rust code,
an {doc}`artifact build <build/artifact_builds>` will be faster.
:::

To run it:

```shell
./mach run
```

This command will open your locally built Firefox in a new window.

{doc}`More information about building Firefox on Linux </setup/linux_build>` / {doc}`More information about building Firefox on MacOS </setup/macos_build>`

If you encounter build errors, please reference the more detailed "Building Firefox" on your specific operating system document and specifically the "Troubleshooting" section.

(write-a-patch)=

## To write a patch

Make the changes you need in the codebase. You can look up UI text in [Searchfox](https://searchfox.org) to find the right file.

:::{note}
If you are unsure of what changes you need to make, or need help from the mentor of the bug,
please don't hesitate to use the needinfo feature ("Request information from") on [Bugzilla](https://bugzilla.mozilla.org/home) to get the attention of your mentor.
:::

After making your changes, visualize your changes to ensure you're including all the necessary work:

```shell
# For files changed/added/removed
git status

# For detailed line changes
git diff
```

Then commit your changes:

```shell
# Git
git commit
```

(commit-message)=

The commit message should look like:

```text
Bug xxxx - Short description of your change. r?reviewer

Optionally, a longer description of the change.
```

**Make sure you include the bug number and at least one reviewer (or reviewer group) in this format.**

For example, here is an example of a good commit message:
"Bug 123456 - Null-check presentation shell so we don't crash when a button removes itself
during its own onclick handler. r=person"

To {doc}`find a reviewer or a review group <reviews>`, the easiest way is to run
`mach file-info reviewers` on the files you modified:

```shell
./mach file-info reviewers dom/media/mediasink/AudioSink.cpp
```

It prints the reviewer groups covering those files, starting with the ones Phabricator
would add on its own. Failing that, run `git log <modified-file>` on the relevant files,
and look who usually is reviewing the actual changes (ie not reformat, renaming of
variables, etc).

To visualize your patch in the repository, run:

```shell
# Git
git show
```

{doc}`More information on how to work with stack of patches <stack_quickref>`

## To make sure the change follows the coding style

To detect coding style violations, use mach lint:

```shell
./mach lint path/to/the/file/or/directory/you/changed

# To get the autofix, add --fix:
./mach lint path/to/the/file/or/directory/you/changed --fix
```

{doc}`More information </code-quality/index>`

## To test a change locally

To run the tests, use mach test with the path. However, it isn’t
always easy to parse the results.

```shell
./mach test dom/serviceworkers
```

To run tests based on {doc}`/gtest/index` (C/C++ based unit tests), run:

```shell
./mach gtest 'QuotaManager.*'
```

## To test a change remotely

Running all the tests for Firefox takes a very long time and requires multiple
operating systems with various configurations. To build Firefox and run its
tests on continuous integration servers (CI), multiple {doc}`options to select tasks </tools/try/selectors/index>`
are available.

To automatically select the tasks that are most likely to be affected by your changes, run:

```shell
./mach try auto
```

To select tasks manually using a fuzzy search interface, run:

```shell
./mach try fuzzy
```

To rerun the same tasks:

```shell
./mach try again
```

From [Treeherder](https://treeherder.mozilla.org/) (our continuous integration system), it is also possible to attach new jobs. As every review has
a try CI run associated, it makes this work easier. See {ref}`attach-job-review` for
more information.

:::{note}
This requires [level 1 commit access](https://www.mozilla.org/about/governance/policies/commit/access-policy/).

You can ask your reviewer to submit the patch for you if you don't have that
level of access.

To request level 1 access, follow the steps in
{doc}`Request commit access level 1 <levelling-up>`.
:::

{doc}`More information </tools/try/index>`

## To submit a patch

To submit a patch for review, we use a tool called [moz-phab](https://pypi.org/project/MozPhab/).
To install it, run:

```shell
./mach install-moz-phab
```

Once you want to submit your patches (make sure you {ref}`use the right commit message <commit-message>`), run:

```shell
moz-phab
```

It will publish all the currently applied patches to Phabricator and inform the reviewer.

If you wrote several patches on top of each other:

```shell
moz-phab submit <first_revision>::<last_revision>
```

[More
information on how to use Phabricator and MozPhab](https://moz-conduit.readthedocs.io/en/latest/phabricator-user.html)

## To update the working directory

If you're finished with a patch and would like to return to the tip to make a new patch:

```shell
git pull --rebase
```

## To update a submitted patch

It is rare that a reviewer will accept the first version of patch. Moreover,
as the code review bot might suggest some improvements, changes to your patch
may be required.

If your patch is not loaded in your working directory, you first need to re-apply it:

```shell
moz-phab patch D<revision_id>

# Or you can use the URL of the revision on Phabricator
moz-phab patch https://phabricator.services.mozilla.com/D<revision_id>
```

Make your changes in the working folder and run:

```shell
# Git
git commit --amend
```

After amending the patch, you will need to submit it using moz-phab again.

:::{warning}
Don't use `git commit --amend -m`.

Phabricator tracks revision by editing the commit message when a
revision is created to add a special `Differential Revision:
<url>` line.

When `--amend -m` is used, that line will be lost, leading to
the creation of a new revision when re-submitted, which isn't
the desired outcome.
:::

If you wrote many changes, you can squash or edit commits with the
command:

```shell
# Git
git rebase -i
```

The submission step is the same as for the initial patch.

{doc}`More information on how to work with stack of patches <stack_quickref>`

## Retrieve new changes from the repository

To pull changes from the repository, run:

```shell
# Git
git pull --rebase
```

(push-a-change)=

## To push a change in the code base

Once the change has been accepted and you've fixed any remaining issues
the reviewer identified, the reviewer should land the patch.

If the patch has not landed on "autoland" (the integration branch) after a few days,
feel free to contact the reviewer and/or
@Aryx or @Sylvestre on the [#introduction](https://chat.mozilla.org/#/room/#introduction:mozilla.org)
channel.

The landing procedure will automatically close the review and the bug.

{doc}`More information <how_to_submit_a_patch>`

## Contributing to GeckoView

Note that the GeckoView setup and contribution processes are different from those of Firefox;
GeckoView setup and contribution docs live in [geckoview.dev](https://geckoview.dev).

## More documentation about contribution

{doc}`/setup/contributing_code`

{doc}`contributing_to_mozilla`

<https://mozilla-version-control-tools.readthedocs.io/en/latest/devguide/contributing.html>

<https://moz-conduit.readthedocs.io/en/latest/phabricator-user.html>
