.. _snap:

======================
Firefox Snap Packaging
======================

This page explains interactions between Firefox and Snap packaging format.

Where is the upstream
=====================

The code reference itself is mozilla-central, but the packaging is being worked
within the `Canonical's firefox-snap repository <https://github.com/canonical/firefox-snap/>`_.

This packaging includes a few more bits and dependencies, including compiler.
It will also re-download the mercurial repository: this is on purpose.

Where to report bugs
====================

All bugs should be reported to Bugzilla's ``Third Party Packaging`` component,
and marked as blocking ``snap`` meta-bug.

Full build process
==================

There may be a few differences between a simple repack and a real full build of
the Snap, you can find mention of the repackage usage below.

The following steps should be enough, assuming you have properly setup:

 - ``snapcraft`` (see `quickstart doc <https://snapcraft.io/docs/snapcraft-quickstart>`_)
 - ``LXD`` (see `providers doc <https://snapcraft.io/docs/build-providers>`_)

While the documentation still refers to `Multipass`, the Firefox Snap and its
dependency had some requirements that made it better suited to use `LXD`.

When performing the checkout, please keep in mind the branch mapping:

 - `edge` is our `nightly`
 - `beta` is our `beta`
 - `stable` is our `release`
 - `esr` is our `esr`

.. code-block:: shell

    $ git clone https://github.com/canonical/firefox-snap --branch BRANCH
    $ snap run snapcraft

You should end up after some time with two files: ``firefox-XXX.snap`` and
``firefox-XXX.debug``. The first one is the package you will want to ``snap
install`` while the second one holds your debugging symbols.

You can then install the package:

.. code-block:: shell

    $ sudo snap install --name firefox --dangerous ./path/to/firefox-XXX.snap

If you want to have parallel installs, then you can change the `--name firefox`
to something else. This will be the name you use for ``snap run
installed-name``, e.g., ``--name firefox_nightly`` will require you to run
``snap run firefox_nightly``.

`Snap` has a notion of plugs and slots, and some gets automatically connected
in various ways, including depending on the ``Snap Sore`` itself, and if you
manually install as ``firefox`` it should reuse them (but you might do bad
things with your profile). If you install using another name, then the ``Snap
Store`` automatic connection will not happen and this can result in a broken
state. Inspecting ``snap connections firefox`` using a store-installed snap
should get your an accurate list that you can replicate.

Cross-compilation
=================

There is now support for cross-compilation for both ``armhf`` and ``arm64``.
To produce cross-compiled version locally:

 - follow the steps above for building, except you need to pass
   ``--build-for=ARCH`` to ``snapcraft``
 - this needs ``snapcraft`` of at least v8.x
 - make sure you uncomment the ``##CROSS-COMPILATION##`` lines due to Launchpad
   limitations

Builds on Treeherder as well as Try pushes are also available using
cross-compilation. Tests on Treeherder will also be supported when ARM-based
workers will be available, confere `Bug 1855463
<https://bugzilla.mozilla.org/show_bug.cgi?id=1855463>`_.

What CI coverage
================

Currently, there are upstream-like builds on treeherder. They are scheduled as
a cron task daily and includes:

 - building opt/debug versions of the snap
 - building them on all branches
 - running a few selenium-based tests

The build definitions `are based on docker <https://searchfox.org/mozilla-central/rev/3c72de9280ec57dc55c24886c6334d9e340500e8/taskcluster/docker/snap-coreXX-build/Dockerfile>`_.

It should be noted that for the moment, all tasks needs to run under docker.
However, this setup is not working for `Snap` since it interacts with `SystemD`
which does not work under `Docker`. This is why the installation is handled by
`the install-snap script
<https://searchfox.org/mozilla-central/rev/3c72de9280ec57dc55c24886c6334d9e340500e8/taskcluster/docker/snap-coreXX-build/install-snap.sh>`_
rather than plain `sudo snap install`, and also why we need to run `snap` in
`destructive mode` (which is fine since we are within a docker container). This
does not apply to the tests case which relies on newly-available wayland
virtual machines.

Outside the build oddities because of the setup, it should be noted that those
builds are as close as possible to upstream. This means:

 - the mozilla-central hash they run against is not matching the source code it
   builds from, and one should inspect the build log to see the mercurial clone
   step
 - it builds using the clang build within the snap definition

The tests are defined `within the docker subdirectory
<https://searchfox.org/mozilla-central/rev/3c72de9280ec57dc55c24886c6334d9e340500e8/taskcluster/docker/snap-coreXX-build/snap-tests/tests.sh>`_.
They are using Selenium because this is what was used by pre-existing tests ran
on GitHub Actions from upstream.

Their coverage is ensuring that we get a basic working browser out of builds.
It includes some tests that previously were manually ran by QA.

How to hack on try
==================

Build and test tasks can be explored via ``mach try fuzzy --full`` by searching
for ``'snap 'upstream``. There is a bit of hacking for try to make sure we
actually don't re-download the mercurial repo and directly reuse the clone
generated by `run-task`, handled in the `run.sh script
<https://searchfox.org/mozilla-central/rev/3c72de9280ec57dc55c24886c6334d9e340500e8/taskcluster/docker/snap-coreXX-build/run.sh#61-72>`_.

So pushing to try is basically just:

.. code-block:: shell

    $ mach try fuzzy --full -q "'snap 'upstream 'local"

Because of the build process, a full opt build will take around 1h45-2h while a
debug build will be around 60 minutes, the difference coming from the use of
PGO on opt builds.

If you need to reuse a package from the Snap Store or from the latest
mozilla-central or a specific successful build, you can use ``USE_SNAP_FROM_STORE_OR_MC`` en
variable ; setting it to ``store`` will download from the Snap Store (warning:
no debug builds on the Snap Store, so whatever ``debug`` variants we have will
be an ``opt`` build in fact), and setting to a TaskCluster index value will
download from the index. Set it to ``latest`` if you want latest, or explore
the TaskCluster index for others. Any ``try`` will be pulled from latest
``nightly`` while others will be fetched from their respective branches.

How to hack locally
===================

After a successful build, you can also build a Snap by performing a repackaging
using the ``mach repackage snap`` tool. This requires a ``snapcraft`` working
installation relying on ``LXD``, which installation steps are
`documented upstream <https://snapcraft.io/docs/build-providers>`_.
