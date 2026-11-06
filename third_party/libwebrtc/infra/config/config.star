#!/usr/bin/env lucicfg

# Copyright (c) 2019 The WebRTC project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# https://chromium.googlesource.com/infra/luci/luci-go/+/main/lucicfg/doc/

"""LUCI project configuration for WebRTC CQ and CI."""

lucicfg.check_version("1.30.9")

WEBRTC_GIT = "https://webrtc.googlesource.com/src"
WEBRTC_GERRIT = "https://webrtc-review.googlesource.com/src"
WEBRTC_TROOPER_EMAIL = "webrtc-troopers-robots@google.com"
WEBRTC_XCODE14 = "14c18"
DEFAULT_CPU = "x86-64"

# Helpers:

def make_reclient_properties(instance, jobs = None):
    """Makes a default reclient property with the specified argument.

    Args:
      instance: RBE insatnce name.
      jobs: Number of jobs to be used by the builder.
    Returns:
      A dictonary with the reclient properties.
    """
    reclient_props = {
        "instance": instance,
        "metrics_project": "chromium-reclient-metrics",
    }
    if jobs:
        reclient_props["jobs"] = jobs
    return {"$build/reclient": reclient_props}

def os_from_name(name):
    """Returns the 'os' dimension based on a builder name.

    Args:
        name: name of the builder.
    Returns:
        The os dimension to use for the provided builder.
    """
    if "ios" in name.lower() or "mac" in name.lower():
        return "Mac"
    if "win" in name.lower():
        return "Windows"
    return "Linux"

# Add names of builders to remove from LKGR finder to this list. This is
# useful when a failure can be safely ignored while fixing it without
# blocking the LKGR finder on it.
skipped_lkgr_bots = []

# Use LUCI Scheduler BBv2 names and add Scheduler realms configs.
lucicfg.enable_experiment("crbug.com/1182002")

luci.builder.defaults.test_presentation.set(
    resultdb.test_presentation(grouping_keys = ["status", "v.test_suite"]),
)

lucicfg.config(
    config_dir = ".",
    tracked_files = [
        "luci-analysis.cfg",
        "commit-queue.cfg",
        "cr-buildbucket.cfg",
        "luci-logdog.cfg",
        "luci-milo.cfg",
        "luci-notify.cfg",
        "luci-notify/**/*",
        "luci-scheduler.cfg",
        "project.cfg",
        "realms.cfg",
    ],
    lint_checks = ["default"],
)

luci.project(
    name = "webrtc",
    buildbucket = "cr-buildbucket.appspot.com",
    logdog = "luci-logdog.appspot.com",
    milo = "luci-milo.appspot.com",
    notify = "luci-notify.appspot.com",
    scheduler = "luci-scheduler.appspot.com",
    swarming = "chromium-swarm.appspot.com",
    acls = [
        acl.entry(
            [acl.BUILDBUCKET_READER, acl.LOGDOG_READER, acl.PROJECT_CONFIGS_READER, acl.SCHEDULER_READER],
            groups = ["all"],
        ),
        acl.entry(acl.LOGDOG_WRITER, groups = ["luci-logdog-chromium-writers"]),
        acl.entry(acl.SCHEDULER_OWNER, groups = ["project-webrtc-admins"]),
    ],
    bindings = [
        luci.binding(
            roles = "role/configs.validator",
            users = [
                "webrtc-try-builder@chops-service-accounts.iam.gserviceaccount.com",
            ],
        ),
        luci.binding(
            roles = "role/swarming.poolOwner",
            groups = "project-webrtc-admins",
        ),
        luci.binding(
            roles = "role/swarming.poolViewer",
            groups = "all",
        ),
        # Allow any WebRTC build to trigger a test ran under chromium-tester@
        # task service account.
        luci.binding(
            roles = "role/swarming.taskServiceAccount",
            users = [
                "chromium-tester@chops-service-accounts.iam.gserviceaccount.com",
            ],
        ),
        # Roles for LUCI Analysis.
        luci.binding(
            roles = "role/analysis.reader",
            groups = "all",
        ),
        luci.binding(
            roles = "role/analysis.queryUser",
            groups = "authenticated-users",
        ),
        luci.binding(
            roles = "role/analysis.editor",
            groups = "googlers",
        ),
    ],
)

