# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Helpers to build scenarii"""

from condprof.util import logger

_SUPPORTED_MOBILE_BROWSERS = "fenix", "gecko", "firefox"


def is_mobile(platform):
    return any(mobile in platform for mobile in _SUPPORTED_MOBILE_BROWSERS)


class TabSwitcher:
    """Helper used to create tabs and circulate in them."""

    def __init__(self, session, options):
        self.handles = None
        self.current = 0
        self.session = session
        self._max = options.get("max_urls", 10)
        self.platform = options.get("platform", "")
        self.num_tabs = self._max >= 100 and 100 or self._max
        self._mobile = is_mobile(self.platform)

    async def create_windows(self):
        # on mobile we don't use tabs for now
        # see https://bugzil.la/1559120
        if self._mobile:
            return
        # creating tabs
        for i in range(self.num_tabs):
            # see https://github.com/HDE/arsenic/issues/71
            await self.session._request(
                url="/window/new", method="POST", data={"type": "tab"}
            )

    async def switch(self):
        if self._mobile:
            return
        try:
            if self.handles is None:
                self.handles = await self.session.get_window_handles()
                self.current = 0
        except Exception:
            logger.error("Could not get window handles")
            return

        handle = self.handles[self.current]
        if self.current == len(self.handles) - 1:
            self.current = 0
        else:
            self.current += 1
        try:
            await self.session.switch_to_window(handle)
        except Exception:
            logger.error(f"Could not switch to handle {handle}")


# 10 minutes
_SCRIPT_TIMEOUT = 10 * 60 * 1000


async def execute_async_script(session, script, *args):
    # switch to the right context if needed
    current_context = await session._request(url="/moz/context", method="GET")
    if current_context != "chrome":
        await session._request(
            url="/moz/context", method="POST", data={"context": "chrome"}
        )
        switch_back = True
    else:
        switch_back = False
    await session._request(
        url="/timeouts", method="POST", data={"script": _SCRIPT_TIMEOUT}
    )
    try:
        attempts = 0
        while True:
            try:
                return await session._request(
                    url="/execute/async",
                    method="POST",
                    data={"script": script, "args": list(args)},
                )
            except Exception as e:
                attempts += 1
                logger.error("The script failed.", exc_info=True)
                if attempts > 2:
                    return {
                        "result": 1,
                        "result_message": str(e),
                        "result_exc": e,
                        "logs": {},
                    }
    finally:
        if switch_back:
            await session._request(
                url="/moz/context", method="POST", data={"context": current_context}
            )


async def close_extra_windows(session):
    logger.info("Closing all tabs")
    handles = await session.get_window_handles()
    # we're closing all tabs except the last one
    for handle in handles[:-1]:
        await session.switch_to_window(handle)
        await session._request(url="/window", method="DELETE")
