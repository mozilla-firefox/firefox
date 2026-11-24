# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sqlite3
import sys
import unittest
from contextlib import closing, contextmanager
from pathlib import Path

sys.path.append(os.fspath(Path(__file__).parents[0]))
from quota_test_case import QuotaTestCase

QM_TESTING_PREF = "dom.quotaManager.testing"
CACHE_CHECK_BUILD_ID_PREF = "dom.quotaManager.caching.checkBuildId"
CLEAR_EMPTY_ORIGINS_PREF = (
    "dom.quotaManager.temporaryStorage.clearNonPersistedZeroUsageOrigins"
)
LAZY_ORIGIN_INIT_PREF = "dom.quotaManager.temporaryStorage.lazyOriginInitialization"
FLUSH_INTERVAL_PREF = "dom.quotaManager.originMetadataFlushCheckIntervalMs"
SECONDARY_CACHE_PREF = "dom.quotaManager.loadQuotaFromSecondaryCache"
FAILURE_PREFS = {
    "dom.quotaManager.artificialFailure.categories": 4,
    "dom.quotaManager.artificialFailure.probability": 100,
    "dom.quotaManager.artificialFailure.errorCode": -2147418113,
}


class CacheRescan(QuotaTestCase):
    def setUp(self):
        super().setUp()
        self.origin = f"https://{self._testMethodName.replace('_', '-')}.example.com"
        profile_path = self.marionette.instance.profile.profile
        self.storage_file = os.path.join(profile_path, "storage.sqlite")
        self.compatibility_file = os.path.join(profile_path, "compatibility.ini")

        self.marionette.set_pref(QM_TESTING_PREF, True)
        self.marionette.set_pref(CACHE_CHECK_BUILD_ID_PREF, True)
        self.marionette.set_pref(CLEAR_EMPTY_ORIGINS_PREF, False)
        self.marionette.set_pref(FLUSH_INTERVAL_PREF, 3600000)
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

    def test_cache_rescanned_after_build_id_change(self):
        with self._stopped_browser():
            self._assert_cache_complete()
            stale_version = self._change_build_id(self.compatibility_file)

        self.assertNotEqual(
            self._read_last_version(self.compatibility_file), stale_version
        )

        assert self.initStorage()
        assert self.initTemporaryStorage()

        with self._stopped_browser(in_app=False):
            self._assert_cache_complete()

    def test_stale_rows_pruned_after_invalidation(self):
        stale_origin = "https://stale.example.com"
        with self._stopped_browser():
            self._assert_cache_complete()
            self._execute_sql(
                f"""
                INSERT INTO origin (repository_id, suffix, group_, origin,
                  client_usages, usage, last_access_time, last_maintenance_date,
                  metadata_flags)
                SELECT repository_id, suffix, group_, '{stale_origin}',
                  client_usages, usage, last_access_time, last_maintenance_date,
                  metadata_flags
                FROM origin WHERE origin = '{self.origin}';
                """
            )
            self._change_build_id(self.compatibility_file)

        assert self.initStorage()
        assert self.initTemporaryStorage()

        with self._stopped_browser(in_app=False):
            self._assert_cache_complete()
            self.assertFalse(self._has_origin(self.storage_file, stale_origin))

    def test_undecodable_row_does_not_block_rebuild(self):
        # The temporary repository is scanned before the default one, so an
        # undecodable default row must not discard its already reconciled rows.
        bad_origin = "https://undecodable.example.com"
        temporary_origin = "https://undecodable-temporary.example.com"
        assert self.initTemporaryOrigin("temporary", temporary_origin)
        with self._stopped_browser():
            self._assert_cache_complete()
            self.assertTrue(self._has_origin(self.storage_file, temporary_origin))
            self._execute_sql(
                f"""
                INSERT INTO origin (repository_id, suffix, group_, origin,
                  client_usages, usage, last_access_time, last_maintenance_date,
                  metadata_flags)
                SELECT repository_id, suffix, group_, '{bad_origin}',
                  'bad', usage, last_access_time, last_maintenance_date,
                  metadata_flags
                FROM origin WHERE origin = '{self.origin}';
                """
            )

        assert self.initStorage()
        assert self.initTemporaryStorage()

        with self._stopped_browser(in_app=False):
            self._assert_cache_complete()
            self.assertFalse(self._has_origin(self.storage_file, bad_origin))
            self.assertTrue(self._has_origin(self.storage_file, temporary_origin))

    def test_changed_usage_refreshes_row_after_invalidation(self):
        value = "refresh-row-value" * 1024
        self.assertEqual(self._indexeddb_value(value), value)
        usage = self.getFullOriginMetadata("default", self.origin)["originUsage"]
        self.assertGreater(usage, 0)

        with self._stopped_browser(in_app=False):
            self._execute_sql(
                f"UPDATE origin SET usage = 0 WHERE origin = '{self.origin}';"
            )
            self._change_build_id(self.compatibility_file)

        assert self.initStorage()
        assert self.initTemporaryStorage()
        cached_usage = self.getFullOriginMetadata("default", self.origin)["originUsage"]
        self.assertGreater(cached_usage, 0)

        with self._stopped_browser(in_app=False):
            self._assert_cache_complete()
            self.assertEqual(self._row_usage(self.origin), cached_usage)

    def test_rescan_completes_without_clean_shutdown(self):
        self.marionette.set_pref(CACHE_CHECK_BUILD_ID_PREF, False)
        with self._stopped_browser():
            self._assert_cache_complete()
            build_id = self._cache_state()[1]
            self._execute_sql("UPDATE cache SET valid = 0; DELETE FROM origin;")

        assert self.initStorage()
        assert self.initTemporaryStorage()

        with self._stopped_browser(in_app=False):
            self.assertEqual(self._cache_state(), (1, build_id))
            self._assert_cache_has_origin()

    def test_invalidation_survives_crash(self):
        # Origin initialization failures keep the rescan from completing, so
        # the state after the invalidating startup is observable regardless of
        # who initializes temporary storage. Unaccessed origins bypass origin
        # initialization unless the secondary cache is disabled.
        self.marionette.set_pref(SECONDARY_CACHE_PREF, False)
        with self._artificial_failure(1):
            with self._stopped_browser():
                self._assert_cache_complete()
                self._change_build_id(self.compatibility_file)

            assert self.initStorage()
            try:
                self.initTemporaryStorage()
            except AssertionError:
                pass

            # Rows are kept for reconciliation; only the completeness bit
            # changes.
            with self._stopped_browser(in_app=False):
                self.assertEqual(self._cache_state()[0], 0)
                self.assertTrue(self._has_origin(self.storage_file, self.origin))

        with self._stopped_browser():
            pass

        assert self.initStorage()
        assert self.initTemporaryStorage()

        with self._stopped_browser(in_app=False):
            self._assert_cache_complete()

    def test_failed_refresh_leaves_cache_incomplete(self):
        origins = {self.origin}
        for index in range(60):
            origin = f"https://cache-rescan-{index}.example.com"
            assert self.initTemporaryOrigin("default", origin)
            origins.add(origin)

        self.marionette.set_pref(CACHE_CHECK_BUILD_ID_PREF, False)
        with self._stopped_browser():
            self._assert_cache_complete()
            self._execute_sql(
                """
                UPDATE cache SET valid = 0; DELETE FROM origin;
                CREATE TRIGGER fail_cache_refresh BEFORE INSERT ON origin
                WHEN (SELECT COUNT(*) FROM origin) >= 55
                BEGIN
                  SELECT RAISE(FAIL, 'injected cache refresh failure');
                END;
                """
            )
        assert self.initStorage()
        assert self.initTemporaryStorage()

        with self._stopped_browser(in_app=False):
            try:
                self.assertEqual(self._cache_state()[0], 0)
                # 55 of 61 origins succeed; the remaining 6 are skipped
                # (warn-only) and retried on the next rescan.
                self.assertEqual(len(self._cached_origins()), 55)
            finally:
                self._execute_sql("DROP TRIGGER fail_cache_refresh;")

        assert self.initStorage()
        assert self.initTemporaryStorage()

        with self._stopped_browser():
            self._assert_cache_complete()
            self.assertTrue(origins.issubset(self._cached_origins()))

    def test_failed_second_batch_leaves_first_batch_committed(self):
        origins = {self.origin}
        for index in range(60):
            origin = f"https://cache-rescan-{index}.example.com"
            assert self.initTemporaryOrigin("default", origin)
            origins.add(origin)

        self.marionette.set_pref(CACHE_CHECK_BUILD_ID_PREF, False)
        with self._stopped_browser():
            self._assert_cache_complete()
            self._execute_sql(
                """
                UPDATE cache SET valid = 0; DELETE FROM origin;
                CREATE TRIGGER fail_second_batch BEFORE INSERT ON origin
                WHEN (SELECT COUNT(*) FROM origin) >= 50
                BEGIN
                  SELECT RAISE(FAIL, 'injected second-batch failure');
                END;
                """
            )

        assert self.initStorage()
        assert self.initTemporaryStorage()

        with self._stopped_browser(in_app=False):
            try:
                self.assertEqual(self._cache_state()[0], 0)
                self.assertEqual(len(self._cached_origins()), 50)
            finally:
                self._execute_sql("DROP TRIGGER fail_second_batch;")

        assert self.initStorage()
        assert self.initTemporaryStorage()
        with self._stopped_browser():
            self._assert_cache_complete()
            self.assertTrue(origins.issubset(self._cached_origins()))

    def test_failed_completion_leaves_cache_incomplete(self):
        with self._stopped_browser():
            self._assert_cache_complete()
            self._execute_sql(
                """
                UPDATE cache SET valid = 0; DELETE FROM origin;
                CREATE TRIGGER fail_cache_completion BEFORE UPDATE OF valid ON cache
                WHEN NEW.valid = 1
                BEGIN
                  SELECT RAISE(ABORT, 'injected cache completion failure');
                END;
                """
            )

        assert self.initStorage()
        assert self.initTemporaryStorage()

        with self._stopped_browser(in_app=False):
            try:
                self.assertEqual(self._cache_state()[0], 0)
                # Batched origin commits are independent of the completion
                # step, so the row persists even though valid stayed 0.
                self.assertTrue(self._has_origin(self.storage_file, self.origin))
            finally:
                self._execute_sql("DROP TRIGGER fail_cache_completion;")

        assert self.initStorage()
        assert self.initTemporaryStorage()
        with self._stopped_browser():
            self._assert_cache_complete()

    def test_failed_commit_leaves_cache_incomplete(self):
        self._assert_transaction_failure(64)

    def test_new_origin_keeps_cache_complete(self):
        new_origin = "https://new-origin.example.com"
        with self._stopped_browser():
            self._assert_cache_complete()

        assert self.initStorage()
        assert self.initTemporaryStorage()
        assert self.initTemporaryOrigin("default", new_origin)

        with self._stopped_browser(in_app=False):
            self._assert_cache_complete()
            self.assertTrue(self._has_origin(self.storage_file, new_origin))

    def test_new_origin_row_failure_recovers_on_access(self):
        new_origin = "https://new-origin.example.com"
        with self._stopped_browser():
            self._assert_cache_complete()

        assert self.initStorage()
        assert self.initTemporaryStorage()
        with self._artificial_failure(4):
            with self.assertRaises(AssertionError):
                self.initTemporaryOrigin("default", new_origin)

        # The directory exists without a row; the cache stays complete and the
        # origin is picked up on first access.
        with self._stopped_browser(in_app=False):
            self._assert_cache_complete()
            self.assertFalse(self._has_origin(self.storage_file, new_origin))

        assert self.initStorage()
        assert self.initTemporaryStorage()
        assert self.initTemporaryOrigin("default", new_origin)
        self.assertEqual(
            self.getFullOriginMetadata("default", new_origin)["originUsage"], 0
        )

        with self._stopped_browser(in_app=False):
            self._assert_cache_complete()
            self.assertTrue(self._has_origin(self.storage_file, new_origin))

    def test_failed_transaction_start_leaves_cache_incomplete(self):
        self._assert_transaction_failure(32)

    def test_failed_rescan_then_clean_shutdown_restores_rows(self):
        with self._artificial_failure(64):
            with self._stopped_browser():
                self._assert_cache_complete()
                self._execute_sql("UPDATE cache SET valid = 0; DELETE FROM origin;")

            assert self.initStorage()
            assert self.initTemporaryStorage()

        # The failure prefs are cleared in memory, so the clean shutdown runs
        # without failures and must not mark the cache complete without
        # writing the rows the failed rescan rolled back.
        with self._stopped_browser():
            self._assert_cache_complete()

    def test_incomplete_cache_preserves_indexeddb(self):
        value = "quota-rescan-value" * 1024
        self.assertEqual(self._indexeddb_value(value), value)
        usage = self.getFullOriginMetadata("default", self.origin)["originUsage"]
        self.assertGreater(usage, 0)
        with self._stopped_browser():
            self._execute_sql("UPDATE cache SET valid = 0; DELETE FROM origin;")

        assert self.initStorage()
        assert self.initTemporaryStorage()
        with self._stopped_browser(in_app=False):
            self._assert_cache_complete()

        assert self.initStorage()
        assert self.initTemporaryStorage()
        assert self.initTemporaryOrigin("default", self.origin)
        # A kill can leave journal files behind, so compare the cached usage
        # with a fresh disk walk instead of the pre-kill value.
        self.assertEqual(
            self.getFullOriginMetadata("default", self.origin)["originUsage"],
            self._disk_usage(self.origin),
        )
        self.assertEqual(self._indexeddb_value(), value)
        with self._stopped_browser():
            self._assert_cache_complete()

    @unittest.skipIf(
        os.environ.get("MOZ_ENABLE_INC_ORIGIN_INIT") == "1",
        "Incremental initialization completes deferred origins in the background",
    )
    def test_lazy_rescan_stays_incomplete(self):
        self._indexeddb_value("value")
        self.marionette.set_pref(LAZY_ORIGIN_INIT_PREF, True)
        self.marionette.set_pref(SECONDARY_CACHE_PREF, False)
        with self._stopped_browser():
            self._execute_sql("UPDATE cache SET valid = 0; DELETE FROM origin;")

        assert self.initStorage()
        assert self.initTemporaryStorage()
        self.marionette.set_pref(LAZY_ORIGIN_INIT_PREF, False)
        with self._stopped_browser():
            self.assertEqual(self._cache_state()[0], 0)

        assert self.initStorage()
        assert self.initTemporaryStorage()
        assert self.initTemporaryOrigin("default", self.origin)
        self.assertEqual(self._indexeddb_value(), "value")
        with self._stopped_browser():
            self._assert_cache_complete()

    def test_complete_cache_does_not_rescan(self):
        with self._stopped_browser():
            self._assert_cache_complete()

        self.marionette.set_pref(SECONDARY_CACHE_PREF, False)
        with self._artificial_failure(1):
            assert self.initStorage()
            assert self.initTemporaryStorage()
        with self._stopped_browser():
            self._assert_cache_complete()

    def test_upgrade_rescans_version_4_cache(self):
        for valid in (0, 1):
            with self.subTest(valid=valid):
                with self._stopped_browser():
                    self._assert_cache_complete()
                    self._execute_sql(
                        f"UPDATE database SET cache_version = 4; "
                        f"UPDATE cache SET valid = {valid}; DELETE FROM origin;"
                    )

                assert self.initStorage()
                assert self.initTemporaryStorage()

                with self._stopped_browser():
                    self._assert_cache_complete()
                    with closing(sqlite3.connect(self.storage_file)) as connection:
                        version = connection.execute(
                            "SELECT cache_version FROM database"
                        ).fetchone()[0]
                    self.assertEqual(version, 5)

    def test_upgrade_version_3_preserves_indexeddb(self):
        self.assertEqual(self._indexeddb_value("version-3-data"), "version-3-data")
        for valid in (0, 1):
            with self.subTest(valid=valid):
                with self._stopped_browser():
                    self._execute_sql(
                        f"""
                        ALTER TABLE origin ADD COLUMN persisted INTEGER NOT NULL DEFAULT 0;
                        UPDATE origin SET persisted = (metadata_flags >> 3) & 1;
                        UPDATE origin SET metadata_flags = (metadata_flags >> 1) & 1;
                        ALTER TABLE origin RENAME COLUMN metadata_flags TO accessed;
                        UPDATE database SET cache_version = 3;
                        UPDATE cache SET valid = {valid};
                        """
                    )
                assert self.initStorage()
                assert self.initTemporaryStorage()
                self.assertEqual(self._indexeddb_value(), "version-3-data")
                with self._stopped_browser():
                    self._assert_cache_complete()

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
        for pref in (
            CLEAR_EMPTY_ORIGINS_PREF,
            LAZY_ORIGIN_INIT_PREF,
            FLUSH_INTERVAL_PREF,
            SECONDARY_CACHE_PREF,
            *FAILURE_PREFS,
        ):
            self.marionette.clear_pref(pref)
        self.marionette.clear_pref(CACHE_CHECK_BUILD_ID_PREF)
        self.marionette.clear_pref(QM_TESTING_PREF)
        self.marionette.restart(in_app=True)
        super().tearDown()

    def _assert_transaction_failure(self, category):
        # Prefs only persist across a clean quit, and temporary storage may be
        # initialized by the browser itself right after startup, so each
        # startup under test is preceded by a clean quit with the prefs it
        # should see.
        with self._artificial_failure(category):
            with self._stopped_browser():
                self._assert_cache_complete()
                self._execute_sql("UPDATE cache SET valid = 0; DELETE FROM origin;")

            assert self.initStorage()
            assert self.initTemporaryStorage()
            with self._stopped_browser(in_app=False):
                self.assertEqual(self._cache_state()[0], 0)
                self.assertFalse(self._has_origin(self.storage_file, self.origin))

        with self._stopped_browser():
            self._execute_sql("UPDATE cache SET valid = 0; DELETE FROM origin;")

        assert self.initStorage()
        assert self.initTemporaryStorage()
        with self._stopped_browser(in_app=False):
            self._assert_cache_complete()

    @contextmanager
    def _artificial_failure(self, category):
        self.marionette.set_prefs({
            **FAILURE_PREFS,
            "dom.quotaManager.artificialFailure.categories": category,
        })
        try:
            yield
        finally:
            for pref in FAILURE_PREFS:
                self.marionette.clear_pref(pref)

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

    def _cached_origins(self):
        with closing(sqlite3.connect(self.storage_file)) as connection:
            return {row[0] for row in connection.execute("SELECT origin FROM origin")}

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

    def _row_usage(self, origin):
        with closing(sqlite3.connect(self.storage_file)) as connection:
            return connection.execute(
                "SELECT usage FROM origin WHERE origin = ?", (origin,)
            ).fetchone()[0]

    def _indexeddb_value(self, value=None):
        with self.marionette.using_context(self.marionette.CONTEXT_CHROME):
            return self.executeAsyncScript(
                """
                async function main() {
                  const [origin, value] = argumentsFromPython;
                  const principal = Services.scriptSecurityManager
                    .createContentPrincipalFromOrigin(origin);
                  const request = indexedDB.openForPrincipal(principal, "cache-rescan", 1);
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

    def _cache_state(self):
        with closing(sqlite3.connect(self.storage_file)) as connection:
            return connection.execute("SELECT valid, build_id FROM cache").fetchone()

    def _assert_cache_complete(self):
        self.assertEqual(self._cache_state()[0], 1)
        self._assert_cache_has_origin()

    def _assert_cache_has_origin(self):
        self.assertTrue(
            self._has_origin(self.storage_file, self.origin),
            "The quota cache should contain the origin after a successful rescan",
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
