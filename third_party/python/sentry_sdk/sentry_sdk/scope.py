from copy import copy
from collections import deque
from itertools import chain

from sentry_sdk._functools import wraps
from sentry_sdk._types import MYPY
from sentry_sdk.utils import logger, capture_internal_exceptions
from sentry_sdk.tracing import Transaction
from sentry_sdk.attachments import Attachment

if MYPY:
    from typing import Any
    from typing import Dict
    from typing import Optional
    from typing import Deque
    from typing import List
    from typing import Callable
    from typing import TypeVar

    from sentry_sdk._types import (
        Breadcrumb,
        Event,
        EventProcessor,
        ErrorProcessor,
        ExcInfo,
        Hint,
        Type,
    )

    from sentry_sdk.profiler import Profile
    from sentry_sdk.tracing import Span
    from sentry_sdk.session import Session

    F = TypeVar("F", bound=Callable[..., Any])
    T = TypeVar("T")


global_event_processors = []  # type: List[EventProcessor]


def add_global_event_processor(processor):
    # type: (EventProcessor) -> None
    global_event_processors.append(processor)


def _attr_setter(fn):
    # type: (Any) -> Any
    return property(fset=fn, doc=fn.__doc__)


def _disable_capture(fn):
    # type: (F) -> F
    @wraps(fn)
    def wrapper(self, *args, **kwargs):
        # type: (Any, *Dict[str, Any], **Any) -> Any
        if not self._should_capture:
            return
        try:
            self._should_capture = False
            return fn(self, *args, **kwargs)
        finally:
            self._should_capture = True

    return wrapper  # type: ignore