luci.logdog(
    gs_bucket = "chromium-luci-logdog",
)

luci.milo(
    logo = "https://storage.googleapis.com/chrome-infra/webrtc-logo-vert-retro-255x305.png",
)

# Configure Weetbix (config is copied verbatim)
################################################################################

lucicfg.emit(
    dest = "luci-analysis.cfg",
    data = io.read_file("luci-analysis.cfg"),
)

################################################################################

luci.notify(tree_closing_enabled = True)

luci.cq(
    status_host = "chromium-cq-status.appspot.com",
    submit_max_burst = 1,
    submit_burst_delay = 1 * time.minute,
)

luci.gitiles_poller(
    name = "webrtc-gitiles-trigger-main",
    bucket = "ci",
    repo = WEBRTC_GIT,
    refs = ["refs/heads/main"],
)

# Swarming permissions:

luci.realm(name = "pools/cron", bindings = [
    # Unlike WebRTC's own builders, other projects need an explicit grant to use this pool.
    luci.binding(
        roles = "role/swarming.poolUser",
        projects = "libyuv",
    ),
])

luci.realm(name = "pools/ci")
luci.realm(name = "pools/ci-tests", bindings = [
    # Allow task service accounts of .ci pool/bucket to trigger tasks here.
    luci.binding(
        roles = "role/swarming.poolUser",
        groups = "project-webrtc-ci-task-accounts",
    ),
    # Allow tasks here to use .ci task service accounts.
    luci.binding(
        roles = "role/swarming.taskServiceAccount",
        groups = "project-webrtc-ci-task-accounts",
    ),
])
luci.realm(name = "ci", bindings = [
    # Allow CI builders to create invocations in their own builds.
    luci.binding(
        roles = "role/resultdb.invocationCreator",
        groups = "project-webrtc-ci-task-accounts",
    ),
])

luci.realm(name = "pools/try", bindings = [
    # Allow to use LED & Swarming "Debug" feature to a larger group but only on try bots / builders.
    luci.binding(
        roles = "role/swarming.poolUser",
        groups = "project-webrtc-led-users",
    ),
])
luci.realm(name = "pools/try-tests", bindings = [
    # Allow task service accounts of .try pool/bucket to trigger tasks here.
    luci.binding(
        roles = "role/swarming.poolUser",
        groups = "project-webrtc-try-task-accounts",
    ),
    # Allow tasks here to use .try task service accounts.
    luci.binding(
        roles = "role/swarming.taskServiceAccount",
        groups = "project-webrtc-try-task-accounts",
    ),
])
luci.realm(name = "try", bindings = [
    luci.binding(
        roles = "role/swarming.taskTriggerer",
        groups = "project-webrtc-led-users",
    ),
    # Allow try builders to create invocations in their own builds.
    luci.binding(
        roles = "role/resultdb.invocationCreator",
        groups = "project-webrtc-try-task-accounts",
    ),
])

luci.realm(name = "pools/perf", bindings = [
    # Allow to use LED & Swarming "Debug" feature to a larger group but only on perf bots / builders.
    luci.binding(
        roles = "role/swarming.poolUser",
        groups = "project-webrtc-led-users",
    ),
])
luci.realm(name = "perf", bindings = [
    luci.binding(
        roles = "role/swarming.taskTriggerer",
        groups = "project-webrtc-led-users",
    ),
])

luci.realm(name = "@root", bindings = [
    # Allow admins to use LED & Swarming "Debug" feature on all WebRTC bots.
    luci.binding(
        roles = "role/swarming.poolUser",
        groups = "project-webrtc-admins",
    ),
    luci.binding(
        roles = "role/swarming.taskTriggerer",
        groups = "project-webrtc-admins",
    ),
])

# Bucket definitions:

luci.bucket(
    name = "try",
    acls = [
        acl.entry(acl.BUILDBUCKET_TRIGGERER, groups = [
            "service-account-cq",
            "project-webrtc-tryjob-access",
        ]),
    ],
)

luci.bucket(
    name = "ci",
    acls = [
        acl.entry(acl.BUILDBUCKET_TRIGGERER, groups = [
            "project-webrtc-ci-schedulers",
        ]),
    ],
)

