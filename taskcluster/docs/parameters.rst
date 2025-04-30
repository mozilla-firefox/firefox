==========
Parameters
==========

Task-graph generation takes a collection of parameters as input, in the form of
a JSON or YAML file.

During decision-task processing, some of these parameters are supplied on the
command line or by environment variables.  The decision task helpfully produces
a full parameters file as one of its output artifacts.  The other ``mach
taskgraph`` commands can take this file as input.  This can be very helpful
when working on a change to the task graph.

When experimenting with local runs of the task-graph generation, it is always
best to find a recent decision task's ``parameters.yml`` file, and modify that
file if necessary, rather than starting from scratch.  This ensures you have a
complete set of parameters.

The properties of the parameters object are described here, divided roughly by
topic.

Push Information
----------------

``android_perftest_backstop``
   Whether or not this push is a "backstop" push for android performance tests.
   That is a push where all android performance tests should run to ensure
   regressions aren't accidentally missed.

``backstop``
   Whether or not this push is a "backstop" push. That is a push where all
   builds and tests should run to ensure regressions aren't accidentally
   missed.

``base_repository``
   The repository from which to do an initial clone, utilizing any available
   caching.

``head_repository``
   The repository containing the changeset to be built.  This may differ from
   ``base_repository`` in cases where ``base_repository`` is likely to be cached
   and only a few additional commits are needed from ``head_repository``.

``base_rev``
   The previous revision before ``head_rev`` got merged into. This can be a short revision string.

``head_rev``
   The revision to check out; this can be a short revision string

``head_git_rev``
   Optionally, the git commit corresponding to the head_rev when it's in a Mercurial
   repository.

``base_ref``
   Reference where ``head_rev`` got merged into. It is usually a branch or a tag.

``head_ref``
   For Mercurial repositories, this is the same as ``head_rev``.  For
   git repositories, which do not allow pulling explicit revisions, this gives
   the symbolic ref containing ``head_rev`` that should be pulled from
   ``head_repository``.

``head_tag``
   The tag attached to the revision, if any.

``files_changed``
   The list of all files added or modified by the push.

``owner``
   Email address indicating the person who made the push.  Note that this
   value may be forged and *must not* be relied on for authentication.

``message``
   The try syntax in the commit message, if any.

``pushlog_id``
   The ID from the ``hg.mozilla.org`` pushlog

``pushdate``
   The timestamp of the push to the repository that triggered this decision
   task.  Expressed as an integer seconds since the UNIX epoch.

``hg_branch``
  The mercurial branch where the revision lives in.

``build_date``
   The timestamp of the build date. Defaults to ``pushdate`` and falls back to present time of
   taskgraph invocation. Expressed as an integer seconds since the UNIX epoch.

``moz_build_date``
   A formatted timestamp of ``build_date``. Expressed as a string with the following
   format: %Y%m%d%H%M%S

``repository_type``
   The type of repository, either ``hg`` or ``git``.

``tasks_for``
   The ``tasks_for`` value used to generate the decision task.

Tree Information
----------------

``project``
   Another name for what may otherwise be called tree or branch or
   repository.  This is the unqualified name, such as ``mozilla-central`` or
   ``cedar``.

``level``
   The `SCM level
   <https://www.mozilla.org/en-US/about/governance/policies/commit/access-policy/>`_
   associated with this tree.  This dictates the names of resources used in the
   generated tasks, and those tasks will fail if it is incorrect.

Try Configuration
-----------------

``try_mode``
   The mode in which a try push is operating.  This can be one of:

    * ``"try_task_config"`` - Used to configure the taskgraph.
    * ``"try_option_syntax"`` - Used when pushing to try with legacy try syntax.
    * ``"try_auto"`` - Used to make try pushes behave more like a push on ``autoland``.
    * ``"try_select"`` - Used by ``mach try`` to build a list of tasks locally.
    * ``None`` - Not a try push or ``mach try release``.

``try_options``
    The arguments given as try syntax (as a dictionary), or ``None`` if
    ``try_mode`` is not ``try_option_syntax``.

``try_task_config``
    The contents of the ``try_task_config.json`` file, or ``{}`` if
    ``try_mode`` is not ``try_task_config``.

Test Configuration
------------------

``test_manifest_loader``
   The test manifest loader to use as defined in ``taskgraph.util.chunking.manifest_loaders``.

Target Set
----------

