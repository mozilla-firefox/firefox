# -*- coding: utf-8 -*-
from __future__ import absolute_import

from django.dispatch import Signal

from sentry_sdk import Hub
from sentry_sdk._functools import wraps
from sentry_sdk._types import MYPY
from sentry_sdk.consts import OP


if MYPY:
    from typing import Any
    from typing import Callable
    from typing import List


def _get_receiver_name(receiver):
    # type: (Callable[..., Any]) -> str
    name = ""

    if hasattr(receiver, "__qualname__"):
        name = receiver.__qualname__
    elif hasattr(receiver, "__name__"):  # Python 2.7 has no __qualname__
        name = receiver.__name__
    elif hasattr(
        receiver, "func"
    ):  # certain functions (like partials) dont have a name
        if hasattr(receiver, "func") and hasattr(receiver.func, "__name__"):  # type: ignore
            name = "partial(<function " + receiver.func.__name__ + ">)"  # type: ignore

    if (
        name == ""
    ):  # In case nothing was found, return the string representation (this is the slowest case)
        return str(receiver)

    if hasattr(receiver, "__module__"):  # prepend with module, if there is one
        name = receiver.__module__ + "." + name

    return name


def patch_signals():
    # type: () -> None
    """Patch django signal receivers to create a span"""

    old_live_receivers = Signal._live_receivers

    def _sentry_live_receivers(self, sender):
        # type: (Signal, Any) -> List[Callable[..., Any]]
        hub = Hub.current
        receivers = old_live_receivers(self, sender)

        def sentry_receiver_wrapper(receiver):
            # type: (Callable[..., Any]) -> Callable[..., Any]
            @wraps(receiver)
            def wrapper(*args, **kwargs):
                # type: (Any, Any) -> Any
                signal_name = _get_receiver_name(receiver)
                with hub.start_span(
                    op=OP.EVENT_DJANGO,
                    description=signal_name,
                ) as span:
                    span.set_data("signal", signal_name)
                    return receiver(*args, **kwargs)

            return wrapper

        for idx, receiver in enumerate(receivers):
            receivers[idx] = sentry_receiver_wrapper(receiver)

        return receivers

    Signal._live_receivers = _sentry_live_receivers
