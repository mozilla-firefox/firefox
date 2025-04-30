import uuid
import random
import threading
import time

from datetime import datetime, timedelta

import sentry_sdk
from sentry_sdk.consts import INSTRUMENTER
from sentry_sdk.utils import logger
from sentry_sdk._types import MYPY


if MYPY:
    import typing

    from typing import Optional
    from typing import Any
    from typing import Dict
    from typing import List
    from typing import Tuple
    from typing import Iterator

    import sentry_sdk.profiler
    from sentry_sdk._types import Event, SamplingContext, MeasurementUnit

BAGGAGE_HEADER_NAME = "baggage"
SENTRY_TRACE_HEADER_NAME = "sentry-trace"


# Transaction source
# see https://develop.sentry.dev/sdk/event-payloads/transaction/#transaction-annotations
TRANSACTION_SOURCE_CUSTOM = "custom"
TRANSACTION_SOURCE_URL = "url"
TRANSACTION_SOURCE_ROUTE = "route"
TRANSACTION_SOURCE_VIEW = "view"
TRANSACTION_SOURCE_COMPONENT = "component"
TRANSACTION_SOURCE_TASK = "task"

# These are typically high cardinality and the server hates them
LOW_QUALITY_TRANSACTION_SOURCES = [
    TRANSACTION_SOURCE_URL,
]

SOURCE_FOR_STYLE = {
    "endpoint": TRANSACTION_SOURCE_COMPONENT,
    "function_name": TRANSACTION_SOURCE_COMPONENT,
    "handler_name": TRANSACTION_SOURCE_COMPONENT,
    "method_and_path_pattern": TRANSACTION_SOURCE_ROUTE,
    "path": TRANSACTION_SOURCE_URL,
    "route_name": TRANSACTION_SOURCE_COMPONENT,
    "route_pattern": TRANSACTION_SOURCE_ROUTE,
    "uri_template": TRANSACTION_SOURCE_ROUTE,
    "url": TRANSACTION_SOURCE_ROUTE,
}


class _SpanRecorder(object):
    """Limits the number of spans recorded in a transaction."""

    __slots__ = ("maxlen", "spans")

    def __init__(self, maxlen):
        # type: (int) -> None
        # FIXME: this is `maxlen - 1` only to preserve historical behavior
        # enforced by tests.
        # Either this should be changed to `maxlen` or the JS SDK implementation
        # should be changed to match a consistent interpretation of what maxlen
        # limits: either transaction+spans or only child spans.
        self.maxlen = maxlen - 1
        self.spans = []  # type: List[Span]

    def add(self, span):
        # type: (Span) -> None
        if len(self.spans) > self.maxlen:
            span._span_recorder = None
        else:
            self.spans.append(span)


