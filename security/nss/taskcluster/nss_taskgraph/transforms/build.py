# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json

from copy import deepcopy
from taskgraph.transforms.base import TransformSequence
from taskgraph.util.taskcluster import find_task_id

transforms = TransformSequence()


@transforms.add
def set_build_attributes(config, jobs):
    """
    Set the build_platform and build_type attributes based on the job name.
    """
    for job in jobs:
        build_platform, build_type = job["name"].split("/")
        attributes = job.setdefault("attributes", {})
        attributes.update(
            {
                "build_platform": build_platform,
                "build_type": build_type,
            }
        )

        yield job


# Modern compilers come from the noble-based `builds` image; the legacy
# compilers (RHEL floor coverage) come from the pinned focal `builds-legacy`
# image. See LEGACY_COMPILERS and set_docker_image below.
EXTRA_COMPILERS = {
    "clang": {"CC": "clang", "CCC": "clang++"},
    "clang-10": {"CC": "clang-10", "CCC": "clang++-10"},
    "clang-23": {"CC": "clang-23", "CCC": "clang++-23"},
    # gcc-8 is the RHEL 8 system compiler (our floor); gcc-11 is RHEL 9.
    "gcc-8": {"CC": "gcc-8", "CCC": "g++-8"},
    "gcc-11": {"CC": "gcc-11", "CCC": "g++-11"},
}

#: Compilers served by the pinned focal `builds-legacy` image (not in noble).
LEGACY_COMPILERS = ("clang-10", "gcc-8", "gcc-11")


@transforms.add
def add_variants(config, jobs):
    for job in jobs:
        attributes = job["attributes"]

        # nspr. Skipped on cross builds: the variant runs NSPR's test suite,
        # and the build host can't execute target binaries.
        if not any(attributes.get(attr) for attr in ("make", "asan", "fuzz", "tsan", "cross")):
            nspr_job = deepcopy(job)
            nspr_job["name"] = f"{attributes['build_platform']}-nspr/{attributes['build_type']}"
            nspr_job["description"]+= " (NSPR only)"
            nspr_job["attributes"]["nspr"] = True
            yield nspr_job

        # base build
        build_job = deepcopy(job)
        build_job["attributes"].setdefault("certs", True)
        yield build_job

        # fips
        if attributes.get("make"):
            fips_build = deepcopy(job)
            fips_build["name"] = f"{attributes['build_platform']}-fips/{attributes['build_type']}"
            fips_build["description"] += " w/ NSS_FORCE_FIPS"
            fips_build.setdefault("worker", {}).setdefault("env", {})["NSS_FORCE_FIPS"] = "1"
            fips_build["attributes"]["make-fips"] = True
            fips_build["attributes"]["certs"] = True
            yield fips_build

        # Windows gyp builds use clang-cl, as Firefox does (see
        # uses_clang_cl). MSVC is kept as a build-only extra, like the extra
        # Linux compilers below. Sanitizer builds are clang-only.
        if (attributes["build_platform"].startswith("win")
                and not any(attributes.get(attr) for attr in ("make", "static", "asan"))):
            msvc_job = deepcopy(job)
            msvc_job["name"] += "-msvc"
            msvc_job["description"] += " w/ MSVC"
            msvc_job["attributes"]["cc"] = "msvc"
            yield msvc_job

        # The extra compilers, modular, and dbm variants are x86-Linux only.
        # aarch64 is a Linux platform too, so exclude it explicitly.
        if "linux" in attributes["build_platform"] and "aarch64" not in attributes["build_platform"]:
            # more compilers
            if not attributes.get("asan") and not attributes.get("fuzz") and not attributes.get("tsan"):
                for cc in EXTRA_COMPILERS:
                    cc_job = deepcopy(job)
                    cc_job["name"] += f"-{cc}"
                    cc_job["description"] += f" w/ {cc}"
                    cc_job["attributes"]["cc"] = cc
                    cc_job.setdefault("worker", {}).setdefault("env", {}).update(EXTRA_COMPILERS[cc])
                    yield cc_job

            # modular
            if attributes.get("make"):
                modular_job = deepcopy(job)
                modular_job["attributes"]["modular"] = True
                modular_job.setdefault("worker", {}).setdefault("env", {})["NSS_BUILD_MODULAR"] = "1"
                modular_job["name"] += "-modular"
                modular_job["description"] += " w/ modular builds"
                yield modular_job

            # dbm
            if not attributes.get("make") and not attributes.get("fuzz") and not attributes.get("tsan"):
                dbm_job = deepcopy(job)
                dbm_job["attributes"]["dbm"] = True
                dbm_job["attributes"].setdefault("certs", True)
                dbm_job["name"] += "-dbm"
                dbm_job["description"] += " w/ legacy-db"
                yield dbm_job

            if attributes.get("fuzz"):
                tlsfuzz_job = deepcopy(job)
                tlsfuzz_job["attributes"]["tlsfuzz"] = True
                tlsfuzz_job["name"] = job["name"].replace("-fuzz", "-tlsfuzz")
                tlsfuzz_job["description"] = job["description"].replace("fuzz", "TLS fuzz")
                yield tlsfuzz_job


