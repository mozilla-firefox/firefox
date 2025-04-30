from sentry_sdk.hub import Hub, init
from sentry_sdk.scope import Scope
from sentry_sdk.transport import Transport, HttpTransport
from sentry_sdk.client import Client

from sentry_sdk.api import *  # noqa

from sentry_sdk.consts import VERSION  # noqa

__all__ = [  # noqa
    "Hub",
    "Scope",
    "Client",
    "Transport",
    "HttpTransport",
    "init",
    "integrations",
    # From sentry_sdk.api
    "capture_event",
    "capture_message",
    "capture_exception",
    "add_breadcrumb",
    "configure_scope",
    "push_scope",
    "flush",
    "last_event_id",
    "start_span",
    "start_transaction",
    "set_tag",
    "set_context",
    "set_extra",
    "set_user",
    "set_level",
]

# Initialize the debug support after everything is loaded
from sentry_sdk.debug import init_debug_support

init_debug_support()
del init_debug_support