luci.bucket(
    name = "perf",
    acls = [
        acl.entry(acl.BUILDBUCKET_TRIGGERER, users = [
            "webrtc-ci-builder@chops-service-accounts.iam.gserviceaccount.com",
        ]),
        acl.entry(acl.BUILDBUCKET_TRIGGERER, groups = [
            # Allow Pinpoint to trigger builds for bisection
            "service-account-chromeperf",
        ]),
    ],
)

luci.bucket(
    name = "cron",
)

# Commit queue definitions:

luci.cq_group(
    name = "cq",
    tree_status_host = "webrtc-status.appspot.com",
    watch = [cq.refset(repo = WEBRTC_GERRIT, refs = ["refs/heads/main"])],
    acls = [
        acl.entry(acl.CQ_COMMITTER, groups = ["project-webrtc-committers"]),
        acl.entry(acl.CQ_DRY_RUNNER, groups = ["project-webrtc-tryjob-access"]),
    ],
    allow_owner_if_submittable = cq.ACTION_DRY_RUN,
    retry_config = cq.RETRY_ALL_FAILURES,
    cancel_stale_tryjobs = True,
)

luci.cq_group(
    name = "cq_branch",
    watch = [cq.refset(repo = WEBRTC_GERRIT, refs = ["refs/branch-heads/.+"])],
    acls = [
        acl.entry(acl.CQ_COMMITTER, groups = ["project-webrtc-committers"]),
        acl.entry(acl.CQ_DRY_RUNNER, groups = ["project-webrtc-tryjob-access"]),
    ],
    retry_config = cq.RETRY_ALL_FAILURES,
    cancel_stale_tryjobs = True,
)

luci.cq_group(
    name = "cq_infra",
    watch = [cq.refset(repo = WEBRTC_GERRIT, refs = ["refs/heads/infra/config"])],
    acls = [
        acl.entry(acl.CQ_COMMITTER, groups = ["project-webrtc-admins"]),
        acl.entry(acl.CQ_DRY_RUNNER, groups = ["project-webrtc-tryjob-access"]),
    ],
    retry_config = cq.RETRY_ALL_FAILURES,
    cancel_stale_tryjobs = True,
)

luci.cq_tryjob_verifier(
    builder = "presubmit",
    cq_group = "cq_infra",
)

# Internal-only tryjob always included into CQ:
luci.cq_tryjob_verifier(
    builder = "webrtc-internal:g3.webrtc-internal.try/internal_compile_lite",
    owner_whitelist = ["project-webrtc-internal-tryjob-access"],
    cq_group = "cq",
)

# Includable via `Cq-Include-Trybots: webrtc-internal/g3.webrtc-internal.try:internal_compile`:
luci.cq_tryjob_verifier(
    builder = "webrtc-internal:g3.webrtc-internal.try/internal_compile",
    owner_whitelist = ["project-webrtc-internal-tryjob-access"],
    cq_group = "cq",
    includable_only = True,
)

# Includable via `Cq-Include-Trybots: webrtc-internal/g3.webrtc-internal.try:internal_tests`:
luci.cq_tryjob_verifier(
    builder = "webrtc-internal:g3.webrtc-internal.try/internal_tests",
    owner_whitelist = ["project-webrtc-internal-tryjob-access"],
    cq_group = "cq",
    includable_only = True,
)

# Notifier definitions:

luci.notifier(
    name = "post_submit_failure_notifier",
    on_new_status = ["FAILURE"],
    notify_emails = [WEBRTC_TROOPER_EMAIL],
    notify_blamelist = True,
    template = luci.notifier_template(
        name = "build_failure",
        body = io.read_file("luci-notify/email-templates/build_failure.template"),
    ),
)

luci.notifier(
    name = "cron_notifier",
    on_new_status = ["FAILURE", "INFRA_FAILURE"],
    notify_emails = [WEBRTC_TROOPER_EMAIL],
    template = luci.notifier_template(
        name = "cron",
        body = io.read_file("luci-notify/email-templates/cron.template"),
    ),
)

luci.notifier(
    name = "infra_failure_notifier",
    on_new_status = ["INFRA_FAILURE"],
    notify_emails = [WEBRTC_TROOPER_EMAIL],
    template = luci.notifier_template(
        name = "infra_failure",
        body = io.read_file("luci-notify/email-templates/infra_failure.template"),
    ),
)

