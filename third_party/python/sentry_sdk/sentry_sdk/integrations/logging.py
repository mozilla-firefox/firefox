from __future__ import absolute_import

import logging
import datetime
from fnmatch import fnmatch

from sentry_sdk.hub import Hub
from sentry_sdk.utils import (
    to_string,
    event_from_exception,
    current_stacktrace,
    capture_internal_exceptions,
)
from sentry_sdk.integrations import Integration
from sentry_sdk._compat import iteritems

from sentry_sdk._types import MYPY

if MYPY:
    from logging import LogRecord
    from typing import Any
    from typing import Dict
    from typing import Optional

DEFAULT_LEVEL = logging.INFO
DEFAULT_EVENT_LEVEL = logging.ERROR
LOGGING_TO_EVENT_LEVEL = {
    logging.NOTSET: "notset",
    logging.DEBUG: "debug",
    logging.INFO: "info",
    logging.WARN: "warning",  # WARN is same a WARNING
    logging.WARNING: "warning",
    logging.ERROR: "error",
    logging.FATAL: "fatal",
    logging.CRITICAL: "fatal",  # CRITICAL is same as FATAL
}

# Capturing events from those loggers causes recursion errors. We cannot allow
# the user to unconditionally create events from those loggers under any
# circumstances.
#
# Note: Ignoring by logger name here is better than mucking with thread-locals.
# We do not necessarily know whether thread-locals work 100% correctly in the user's environment.
_IGNORED_LOGGERS = set(
    ["sentry_sdk.errors", "urllib3.connectionpool", "urllib3.connection"]
)


def ignore_logger(
    name,  # type: str
):
    # type: (...) -> None
    """This disables recording (both in breadcrumbs and as events) calls to
    a logger of a specific name.  Among other uses, many of our integrations
    use this to prevent their actions being recorded as breadcrumbs. Exposed
    to users as a way to quiet spammy loggers.

    :param name: The name of the logger to ignore (same string you would pass to ``logging.getLogger``).
    """
    _IGNORED_LOGGERS.add(name)


class LoggingIntegration(Integration):
    identifier = "logging"

    def __init__(self, level=DEFAULT_LEVEL, event_level=DEFAULT_EVENT_LEVEL):
        # type: (Optional[int], Optional[int]) -> None
        self._handler = None
        self._breadcrumb_handler = None

        if level is not None:
            self._breadcrumb_handler = BreadcrumbHandler(level=level)

        if event_level is not None:
            self._handler = EventHandler(level=event_level)

    def _handle_record(self, record):
        # type: (LogRecord) -> None
        if self._handler is not None and record.levelno >= self._handler.level:
            self._handler.handle(record)

        if (
            self._breadcrumb_handler is not None
            and record.levelno >= self._breadcrumb_handler.level
        ):
            self._breadcrumb_handler.handle(record)

    @staticmethod
    def setup_once():
        # type: () -> None
        old_callhandlers = logging.Logger.callHandlers

        def sentry_patched_callhandlers(self, record):
            # type: (Any, LogRecord) -> Any
            try:
                return old_callhandlers(self, record)
            finally:
                # This check is done twice, once also here before we even get
                # the integration.  Otherwise we have a high chance of getting
                # into a recursion error when the integration is resolved
                # (this also is slower).
                if record.name not in _IGNORED_LOGGERS:
                    integration = Hub.current.get_integration(LoggingIntegration)
                    if integration is not None:
                        integration._handle_record(record)

        logging.Logger.callHandlers = sentry_patched_callhandlers  # type: ignore


def _can_record(record):
    # type: (LogRecord) -> bool
    """Prevents ignored loggers from recording"""
    for logger in _IGNORED_LOGGERS:
        if fnmatch(record.name, logger):
            return False
    return True


