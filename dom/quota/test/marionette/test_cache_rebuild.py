# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sqlite3
import sys
from contextlib import closing, contextmanager
from pathlib import Path

sys.path.append(os.fspath(Path(__file__).parents[0]))
from quota_test_case import QuotaTestCase

QM_TESTING_PREF = "dom.quotaManager.testing"
CACHE_CHECK_BUILD_ID_PREF = "dom.quotaManager.caching.checkBuildId"
CLEAR_EMPTY_ORIGINS_PREF = (
    "dom.quotaManager.temporaryStorage.clearNonPersistedZeroUsageOrigins"
)


class CacheRebuild(QuotaTestCase):
    def setUp(self):
        super().setUp()
        self.origin = f"https://{self._testMethodName.replace('_', '-')}.example.com"
        profile_path = self.marionette.instance.profile.profile
        self.storage_file = os.path.join(profile_path, "storage.sqlite")
        self.compatibility_file = os.path.join(profile_path, "compatibility.ini")

        self.marionette.set_pref(QM_TESTING_PREF, True)
        self.marionette.set_pref(CACHE_CHECK_BUILD_ID_PREF, True)
        self.marionette.set_pref(CLEAR_EMPTY_ORIGINS_PREF, False)
        self.marionette.restart(in_app=True)
        with self.marionette.using_context(self.marionette.CONTEXT_CHROME):
            self.executeAsyncScript(
                """
                async function main() {
                  await requestFinished(Services.qms.clear());
                  return true;
                }
                """,
                script_args=(),
            )
        assert self.initStorage()
        assert self.initTemporaryStorage()
        assert self.initTemporaryOrigin("default", self.origin)

    def test_cache_rebuilt_after_build_id_change(self):
        with self._stopped_browser():
            self._assert_cache_has_origin()
            stale_version = self._change_build_id(self.compatibility_file)

        self.assertNotEqual(
            self._read_last_version(self.compatibility_file), stale_version
        )

        assert self.initStorage()
        assert self.initTemporaryStorage()

        with self._stopped_browser():
            self._assert_cache_has_origin()

    def test_missing_database_recovers_indexeddb(self):
        self.marionette.set_pref(CACHE_CHECK_BUILD_ID_PREF, False)
        value = "missing-cache-data"
        self.assertEqual(self._indexeddb_value(value), value)
        usage = self.getFullOriginMetadata("default", self.origin)["originUsage"]
        self.assertGreater(usage, 0)

        with self._stopped_browser():
            self._assert_cache_has_origin()
            os.remove(self.storage_file)

        for _ in range(2):
            assert self.initStorage()
            assert self.initTemporaryStorage()
            # WAL checkpoint at shutdown can change the physical file layout,
            # so compare against a fresh disk walk instead of the pre-restart
            # in-memory value.
            self.assertEqual(
                self.getFullOriginMetadata("default", self.origin)["originUsage"],
                self._disk_usage(self.origin),
            )
            self.assertEqual(self._indexeddb_value(), value)
            with self._stopped_browser():
                self._assert_cache_has_origin()

    def test_unsupported_cache_version_recovers_indexeddb(self):
        self.marionette.set_pref(CACHE_CHECK_BUILD_ID_PREF, False)
        value = "unsupported-cache-data"
        self.assertEqual(self._indexeddb_value(value), value)

        with self._stopped_browser():
            unsupported_version = self._cache_version() + 1
            self._execute_sql(
                f"UPDATE database SET cache_version = {unsupported_version}; "
                "DELETE FROM origin;"
            )

        assert self.initStorage()
        assert self.initTemporaryStorage()
        self.assertEqual(
            self.getFullOriginMetadata("default", self.origin)["originUsage"],
            self._disk_usage(self.origin),
        )
        self.assertEqual(self._indexeddb_value(), value)
        with self._stopped_browser():
            self.assertEqual(self._cache_version(), unsupported_version)

    def tearDown(self):
        self.marionette.clear_pref(CLEAR_EMPTY_ORIGINS_PREF)
        self.marionette.clear_pref(CACHE_CHECK_BUILD_ID_PREF)
        self.marionette.clear_pref(QM_TESTING_PREF)
        self.marionette.restart(in_app=True)
        super().tearDown()

    @contextmanager
    def _stopped_browser(self, in_app=True):
        self.marionette.quit(in_app=in_app)
        try:
            yield
        finally:
            self.marionette.start_session()

    def _cache_version(self):
        with closing(sqlite3.connect(self.storage_file)) as connection:
            return connection.execute("SELECT cache_version FROM database").fetchone()[
                0
            ]

    def _execute_sql(self, sql):
        with closing(sqlite3.connect(self.storage_file)) as connection:
            connection.executescript(f"BEGIN IMMEDIATE;\n{sql}\nCOMMIT;")

    def _disk_usage(self, origin):
        with self.marionette.using_context(self.marionette.CONTEXT_CHROME):
            return self.executeAsyncScript(
                """
                const [origin] = arguments;
                async function main() {
                  const principal = Services.scriptSecurityManager
                    .createContentPrincipalFromOrigin(origin);
                  const request = Services.qms.getUsageForPrincipal(
                    principal, () => {}
                  );
                  const result = await requestFinished(request);
                  return result.usage;
                }
                """,
                script_args=(origin,),
            )

    def _indexeddb_value(self, value=None):
        with self.marionette.using_context(self.marionette.CONTEXT_CHROME):
            return self.executeAsyncScript(
                """
                async function main() {
                  const [origin, value] = argumentsFromPython;
                  const principal = Services.scriptSecurityManager
                    .createContentPrincipalFromOrigin(origin);
                  const request = indexedDB.openForPrincipal(principal, "cache-rebuild", 1);
                  request.onupgradeneeded = () => request.result.createObjectStore("store");
                  const database = await new Promise((resolve, reject) => {
                    request.onsuccess = () => resolve(request.result);
                    request.onerror = () => reject(request.error);
                  });
                  try {
                    return await new Promise((resolve, reject) => {
                      const transaction = database.transaction(
                        "store", value === null ? "readonly" : "readwrite"
                      );
                      const store = transaction.objectStore("store");
                      if (value !== null) {
                        store.put(value, "key");
                      }
                      const read = store.get("key");
                      transaction.oncomplete = () => resolve(read.result);
                      transaction.onabort = () => reject(transaction.error);
                    });
                  } finally {
                    database.close();
                  }
                }
                const argumentsFromPython = arguments;
                """,
                script_args=(self.origin, value),
            )

    def _assert_cache_has_origin(self):
        self.assertTrue(
            self._has_origin(self.storage_file, self.origin),
            "The quota cache should contain the origin after a successful rebuild",
        )

    @staticmethod
    def _has_origin(storage_file, origin):
        with closing(sqlite3.connect(storage_file)) as connection:
            result = connection.execute(
                "SELECT EXISTS(SELECT 1 FROM origin WHERE origin = ?)", (origin,)
            ).fetchone()[0]
        return bool(result)

    @staticmethod
    def _change_build_id(compatibility_file):
        with open(compatibility_file, newline="") as file:
            lines = file.readlines()

        for index, line in enumerate(lines):
            if line.startswith("LastVersion="):
                last_version = line.partition("=")[2].strip()
                version_and_app_build_id, separator, toolkit_build_id = (
                    last_version.partition("/")
                )
                app_version, build_separator, _ = version_and_app_build_id.rpartition(
                    "_"
                )
                if not separator or not build_separator:
                    raise AssertionError("LastVersion has no build ID")

                newline = line[len(line.rstrip("\r\n")) :]
                lines[index] = (
                    f"LastVersion={app_version}_00000000000000/"
                    f"{toolkit_build_id}{newline}"
                )
                break
        else:
            raise AssertionError("LastVersion not found in compatibility.ini")

        with open(compatibility_file, "w", newline="") as file:
            file.writelines(lines)

        return lines[index].partition("=")[2].strip()

    @staticmethod
    def _read_last_version(compatibility_file):
        with open(compatibility_file) as file:
            for line in file:
                if line.startswith("LastVersion="):
                    return line.partition("=")[2].strip()

        raise AssertionError("LastVersion not found in compatibility.ini")
