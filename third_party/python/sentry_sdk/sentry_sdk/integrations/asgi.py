"""
An ASGI middleware.

Based on Tom Christie's `sentry-asgi <https://github.com/encode/sentry-asgi>`.
"""

import asyncio
import inspect
import urllib

from sentry_sdk._functools import partial
from sentry_sdk._types import MYPY
from sentry_sdk.consts import OP
from sentry_sdk.hub import Hub, _should_send_default_pii
from sentry_sdk.integrations._wsgi_common import _filter_headers
from sentry_sdk.integrations.modules import _get_installed_modules
from sentry_sdk.profiler import start_profiling
from sentry_sdk.sessions import auto_session_tracking
from sentry_sdk.tracing import (
    SOURCE_FOR_STYLE,
    TRANSACTION_SOURCE_ROUTE,
)
from sentry_sdk.utils import (
    ContextVar,
    event_from_exception,
    HAS_REAL_CONTEXTVARS,
    CONTEXTVARS_ERROR_MESSAGE,
    logger,
    transaction_from_function,
)
from sentry_sdk.tracing import Transaction

if MYPY:
    from typing import Dict
    from typing import Any
    from typing import Optional
    from typing import Callable

    from typing_extensions import Literal

    from sentry_sdk._types import Event, Hint


_asgi_middleware_applied = ContextVar("sentry_asgi_middleware_applied")

_DEFAULT_TRANSACTION_NAME = "generic ASGI request"

TRANSACTION_STYLE_VALUES = ("endpoint", "url")


def _capture_exception(hub, exc, mechanism_type="asgi"):
    # type: (Hub, Any, str) -> None

    # Check client here as it might have been unset while streaming response
    if hub.client is not None:
        event, hint = event_from_exception(
            exc,
            client_options=hub.client.options,
            mechanism={"type": mechanism_type, "handled": False},
        )
        hub.capture_event(event, hint=hint)


def _looks_like_asgi3(app):
    # type: (Any) -> bool
    """
    Try to figure out if an application object supports ASGI3.

    This is how uvicorn figures out the application version as well.
    """
    if inspect.isclass(app):
        return hasattr(app, "__await__")
    elif inspect.isfunction(app):
        return asyncio.iscoroutinefunction(app)
    else:
        call = getattr(app, "__call__", None)  # noqa
        return asyncio.iscoroutinefunction(call)