def _breadcrumb_from_record(record):
    # type: (LogRecord) -> Dict[str, Any]
    return {
        "type": "log",
        "level": _logging_to_event_level(record),
        "category": record.name,
        "message": record.message,
        "timestamp": datetime.datetime.utcfromtimestamp(record.created),
        "data": _extra_from_record(record),
    }


def _logging_to_event_level(record):
    # type: (LogRecord) -> str
    return LOGGING_TO_EVENT_LEVEL.get(
        record.levelno, record.levelname.lower() if record.levelname else ""
    )


COMMON_RECORD_ATTRS = frozenset(
    (
        "args",
        "created",
        "exc_info",
        "exc_text",
        "filename",
        "funcName",
        "levelname",
        "levelno",
        "linenno",
        "lineno",
        "message",
        "module",
        "msecs",
        "msg",
        "name",
        "pathname",
        "process",
        "processName",
        "relativeCreated",
        "stack",
        "tags",
        "thread",
        "threadName",
        "stack_info",
    )
)


def _extra_from_record(record):
    # type: (LogRecord) -> Dict[str, None]
    return {
        k: v
        for k, v in iteritems(vars(record))
        if k not in COMMON_RECORD_ATTRS
        and (not isinstance(k, str) or not k.startswith("_"))
    }


class EventHandler(logging.Handler, object):
    """
    A logging handler that emits Sentry events for each log record

    Note that you do not have to use this class if the logging integration is enabled, which it is by default.
    """

    def emit(self, record):
        # type: (LogRecord) -> Any
        with capture_internal_exceptions():
            self.format(record)
            return self._emit(record)

    def _emit(self, record):
        # type: (LogRecord) -> None
        if not _can_record(record):
            return

        hub = Hub.current
        if hub.client is None:
            return

        client_options = hub.client.options

        # exc_info might be None or (None, None, None)
        #
        # exc_info may also be any falsy value due to Python stdlib being
        # liberal with what it receives and Celery's billiard being "liberal"
        # with what it sends. See
        # https://github.com/getsentry/sentry-python/issues/904
        if record.exc_info and record.exc_info[0] is not None:
            event, hint = event_from_exception(
                record.exc_info,
                client_options=client_options,
                mechanism={"type": "logging", "handled": True},
            )
        elif record.exc_info and record.exc_info[0] is None:
            event = {}
            hint = {}
            with capture_internal_exceptions():
                event["threads"] = {
                    "values": [
                        {
                            "stacktrace": current_stacktrace(
                                client_options["with_locals"]
                            ),
                            "crashed": False,
                            "current": True,
                        }
                    ]
                }
        else:
            event = {}
            hint = {}

        hint["log_record"] = record

        event["level"] = _logging_to_event_level(record)
        event["logger"] = record.name

        # Log records from `warnings` module as separate issues
        record_caputured_from_warnings_module = (
            record.name == "py.warnings" and record.msg == "%s"
        )
        if record_caputured_from_warnings_module:
            # use the actual message and not "%s" as the message
            # this prevents grouping all warnings under one "%s" issue
            msg = record.args[0]  # type: ignore

            event["logentry"] = {
                "message": msg,
                "params": (),
            }

        else:
            event["logentry"] = {
                "message": to_string(record.msg),
                "params": record.args,
            }

        event["extra"] = _extra_from_record(record)

        hub.capture_event(event, hint=hint)


# Legacy name
SentryHandler = EventHandler


class BreadcrumbHandler(logging.Handler, object):
    """
    A logging handler that records breadcrumbs for each log record.

    Note that you do not have to use this class if the logging integration is enabled, which it is by default.
    """

    def emit(self, record):
        # type: (LogRecord) -> Any
        with capture_internal_exceptions():
            self.format(record)
            return self._emit(record)

    def _emit(self, record):
        # type: (LogRecord) -> None
        if not _can_record(record):
            return

        Hub.current.add_breadcrumb(
            _breadcrumb_from_record(record), hint={"log_record": record}
        )
