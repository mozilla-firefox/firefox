# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import signal
import threading
from contextlib import contextmanager


class TerminationRequested(KeyboardInterrupt):
    """Raised when the process receives SIGTERM. Being a KeyboardInterrupt,
    harnesses stop and clean up as they do when interrupted with Ctrl+C."""


@contextmanager
def handle_termination(logger, message="Received SIGTERM"):
    """Turn the first SIGTERM received in this context into a
    TerminationRequested exception, rather than letting it kill the process.

    When the context exits after a SIGTERM, `message` is logged with `logger`
    at the critical level, and the KeyboardInterrupt is suppressed so that the
    caller can still clean up. Later SIGTERMs are ignored.

    The handler is only installed when called from the main thread.
    """
    if threading.current_thread() is not threading.main_thread():
        yield
        return

    received = False

    def handler(signum, frame):
        nonlocal received
        if received:
            return
        received = True
        raise TerminationRequested()

    previous = signal.signal(signal.SIGTERM, handler)
    try:
        yield
    except KeyboardInterrupt:
        if not received:
            raise
    finally:
        if received:
            logger.critical(message)
        signal.signal(signal.SIGTERM, previous)