def uses_clang_cl(attributes):
    """Windows gyp builds use clang-cl unless they are the MSVC variant. The
    make builds go through coreconf, which only knows MSVC."""
    return (attributes["build_platform"].startswith("win")
            and not attributes.get("make")
            and attributes.get("cc") != "msvc")


@transforms.add
def set_attributes_defaults(config, jobs):
    for job in jobs:
        attributes = job["attributes"]
        for attr in ("make", "asan", "make-fips", "fuzz", "certs", "nspr", "cc", "dbm", "modular", "tlsfuzz", "tsan", "cross"):
            attributes.setdefault(attr, False)
        yield job


@transforms.add
def set_make_command(config, jobs):
    for job in jobs:
        if not job["attributes"].get("make"):
            yield job
            continue
        platform = job["attributes"]["build_platform"]
        if "win" in platform:
            script = "${VCS_PATH}/nss/automation/taskcluster/windows/build.sh"
        else:
            script = "${VCS_PATH}/nss/automation/taskcluster/scripts/build.sh"
        if "64" in job["attributes"]["build_platform"]:
            job["worker"].setdefault("env", {})["USE_64"] = "1"
        job["run"]["command"] = script
        yield job


@transforms.add
def set_gyp_command(config, jobs):
    for job in jobs:
        attributes = job["attributes"]
        if attributes.get("make"):
            yield job
            continue
        platform = attributes["build_platform"]
        if "win" in platform:
            script = "${VCS_PATH}/nss/automation/taskcluster/windows/build_gyp.sh"
        else:
            script = "${VCS_PATH}/nss/automation/taskcluster/scripts/build_gyp.sh"
        command = script + " --python=python3"
        if "64" not in platform:
            command += " -t ia32"
        elif platform.startswith("win") and "aarch64" in platform:
            # Cross-compiled on the x64 Windows builder; native aarch64 Linux
            # builds default target_arch to the host and are left alone.
            # shlibsign runs what it signs, which the host can't do here. No
            # test on this platform reads the .chk files it would produce.
            command += " -t arm64 -Dsign_libs=0"
        if uses_clang_cl(attributes):
            command += " --clang-cl"
        if attributes["build_type"] in ("opt", "opt-static"):
            command += " --opt"
        if "fips" in attributes["build_type"]:
            command += " --enable-fips"
        if attributes.get("asan"):
            command += " --ubsan --asan"
        if attributes.get("tsan"):
            command += " --tsan"
        if attributes.get("nspr"):
            command += " --nspr-only --nspr-test-build --nspr-test-run"
        if attributes.get("static"):
            command += " --static -Ddisable_libpkix=1"
        if attributes.get("fuzz"):
            command += " --disable-tests -Ddisable_libpkix=1 --fuzz"
            job.setdefault("worker", {}).setdefault("env", {}).update({
                "ASAN_OPTIONS": "allocator_may_return_null=1:detect_stack_use_after_return=1",
                "UBSAN_OPTIONS": "print_stacktrace=1",
                "NSS_DISABLE_ARENA_FREE_LIST": "1",
                "NSS_DISABLE_UNLOAD": "1",
                "CC": "clang",
                "CCC": "clang++",
            })
            if attributes.get("tlsfuzz"):
                command += "=tls"
        job["run"]["command"] = command
        yield job


# The Visual Studio layout the Windows ARM64 and clang-cl builds compile
# against. See automation/taskcluster/windows/setup.sh for why it comes from
# gecko rather than from the tooltool package the other Windows builds use.
# Bump the index when gecko moves to a newer Visual Studio.
VS_TOOLCHAIN_INDEX = "gecko.cache.level-3.toolchains.v3.win64-vs2026.latest"
VS_TOOLCHAIN_ARTIFACT = "project/gecko/vs/vs.tar.zst"