# Tree closer definitions:

luci.tree_closer(
    name = "webrtc_tree_closer",
    tree_status_host = "webrtc-status.appspot.com",
    # TODO: These step filters are copied verbatim from Gatekeeper, for testing
    # that LUCI-Notify would take the exact same actions. Once we've switched
    # over, this should be updated - several of these steps don't exist in
    # WebRTC recipes.
    failed_step_regexp = [
        "bot_update",
        "compile",
        "gclient runhooks",
        "runhooks",
        "update",
        "extract build",
        "cleanup_temp",
        "taskkill",
        "compile",
        "gn",
    ],
    failed_step_regexp_exclude = ".*\\(experimental\\).*",
)

# Recipe definitions:

def recipe(recipe, pkg = "infra/recipe_bundles/chromium.googlesource.com/chromium/tools/build"):
    return luci.recipe(
        name = recipe.split("/")[-1],
        cipd_package = pkg,
        cipd_version = "refs/heads/main",
        recipe = recipe,
        use_python3 = True,
    )

recipe("chromium_trybot")
recipe("run_presubmit")
recipe("webrtc/auto_roll_webrtc_deps")
recipe("webrtc/ios_api_framework")
recipe("webrtc/libfuzzer")
recipe("webrtc/standalone")
recipe("webrtc/update_webrtc_binary_version")
recipe("lkgr_finder", pkg = "infra/recipe_bundles/chromium.googlesource.com/chromium/tools/build")

# Console definitions:

luci.console_view(name = "ci", title = "Main", repo = WEBRTC_GIT, header = "console-header.textpb", refs = ["refs/heads/main"])
luci.console_view(name = "perf", title = "Perf", repo = WEBRTC_GIT, header = "console-header.textpb", refs = ["refs/heads/main"])
luci.list_view(name = "cron", title = "Cron")
luci.list_view(name = "try", title = "Tryserver")

def add_milo(builder, views):
    """Add Milo console entries for the builder.

    Args:
      builder: builder name (str).
      views: dict where keys are names of consoles and values are either a
        category for the console (str, pipe-separated) or True, which means
        adding to a list view rather than a console.
    """
    for view_name, category in views.items():
        if category == None:
            continue
        elif type(category) == "string":
            category, _, short_name = category.rpartition("|")
            luci.console_view_entry(
                console_view = view_name,
                builder = builder,
                category = category or None,
                short_name = short_name or None,
            )
        elif category == True:
            luci.list_view_entry(
                list_view = view_name,
                builder = builder,
            )
        else:
            fail("Unexpected value for category: %r" % category)

lkgr_builders = []

# Builder-defining functions:

def webrtc_builder(
        name,
        bucket,
        dimensions,
        properties = None,
        recipe = "standalone",
        priority = 30,
        execution_timeout = 2 * time.hour,
        **kwargs):
    """WebRTC specific wrapper around luci.builder.

    Args:
      name: builder name (str).
      bucket: The name of the bucket the builder belongs to.
      dimensions: dict of Swarming dimensions (strings) to search machines by.
      properties: dict of properties to pass to the recipe (on top of the default ones).
      recipe: string with the name of the recipe to run.
      priority: int [1-255] or None, indicating swarming task priority, lower is
        more important. If None, defer the decision to Buildbucket service.
      execution_timeout: int or None, how long to wait for a running build to finish before
        forcefully aborting it and marking the build as timed out. If None,
        defer the decision to Buildbucket service.
      **kwargs: Pass on to webrtc_builder / luci.builder.
    Returns:
      A luci.builder.
    """
    properties = properties or {}
    resultdb_bq_table = "webrtc-ci.resultdb." + bucket + "_test_results"
    return luci.builder(
        name = name,
        bucket = bucket,
        executable = recipe,
        dimensions = dimensions,
        properties = properties,
        execution_timeout = execution_timeout,
        priority = priority,
        build_numbers = True,
        swarming_tags = ["vpython:native-python-wrapper"],
        resultdb_settings = resultdb.settings(
            enable = True,
            bq_exports = [
                resultdb.export_test_results(bq_table = resultdb_bq_table),
            ],
        ),
        **kwargs
    )

