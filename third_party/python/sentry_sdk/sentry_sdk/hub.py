import copy
import sys

from datetime import datetime
from contextlib import contextmanager

from sentry_sdk._compat import with_metaclass
from sentry_sdk.consts import INSTRUMENTER
from sentry_sdk.scope import Scope
from sentry_sdk.client import Client
from sentry_sdk.tracing import NoOpSpan, Span, Transaction
from sentry_sdk.session import Session
from sentry_sdk.utils import (
    exc_info_from_error,
    event_from_exception,
    logger,
    ContextVar,
)

from sentry_sdk._types import MYPY

if MYPY:
    from typing import Union
    from typing import Any
    from typing import Optional
    from typing import Tuple
    from typing import Dict
    from typing import List
    from typing import Callable
    from typing import Generator
    from typing import Type
    from typing import TypeVar
    from typing import overload
    from typing import ContextManager

    from sentry_sdk.integrations import Integration
    from sentry_sdk._types import (
        Event,
        Hint,
        Breadcrumb,
        BreadcrumbHint,
        ExcInfo,
    )
    from sentry_sdk.consts import ClientConstructor

    T = TypeVar("T")

else:

    def overload(x):
        # type: (T) -> T
        return x


_local = ContextVar("sentry_current_hub")


def _update_scope(base, scope_change, scope_kwargs):
    # type: (Scope, Optional[Any], Dict[str, Any]) -> Scope
    if scope_change and scope_kwargs:
        raise TypeError("cannot provide scope and kwargs")
    if scope_change is not None:
        final_scope = copy.copy(base)
        if callable(scope_change):
            scope_change(final_scope)
        else:
            final_scope.update_from_scope(scope_change)
    elif scope_kwargs:
        final_scope = copy.copy(base)
        final_scope.update_from_kwargs(**scope_kwargs)
    else:
        final_scope = base
    return final_scope


def _should_send_default_pii():
    # type: () -> bool
    client = Hub.current.client
    if not client:
        return False
    return client.options["send_default_pii"]


class _InitGuard(object):
    def __init__(self, client):
        # type: (Client) -> None
        self._client = client

    def __enter__(self):
        # type: () -> _InitGuard
        return self

    def __exit__(self, exc_type, exc_value, tb):
        # type: (Any, Any, Any) -> None
        c = self._client
        if c is not None:
            c.close()


def _check_python_deprecations():
    # type: () -> None
    version = sys.version_info[:2]

    if version == (3, 4) or version == (3, 5):
        logger.warning(
            "sentry-sdk 2.0.0 will drop support for Python %s.",
            "{}.{}".format(*version),
        )
        logger.warning(
            "Please upgrade to the latest version to continue receiving upgrades and bugfixes."
        )


def _init(*args, **kwargs):
    # type: (*Optional[str], **Any) -> ContextManager[Any]
    """Initializes the SDK and optionally integrations.

    This takes the same arguments as the client constructor.
    """
    client = Client(*args, **kwargs)  # type: ignore
    Hub.current.bind_client(client)
    _check_python_deprecations()
    rv = _InitGuard(client)
    return rv


from sentry_sdk._types import MYPY

if MYPY:
    # Make mypy, PyCharm and other static analyzers think `init` is a type to
    # have nicer autocompletion for params.
    #
    # Use `ClientConstructor` to define the argument types of `init` and
    # `ContextManager[Any]` to tell static analyzers about the return type.

    class init(ClientConstructor, _InitGuard):  # noqa: N801
        pass

else:
    # Alias `init` for actual usage. Go through the lambda indirection to throw
    # PyCharm off of the weakly typed signature (it would otherwise discover
    # both the weakly typed signature of `_init` and our faked `init` type).

    init = (lambda: _init)()


class HubMeta(type):
    @property
    def current(cls):
        # type: () -> Hub
        """Returns the current instance of the hub."""
        rv = _local.get(None)
        if rv is None:
            rv = Hub(GLOBAL_HUB)
            _local.set(rv)
        return rv

    @property
    def main(cls):
        # type: () -> Hub
        """Returns the main instance of the hub."""
        return GLOBAL_HUB


