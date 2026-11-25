# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import sqlite3
import time


class SSLTokensCacheMixin:
    """Helpers for inspecting ssl_tokens_cache.sqlite in a test profile."""

    def cache_row_count(self, cache_file):
        """Row count, or None if the file/table isn't there yet or is locked.
        The DB file is created as soon as persistence activates, so existence
        alone doesn't prove a token was written."""
        if not cache_file.exists():
            return None
        try:
            conn = sqlite3.connect(str(cache_file))
        except sqlite3.Error:
            return None
        try:
            return conn.execute("SELECT COUNT(*) FROM ssl_tokens").fetchone()[0]
        except sqlite3.Error:
            return None
        finally:
            conn.close()

    def wait_for_cache_rows(self, cache_file, min_count=1, timeout=5):
        """Polls the row count until it reaches min_count or times out,
        returning the last count seen."""
        deadline = time.monotonic() + timeout
        last_count = 0
        while time.monotonic() < deadline:
            count = self.cache_row_count(cache_file)
            if count is not None:
                last_count = count
                if count >= min_count:
                    return count
            time.sleep(0.25)
        return last_count