def ci_builder(
        name,
        ci_cat,
        properties = None,
        perf_cat = None,
        prioritized = False,
        enabled = True,
        **kwargs):
    """Add a post-submit builder.

    Args:
      name: builder name (str).
      ci_cat: the category + name for the /ci/ console, or None to omit from the console.
      properties: dict of properties to pass to the recipe (on top of the default ones).
      perf_cat: the category + name for the /perf/ console, or None to omit from the console.
      prioritized: True to make this builder have a higher priority and never batch builds.
      enabled: False to exclude this builder from consoles and failure notifications.
      **kwargs: Pass on to webrtc_builder / luci.builder.
    Returns:
      A luci.builder.

    Notifications are also disabled if a builder is not on either of /ci/ or /perf/ consoles.
    """
    if prioritized:
        kwargs["triggering_policy"] = scheduler.greedy_batching(
            max_batch_size = 1,
            max_concurrent_invocations = 3,
        )
        kwargs["priority"] = 29

    if enabled:
        add_milo(name, {"ci": ci_cat, "perf": perf_cat})
        if ci_cat and not perf_cat:
            lkgr_builders.append(name)
    dimensions = ({"os": os_from_name(name), "pool": "luci.webrtc.ci", "cpu": kwargs.pop("cpu", DEFAULT_CPU)})
    dimensions["builderless"] = "1"
    properties = properties or {}
    properties["builder_group"] = "client.webrtc"
    properties.update(make_reclient_properties("rbe-webrtc-trusted"))

    notifies = ["post_submit_failure_notifier", "infra_failure_notifier"]
    notifies += ["webrtc_tree_closer"] if name not in skipped_lkgr_bots else []
    return webrtc_builder(
        name = name,
        dimensions = dimensions,
        properties = properties,
        bucket = "perf" if perf_cat else "ci",
        service_account = "webrtc-ci-builder@chops-service-accounts.iam.gserviceaccount.com",
        triggered_by = ["webrtc-gitiles-trigger-main"] if enabled else None,
        repo = WEBRTC_GIT,
        notifies = notifies if enabled else None,
        **kwargs
    )

def try_builder(
        name,
        properties = None,
        try_cat = True,
        cq = {},
        branch_cq = True,
        builder = None,
        **kwargs):
    """Add a pre-submit builder.

    Args:
      name: builder name (str).
      properties: dict of properties to pass to the recipe (on top of the default ones).
      try_cat: boolean, whether to include this builder in the /try/ console. See also: `add_milo`.
      cq: None to exclude this from all commit queues, or a dict of kwargs for cq_tryjob_verifier.
      branch_cq: False to exclude this builder just from the release-branch CQ.
      builder: builder to set in the dimensions, if None, builderless:1 is used.
      **kwargs: Pass on to webrtc_builder / luci.builder.
    Returns:
      A luci.builder.
    """
    add_milo(name, {"try": try_cat})
    dimensions = ({"os": os_from_name(name), "pool": "luci.webrtc.try", "cpu": DEFAULT_CPU})
    if builder != None:
        dimensions["builder"] = builder
    else:
        dimensions["builderless"] = "1"
    properties = properties or {}
    properties["builder_group"] = "tryserver.webrtc"
    properties.update(make_reclient_properties("rbe-webrtc-untrusted"))
    if cq != None:
        luci.cq_tryjob_verifier(name, cq_group = "cq", **cq)
        if branch_cq:
            luci.cq_tryjob_verifier(name, cq_group = "cq_branch", **cq)

    return webrtc_builder(
        name = name,
        dimensions = dimensions,
        properties = properties,
        bucket = "try",
        service_account = "webrtc-try-builder@chops-service-accounts.iam.gserviceaccount.com",
        notifies = ["infra_failure_notifier"],
        **kwargs
    )

