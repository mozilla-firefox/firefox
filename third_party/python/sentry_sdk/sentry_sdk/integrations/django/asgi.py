"""
Instrumentation for Django 3.0

Since this file contains `async def` it is conditionally imported in
`sentry_sdk.integrations.django` (depending on the existence of
`django.core.handlers.asgi`.
"""

import asyncio
import threading

from sentry_sdk import Hub, _functools
from sentry_sdk._types import MYPY
from sentry_sdk.consts import OP

from sentry_sdk.integrations.asgi import SentryAsgiMiddleware

if MYPY:
    from typing import Any
    from typing import Union
    from typing import Callable

    from django.http.response import HttpResponse


def patch_django_asgi_handler_impl(cls):
    # type: (Any) -> None

    from sentry_sdk.integrations.django import DjangoIntegration

    old_app = cls.__call__

    async def sentry_patched_asgi_handler(self, scope, receive, send):
        # type: (Any, Any, Any, Any) -> Any
        if Hub.current.get_integration(DjangoIntegration) is None:
            return await old_app(self, scope, receive, send)

        middleware = SentryAsgiMiddleware(
            old_app.__get__(self, cls), unsafe_context_data=True
        )._run_asgi3
        return await middleware(scope, receive, send)

    cls.__call__ = sentry_patched_asgi_handler


def patch_get_response_async(cls, _before_get_response):
    # type: (Any, Any) -> None
    old_get_response_async = cls.get_response_async

    async def sentry_patched_get_response_async(self, request):
        # type: (Any, Any) -> Union[HttpResponse, BaseException]
        _before_get_response(request)
        return await old_get_response_async(self, request)

    cls.get_response_async = sentry_patched_get_response_async


def patch_channels_asgi_handler_impl(cls):
    # type: (Any) -> None

    import channels  # type: ignore
    from sentry_sdk.integrations.django import DjangoIntegration

    if channels.__version__ < "3.0.0":

        old_app = cls.__call__

        async def sentry_patched_asgi_handler(self, receive, send):
            # type: (Any, Any, Any) -> Any
            if Hub.current.get_integration(DjangoIntegration) is None:
                return await old_app(self, receive, send)

            middleware = SentryAsgiMiddleware(
                lambda _scope: old_app.__get__(self, cls), unsafe_context_data=True
            )

            return await middleware(self.scope)(receive, send)

        cls.__call__ = sentry_patched_asgi_handler

    else:
        # The ASGI handler in Channels >= 3 has the same signature as
        # the Django handler.
        patch_django_asgi_handler_impl(cls)


def wrap_async_view(hub, callback):
    # type: (Hub, Any) -> Any
    @_functools.wraps(callback)
    async def sentry_wrapped_callback(request, *args, **kwargs):
        # type: (Any, *Any, **Any) -> Any

        with hub.configure_scope() as sentry_scope:
            if sentry_scope.profile is not None:
                sentry_scope.profile.active_thread_id = threading.current_thread().ident

            with hub.start_span(
                op=OP.VIEW_RENDER, description=request.resolver_match.view_name
            ):
                return await callback(request, *args, **kwargs)

    return sentry_wrapped_callback


def _asgi_middleware_mixin_factory(_check_middleware_span):
    # type: (Callable[..., Any]) -> Any
    """
    Mixin class factory that generates a middleware mixin for handling requests
    in async mode.
    """

    class SentryASGIMixin:
        if MYPY:
            _inner = None

        def __init__(self, get_response):
            # type: (Callable[..., Any]) -> None
            self.get_response = get_response
            self._acall_method = None
            self._async_check()

        def _async_check(self):
            # type: () -> None
            """
            If get_response is a coroutine function, turns us into async mode so
            a thread is not consumed during a whole request.
            Taken from django.utils.deprecation::MiddlewareMixin._async_check
            """
            if asyncio.iscoroutinefunction(self.get_response):
                self._is_coroutine = asyncio.coroutines._is_coroutine  # type: ignore

        def async_route_check(self):
            # type: () -> bool
            """
            Function that checks if we are in async mode,
            and if we are forwards the handling of requests to __acall__
            """
            return asyncio.iscoroutinefunction(self.get_response)

        async def __acall__(self, *args, **kwargs):
            # type: (*Any, **Any) -> Any
            f = self._acall_method
            if f is None:
                if hasattr(self._inner, "__acall__"):
                    self._acall_method = f = self._inner.__acall__  # type: ignore
                else:
                    self._acall_method = f = self._inner

            middleware_span = _check_middleware_span(old_method=f)

            if middleware_span is None:
                return await f(*args, **kwargs)

            with middleware_span:
                return await f(*args, **kwargs)

    return SentryASGIMixin