# The clang-cl that Firefox's Windows builds use, runnable on an x64 Windows
# host; it unpacks to clang/bin. Bump the index when gecko moves to a newer
# clang.
CLANG_TOOLCHAIN_INDEX = "gecko.cache.level-3.toolchains.v3.win64-clang-22.latest"
CLANG_TOOLCHAIN_ARTIFACT = "public/build/clang.tar.zst"


@transforms.add
def add_gecko_toolchain_fetches(config, jobs):
    """Have run-task fetch gecko's Visual Studio layout, and for clang-cl
    builds its clang, for the Windows builds that need them.

    run-task invokes fetch-content whenever MOZ_FETCHES is set, and
    fetch-content already knows how to pull a non-public artifact through the
    taskcluster proxy, check it against the producing task's chain of trust,
    and unpack a .tar.zst.

    taskgraph's own `fetches` stanza can't express this: it only resolves
    `fetch`/`toolchain` tasks inside our own graph, and these artifacts come
    from gecko's. So build the MOZ_FETCHES entries by hand, resolving each index
    to a concrete task id at decision time -- which also pins the toolchains
    for the whole push instead of re-resolving `.latest` per task. The lookups
    are lazy so that generating a graph with no such task in it makes no
    network request.
    """
    task_ids = {}

    def fetch(index, artifact):
        if index not in task_ids:
            task_ids[index] = find_task_id(index)
        return {
            "artifact": artifact,
            "task": task_ids[index],
            "extract": True,
            "verify-hash": True,
        }

    for job in jobs:
        platform = job["attributes"]["build_platform"]
        clang_cl = uses_clang_cl(job["attributes"])
        if not (platform.startswith("win") and ("aarch64" in platform or clang_cl)):
            yield job
            continue

        fetches = [fetch(VS_TOOLCHAIN_INDEX, VS_TOOLCHAIN_ARTIFACT)]
        if clang_cl:
            fetches.append(fetch(CLANG_TOOLCHAIN_INDEX, CLANG_TOOLCHAIN_ARTIFACT))

        env = job.setdefault("worker", {}).setdefault("env", {})
        env["MOZ_FETCHES"] = json.dumps(fetches, sort_keys=True)
        env.setdefault("MOZ_FETCHES_DIR", "fetches")

        # What taskgraph.transforms.run:use_fetches would have done for a
        # non-public artifact. The clang artifact is public.
        job["worker"]["taskcluster-proxy"] = True
        prefix = VS_TOOLCHAIN_ARTIFACT.rsplit("/", 1)[0]
        scope = f"queue:get-artifact:{prefix}/*"
        if scope not in job.get("scopes", []):
            job["scopes"] = job.get("scopes", []) + [scope]

        yield job


@transforms.add
def set_docker_image(config, jobs):
    for job in jobs:
        platform = job["attributes"]["build_platform"]
        if "linux" not in platform:
            yield job
            continue

        cc = job["attributes"].get("cc")
        if cc in LEGACY_COMPILERS:
            image = "builds-legacy"
        elif cc:
            image = "builds"
        elif job["attributes"].get("fuzz"):
            image = "fuzz"
        elif job["attributes"].get("asan") or job["attributes"].get("tsan"):
            # Sanitizers needs a modern clang + compiler-rt, which live in builds.
            image = "builds"
        else:
            image = "base"
        job["worker"]["docker-image"] = {"in-tree": image}
        yield job


def get_job_symbol(job):
    if job["attributes"].get("nspr"):
        return "NSPR"
    if job["attributes"].get("cc"):
        return f"Builds({job['attributes']['cc']})"
    if job["attributes"].get("tlsfuzz"):
        return "TLS(B)"
    if job["attributes"].get("dbm"):
        return "DBM(B)"
    if job["attributes"].get("make-fips"):
        return "FIPS(B)"
    if job["attributes"].get("modular"):
        return "Builds(modular)"
    return "B"

@transforms.add
def set_treeherder_symbol(config, jobs):
    for job in jobs:
        job["treeherder"].update({
            "symbol": get_job_symbol(job),
            "platform": f'{job["attributes"]["build_platform"]}/{job["attributes"]["build_type"]}',
        })
        yield job