class _ScopeManager(object):
    def __init__(self, hub):
        # type: (Hub) -> None
        self._hub = hub
        self._original_len = len(hub._stack)
        self._layer = hub._stack[-1]

    def __enter__(self):
        # type: () -> Scope
        scope = self._layer[1]
        assert scope is not None
        return scope

    def __exit__(self, exc_type, exc_value, tb):
        # type: (Any, Any, Any) -> None
        current_len = len(self._hub._stack)
        if current_len < self._original_len:
            logger.error(
                "Scope popped too soon. Popped %s scopes too many.",
                self._original_len - current_len,
            )
            return
        elif current_len > self._original_len:
            logger.warning(
                "Leaked %s scopes: %s",
                current_len - self._original_len,
                self._hub._stack[self._original_len :],
            )

        layer = self._hub._stack[self._original_len - 1]
        del self._hub._stack[self._original_len - 1 :]

        if layer[1] != self._layer[1]:
            logger.error(
                "Wrong scope found. Meant to pop %s, but popped %s.",
                layer[1],
                self._layer[1],
            )
        elif layer[0] != self._layer[0]:
            warning = (
                "init() called inside of pushed scope. This might be entirely "
                "legitimate but usually occurs when initializing the SDK inside "
                "a request handler or task/job function. Try to initialize the "
                "SDK as early as possible instead."
            )
            logger.warning(warning)