class Scope(object):
    """The scope holds extra information that should be sent with all
    events that belong to it.
    """

    # NOTE: Even though it should not happen, the scope needs to not crash when
    # accessed by multiple threads. It's fine if it's full of races, but those
    # races should never make the user application crash.
    #
    # The same needs to hold for any accesses of the scope the SDK makes.

    __slots__ = (
        "_level",
        "_name",
        "_fingerprint",
        # note that for legacy reasons, _transaction is the transaction *name*,
        # not a Transaction object (the object is stored in _span)
        "_transaction",
        "_transaction_info",
        "_user",
        "_tags",
        "_contexts",
        "_extras",
        "_breadcrumbs",
        "_event_processors",
        "_error_processors",
        "_should_capture",
        "_span",
        "_session",
        "_attachments",
        "_force_auto_session_tracking",
        "_profile",
    )

    def __init__(self):
        # type: () -> None
        self._event_processors = []  # type: List[EventProcessor]
        self._error_processors = []  # type: List[ErrorProcessor]

        self._name = None  # type: Optional[str]
        self.clear()

    def clear(self):
        # type: () -> None
        """Clears the entire scope."""
        self._level = None  # type: Optional[str]
        self._fingerprint = None  # type: Optional[List[str]]
        self._transaction = None  # type: Optional[str]
        self._transaction_info = {}  # type: Dict[str, str]
        self._user = None  # type: Optional[Dict[str, Any]]

        self._tags = {}  # type: Dict[str, Any]
        self._contexts = {}  # type: Dict[str, Dict[str, Any]]
        self._extras = {}  # type: Dict[str, Any]
        self._attachments = []  # type: List[Attachment]

        self.clear_breadcrumbs()
        self._should_capture = True

        self._span = None  # type: Optional[Span]
        self._session = None  # type: Optional[Session]
        self._force_auto_session_tracking = None  # type: Optional[bool]

        self._profile = None  # type: Optional[Profile]

    @_attr_setter
    def level(self, value):
        # type: (Optional[str]) -> None
        """When set this overrides the level. Deprecated in favor of set_level."""
        self._level = value

    def set_level(self, value):
        # type: (Optional[str]) -> None
        """Sets the level for the scope."""
        self._level = value

    @_attr_setter
    def fingerprint(self, value):
        # type: (Optional[List[str]]) -> None
        """When set this overrides the default fingerprint."""
        self._fingerprint = value

    @property
    def transaction(self):
        # type: () -> Any
        # would be type: () -> Optional[Transaction], see https://github.com/python/mypy/issues/3004
        """Return the transaction (root span) in the scope, if any."""

        # there is no span/transaction on the scope
        if self._span is None:
            return None

        # there is an orphan span on the scope
        if self._span.containing_transaction is None:
            return None

        # there is either a transaction (which is its own containing
        # transaction) or a non-orphan span on the scope
        return self._span.containing_transaction

    @transaction.setter
    def transaction(self, value):
        # type: (Any) -> None
        # would be type: (Optional[str]) -> None, see https://github.com/python/mypy/issues/3004
        """When set this forces a specific transaction name to be set.

        Deprecated: use set_transaction_name instead."""

        # XXX: the docstring above is misleading. The implementation of
        # apply_to_event prefers an existing value of event.transaction over
        # anything set in the scope.
        # XXX: note that with the introduction of the Scope.transaction getter,
        # there is a semantic and type mismatch between getter and setter. The
        # getter returns a Transaction, the setter sets a transaction name.
        # Without breaking version compatibility, we could make the setter set a
        # transaction name or transaction (self._span) depending on the type of
        # the value argument.

        logger.warning(
            "Assigning to scope.transaction directly is deprecated: use scope.set_transaction_name() instead."
        )
        self._transaction = value
        if self._span and self._span.containing_transaction:
            self._span.containing_transaction.name = value

    def set_transaction_name(self, name, source=None):
        # type: (str, Optional[str]) -> None
        """Set the transaction name and optionally the transaction source."""
        self._transaction = name

        if self._span and self._span.containing_transaction:
            self._span.containing_transaction.name = name
            if source:
                self._span.containing_transaction.source = source

        if source:
            self._transaction_info["source"] = source

    @_attr_setter
    def user(self, value):
        # type: (Optional[Dict[str, Any]]) -> None
        """When set a specific user is bound to the scope. Deprecated in favor of set_user."""
        self.set_user(value)

    def set_user(self, value):
        # type: (Optional[Dict[str, Any]]) -> None
        """Sets a user for the scope."""
        self._user = value
        if self._session is not None:
            self._session.update(user=value)

    @property
    def span(self):
        # type: () -> Optional[Span]
        """Get/set current tracing span or transaction."""
        return self._span

    @span.setter
    def span(self, span):
        # type: (Optional[Span]) -> None
        self._span = span
        # XXX: this differs from the implementation in JS, there Scope.setSpan
        # does not set Scope._transactionName.
        if isinstance(span, Transaction):
            transaction = span
            if transaction.name:
                self._transaction = transaction.name

    @property
    def profile(self):
        # type: () -> Optional[Profile]
        return self._profile

    @profile.setter
    def profile(self, profile):
        # type: (Optional[Profile]) -> None

        self._profile = profile

    def set_tag(
        self,
        key,  # type: str
        value,  # type: Any
    ):
        # type: (...) -> None
        """Sets a tag for a key to a specific value."""
        self._tags[key] = value

    def remove_tag(
        self, key  # type: str
    ):
        # type: (...) -> None
        """Removes a specific tag."""
        self._tags.pop(key, None)

    def set_context(
        self,
        key,  # type: str
        value,  # type: Dict[str, Any]
    ):
        # type: (...) -> None
        """Binds a context at a certain key to a specific value."""
        self._contexts[key] = value

    def remove_context(
        self, key  # type: str
    ):
        # type: (...) -> None
        """Removes a context."""
        self._contexts.pop(key, None)

    def set_extra(
        self,
        key,  # type: str
        value,  # type: Any
    ):
        # type: (...) -> None
        """Sets an extra key to a specific value."""
        self._extras[key] = value

    def remove_extra(
        self, key  # type: str
    ):
        # type: (...) -> None
        """Removes a specific extra key."""
        self._extras.pop(key, None)

    def clear_breadcrumbs(self):
        # type: () -> None
        """Clears breadcrumb buffer."""
        self._breadcrumbs = deque()  # type: Deque[Breadcrumb]

    def add_attachment(
        self,
        bytes=None,  # type: Optional[bytes]
        filename=None,  # type: Optional[str]
        path=None,  # type: Optional[str]
        content_type=None,  # type: Optional[str]
        add_to_transactions=False,  # type: bool
    ):
        # type: (...) -> None
        """Adds an attachment to future events sent."""
        self._attachments.append(
            Attachment(
                bytes=bytes,
                path=path,
                filename=filename,
                content_type=content_type,
                add_to_transactions=add_to_transactions,
            )
        )

    def add_event_processor(
        self, func  # type: EventProcessor
    ):
        # type: (...) -> None
        """Register a scope local event processor on the scope.

        :param func: This function behaves like `before_send.`
        """
        if len(self._event_processors) > 20:
            logger.warning(
                "Too many event processors on scope! Clearing list to free up some memory: %r",
                self._event_processors,
            )
            del self._event_processors[:]

        self._event_processors.append(func)

    def add_error_processor(
        self,
        func,  # type: ErrorProcessor
        cls=None,  # type: Optional[Type[BaseException]]
    ):
        # type: (...) -> None
        """Register a scope local error processor on the scope.

        :param func: A callback that works similar to an event processor but is invoked with the original exception info triple as second argument.

        :param cls: Optionally, only process exceptions of this type.
        """
        if cls is not None:
            cls_ = cls  # For mypy.
            real_func = func

            def func(event, exc_info):
                # type: (Event, ExcInfo) -> Optional[Event]
                try:
                    is_inst = isinstance(exc_info[1], cls_)
                except Exception:
                    is_inst = False
                if is_inst:
                    return real_func(event, exc_info)
                return event

        self._error_processors.append(func)

    @_disable_capture
    def apply_to_event(
        self,
        event,  # type: Event
        hint,  # type: Hint
    ):
        # type: (...) -> Optional[Event]
        """Applies the information contained on the scope to the given event."""

        def _drop(event, cause, ty):
            # type: (Dict[str, Any], Any, str) -> Optional[Any]
            logger.info("%s (%s) dropped event (%s)", ty, cause, event)
            return None

        is_transaction = event.get("type") == "transaction"

        # put all attachments into the hint. This lets callbacks play around
        # with attachments. We also later pull this out of the hint when we
        # create the envelope.
        attachments_to_send = hint.get("attachments") or []
        for attachment in self._attachments:
            if not is_transaction or attachment.add_to_transactions:
                attachments_to_send.append(attachment)
        hint["attachments"] = attachments_to_send

        if self._level is not None:
            event["level"] = self._level

        if not is_transaction:
            event.setdefault("breadcrumbs", {}).setdefault("values", []).extend(
                self._breadcrumbs
            )

        if event.get("user") is None and self._user is not None:
            event["user"] = self._user

        if event.get("transaction") is None and self._transaction is not None:
            event["transaction"] = self._transaction

        if event.get("transaction_info") is None and self._transaction_info is not None:
            event["transaction_info"] = self._transaction_info

        if event.get("fingerprint") is None and self._fingerprint is not None:
            event["fingerprint"] = self._fingerprint

        if self._extras:
            event.setdefault("extra", {}).update(self._extras)

        if self._tags:
            event.setdefault("tags", {}).update(self._tags)

        if self._contexts:
            event.setdefault("contexts", {}).update(self._contexts)

        if self._span is not None:
            contexts = event.setdefault("contexts", {})
            if not contexts.get("trace"):
                contexts["trace"] = self._span.get_trace_context()

        exc_info = hint.get("exc_info")
        if exc_info is not None:
            for error_processor in self._error_processors:
                new_event = error_processor(event, exc_info)
                if new_event is None:
                    return _drop(event, error_processor, "error processor")
                event = new_event

        for event_processor in chain(global_event_processors, self._event_processors):
            new_event = event
            with capture_internal_exceptions():
                new_event = event_processor(event, hint)
            if new_event is None:
                return _drop(event, event_processor, "event processor")
            event = new_event

        return event

    def update_from_scope(self, scope):
        # type: (Scope) -> None
        if scope._level is not None:
            self._level = scope._level
        if scope._fingerprint is not None:
            self._fingerprint = scope._fingerprint
        if scope._transaction is not None:
            self._transaction = scope._transaction
        if scope._transaction_info is not None:
            self._transaction_info.update(scope._transaction_info)
        if scope._user is not None:
            self._user = scope._user
        if scope._tags:
            self._tags.update(scope._tags)
        if scope._contexts:
            self._contexts.update(scope._contexts)
        if scope._extras:
            self._extras.update(scope._extras)
        if scope._breadcrumbs:
            self._breadcrumbs.extend(scope._breadcrumbs)
        if scope._span:
            self._span = scope._span
        if scope._attachments:
            self._attachments.extend(scope._attachments)
        if scope._profile:
            self._profile = scope._profile

    def update_from_kwargs(
        self,
        user=None,  # type: Optional[Any]
        level=None,  # type: Optional[str]
        extras=None,  # type: Optional[Dict[str, Any]]
        contexts=None,  # type: Optional[Dict[str, Any]]
        tags=None,  # type: Optional[Dict[str, str]]
        fingerprint=None,  # type: Optional[List[str]]
    ):
        # type: (...) -> None
        if level is not None:
            self._level = level
        if user is not None:
            self._user = user
        if extras is not None:
            self._extras.update(extras)
        if contexts is not None:
            self._contexts.update(contexts)
        if tags is not None:
            self._tags.update(tags)
        if fingerprint is not None:
            self._fingerprint = fingerprint

    def __copy__(self):
        # type: () -> Scope
        rv = object.__new__(self.__class__)  # type: Scope

        rv._level = self._level
        rv._name = self._name
        rv._fingerprint = self._fingerprint
        rv._transaction = self._transaction
        rv._transaction_info = dict(self._transaction_info)
        rv._user = self._user

        rv._tags = dict(self._tags)
        rv._contexts = dict(self._contexts)
        rv._extras = dict(self._extras)

        rv._breadcrumbs = copy(self._breadcrumbs)
        rv._event_processors = list(self._event_processors)
        rv._error_processors = list(self._error_processors)

        rv._should_capture = self._should_capture
        rv._span = self._span
        rv._session = self._session
        rv._force_auto_session_tracking = self._force_auto_session_tracking
        rv._attachments = list(self._attachments)

        rv._profile = self._profile

        return rv

    def __repr__(self):
        # type: () -> str
        return "<%s id=%s name=%s>" % (
            self.__class__.__name__,
            hex(id(self)),
            self._name,
        )