def perf_builder(name, perf_cat, **kwargs):
    """Add a perf builder.

    Args:
      name: builder name (str).
      perf_cat: the category + name for the /perf/ console, or None to omit from the console.
      **kwargs: Pass on to webrtc_builder / luci.builder.
    Returns:
      A luci.builder.

    Notifications are also disabled.
    """
    add_milo(name, {"perf": perf_cat})
    properties = make_reclient_properties("rbe-webrtc-trusted")
    properties["builder_group"] = "client.webrtc.perf"
    dimensions = {"pool": "luci.webrtc.perf", "os": "Linux", "cores": "2"}
    if "Android" in name or "Fuchsia" in name:
        # Android perf testers require more performant bots to finish under 3 hours.
        # Fuchsia perf testers encountered "no space left on device" error on multiple runs.
        dimensions["cores"] = "8"
    return webrtc_builder(
        name = name,
        dimensions = dimensions,
        properties = properties,
        bucket = "perf",
        service_account = "webrtc-ci-builder@chops-service-accounts.iam.gserviceaccount.com",
        # log_base of 1.7 means:
        # when there are P pending builds, LUCI will batch the first B builds.
        # P:  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 ...
        # B:  1  1  2  2  3  3  3  3  4  4  4  4  4  4  5 ...
        triggering_policy = scheduler.logarithmic_batching(log_base = 1.7),
        repo = WEBRTC_GIT,
        execution_timeout = 3 * time.hour,
        notifies = ["post_submit_failure_notifier", "infra_failure_notifier"],
        **kwargs
    )

def cron_builder(name, service_account = None, **kwargs):
    if service_account == None:
        service_account = "chromium-webrtc-autoroll@webrtc-ci.iam.gserviceaccount.com"
    add_milo(name, {"cron": True})
    return webrtc_builder(
        name = name,
        dimensions = {"pool": "luci.webrtc.cron", "os": "Linux", "cpu": DEFAULT_CPU},
        bucket = "cron",
        service_account = service_account,
        notifies = ["cron_notifier"],
        **kwargs
    )

def chromium_try_builder(name, **kwargs):
    return try_builder(
        name,
        builder = "chromium-compile",
        recipe = "chromium_trybot",
        branch_cq = False,
        execution_timeout = 3 * time.hour,
        **kwargs
    )

def normal_builder_factory(**common_kwargs):
    def builder(*args, **kwargs):
        kwargs.update(common_kwargs)
        return ci_builder(*args, **kwargs)

    def try_job(name, **kwargs):
        kwargs.update(common_kwargs)
        return try_builder(name, **kwargs)

    return builder, try_job

# Mixins:

ios_builder, ios_try_job = normal_builder_factory(
    properties = {"xcode_build_version": WEBRTC_XCODE14},
    caches = [swarming.cache(
        name = "xcode_ios_" + WEBRTC_XCODE14,
        path = "xcode_ios_" + WEBRTC_XCODE14 + ".app",
    )],
)

# Actual builder configuration:

ci_builder("Android32 (dbg)", "Android|arm|dbg")
try_builder("android_compile_arm_dbg", cq = {"experiment_percentage": 100})
try_builder("android_arm_dbg")
ci_builder("Android32", "Android|arm|rel")
try_builder("android_arm_rel")
ci_builder("Android32 Builder arm", "Android|arm|size", perf_cat = "Android|arm|Builder|", prioritized = True)
try_builder("android_compile_arm_rel")
perf_builder("Perf Android32 (R Pixel5)", "Android|arm|Tester|R Pixel5", triggered_by = ["Android32 Builder arm"])
try_builder("android_compile_arm64_dbg", cq = None)
try_builder("android_arm64_dbg", cq = None)
ci_builder("Android64", "Android|arm64|rel")
try_builder("android_arm64_rel")
ci_builder("Android64 Builder arm64", "Android|arm64|size", perf_cat = "Android|arm64|Builder|", prioritized = True)
perf_builder("Perf Android64 (R Pixel5)", "Android|arm64|Tester|R Pixel5", triggered_by = ["Android64 Builder arm64"])
try_builder("android_compile_arm64_rel")
ci_builder("Android64 Builder x64 (dbg)", "Android|x64|dbg")
try_builder("android_compile_x64_dbg")
try_builder("android_compile_x64_rel", cq = None)
ci_builder("Android32 Builder x86 (dbg)", "Android|x86|dbg")
try_builder("android_compile_x86_dbg")
ci_builder("Android32 Builder x86", "Android|x86|rel")
try_builder("android_compile_x86_rel")
ci_builder("Android32 (more configs)", "Android|arm|more")
try_builder("android_arm_more_configs")
chromium_try_builder("android_chromium_compile")

