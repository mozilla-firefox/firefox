#!/usr/bin/env lucicfg

#  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
#
#  Use of this source code is governed by a BSD-style license
#  that can be found in the LICENSE file in the root of the source
#  tree. An additional intellectual property rights grant can be found
#  in the file PATENTS.  All contributing project authors may
#  be found in the AUTHORS file in the root of the source tree.

""" CQ and CI bots """

load("@chromium-luci//xcode.star", "xcode")

WEBRTC_XCODE = "17a5305k"
DEFAULT_CPU = "x86-64"
WEBRTC_GIT = "https://webrtc.googlesource.com/src"

# Add names of builders to remove from LKGR finder to this list. This is
# useful when a failure can be safely ignored while fixing it without
# blocking the LKGR finder on it.
skipped_lkgr_bots = [
]

lkgr_builders = []

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

def make_siso_properties(instance, jobs = None):
    """Makes a default RBE property with the specified argument.

    Args:
      instance: RBE insatnce name.
      jobs: Number of jobs to be used by the builder.
    Returns:
      A dictonary with the siso properties.
    """
    siso_props = {
        "project": instance,
        "configs": ["builder"],
        "enable_cloud_profiler": True,
        "enable_cloud_trace": True,
        "enable_monitoring": True,
    }
    if jobs:
        siso_props["remote_jobs"] = jobs
    props = {
        "$build/siso": siso_props,
    }
    return props

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

def webrtc_builder(
        name,
        bucket,
        dimensions,
        properties = None,
        recipe = "standalone",
        priority = 30,
        execution_timeout = 3 * time.hour,
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
        **kwargs):
    """Add a post-submit builder.

    Args:
      name: builder name (str).
      ci_cat: the category + name for the /ci/ console, or None to omit from the console.
      properties: dict of properties to pass to the recipe (on top of the default ones).
      perf_cat: the category + name for the /perf/ console, or None to omit from the console.
      prioritized: True to make this builder have a higher priority and never batch builds.
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

    add_milo(name, {"ci": ci_cat, "perf": perf_cat})
    dimensions = ({"os": os_from_name(name), "pool": "luci.webrtc.ci", "cpu": kwargs.pop("cpu", DEFAULT_CPU)})
    dimensions["builderless"] = "1"
    properties = properties or {}
    properties["builder_group"] = "client.webrtc"
    properties.update(make_siso_properties("rbe-webrtc-trusted"))

    if ci_cat and not perf_cat:
        properties["gardener_rotations"] = ["webrtc"]
        lkgr_builders.append(name)

    notifies = ["post_submit_failure_notifier", "infra_failure_notifier"]
    notifies += ["webrtc_tree_closer"] if name not in skipped_lkgr_bots else []
    return webrtc_builder(
        name = name,
        dimensions = dimensions,
        properties = properties,
        bucket = "perf" if perf_cat else "ci",
        service_account = "webrtc-ci-builder@chops-service-accounts.iam.gserviceaccount.com",
        triggered_by = ["webrtc-gitiles-trigger-main"],
        repo = WEBRTC_GIT,
        notifies = notifies,
        **kwargs
    )

def try_builder(
        name,
        properties = None,
        try_cat = True,
        cq = {},
        branch_cq = True,
        builder = None,
        jobs = None,
        **kwargs):
    """Add a pre-submit builder.

    Args:
      name: builder name (str).
      properties: dict of properties to pass to the recipe (on top of the default ones).
      try_cat: boolean, whether to include this builder in the /try/ console. See also: `add_milo`.
      cq: None to exclude this from all commit queues, or a dict of kwargs for cq_tryjob_verifier.
      branch_cq: False to exclude this builder just from the release-branch CQ.
      builder: builder to set in the dimensions, if None, builderless:1 is used.
      jobs: int, number of remote jobs to allow in parallel.
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
    properties.update(make_siso_properties("rbe-webrtc-untrusted", jobs))
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
    properties = make_siso_properties("rbe-webrtc-trusted")
    properties["builder_group"] = "client.webrtc.perf"
    dimensions = {"pool": "luci.webrtc.perf", "os": "Linux"}
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
        jobs = 500,
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
    properties = {"xcode_build_version": xcode.for_ios(WEBRTC_XCODE).version},
    caches = [xcode.for_ios(WEBRTC_XCODE).cache],
)

# Actual builder configuration:

