# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
from contextlib import contextmanager

import mozpack.path as mozpath
from mach.filelock import SoftFileLock
from mozbuild.dirutils import ensureParentDir


@contextmanager
def gradle_lock(topobjdir, max_wait_seconds=600):
    # Building the same Gradle root project with multiple concurrent processes
    # is not well supported, so we use a simple lock file to serialize build
    # steps.
    lock_path = f"{topobjdir}/gradle/mach_android.lockfile"
    ensureParentDir(lock_path)
    with SoftFileLock(lock_path, timeout=max_wait_seconds):
        yield


def jvmargs_with_max_heap(topsrcdir, max_heap):
    with open(mozpath.join(topsrcdir, "gradle.properties"), encoding="utf-8") as fh:
        jvmargs = ""
        for line in fh:
            if m := re.match(r"\s*org\.gradle\.jvmargs\s*[=:]\s*(.*?)\s*$", line):
                jvmargs = m.group(1)
    jvmargs, replaced = re.subn(r"-Xmx\S+", lambda _: f"-Xmx{max_heap}", jvmargs)
    if not replaced or jvmargs.endswith("\\"):
        raise Exception(
            f"MOZ_GRADLE_MAX_HEAP is set, but {topsrcdir}/gradle.properties "
            "has no single line org.gradle.jvmargs with -Xmx"
        )
    return jvmargs


def _seed_digest(seed):
    sidecar = f"{seed}.sha256"
    if os.path.exists(sidecar):
        with open(sidecar, encoding="utf-8") as fh:
            return fh.read().strip()
    sha256 = hashlib.sha256()
    with open(seed, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            sha256.update(chunk)
    with open(sidecar, "w", encoding="utf-8") as fh:
        fh.write(sha256.hexdigest())
    return sha256.hexdigest()


def _merge_tree(src, dst):
    for name in os.listdir(src):
        src_path = mozpath.join(src, name)
        dst_path = mozpath.join(dst, name)
        if not os.path.lexists(dst_path):
            os.rename(src_path, dst_path)
        elif os.path.isdir(src_path) and os.path.isdir(dst_path):
            _merge_tree(src_path, dst_path)


def seed_gradle_user_home(seed, gradle_user_home):
    if not seed:
        return
    if not gradle_user_home:
        raise Exception("MOZ_GRADLE_USER_HOME_SEED is set, but GRADLE_USER_HOME is not")
    if not os.path.isfile(seed):
        raise Exception(
            f"MOZ_GRADLE_USER_HOME_SEED is set to {seed}, which is not a file"
        )
    digest = _seed_digest(seed)
    applied = mozpath.join(gradle_user_home, ".seeds")
    with SoftFileLock(f"{applied}.lock", timeout=600):
        if os.path.exists(applied):
            with open(applied, encoding="utf-8") as fh:
                if digest in fh.read().split():
                    return
        staging = mozpath.join(gradle_user_home, "seed.tmp")
        shutil.rmtree(staging, ignore_errors=True)
        os.makedirs(staging)
        subprocess.check_call(["tar", "xf", seed, "-C", staging])
        caches = mozpath.join(gradle_user_home, "caches")
        os.makedirs(caches, exist_ok=True)
        _merge_tree(mozpath.join(staging, "gradle-user-home-seed", "caches"), caches)
        shutil.rmtree(staging, ignore_errors=True)
        with open(applied, "a", encoding="utf-8") as fh:
            fh.write(f"{digest}\n")


def main(dummy_output_file, *args):
    env = dict(os.environ)
    import buildconfig

    cmd = [
        sys.executable,
        mozpath.join(buildconfig.topsrcdir, "mach"),
        "android",
        "export",
    ]
    cmd.extend(args)
    # Confusingly, `MACH` is set only within `mach build`.
    if env.get("MACH"):
        env["GRADLE_INVOKED_WITHIN_MACH_BUILD"] = "1"
    if env.get("LD_LIBRARY_PATH"):
        del env["LD_LIBRARY_PATH"]

    should_print_status = env.get("MACH") and not env.get("NO_BUILDSTATUS_MESSAGES")
    if should_print_status:
        print("BUILDSTATUS " + str(time.time()) + " START_Gradle export")

    subprocess.check_call(cmd, env=env)

    if should_print_status:
        print("BUILDSTATUS " + str(time.time()) + " END_Gradle export")
    return 0


if __name__ == "__main__":
    seed_gradle_user_home(
        os.environ.get("MOZ_GRADLE_USER_HOME_SEED"), os.environ.get("GRADLE_USER_HOME")
    )
