# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import sqlite3
import time


class SSLTokensCacheMixin:
    """Helpers for inspecting ssl_tokens_cache.sqlite in a test profile."""

    def _query(self, cache_file, sql):
        """Rows of sql, or None if the file/table isn't there yet or is locked."""
        if not cache_file.exists():
            return None
        try:
            conn = sqlite3.connect(str(cache_file))
        except sqlite3.Error:
            return None
        try:
            return conn.execute(sql).fetchall()
        except sqlite3.Error:
            return None
        finally:
            conn.close()

    def cache_row_count(self, cache_file):
        """Row count, or None if the file/table isn't there yet or is locked.
        The DB file is created as soon as persistence activates, so existence
        alone doesn't prove a token was written."""
        rows = self._query(cache_file, "SELECT COUNT(*) FROM ssl_tokens")
        return None if rows is None else rows[0][0]

    def cache_keys(self, cache_file):
        """Sorted row keys, or None like cache_row_count."""
        rows = self._query(cache_file, "SELECT key FROM ssl_tokens ORDER BY key")
        return None if rows is None else [row[0] for row in rows]

    def wait_for_cache_file_removed(self, cache_file, timeout=5):
        """Polls until the cache file is gone; returns whether it is."""
        deadline = time.monotonic() + timeout
        while cache_file.exists() and time.monotonic() < deadline:
            time.sleep(0.25)
        return not cache_file.exists()

    def _wait_for(self, read, done, initial, timeout):
        """Polls read() until done() accepts its value or times out, returning
        the last non-None value seen, or initial."""
        deadline = time.monotonic() + timeout
        last = initial
        while time.monotonic() < deadline:
            value = read()
            if value is not None:
                last = value
                if done(value):
                    return value
            time.sleep(0.25)
        return last

    def wait_for_cache_keys(self, cache_file, expected, timeout=5):
        """Polls the row keys until they equal expected or times out,
        returning the last keys seen."""
        return self._wait_for(
            lambda: self.cache_keys(cache_file),
            lambda keys: keys == expected,
            None,
            timeout,
        )

    def wait_for_cache_rows(self, cache_file, min_count=1, timeout=5):
        """Polls the row count until it reaches min_count or times out,
        returning the last count seen."""
        return self._wait_for(
            lambda: self.cache_row_count(cache_file),
            lambda count: count >= min_count,
            0,
            timeout,
        )