class Span(object):
    __slots__ = (
        "trace_id",
        "span_id",
        "parent_span_id",
        "same_process_as_parent",
        "sampled",
        "op",
        "description",
        "start_timestamp",
        "_start_timestamp_monotonic",
        "status",
        "timestamp",
        "_tags",
        "_data",
        "_span_recorder",
        "hub",
        "_context_manager_state",
        "_containing_transaction",
    )

    def __new__(cls, **kwargs):
        # type: (**Any) -> Any
        """
        Backwards-compatible implementation of Span and Transaction
        creation.
        """

        # TODO: consider removing this in a future release.
        # This is for backwards compatibility with releases before Transaction
        # existed, to allow for a smoother transition.
        if "transaction" in kwargs:
            return object.__new__(Transaction)
        return object.__new__(cls)

    def __init__(
        self,
        trace_id=None,  # type: Optional[str]
        span_id=None,  # type: Optional[str]
        parent_span_id=None,  # type: Optional[str]
        same_process_as_parent=True,  # type: bool
        sampled=None,  # type: Optional[bool]
        op=None,  # type: Optional[str]
        description=None,  # type: Optional[str]
        hub=None,  # type: Optional[sentry_sdk.Hub]
        status=None,  # type: Optional[str]
        transaction=None,  # type: Optional[str] # deprecated
        containing_transaction=None,  # type: Optional[Transaction]
        start_timestamp=None,  # type: Optional[datetime]
    ):
        # type: (...) -> None
        self.trace_id = trace_id or uuid.uuid4().hex
        self.span_id = span_id or uuid.uuid4().hex[16:]
        self.parent_span_id = parent_span_id
        self.same_process_as_parent = same_process_as_parent
        self.sampled = sampled
        self.op = op
        self.description = description
        self.status = status
        self.hub = hub
        self._tags = {}  # type: Dict[str, str]
        self._data = {}  # type: Dict[str, Any]
        self._containing_transaction = containing_transaction
        self.start_timestamp = start_timestamp or datetime.utcnow()
        try:
            # TODO: For Python 3.7+, we could use a clock with ns resolution:
            # self._start_timestamp_monotonic = time.perf_counter_ns()

            # Python 3.3+
            self._start_timestamp_monotonic = time.perf_counter()
        except AttributeError:
            pass

        #: End timestamp of span
        self.timestamp = None  # type: Optional[datetime]

        self._span_recorder = None  # type: Optional[_SpanRecorder]

    # TODO this should really live on the Transaction class rather than the Span
    # class
    def init_span_recorder(self, maxlen):
        # type: (int) -> None
        if self._span_recorder is None:
            self._span_recorder = _SpanRecorder(maxlen)

    def __repr__(self):
        # type: () -> str
        return (
            "<%s(op=%r, description:%r, trace_id=%r, span_id=%r, parent_span_id=%r, sampled=%r)>"
            % (
                self.__class__.__name__,
                self.op,
                self.description,
                self.trace_id,
                self.span_id,
                self.parent_span_id,
                self.sampled,
            )
        )

    def __enter__(self):
        # type: () -> Span
        hub = self.hub or sentry_sdk.Hub.current

        _, scope = hub._stack[-1]
        old_span = scope.span
        scope.span = self
        self._context_manager_state = (hub, scope, old_span)
        return self

    def __exit__(self, ty, value, tb):
        # type: (Optional[Any], Optional[Any], Optional[Any]) -> None
        if value is not None:
            self.set_status("internal_error")

        hub, scope, old_span = self._context_manager_state
        del self._context_manager_state

        self.finish(hub)
        scope.span = old_span

    @property
    def containing_transaction(self):
        # type: () -> Optional[Transaction]

        # this is a getter rather than a regular attribute so that transactions
        # can return `self` here instead (as a way to prevent them circularly
        # referencing themselves)
        return self._containing_transaction

    def start_child(self, instrumenter=INSTRUMENTER.SENTRY, **kwargs):
        # type: (str, **Any) -> Span
        """
        Start a sub-span from the current span or transaction.

        Takes the same arguments as the initializer of :py:class:`Span`. The
        trace id, sampling decision, transaction pointer, and span recorder are
        inherited from the current span/transaction.
        """
        hub = self.hub or sentry_sdk.Hub.current
        client = hub.client
        configuration_instrumenter = client and client.options["instrumenter"]

        if instrumenter != configuration_instrumenter:
            return NoOpSpan()

        kwargs.setdefault("sampled", self.sampled)

        child = Span(
            trace_id=self.trace_id,
            parent_span_id=self.span_id,
            containing_transaction=self.containing_transaction,
            **kwargs
        )

        span_recorder = (
            self.containing_transaction and self.containing_transaction._span_recorder
        )
        if span_recorder:
            span_recorder.add(child)
        return child

    def new_span(self, **kwargs):
        # type: (**Any) -> Span
        """Deprecated: use start_child instead."""
        logger.warning("Deprecated: use Span.start_child instead of Span.new_span.")
        return self.start_child(**kwargs)

    @classmethod
    def continue_from_environ(
        cls,
        environ,  # type: typing.Mapping[str, str]
        **kwargs  # type: Any
    ):
        # type: (...) -> Transaction
        """
        Create a Transaction with the given params, then add in data pulled from
        the 'sentry-trace', 'baggage' and 'tracestate' headers from the environ (if any)
        before returning the Transaction.

        This is different from `continue_from_headers` in that it assumes header
        names in the form "HTTP_HEADER_NAME" - such as you would get from a wsgi
        environ - rather than the form "header-name".
        """
        if cls is Span:
            logger.warning(
                "Deprecated: use Transaction.continue_from_environ "
                "instead of Span.continue_from_environ."
            )
        return Transaction.continue_from_headers(EnvironHeaders(environ), **kwargs)

    @classmethod
    def continue_from_headers(
        cls,
        headers,  # type: typing.Mapping[str, str]
        **kwargs  # type: Any
    ):
        # type: (...) -> Transaction
        """
        Create a transaction with the given params (including any data pulled from
        the 'sentry-trace', 'baggage' and 'tracestate' headers).
        """
        # TODO move this to the Transaction class
        if cls is Span:
            logger.warning(
                "Deprecated: use Transaction.continue_from_headers "
                "instead of Span.continue_from_headers."
            )

        # TODO-neel move away from this kwargs stuff, it's confusing and opaque
        # make more explicit
        baggage = Baggage.from_incoming_header(headers.get(BAGGAGE_HEADER_NAME))
        kwargs.update({BAGGAGE_HEADER_NAME: baggage})

        sentrytrace_kwargs = extract_sentrytrace_data(
            headers.get(SENTRY_TRACE_HEADER_NAME)
        )

        if sentrytrace_kwargs is not None:
            kwargs.update(sentrytrace_kwargs)

            # If there's an incoming sentry-trace but no incoming baggage header,
            # for instance in traces coming from older SDKs,
            # baggage will be empty and immutable and won't be populated as head SDK.
            baggage.freeze()

        kwargs.update(extract_tracestate_data(headers.get("tracestate")))

        transaction = Transaction(**kwargs)
        transaction.same_process_as_parent = False

        return transaction

    def iter_headers(self):
        # type: () -> Iterator[Tuple[str, str]]
        """
        Creates a generator which returns the span's `sentry-trace`, `baggage` and
        `tracestate` headers.

        If the span's containing transaction doesn't yet have a
        `sentry_tracestate` value, this will cause one to be generated and
        stored.
        """
        yield SENTRY_TRACE_HEADER_NAME, self.to_traceparent()

        tracestate = self.to_tracestate() if has_tracestate_enabled(self) else None
        # `tracestate` will only be `None` if there's no client or no DSN
        # TODO (kmclb) the above will be true once the feature is no longer
        # behind a flag
        if tracestate:
            yield "tracestate", tracestate

        if self.containing_transaction:
            baggage = self.containing_transaction.get_baggage().serialize()
            if baggage:
                yield BAGGAGE_HEADER_NAME, baggage

    @classmethod
    def from_traceparent(
        cls,
        traceparent,  # type: Optional[str]
        **kwargs  # type: Any
    ):
        # type: (...) -> Optional[Transaction]
        """
        DEPRECATED: Use Transaction.continue_from_headers(headers, **kwargs)

        Create a Transaction with the given params, then add in data pulled from
        the given 'sentry-trace' header value before returning the Transaction.

        """
        logger.warning(
            "Deprecated: Use Transaction.continue_from_headers(headers, **kwargs) "
            "instead of from_traceparent(traceparent, **kwargs)"
        )

        if not traceparent:
            return None

        return cls.continue_from_headers(
            {SENTRY_TRACE_HEADER_NAME: traceparent}, **kwargs
        )

    def to_traceparent(self):
        # type: () -> str
        sampled = ""
        if self.sampled is True:
            sampled = "1"
        if self.sampled is False:
            sampled = "0"
        return "%s-%s-%s" % (self.trace_id, self.span_id, sampled)

    def to_tracestate(self):
        # type: () -> Optional[str]
        """
        Computes the `tracestate` header value using data from the containing
        transaction.

        If the containing transaction doesn't yet have a `sentry_tracestate`
        value, this will cause one to be generated and stored.

        If there is no containing transaction, a value will be generated but not
        stored.

        Returns None if there's no client and/or no DSN.
        """

        sentry_tracestate = self.get_or_set_sentry_tracestate()
        third_party_tracestate = (
            self.containing_transaction._third_party_tracestate
            if self.containing_transaction
            else None
        )

        if not sentry_tracestate:
            return None

        header_value = sentry_tracestate

        if third_party_tracestate:
            header_value = header_value + "," + third_party_tracestate

        return header_value

    def get_or_set_sentry_tracestate(self):
        # type: (Span) -> Optional[str]
        """
        Read sentry tracestate off of the span's containing transaction.

        If the transaction doesn't yet have a `_sentry_tracestate` value,
        compute one and store it.
        """
        transaction = self.containing_transaction

        if transaction:
            if not transaction._sentry_tracestate:
                transaction._sentry_tracestate = compute_tracestate_entry(self)

            return transaction._sentry_tracestate

        # orphan span - nowhere to store the value, so just return it
        return compute_tracestate_entry(self)

    def set_tag(self, key, value):
        # type: (str, Any) -> None
        self._tags[key] = value

    def set_data(self, key, value):
        # type: (str, Any) -> None
        self._data[key] = value

    def set_status(self, value):
        # type: (str) -> None
        self.status = value

    def set_http_status(self, http_status):
        # type: (int) -> None
        self.set_tag("http.status_code", str(http_status))

        if http_status < 400:
            self.set_status("ok")
        elif 400 <= http_status < 500:
            if http_status == 403:
                self.set_status("permission_denied")
            elif http_status == 404:
                self.set_status("not_found")
            elif http_status == 429:
                self.set_status("resource_exhausted")
            elif http_status == 413:
                self.set_status("failed_precondition")
            elif http_status == 401:
                self.set_status("unauthenticated")
            elif http_status == 409:
                self.set_status("already_exists")
            else:
                self.set_status("invalid_argument")
        elif 500 <= http_status < 600:
            if http_status == 504:
                self.set_status("deadline_exceeded")
            elif http_status == 501:
                self.set_status("unimplemented")
            elif http_status == 503:
                self.set_status("unavailable")
            else:
                self.set_status("internal_error")
        else:
            self.set_status("unknown_error")

    def is_success(self):
        # type: () -> bool
        return self.status == "ok"

    def finish(self, hub=None, end_timestamp=None):
        # type: (Optional[sentry_sdk.Hub], Optional[datetime]) -> Optional[str]
        # XXX: would be type: (Optional[sentry_sdk.Hub]) -> None, but that leads
        # to incompatible return types for Span.finish and Transaction.finish.
        if self.timestamp is not None:
            # This span is already finished, ignore.
            return None

        hub = hub or self.hub or sentry_sdk.Hub.current

        try:
            if end_timestamp:
                self.timestamp = end_timestamp
            else:
                duration_seconds = time.perf_counter() - self._start_timestamp_monotonic
                self.timestamp = self.start_timestamp + timedelta(
                    seconds=duration_seconds
                )
        except AttributeError:
            self.timestamp = datetime.utcnow()

        maybe_create_breadcrumbs_from_span(hub, self)
        return None

    def to_json(self):
        # type: () -> Dict[str, Any]
        rv = {
            "trace_id": self.trace_id,
            "span_id": self.span_id,
            "parent_span_id": self.parent_span_id,
            "same_process_as_parent": self.same_process_as_parent,
            "op": self.op,
            "description": self.description,
            "start_timestamp": self.start_timestamp,
            "timestamp": self.timestamp,
        }  # type: Dict[str, Any]

        if self.status:
            self._tags["status"] = self.status

        tags = self._tags
        if tags:
            rv["tags"] = tags

        data = self._data
        if data:
            rv["data"] = data

        return rv

    def get_trace_context(self):
        # type: () -> Any
        rv = {
            "trace_id": self.trace_id,
            "span_id": self.span_id,
            "parent_span_id": self.parent_span_id,
            "op": self.op,
            "description": self.description,
        }  # type: Dict[str, Any]
        if self.status:
            rv["status"] = self.status

        # if the transaction didn't inherit a tracestate value, and no outgoing
        # requests - whose need for headers would have caused a tracestate value
        # to be created - were made as part of the transaction, the transaction
        # still won't have a tracestate value, so compute one now
        sentry_tracestate = self.get_or_set_sentry_tracestate()

        if sentry_tracestate:
            rv["tracestate"] = sentry_tracestate

        if self.containing_transaction:
            rv[
                "dynamic_sampling_context"
            ] = self.containing_transaction.get_baggage().dynamic_sampling_context()

        return rv


