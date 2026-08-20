# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import asyncio
import os
import random

from arsenic.errors import UnknownArsenicError, UnknownError

from condprof.helpers import TabSwitcher, execute_async_script, is_mobile
from condprof.util import get_credentials, logger

BOOKMARK_FREQUENCY = 5
MAX_URLS = 150
MAX_BOOKMARKS = 250
CallErrors = asyncio.TimeoutError, UnknownError, UnknownArsenicError


class Builder:
    def __init__(self, options):
        self.options = options
        self.words = self._read_lines("words.txt")
        self.urls = self._build_url_list(self._read_lines("urls.txt"))
        self.sync_js = self._load_script("sync")
        self.max_bookmarks = options.get("max_bookmarks", MAX_BOOKMARKS)
        self.bookmark_js = self._load_script("bookmark")
        self.platform = options.get("platform", "")
        self.mobile = is_mobile(self.platform)
        self.max_urls = options.get("max_urls", MAX_URLS)

        # see Bug 1608604 & see Bug 1619107 - we have stability issues @ bitbar
        if self.mobile:
            self.max_urls = min(self.max_urls, 20)

        logger.info("platform: %s" % self.platform)
        logger.info("max_urls: %s" % self.max_urls)
        self.bookmark_frequency = options.get("bookmark_frequency", BOOKMARK_FREQUENCY)

        # Syncing is disabled for all platforms
        self.syncing = False
        if self.syncing:
            self.username, self.password = get_credentials()
            if self.username is None:
                raise ValueError("Sync operations need an FxA username and password")
        else:
            self.username, self.password = None, None

    def _load_script(self, name):
        return "\n".join(self._read_lines("%s.js" % name))

    def _read_lines(self, filename):
        path = os.path.join(os.path.dirname(__file__), filename)
        with open(path) as f:
            return f.readlines()

    def _build_url_list(self, urls):
        url_list = []
        for url in urls:
            url = url.strip()
            if url.startswith("#"):
                continue
            for word in self.words:
                word = word.strip()
                if word.startswith("#"):
                    continue
                url_list.append((url.format(word), word))
        random.shuffle(url_list)
        return url_list

    async def add_bookmark(self, session, url, title):
        logger.info("Adding bookmark to %s" % url)
        return await execute_async_script(
            session, self.bookmark_js, url, title, self.max_bookmarks
        )

    async def sync(self, session, metadata):
        if not self.syncing:
            return
        # now that we've visited all pages, we want to upload to FXSync
        logger.info("Syncing profile to FxSync")
        logger.info("Username is %s, password is %s" % (self.username, self.password))
        script_res = await execute_async_script(
            session,
            self.sync_js,
            self.username,
            self.password,
            "https://accounts.stage.mozaws.net",
        )
        if script_res is None:
            script_res = {}
        metadata["logs"] = script_res.get("logs", {})
        metadata["result"] = script_res.get("result", 0)
        metadata["result_message"] = script_res.get("result_message", "SUCCESS")
        return metadata

    async def _visit_url(self, current, session, url, word):
        await asyncio.wait_for(session.get(url), 5)
        if current % self.bookmark_frequency == 0 and not self.mobile:
            await asyncio.wait_for(self.add_bookmark(session, url, word), 5)

    async def __call__(self, session):
        metadata = {}

        tabs = TabSwitcher(session, self.options)
        await tabs.create_windows()
        visited = 0

        for current, (url, word) in enumerate(self.urls):
            logger.info("%d/%d %s" % (current + 1, self.max_urls, url))
            retries = 0
            while retries < 3:
                try:
                    await self._visit_url(current, session, url, word)
                    visited += 1
                    break
                except CallErrors:
                    await asyncio.sleep(1.0)
                    retries += 1

            if current == self.max_urls - 1:
                break

            # switch to the next tab
            try:
                await asyncio.wait_for(tabs.switch(), 5)
            except CallErrors:
                # if we can't switch, it's ok
                pass

        metadata["visited_url"] = visited
        await self.sync(session, metadata)
        return metadata


async def full(session, options):
    builder = Builder(options)
    return await builder(session)