ios_builder("iOS64 Debug", "iOS|arm64|dbg")
ios_try_job("ios_compile_arm64_dbg")
ios_builder("iOS64 Release", "iOS|arm64|rel")
ios_try_job("ios_compile_arm64_rel")
ios_builder("iOS Debug (simulator)", "iOS|x64|sim")
ios_try_job("ios_dbg_simulator")
ios_builder("iOS API Framework Builder", "iOS|fat|size", recipe = "ios_api_framework", prioritized = True)
ios_try_job("ios_api_framework", recipe = "ios_api_framework")

ci_builder("Linux32 Debug", "Linux|x86|dbg")
try_builder("linux_x86_dbg")
ci_builder("Linux32 Release", "Linux|x86|rel")
try_builder("linux_x86_rel")
ci_builder("Linux64 Debug", "Linux|x64|dbg")
try_builder("linux_dbg", cq = None)
try_builder("linux_compile_dbg")
ci_builder("Linux64 Release", "Linux|x64|rel")
try_builder("linux_rel")
ci_builder("Linux64 Builder", "Linux|x64|size", perf_cat = "Linux|x64|Builder|", prioritized = True)
try_builder("linux_compile_rel")
perf_builder("Perf Linux Bionic", "Linux|x64|Tester|Bionic", triggered_by = ["Linux64 Builder"])
ci_builder("Linux32 Debug (ARM)", "Linux|arm|dbg")
try_builder("linux_compile_arm_dbg")
ci_builder("Linux32 Release (ARM)", "Linux|arm|rel")
try_builder("linux_compile_arm_rel")
ci_builder("Linux64 Debug (ARM)", "Linux|arm64|dbg")
try_builder("linux_compile_arm64_dbg")
ci_builder("Linux64 Release (ARM)", "Linux|arm64|rel")
try_builder("linux_compile_arm64_rel")
ci_builder("Linux Asan", "Linux|x64|asan")
try_builder("linux_asan")
ci_builder("Linux MSan", "Linux|x64|msan")
try_builder("linux_msan")
ci_builder("Linux Tsan v2", "Linux|x64|tsan")
try_builder("linux_tsan2")
ci_builder("Linux UBSan", "Linux|x64|ubsan")
try_builder("linux_ubsan")
ci_builder("Linux UBSan vptr", "Linux|x64|ubsan")
try_builder("linux_ubsan_vptr")
ci_builder("Linux64 Release (Libfuzzer)", "Linux|x64|fuzz", recipe = "libfuzzer")
try_builder("linux_libfuzzer_rel", recipe = "libfuzzer")
ci_builder("Linux (more configs)", "Linux|x64|more")
try_builder("linux_more_configs")
try_builder("linux_coverage")
chromium_try_builder("webrtc_linux_chromium")
chromium_try_builder("linux_chromium_compile", cq = None)
chromium_try_builder("linux_chromium_compile_dbg")

ci_builder("Fuchsia Builder", ci_cat = None, perf_cat = "Fuchsia|x64|Builder|", prioritized = True)
ci_builder("Fuchsia Release", "Fuchsia|x64|rel")
try_builder("fuchsia_rel")
perf_builder("Perf Fuchsia", "Fuchsia|x64|Tester|", triggered_by = ["Fuchsia Builder"])

ci_builder("Mac64 Debug", "Mac|x64|dbg")
try_builder("mac_dbg", cq = None)
try_builder("mac_compile_dbg")
ci_builder("Mac64 Release", "Mac|x64|rel")
try_builder("mac_rel")
try_builder("mac_compile_rel", cq = None)
ci_builder("Mac64 Builder", ci_cat = None, perf_cat = "Mac|x64|Builder|")
ci_builder("MacArm64 Builder", ci_cat = None, perf_cat = "Mac|arm64|Builder|")
perf_builder("Perf Mac 11", "Mac|x64|Tester|11", triggered_by = ["Mac64 Builder"])
perf_builder("Perf Mac M1 Arm64 12", "Mac|arm64|Tester|12", triggered_by = ["MacArm64 Builder"])
ci_builder("Mac Asan", "Mac|x64|asan")
try_builder("mac_asan")
ci_builder("MacARM64 M1 Release", "Mac|arm64M1|rel", cpu = "arm64-64-Apple_M1")
try_builder("mac_rel_m1")
try_builder("mac_dbg_m1")
chromium_try_builder("mac_chromium_compile")