class Transaction(Span):
    __slots__ = (
        "name",
        "source",
        "parent_sampled",
        # used to create baggage value for head SDKs in dynamic sampling
        "sample_rate",
        # the sentry portion of the `tracestate` header used to transmit
        # correlation context for server-side dynamic sampling, of the form
        # `sentry=xxxxx`, where `xxxxx` is the base64-encoded json of the
        # correlation context data, missing trailing any =
        "_sentry_tracestate",
        # tracestate data from other vendors, of the form `dogs=yes,cats=maybe`
        "_third_party_tracestate",
        "_measurements",
        "_contexts",
        "_profile",
        "_baggage",
        "_active_thread_id",
    )

    def __init__(
        self,
        name="",  # type: str
        parent_sampled=None,  # type: Optional[bool]
        sentry_tracestate=None,  # type: Optional[str]
        third_party_tracestate=None,  # type: Optional[str]
        baggage=None,  # type: Optional[Baggage]
        source=TRANSACTION_SOURCE_CUSTOM,  # type: str
        **kwargs  # type: Any
    ):
        # type: (...) -> None
        # TODO: consider removing this in a future release.
        # This is for backwards compatibility with releases before Transaction
        # existed, to allow for a smoother transition.
        if not name and "transaction" in kwargs:
            logger.warning(
                "Deprecated: use Transaction(name=...) to create transactions "
                "instead of Span(transaction=...)."
            )
            name = kwargs.pop("transaction")

        Span.__init__(self, **kwargs)

        self.name = name
        self.source = source
        self.sample_rate = None  # type: Optional[float]
        self.parent_sampled = parent_sampled
        # if tracestate isn't inherited and set here, it will get set lazily,
        # either the first time an outgoing request needs it for a header or the
        # first time an event needs it for inclusion in the captured data
        self._sentry_tracestate = sentry_tracestate
        self._third_party_tracestate = third_party_tracestate
        self._measurements = {}  # type: Dict[str, Any]
        self._contexts = {}  # type: Dict[str, Any]
        self._profile = None  # type: Optional[sentry_sdk.profiler.Profile]
        self._baggage = baggage
        # for profiling, we want to know on which thread a transaction is started
        # to accurately show the active thread in the UI
        self._active_thread_id = (
            threading.current_thread().ident
        )  # used by profiling.py

    def __repr__(self):
        # type: () -> str
        return (
            "<%s(name=%r, op=%r, trace_id=%r, span_id=%r, parent_span_id=%r, sampled=%r, source=%r)>"
            % (
                self.__class__.__name__,
                self.name,
                self.op,
                self.trace_id,
                self.span_id,
                self.parent_span_id,
                self.sampled,
                self.source,
            )
        )

    @property
    def containing_transaction(self):
        # type: () -> Transaction

        # Transactions (as spans) belong to themselves (as transactions). This
        # is a getter rather than a regular attribute to avoid having a circular
        # reference.
        return self

    def finish(self, hub=None, end_timestamp=None):
        # type: (Optional[sentry_sdk.Hub], Optional[datetime]) -> Optional[str]
        if self.timestamp is not None:
            # This transaction is already finished, ignore.
            return None

        hub = hub or self.hub or sentry_sdk.Hub.current
        client = hub.client

        if client is None:
            # We have no client and therefore nowhere to send this transaction.
            return None

        # This is a de facto proxy for checking if sampled = False
        if self._span_recorder is None:
            logger.debug("Discarding transaction because sampled = False")

            # This is not entirely accurate because discards here are not
            # exclusively based on sample rate but also traces sampler, but
            # we handle this the same here.
            if client.transport and has_tracing_enabled(client.options):
                client.transport.record_lost_event(
                    "sample_rate", data_category="transaction"
                )

            return None

        if not self.name:
            logger.warning(
                "Transaction has no name, falling back to `<unlabeled transaction>`."
            )
            self.name = "<unlabeled transaction>"

        Span.finish(self, hub, end_timestamp)

        if not self.sampled:
            # At this point a `sampled = None` should have already been resolved
            # to a concrete decision.
            if self.sampled is None:
                logger.warning("Discarding transaction without sampling decision.")

            return None

        finished_spans = [
            span.to_json()
            for span in self._span_recorder.spans
            if span.timestamp is not None
        ]

        # we do this to break the circular reference of transaction -> span
        # recorder -> span -> containing transaction (which is where we started)
        # before either the spans or the transaction goes out of scope and has
        # to be garbage collected
        self._span_recorder = None

        contexts = {}
        contexts.update(self._contexts)
        contexts.update({"trace": self.get_trace_context()})

        event = {
            "type": "transaction",
            "transaction": self.name,
            "transaction_info": {"source": self.source},
            "contexts": contexts,
            "tags": self._tags,
            "timestamp": self.timestamp,
            "start_timestamp": self.start_timestamp,
            "spans": finished_spans,
        }  # type: Event

        if hub.client is not None and self._profile is not None:
            event["profile"] = self._profile
            contexts.update({"profile": self._profile.get_profile_context()})

        if has_custom_measurements_enabled():
            event["measurements"] = self._measurements

        return hub.capture_event(event)

    def set_measurement(self, name, value, unit=""):
        # type: (str, float, MeasurementUnit) -> None
        if not has_custom_measurements_enabled():
            logger.debug(
                "[Tracing] Experimental custom_measurements feature is disabled"
            )
            return

        self._measurements[name] = {"value": value, "unit": unit}

    def set_context(self, key, value):
        # type: (str, Any) -> None
        self._contexts[key] = value

    def to_json(self):
        # type: () -> Dict[str, Any]
        rv = super(Transaction, self).to_json()

        rv["name"] = self.name
        rv["source"] = self.source
        rv["sampled"] = self.sampled

        return rv

    def get_baggage(self):
        # type: () -> Baggage
        """
        The first time a new baggage with sentry items is made,
        it will be frozen.
        """
        if not self._baggage or self._baggage.mutable:
            self._baggage = Baggage.populate_from_transaction(self)

        return self._baggage

    def _set_initial_sampling_decision(self, sampling_context):
        # type: (SamplingContext) -> None
        """
        Sets the transaction's sampling decision, according to the following
        precedence rules:

        1. If a sampling decision is passed to `start_transaction`
        (`start_transaction(name: "my transaction", sampled: True)`), that
        decision will be used, regardless of anything else

        2. If `traces_sampler` is defined, its decision will be used. It can
        choose to keep or ignore any parent sampling decision, or use the
        sampling context data to make its own decision or to choose a sample
        rate for the transaction.

        3. If `traces_sampler` is not defined, but there's a parent sampling
        decision, the parent sampling decision will be used.

        4. If `traces_sampler` is not defined and there's no parent sampling
        decision, `traces_sample_rate` will be used.
        """

        hub = self.hub or sentry_sdk.Hub.current
        client = hub.client
        options = (client and client.options) or {}
        transaction_description = "{op}transaction <{name}>".format(
            op=("<" + self.op + "> " if self.op else ""), name=self.name
        )

        # nothing to do if there's no client or if tracing is disabled
        if not client or not has_tracing_enabled(options):
            self.sampled = False
            return

        # if the user has forced a sampling decision by passing a `sampled`
        # value when starting the transaction, go with that
        if self.sampled is not None:
            self.sample_rate = float(self.sampled)
            return

        # we would have bailed already if neither `traces_sampler` nor
        # `traces_sample_rate` were defined, so one of these should work; prefer
        # the hook if so
        sample_rate = (
            options["traces_sampler"](sampling_context)
            if callable(options.get("traces_sampler"))
            else (
                # default inheritance behavior
                sampling_context["parent_sampled"]
                if sampling_context["parent_sampled"] is not None
                else options["traces_sample_rate"]
            )
        )

        # Since this is coming from the user (or from a function provided by the
        # user), who knows what we might get. (The only valid values are
        # booleans or numbers between 0 and 1.)
        if not is_valid_sample_rate(sample_rate):
            logger.warning(
                "[Tracing] Discarding {transaction_description} because of invalid sample rate.".format(
                    transaction_description=transaction_description,
                )
            )
            self.sampled = False
            return

        self.sample_rate = float(sample_rate)

        # if the function returned 0 (or false), or if `traces_sample_rate` is
        # 0, it's a sign the transaction should be dropped
        if not sample_rate:
            logger.debug(
                "[Tracing] Discarding {transaction_description} because {reason}".format(
                    transaction_description=transaction_description,
                    reason=(
                        "traces_sampler returned 0 or False"
                        if callable(options.get("traces_sampler"))
                        else "traces_sample_rate is set to 0"
                    ),
                )
            )
            self.sampled = False
            return

        # Now we roll the dice. random.random is inclusive of 0, but not of 1,
        # so strict < is safe here. In case sample_rate is a boolean, cast it
        # to a float (True becomes 1.0 and False becomes 0.0)
        self.sampled = random.random() < float(sample_rate)

        if self.sampled:
            logger.debug(
                "[Tracing] Starting {transaction_description}".format(
                    transaction_description=transaction_description,
                )
            )
        else:
            logger.debug(
                "[Tracing] Discarding {transaction_description} because it's not included in the random sample (sampling rate = {sample_rate})".format(
                    transaction_description=transaction_description,
                    sample_rate=float(sample_rate),
                )
            )


class NoOpSpan(Span):
    def __repr__(self):
        # type: () -> str
        return self.__class__.__name__

    def start_child(self, instrumenter=INSTRUMENTER.SENTRY, **kwargs):
        # type: (str, **Any) -> NoOpSpan
        return NoOpSpan()

    def new_span(self, **kwargs):
        # type: (**Any) -> NoOpSpan
        pass

    def set_tag(self, key, value):
        # type: (str, Any) -> None
        pass

    def set_data(self, key, value):
        # type: (str, Any) -> None
        pass

    def set_status(self, value):
        # type: (str) -> None
        pass

    def set_http_status(self, http_status):
        # type: (int) -> None
        pass

    def finish(self, hub=None, end_timestamp=None):
        # type: (Optional[sentry_sdk.Hub], Optional[datetime]) -> Optional[str]
        pass


# Circular imports

from sentry_sdk.tracing_utils import (
    Baggage,
    EnvironHeaders,
    compute_tracestate_entry,
    extract_sentrytrace_data,
    extract_tracestate_data,
    has_tracestate_enabled,
    has_tracing_enabled,
    is_valid_sample_rate,
    maybe_create_breadcrumbs_from_span,
    has_custom_measurements_enabled,
)