ci_builder("Android64 (dbg)", "Android|arm64|dbg")
try_builder("android_compile_arm_dbg")
try_builder("android_arm_dbg", cq = None)
ci_builder("Android64", "Android|arm64|rel")
try_builder("android_arm_rel")
ci_builder("Android32 Builder arm", "Android|arm|size", perf_cat = "Android|arm|Builder|", prioritized = True)
try_builder("android_compile_arm_rel")
perf_builder("Perf Android32 (R Pixel5)", "Android|arm|Tester|R Pixel5", triggered_by = ["Android32 Builder arm"])
ci_builder("Android64 Builder arm64", "Android|arm64|size", perf_cat = "Android|arm64|Builder|", prioritized = True)
perf_builder("Perf Android64 (R Pixel5)", "Android|arm64|Tester|R Pixel5", triggered_by = ["Android64 Builder arm64"])
ci_builder("Android64 Builder x64 (dbg)", "Android|x64|dbg")
try_builder("android_compile_x64_dbg")
try_builder("android_compile_x64_rel")
ci_builder("Android32 Builder x86 (dbg)", "Android|x86|dbg")
try_builder("android_compile_x86_dbg")
ci_builder("Android32 Builder x86", "Android|x86|rel")
try_builder("android_compile_x86_rel")
ci_builder("Android64 (more configs)", "Android|arm64|more")
try_builder("android_arm_more_configs")
chromium_try_builder("android_chromium_compile")

ios_builder("iOS64 Debug", "iOS|arm64|dbg")
ios_try_job("ios_compile_arm64_dbg", cq = None)
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
perf_builder("Perf Linux", "Linux|x64|Tester", triggered_by = ["Linux64 Builder"])
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
ci_builder("MacArm64 Builder", ci_cat = None, perf_cat = "Mac|arm64|Builder|")
perf_builder("Perf Mac M1 Arm64 12", "Mac|arm64|Tester|12", triggered_by = ["MacArm64 Builder"])
ci_builder("Mac Asan", "Mac|x64|asan")
try_builder("mac_asan")
ci_builder("MacARM64 M1 Release", "Mac|arm64M1|rel", cpu = "arm64-64-Apple_M1")
try_builder("mac_rel_m1", cq = None)
try_builder("mac_dbg_m1", cq = None)

# TODO(b/427073823): Re-enable once the slow compilation issue is fixed.
chromium_try_builder("mac_chromium_compile", cq = None)

ci_builder("Win32 Debug (Clang)", "Win Clang|x86|dbg")
try_builder("win_x86_clang_dbg", cq = None)
try_builder("win_compile_x86_clang_dbg")
ci_builder("Win32 Release (Clang)", "Win Clang|x86|rel")
try_builder("win_x86_clang_rel")
try_builder("win_compile_x86_clang_rel", cq = None)
ci_builder("Win64 Builder (Clang)", ci_cat = None, perf_cat = "Win|x64|Builder|")
perf_builder("Perf Win 10", "Win|x64|Tester|10", triggered_by = ["Win64 Builder (Clang)"])
ci_builder("Win64 Debug (Clang)", "Win Clang|x64|dbg")
try_builder("win_x64_clang_dbg", cq = None)
try_builder("win_compile_x64_clang_dbg")
ci_builder("Win64 Release (Clang)", "Win Clang|x64|rel")
try_builder("win_x64_clang_rel")
try_builder("win_compile_x64_clang_rel", cq = None)
ci_builder("Win64 ASan", "Win Clang|x64|asan")
try_builder("win_asan")
ci_builder("Win (more configs)", "Win Clang|x86|more")
try_builder("win_x86_more_configs")
try_builder("win11_release", cq = None)
try_builder("win11_debug", cq = None)

# TODO(b/562984134): Re-enable once the infra issue is resolved
chromium_try_builder("win_chromium_compile", cq = None)
chromium_try_builder("win_chromium_compile_dbg", cq = None)

try_builder("iwyu_verifier")

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
                "WebRTC Chromium FYI Android Builder",
                "WebRTC Chromium FYI Android Tester",
                "WebRTC Chromium FYI Linux Builder (dbg)",
                "WebRTC Chromium FYI Linux Builder",
                "WebRTC Chromium FYI Linux Tester",
                "WebRTC Chromium FYI Mac Builder (dbg)",
                "WebRTC Chromium FYI Mac Builder",
                "WebRTC Chromium FYI Mac Tester",
                # TODO(b/562984134): Re-enable once the infra issue is resolved
                # "WebRTC Chromium FYI Win Builder (dbg)",
                # "WebRTC Chromium FYI Win Builder",
                # "WebRTC Chromium FYI Win Tester",
                # TODO(b/441273941): Re-enable once the ios infra issue is resolved
                # "WebRTC Chromium FYI ios-device",
                # "WebRTC Chromium FYI ios-simulator",
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