class SentryAsgiMiddleware:
    __slots__ = ("app", "__call__", "transaction_style", "mechanism_type")

    def __init__(
        self,
        app,
        unsafe_context_data=False,
        transaction_style="endpoint",
        mechanism_type="asgi",
    ):
        # type: (Any, bool, str, str) -> None
        """
        Instrument an ASGI application with Sentry. Provides HTTP/websocket
        data to sent events and basic handling for exceptions bubbling up
        through the middleware.

        :param unsafe_context_data: Disable errors when a proper contextvars installation could not be found. We do not recommend changing this from the default.
        """
        if not unsafe_context_data and not HAS_REAL_CONTEXTVARS:
            # We better have contextvars or we're going to leak state between
            # requests.
            raise RuntimeError(
                "The ASGI middleware for Sentry requires Python 3.7+ "
                "or the aiocontextvars package." + CONTEXTVARS_ERROR_MESSAGE
            )
        if transaction_style not in TRANSACTION_STYLE_VALUES:
            raise ValueError(
                "Invalid value for transaction_style: %s (must be in %s)"
                % (transaction_style, TRANSACTION_STYLE_VALUES)
            )

        asgi_middleware_while_using_starlette_or_fastapi = (
            mechanism_type == "asgi" and "starlette" in _get_installed_modules()
        )
        if asgi_middleware_while_using_starlette_or_fastapi:
            logger.warning(
                "The Sentry Python SDK can now automatically support ASGI frameworks like Starlette and FastAPI. "
                "Please remove 'SentryAsgiMiddleware' from your project. "
                "See https://docs.sentry.io/platforms/python/guides/asgi/ for more information."
            )

        self.transaction_style = transaction_style
        self.mechanism_type = mechanism_type
        self.app = app

        if _looks_like_asgi3(app):
            self.__call__ = self._run_asgi3  # type: Callable[..., Any]
        else:
            self.__call__ = self._run_asgi2

    def _run_asgi2(self, scope):
        # type: (Any) -> Any
        async def inner(receive, send):
            # type: (Any, Any) -> Any
            return await self._run_app(scope, lambda: self.app(scope)(receive, send))

        return inner

    async def _run_asgi3(self, scope, receive, send):
        # type: (Any, Any, Any) -> Any
        return await self._run_app(scope, lambda: self.app(scope, receive, send))

    async def _run_app(self, scope, callback):
        # type: (Any, Any) -> Any
        is_recursive_asgi_middleware = _asgi_middleware_applied.get(False)
        if is_recursive_asgi_middleware:
            try:
                return await callback()
            except Exception as exc:
                _capture_exception(Hub.current, exc, mechanism_type=self.mechanism_type)
                raise exc from None

        _asgi_middleware_applied.set(True)
        try:
            hub = Hub(Hub.current)
            with auto_session_tracking(hub, session_mode="request"):
                with hub:
                    with hub.configure_scope() as sentry_scope:
                        sentry_scope.clear_breadcrumbs()
                        sentry_scope._name = "asgi"
                        processor = partial(self.event_processor, asgi_scope=scope)
                        sentry_scope.add_event_processor(processor)

                    ty = scope["type"]

                    if ty in ("http", "websocket"):
                        transaction = Transaction.continue_from_headers(
                            self._get_headers(scope),
                            op="{}.server".format(ty),
                        )
                    else:
                        transaction = Transaction(op=OP.HTTP_SERVER)

                    transaction.name = _DEFAULT_TRANSACTION_NAME
                    transaction.source = TRANSACTION_SOURCE_ROUTE
                    transaction.set_tag("asgi.type", ty)

                    with hub.start_transaction(
                        transaction, custom_sampling_context={"asgi_scope": scope}
                    ), start_profiling(transaction, hub):
                        # XXX: Would be cool to have correct span status, but we
                        # would have to wrap send(). That is a bit hard to do with
                        # the current abstraction over ASGI 2/3.
                        try:
                            return await callback()
                        except Exception as exc:
                            _capture_exception(
                                hub, exc, mechanism_type=self.mechanism_type
                            )
                            raise exc from None
        finally:
            _asgi_middleware_applied.set(False)

    def event_processor(self, event, hint, asgi_scope):
        # type: (Event, Hint, Any) -> Optional[Event]
        request_info = event.get("request", {})

        ty = asgi_scope["type"]
        if ty in ("http", "websocket"):
            request_info["method"] = asgi_scope.get("method")
            request_info["headers"] = headers = _filter_headers(
                self._get_headers(asgi_scope)
            )
            request_info["query_string"] = self._get_query(asgi_scope)

            request_info["url"] = self._get_url(
                asgi_scope, "http" if ty == "http" else "ws", headers.get("host")
            )

        client = asgi_scope.get("client")
        if client and _should_send_default_pii():
            request_info["env"] = {"REMOTE_ADDR": self._get_ip(asgi_scope)}

        self._set_transaction_name_and_source(event, self.transaction_style, asgi_scope)

        event["request"] = request_info

        return event

    # Helper functions for extracting request data.
    #
    # Note: Those functions are not public API. If you want to mutate request
    # data to your liking it's recommended to use the `before_send` callback
    # for that.

    def _set_transaction_name_and_source(self, event, transaction_style, asgi_scope):
        # type: (Event, str, Any) -> None
        transaction_name_already_set = (
            event.get("transaction", _DEFAULT_TRANSACTION_NAME)
            != _DEFAULT_TRANSACTION_NAME
        )
        if transaction_name_already_set:
            return

        name = ""

        if transaction_style == "endpoint":
            endpoint = asgi_scope.get("endpoint")
            # Webframeworks like Starlette mutate the ASGI env once routing is
            # done, which is sometime after the request has started. If we have
            # an endpoint, overwrite our generic transaction name.
            if endpoint:
                name = transaction_from_function(endpoint) or ""

        elif transaction_style == "url":
            # FastAPI includes the route object in the scope to let Sentry extract the
            # path from it for the transaction name
            route = asgi_scope.get("route")
            if route:
                path = getattr(route, "path", None)
                if path is not None:
                    name = path

        if not name:
            event["transaction"] = _DEFAULT_TRANSACTION_NAME
            event["transaction_info"] = {"source": TRANSACTION_SOURCE_ROUTE}
            return

        event["transaction"] = name
        event["transaction_info"] = {"source": SOURCE_FOR_STYLE[transaction_style]}

    def _get_url(self, scope, default_scheme, host):
        # type: (Dict[str, Any], Literal["ws", "http"], Optional[str]) -> str
        """
        Extract URL from the ASGI scope, without also including the querystring.
        """
        scheme = scope.get("scheme", default_scheme)

        server = scope.get("server", None)
        path = scope.get("root_path", "") + scope.get("path", "")

        if host:
            return "%s://%s%s" % (scheme, host, path)

        if server is not None:
            host, port = server
            default_port = {"http": 80, "https": 443, "ws": 80, "wss": 443}[scheme]
            if port != default_port:
                return "%s://%s:%s%s" % (scheme, host, port, path)
            return "%s://%s%s" % (scheme, host, path)
        return path

    def _get_query(self, scope):
        # type: (Any) -> Any
        """
        Extract querystring from the ASGI scope, in the format that the Sentry protocol expects.
        """
        qs = scope.get("query_string")
        if not qs:
            return None
        return urllib.parse.unquote(qs.decode("latin-1"))

    def _get_ip(self, scope):
        # type: (Any) -> str
        """
        Extract IP Address from the ASGI scope based on request headers with fallback to scope client.
        """
        headers = self._get_headers(scope)
        try:
            return headers["x-forwarded-for"].split(",")[0].strip()
        except (KeyError, IndexError):
            pass

        try:
            return headers["x-real-ip"]
        except KeyError:
            pass

        return scope.get("client")[0]

    def _get_headers(self, scope):
        # type: (Any) -> Dict[str, str]
        """
        Extract headers from the ASGI scope, in the format that the Sentry protocol expects.
        """
        headers = {}  # type: Dict[str, str]
        for raw_key, raw_value in scope["headers"]:
            key = raw_key.decode("latin-1")
            value = raw_value.decode("latin-1")
            if key in headers:
                headers[key] = headers[key] + ", " + value
            else:
                headers[key] = value
        return headers