class Hub(with_metaclass(HubMeta)):  # type: ignore
    """The hub wraps the concurrency management of the SDK.  Each thread has
    its own hub but the hub might transfer with the flow of execution if
    context vars are available.

    If the hub is used with a with statement it's temporarily activated.
    """

    _stack = None  # type: List[Tuple[Optional[Client], Scope]]

    # Mypy doesn't pick up on the metaclass.

    if MYPY:
        current = None  # type: Hub
        main = None  # type: Hub

    def __init__(
        self,
        client_or_hub=None,  # type: Optional[Union[Hub, Client]]
        scope=None,  # type: Optional[Any]
    ):
        # type: (...) -> None
        if isinstance(client_or_hub, Hub):
            hub = client_or_hub
            client, other_scope = hub._stack[-1]
            if scope is None:
                scope = copy.copy(other_scope)
        else:
            client = client_or_hub
        if scope is None:
            scope = Scope()

        self._stack = [(client, scope)]
        self._last_event_id = None  # type: Optional[str]
        self._old_hubs = []  # type: List[Hub]

    def __enter__(self):
        # type: () -> Hub
        self._old_hubs.append(Hub.current)
        _local.set(self)
        return self

    def __exit__(
        self,
        exc_type,  # type: Optional[type]
        exc_value,  # type: Optional[BaseException]
        tb,  # type: Optional[Any]
    ):
        # type: (...) -> None
        old = self._old_hubs.pop()
        _local.set(old)

    def run(
        self, callback  # type: Callable[[], T]
    ):
        # type: (...) -> T
        """Runs a callback in the context of the hub.  Alternatively the
        with statement can be used on the hub directly.
        """
        with self:
            return callback()

    def get_integration(
        self, name_or_class  # type: Union[str, Type[Integration]]
    ):
        # type: (...) -> Any
        """Returns the integration for this hub by name or class.  If there
        is no client bound or the client does not have that integration
        then `None` is returned.

        If the return value is not `None` the hub is guaranteed to have a
        client attached.
        """
        if isinstance(name_or_class, str):
            integration_name = name_or_class
        elif name_or_class.identifier is not None:
            integration_name = name_or_class.identifier
        else:
            raise ValueError("Integration has no name")

        client = self.client
        if client is not None:
            rv = client.integrations.get(integration_name)
            if rv is not None:
                return rv

    @property
    def client(self):
        # type: () -> Optional[Client]
        """Returns the current client on the hub."""
        return self._stack[-1][0]

    @property
    def scope(self):
        # type: () -> Scope
        """Returns the current scope on the hub."""
        return self._stack[-1][1]

    def last_event_id(self):
        # type: () -> Optional[str]
        """Returns the last event ID."""
        return self._last_event_id

    def bind_client(
        self, new  # type: Optional[Client]
    ):
        # type: (...) -> None
        """Binds a new client to the hub."""
        top = self._stack[-1]
        self._stack[-1] = (new, top[1])

    def capture_event(
        self,
        event,  # type: Event
        hint=None,  # type: Optional[Hint]
        scope=None,  # type: Optional[Any]
        **scope_args  # type: Any
    ):
        # type: (...) -> Optional[str]
        """Captures an event. Alias of :py:meth:`sentry_sdk.Client.capture_event`."""
        client, top_scope = self._stack[-1]
        scope = _update_scope(top_scope, scope, scope_args)
        if client is not None:
            is_transaction = event.get("type") == "transaction"
            rv = client.capture_event(event, hint, scope)
            if rv is not None and not is_transaction:
                self._last_event_id = rv
            return rv
        return None

    def capture_message(
        self,
        message,  # type: str
        level=None,  # type: Optional[str]
        scope=None,  # type: Optional[Any]
        **scope_args  # type: Any
    ):
        # type: (...) -> Optional[str]
        """Captures a message.  The message is just a string.  If no level
        is provided the default level is `info`.

        :returns: An `event_id` if the SDK decided to send the event (see :py:meth:`sentry_sdk.Client.capture_event`).
        """
        if self.client is None:
            return None
        if level is None:
            level = "info"
        return self.capture_event(
            {"message": message, "level": level}, scope=scope, **scope_args
        )

    def capture_exception(
        self,
        error=None,  # type: Optional[Union[BaseException, ExcInfo]]
        scope=None,  # type: Optional[Any]
        **scope_args  # type: Any
    ):
        # type: (...) -> Optional[str]
        """Captures an exception.

        :param error: An exception to catch. If `None`, `sys.exc_info()` will be used.

        :returns: An `event_id` if the SDK decided to send the event (see :py:meth:`sentry_sdk.Client.capture_event`).
        """
        client = self.client
        if client is None:
            return None
        if error is not None:
            exc_info = exc_info_from_error(error)
        else:
            exc_info = sys.exc_info()

        event, hint = event_from_exception(exc_info, client_options=client.options)
        try:
            return self.capture_event(event, hint=hint, scope=scope, **scope_args)
        except Exception:
            self._capture_internal_exception(sys.exc_info())

        return None

    def _capture_internal_exception(
        self, exc_info  # type: Any
    ):
        # type: (...) -> Any
        """
        Capture an exception that is likely caused by a bug in the SDK
        itself.

        These exceptions do not end up in Sentry and are just logged instead.
        """
        logger.error("Internal error in sentry_sdk", exc_info=exc_info)

    def add_breadcrumb(
        self,
        crumb=None,  # type: Optional[Breadcrumb]
        hint=None,  # type: Optional[BreadcrumbHint]
        **kwargs  # type: Any
    ):
        # type: (...) -> None
        """
        Adds a breadcrumb.

        :param crumb: Dictionary with the data as the sentry v7/v8 protocol expects.

        :param hint: An optional value that can be used by `before_breadcrumb`
            to customize the breadcrumbs that are emitted.
        """
        client, scope = self._stack[-1]
        if client is None:
            logger.info("Dropped breadcrumb because no client bound")
            return

        crumb = dict(crumb or ())  # type: Breadcrumb
        crumb.update(kwargs)
        if not crumb:
            return

        hint = dict(hint or ())  # type: Hint

        if crumb.get("timestamp") is None:
            crumb["timestamp"] = datetime.utcnow()
        if crumb.get("type") is None:
            crumb["type"] = "default"

        if client.options["before_breadcrumb"] is not None:
            new_crumb = client.options["before_breadcrumb"](crumb, hint)
        else:
            new_crumb = crumb

        if new_crumb is not None:
            scope._breadcrumbs.append(new_crumb)
        else:
            logger.info("before breadcrumb dropped breadcrumb (%s)", crumb)

        max_breadcrumbs = client.options["max_breadcrumbs"]  # type: int
        while len(scope._breadcrumbs) > max_breadcrumbs:
            scope._breadcrumbs.popleft()

    def start_span(
        self,
        span=None,  # type: Optional[Span]
        instrumenter=INSTRUMENTER.SENTRY,  # type: str
        **kwargs  # type: Any
    ):
        # type: (...) -> Span
        """
        Create and start timing a new span whose parent is the currently active
        span or transaction, if any. The return value is a span instance,
        typically used as a context manager to start and stop timing in a `with`
        block.

        Only spans contained in a transaction are sent to Sentry. Most
        integrations start a transaction at the appropriate time, for example
        for every incoming HTTP request. Use `start_transaction` to start a new
        transaction when one is not already in progress.
        """
        configuration_instrumenter = self.client and self.client.options["instrumenter"]

        if instrumenter != configuration_instrumenter:
            return NoOpSpan()

        # TODO: consider removing this in a future release.
        # This is for backwards compatibility with releases before
        # start_transaction existed, to allow for a smoother transition.
        if isinstance(span, Transaction) or "transaction" in kwargs:
            deprecation_msg = (
                "Deprecated: use start_transaction to start transactions and "
                "Transaction.start_child to start spans."
            )
            if isinstance(span, Transaction):
                logger.warning(deprecation_msg)
                return self.start_transaction(span)
            if "transaction" in kwargs:
                logger.warning(deprecation_msg)
                name = kwargs.pop("transaction")
                return self.start_transaction(name=name, **kwargs)

        if span is not None:
            return span

        kwargs.setdefault("hub", self)

        span = self.scope.span
        if span is not None:
            return span.start_child(**kwargs)

        return Span(**kwargs)

    def start_transaction(
        self,
        transaction=None,  # type: Optional[Transaction]
        instrumenter=INSTRUMENTER.SENTRY,  # type: str
        **kwargs  # type: Any
    ):
        # type: (...) -> Union[Transaction, NoOpSpan]
        """
        Start and return a transaction.

        Start an existing transaction if given, otherwise create and start a new
        transaction with kwargs.

        This is the entry point to manual tracing instrumentation.

        A tree structure can be built by adding child spans to the transaction,
        and child spans to other spans. To start a new child span within the
        transaction or any span, call the respective `.start_child()` method.

        Every child span must be finished before the transaction is finished,
        otherwise the unfinished spans are discarded.

        When used as context managers, spans and transactions are automatically
        finished at the end of the `with` block. If not using context managers,
        call the `.finish()` method.

        When the transaction is finished, it will be sent to Sentry with all its
        finished child spans.
        """
        configuration_instrumenter = self.client and self.client.options["instrumenter"]

        if instrumenter != configuration_instrumenter:
            return NoOpSpan()

        custom_sampling_context = kwargs.pop("custom_sampling_context", {})

        # if we haven't been given a transaction, make one
        if transaction is None:
            kwargs.setdefault("hub", self)
            transaction = Transaction(**kwargs)

        # use traces_sample_rate, traces_sampler, and/or inheritance to make a
        # sampling decision
        sampling_context = {
            "transaction_context": transaction.to_json(),
            "parent_sampled": transaction.parent_sampled,
        }
        sampling_context.update(custom_sampling_context)
        transaction._set_initial_sampling_decision(sampling_context=sampling_context)

        # we don't bother to keep spans if we already know we're not going to
        # send the transaction
        if transaction.sampled:
            max_spans = (
                self.client and self.client.options["_experiments"].get("max_spans")
            ) or 1000
            transaction.init_span_recorder(maxlen=max_spans)

        return transaction

    @overload
    def push_scope(
        self, callback=None  # type: Optional[None]
    ):
        # type: (...) -> ContextManager[Scope]
        pass

    @overload
    def push_scope(  # noqa: F811
        self, callback  # type: Callable[[Scope], None]
    ):
        # type: (...) -> None
        pass

    def push_scope(  # noqa
        self, callback=None  # type: Optional[Callable[[Scope], None]]
    ):
        # type: (...) -> Optional[ContextManager[Scope]]
        """
        Pushes a new layer on the scope stack.

        :param callback: If provided, this method pushes a scope, calls
            `callback`, and pops the scope again.

        :returns: If no `callback` is provided, a context manager that should
            be used to pop the scope again.
        """
        if callback is not None:
            with self.push_scope() as scope:
                callback(scope)
            return None

        client, scope = self._stack[-1]
        new_layer = (client, copy.copy(scope))
        self._stack.append(new_layer)

        return _ScopeManager(self)

    def pop_scope_unsafe(self):
        # type: () -> Tuple[Optional[Client], Scope]
        """
        Pops a scope layer from the stack.

        Try to use the context manager :py:meth:`push_scope` instead.
        """
        rv = self._stack.pop()
        assert self._stack, "stack must have at least one layer"
        return rv

    @overload
    def configure_scope(
        self, callback=None  # type: Optional[None]
    ):
        # type: (...) -> ContextManager[Scope]
        pass

    @overload
    def configure_scope(  # noqa: F811
        self, callback  # type: Callable[[Scope], None]
    ):
        # type: (...) -> None
        pass

    def configure_scope(  # noqa
        self, callback=None  # type: Optional[Callable[[Scope], None]]
    ):
        # type: (...) -> Optional[ContextManager[Scope]]

        """
        Reconfigures the scope.

        :param callback: If provided, call the callback with the current scope.

        :returns: If no callback is provided, returns a context manager that returns the scope.
        """

        client, scope = self._stack[-1]
        if callback is not None:
            if client is not None:
                callback(scope)

            return None

        @contextmanager
        def inner():
            # type: () -> Generator[Scope, None, None]
            if client is not None:
                yield scope
            else:
                yield Scope()

        return inner()

    def start_session(
        self, session_mode="application"  # type: str
    ):
        # type: (...) -> None
        """Starts a new session."""
        self.end_session()
        client, scope = self._stack[-1]
        scope._session = Session(
            release=client.options["release"] if client else None,
            environment=client.options["environment"] if client else None,
            user=scope._user,
            session_mode=session_mode,
        )

    def end_session(self):
        # type: (...) -> None
        """Ends the current session if there is one."""
        client, scope = self._stack[-1]
        session = scope._session
        self.scope._session = None

        if session is not None:
            session.close()
            if client is not None:
                client.capture_session(session)

    def stop_auto_session_tracking(self):
        # type: (...) -> None
        """Stops automatic session tracking.

        This temporarily session tracking for the current scope when called.
        To resume session tracking call `resume_auto_session_tracking`.
        """
        self.end_session()
        client, scope = self._stack[-1]
        scope._force_auto_session_tracking = False

    def resume_auto_session_tracking(self):
        # type: (...) -> None
        """Resumes automatic session tracking for the current scope if
        disabled earlier.  This requires that generally automatic session
        tracking is enabled.
        """
        client, scope = self._stack[-1]
        scope._force_auto_session_tracking = None

    def flush(
        self,
        timeout=None,  # type: Optional[float]
        callback=None,  # type: Optional[Callable[[int, float], None]]
    ):
        # type: (...) -> None
        """
        Alias for :py:meth:`sentry_sdk.Client.flush`
        """
        client, scope = self._stack[-1]
        if client is not None:
            return client.flush(timeout=timeout, callback=callback)

    def iter_trace_propagation_headers(self, span=None):
        # type: (Optional[Span]) -> Generator[Tuple[str, str], None, None]
        """
        Return HTTP headers which allow propagation of trace data. Data taken
        from the span representing the request, if available, or the current
        span on the scope if not.
        """
        span = span or self.scope.span
        if not span:
            return

        client = self._stack[-1][0]

        propagate_traces = client and client.options["propagate_traces"]
        if not propagate_traces:
            return

        for header in span.iter_headers():
            yield header

    def trace_propagation_meta(self, span=None):
        # type: (Optional[Span]) -> str
        """
        Return meta tags which should be injected into the HTML template
        to allow propagation of trace data.
        """
        meta = ""

        for name, content in self.iter_trace_propagation_headers(span):
            meta += '<meta name="%s" content="%s">' % (name, content)

        return meta


GLOBAL_HUB = Hub()
_local.set(GLOBAL_HUB)