The "target set" is the set of task labels which must be included in a task
graph.  The task graph generation process will include any tasks required by
those in the target set, recursively.  In a decision task, this set can be
specified programmatically using one of a variety of methods (e.g., parsing try
syntax or reading a project-specific configuration file).

``enable_always_target``
   Can either be a boolean or a list of kinds.

   When ``True``, any task with the ``always_target`` attribute will be included
   in the ``target_task_graph`` regardless of whether they were filtered out by
   the ``target_tasks_method`` or not. Because they are not part of the
   ``target_set``, they will still be eligible for optimization when the
   ``optimize_target_tasks`` parameter is ``False``.

   When specified as a list of kinds, only tasks with a matching kind will be
   eligible for addition to the graph.

``filters``
   List of filter functions (from ``taskcluster/gecko_taskgraph/filter_tasks.py``) to
   apply. This is usually defined internally, as filters are typically
   global.

``target_tasks_method``
   The method to use to determine the target task set.  This is the suffix of
   one of the functions in ``taskcluster/gecko_taskgraph/target_tasks.py``.

``release_history``
   History of recent releases by platform and locale, used when generating
   partial updates for nightly releases.
   Suitable contents can be generated with ``mach release-history``,
   which will print to the console by default.

Optimization
------------

``optimize_strategies``
   A python path of the form ``<module>:<object>`` containing a dictionary of
   optimization strategies to use, overwriting the defaults.

``optimize_target_tasks``
   If true, then target tasks are eligible for optimization.

``do_not_optimize``
   Specify tasks to not optimize out of the graph. This is a list of labels.
   Any tasks in the graph matching one of the labels will not be optimized out
   of the graph.

``existing_tasks``
   Specify tasks to optimize out of the graph. This is a dictionary of label to taskId.
   Any tasks in the graph matching one of the labels will use the previously-run
   taskId rather than submitting a new task.

Release Promotion
-----------------

``build_number``
   Specify the release promotion build number.

``version``
   Specify the version for release tasks.

``app_version``
   Specify the application version for release tasks. For releases, this is often a less specific version number than ``version``.

``next_version``
   Specify the next version for version bump tasks.

``release_type``
   The type of release being promoted. One of "nightly", "beta", "esr115", "esr128", "release-rc", or "release".

``release_eta``
   The time and date when a release is scheduled to live. This value is passed to Balrog.

``release_enable_partner_repack``
   Boolean which controls repacking vanilla Firefox builds for partners.

``release_enable_partner_attribution``
   Boolean which controls adding attribution to vanilla Firefox builds for partners.

``release_enable_emefree``
   Boolean which controls repacking vanilla Firefox builds into EME-free builds.

``release_partners``
   List of partners to repack or attribute if a subset of the whole config. A null value defaults to all.

``release_partner_config``
   Configuration for partner repacks & attribution, as well as EME-free repacks.

``release_partner_build_number``
   The build number for partner repacks. We sometimes have multiple partner build numbers per release build number; this parameter lets us bump them independently. Defaults to 1.

``release_product``
   The product that is being released.

``required_signoffs``
   A list of signoffs that are required for this release promotion flavor. If specified, and if the corresponding `signoff_urls` url isn't specified, tasks that require these signoffs will not be scheduled.

``signoff_urls``
   A dictionary of signoff keys to url values. These are the urls marking the corresponding ``required_signoffs`` as signed off.


Repository Merge Day
--------------------

``merge_config``
   Merge config describes the repository merge behaviour, using an alias to cover which set of file replacements and version increments are required, along with overrides for the source and target repository URIs.

``source_repo``
   The clone/push URI of the source repository, such as https://hg.mozilla.org/mozilla-central

``target_repo``
   The clone/push URI of the target repository, such as https://hg.mozilla.org/releases/mozilla-beta

``source_branch``
   The firefoxtree alias of the source branch, such as 'central', 'beta'

``target_branch``
   The firefoxtree alias of the target branch, such as 'beta', 'release'

``force-dry-run``
   Don't push any results to target repositories.


Code Review
-----------

``phabricator_diff``
   The code review process needs to know the Phabricator Differential diff that
   started the analysis. This parameter must start with `PHID-DIFF-`

Local configuration
-------------------

``target-kinds``
  Generate only the given kinds and their kind-dependencies. This is used for local inspection of the graph
  and is not supported at run-time.
