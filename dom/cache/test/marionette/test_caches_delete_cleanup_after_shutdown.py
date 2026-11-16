# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import time

from marionette_driver import Wait
from marionette_harness import MarionetteTestCase

"""
 Currently we expect our size after cleanup to be 128k for the current
 database schema and page sizes and constants used in this test. We
 set the threshold at 160k so the test doesn't start failing if these
 control objects/structures increase somewhat in size, but should still
 fail if we fail to delete all of the 5,000 1k files we create on disk
 as part of the test.
"""
QM_TESTING_PREF = "dom.quotaManager.testing"
SQLITE_ENCRYPTION_PREF = "security.storage.encryption.sqlite.enabled"

EXPECTED_CACHEDIR_SIZE_AFTER_CLEANUP = 160 * 1024  # 160KB
# Encryption roughly doubles the size of caches.sqlite, 256k instead of 128k.
ENCRYPTED_CACHEDIR_SIZE_AFTER_CLEANUP = 2 * EXPECTED_CACHEDIR_SIZE_AFTER_CLEANUP
CACHE_ID = "data"


class CachesDeleteCleanupAtShutdownTestCase(MarionetteTestCase):
    """
    Bug1784700: This test ensures that cache body files get cleaned up
    properly after cache has been deleted. Note that body files gets
    cleaned up asynchronously which means that these files might still
    be around even after Caches.Delete promise gets resolved. Currently,
    we would only clean up body files on origin initialization and that's
    a firefox is necessary here.
    """

    def setUp(self):
        super().setUp()
        self.marionette.restart(in_app=False, clean=True)
        self.marionette.set_pref(QM_TESTING_PREF, True)
        # The unclean restart kills the process, so make sure the pref has
        # reached prefs.js instead of relying on the delayed async save.
        with self.marionette.using_context("chrome"):
            self.marionette.execute_script("Services.prefs.savePrefFile(null);")

    def tearDown(self):
        self.marionette.restart(in_app=False, clean=True)
        super().tearDown()
        self.marionette.set_pref(QM_TESTING_PREF, False)

    def getUsage(self):
        usage = self.marionette.execute_async_script(
            """
                const [resolve] = arguments;
                window.wrappedJSObject.getStorageEstimate()
                    .then(resolve)
                    .catch(()=>resolve(-1));
            """,
            new_sandbox=False,
        )
        assert (
            usage != -1
        )  # usage should not be an invalid number which gets sets in catch handler above.
        return usage

    def doCacheWork(self, n):
        # max timeout for this script to execute is 5 minutes
        maxTimeout = 5 * 60 * 1000

        assert self.marionette.execute_async_script(
            """
                const [cacheId, n, resolve] = arguments;
                window.wrappedJSObject.doCacheWork(cacheId, n)
                    .then(()=>resolve(true))
                    .catch(()=>resolve(false));
            """,
            script_args=(
                CACHE_ID,
                n,
            ),
            new_sandbox=False,
            script_timeout=maxTimeout,
        )

    def openCache(self):
        assert self.marionette.execute_async_script(
            """
                const [cacheId, resolve] = arguments;
                window.wrappedJSObject.openCache(cacheId)
                    .then(()=>resolve(true))
                    .catch(()=>resolve(false));
            """,
            new_sandbox=False,
            script_args=(CACHE_ID,),
        )

    # asynchronously iterating over body files could be challenging as firefox
    # might be doing some cleanup while we are iterating over it's morgue dir.
    # It's expected for this helper to throw FileNotFoundError exception in
    # such cases.
    def fallibleCountBodies(self, morgueDir):
        bodyCount = 0
        for elem in os.listdir(morgueDir):
            absPathElem = os.path.join(morgueDir, elem)

            if os.path.isdir(absPathElem):
                bodyCount += sum(
                    1
                    for e in os.listdir(absPathElem)
                    if os.path.isfile(os.path.join(absPathElem, e))
                )
        return bodyCount

    def expectedSizeAfterCleanup(self):
        if self.marionette.get_pref(SQLITE_ENCRYPTION_PREF):
            return ENCRYPTED_CACHEDIR_SIZE_AFTER_CLEANUP
        return EXPECTED_CACHEDIR_SIZE_AFTER_CLEANUP

    def countBodies(self):
        profile = self.marionette.instance.profile.profile
        originDir = (
            self.marionette.absolute_url("")[:-1].replace(":", "+").replace("/", "+")
        )

        morgueDir = f"{profile}/storage/default/{originDir}/cache/morgue"
        print("morgueDir path = ", morgueDir)

        if not os.path.exists(morgueDir):
            print("morgue directory does not exists.")
            return -1

        while True:
            try:
                return self.fallibleCountBodies(morgueDir)
            except FileNotFoundError:
                # we probably just got in the period when firefox was cleaning up.
                # retry...
                time.sleep(0.5)

    def ensureCleanDirectory(self):
        orphanedBodiesCount = self.countBodies()
        return orphanedBodiesCount <= 0

    def isStorageInitialized(self, temporary=False):
        with self.marionette.using_context("chrome"):
            return self.marionette.execute_async_script(
                """
                    const [resolve] = arguments;
                    const req = %s;
                    req.callback = () => {
                        resolve(req.resultCode == Cr.NS_OK && req.result)
                    };
                """
                % (
                    "Services.qms.temporaryStorageInitialized()"
                    if temporary
                    else "Services.qms.storageInitialized()"
                ),
                new_sandbox=False,
            )

    def create_and_cleanup_cache(self, ensureCleanCallback, in_app):
        # create 640 cache entries
        self.doCacheWork(640)
        print("usage after doCacheWork = ", self.getUsage())

        self.marionette.restart(in_app=in_app)
        print("restart successful")

        self.marionette.navigate(
            self.marionette.absolute_url("dom/cache/cacheUsage.html")
        )
        return ensureCleanCallback()

    def afterCleanupClosure(self, usage):
        print(
            f"Storage initialized = {self.isStorageInitialized()}, temporary storage initialized = {self.isStorageInitialized(True)}"
        )

        print(f"Usage = {usage} and number of orphaned bodies = {self.countBodies()}")
        return usage < self.expectedSizeAfterCleanup()

    # The freed database pages are only vacuumed when the Cache connection
    # closes, which happens once the Cache object from openCache is collected.
    def releaseCacheAndCheck(self, beforeUsage):
        self.marionette.execute_script(
            "Components.utils.forceGC(); Components.utils.forceCC();",
            sandbox="system",
            new_sandbox=False,
        )
        return self.afterCleanupClosure(self.getUsage() - beforeUsage)

    def test_ensure_cache_cleanup_after_clean_restart(self):
        self.marionette.navigate(
            self.marionette.absolute_url("dom/cache/cacheUsage.html")
        )
        beforeUsage = self.getUsage()

        def ensureCleanCallback():
            self.openCache()
            Wait(self.marionette, interval=1, timeout=60).until(
                lambda _: self.releaseCacheAndCheck(beforeUsage),
                message="Cache directory is not cleaned up properly",
            )

            return (
                abs(beforeUsage - self.getUsage()) <= self.expectedSizeAfterCleanup()
                and self.ensureCleanDirectory()
            )

        if not self.create_and_cleanup_cache(ensureCleanCallback, True):
            print(f"beforeUsage = {beforeUsage}, and afterUsage = {self.getUsage()}")
            assert False

    def test_ensure_cache_cleanup_after_unclean_restart(self):
        self.marionette.navigate(
            self.marionette.absolute_url("dom/cache/cacheUsage.html")
        )

        print(f"profile path = {self.marionette.instance.profile.profile}")

        beforeUsage = self.getUsage()

        def ensureCleanCallback():
            print(
                f"ensureCleanCallback, profile path = {self.marionette.instance.profile.profile}"
            )

            self.openCache()
            Wait(self.marionette, interval=1, timeout=60).until(
                lambda _: self.releaseCacheAndCheck(beforeUsage),
                message="Cache directory is not cleaned up properly",
            )

            return (
                abs(beforeUsage - self.getUsage()) <= self.expectedSizeAfterCleanup()
                and self.ensureCleanDirectory()
            )

        if not self.create_and_cleanup_cache(ensureCleanCallback, False):
            print(f"beforeUsage = {beforeUsage}, and afterUsage = {self.getUsage()}")
            assert False