ci_builder("Win32 Debug (Clang)", "Win Clang|x86|dbg")
try_builder("win_x86_clang_dbg", cq = None)
try_builder("win_compile_x86_clang_dbg")
ci_builder("Win32 Release (Clang)", "Win Clang|x86|rel")
try_builder("win_x86_clang_rel")
try_builder("win_compile_x86_clang_rel", cq = None)
ci_builder("Win64 Builder (Clang)", ci_cat = None, perf_cat = "Win|x64|Builder|")
perf_builder("Perf Win 10", "Win|x64|Tester|10", triggered_by = ["Win64 Builder (Clang)"])
ci_builder("Win64 Debug (Clang)", "Win Clang|x64|dbg")
try_builder("win_x64_clang_dbg")
try_builder("win_compile_x64_clang_dbg")
ci_builder("Win64 Release (Clang)", "Win Clang|x64|rel")
try_builder("win_x64_clang_rel")
try_builder("win_compile_x64_clang_rel")
ci_builder("Win64 ASan", "Win Clang|x64|asan")
try_builder("win_asan")
ci_builder("Win (more configs)", "Win Clang|x86|more")
try_builder("win_x86_more_configs")
try_builder("win11_release", cq = None)
try_builder("win11_debug", cq = None)
chromium_try_builder("win_chromium_compile")
chromium_try_builder("win_chromium_compile_dbg")

try_builder(
    "presubmit",
    recipe = "run_presubmit",
    properties = {"repo_name": "webrtc", "runhooks": True},
    priority = 28,
    cq = {"disable_reuse": True},
)

cron_builder(
    "Auto-roll - WebRTC DEPS",
    recipe = "auto_roll_webrtc_deps",
    schedule = "0 */2 * * *",  # Every 2 hours.
)

cron_builder(
    "WebRTC version update",
    recipe = "update_webrtc_binary_version",
    schedule = "0 4 * * *",  # Every day at 4am.
    service_account = "webrtc-version-updater@webrtc-ci.iam.gserviceaccount.com",
)

lkgr_config = {
    "project": "webrtc",
    "source_url": WEBRTC_GIT,
    "status_url": "https://webrtc-status.appspot.com",
    "allowed_lag": 9,  # hours (up to 10x during low commit volume periods)
    "allowed_gap": 150,  # commits behind
    "buckets": {
        "webrtc/ci": {
            # bucket alias: luci.webrtc.ci
            "builders": [
                b
                for b in sorted(lkgr_builders)
                if b not in skipped_lkgr_bots
            ],
        },
        "chromium/webrtc.fyi": {
            # bucket alias: luci.chromium.webrtc.fyi
            "builders": [
                "WebRTC Chromium FYI Android Builder (dbg)",
                "WebRTC Chromium FYI Android Builder ARM64 (dbg)",
                "WebRTC Chromium FYI Android Builder",
                "WebRTC Chromium FYI Android Tests (dbg)",
                "WebRTC Chromium FYI Android Tests ARM64 (dbg)",
                "WebRTC Chromium FYI Linux Builder (dbg)",
                "WebRTC Chromium FYI Linux Builder",
                "WebRTC Chromium FYI Linux Tester",
                "WebRTC Chromium FYI Mac Builder (dbg)",
                "WebRTC Chromium FYI Mac Builder",
                "WebRTC Chromium FYI Mac Tester",
                "WebRTC Chromium FYI Win Builder (dbg)",
                "WebRTC Chromium FYI Win Builder",
                "WebRTC Chromium FYI Win10 Tester",
                "WebRTC Chromium FYI ios-device",
                "WebRTC Chromium FYI ios-simulator",
            ],
        },
    },
}

cron_builder(
    "WebRTC lkgr finder",
    recipe = "lkgr_finder",
    properties = {
        "project": "webrtc",
        "repo": WEBRTC_GIT,
        "ref": "refs/heads/lkgr",
        "src_ref": "refs/heads/main",
        "lkgr_status_gs_path": "chromium-webrtc/lkgr-status",
        "config": lkgr_config,
    },
    schedule = "*/10 * * * *",  # Every 10 minutes.
)
