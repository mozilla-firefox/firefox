import os
import uuid
import random
from datetime import datetime
import socket

from sentry_sdk._compat import string_types, text_type, iteritems
from sentry_sdk.utils import (
    capture_internal_exceptions,
    current_stacktrace,
    disable_capture_event,
    format_timestamp,
    get_sdk_name,
    get_type_name,
    get_default_release,
    handle_in_app,
    logger,
)
from sentry_sdk.serializer import serialize
from sentry_sdk.transport import make_transport
from sentry_sdk.consts import (
    DEFAULT_OPTIONS,
    INSTRUMENTER,
    VERSION,
    ClientConstructor,
)
from sentry_sdk.integrations import setup_integrations
from sentry_sdk.utils import ContextVar
from sentry_sdk.sessions import SessionFlusher
from sentry_sdk.envelope import Envelope
from sentry_sdk.profiler import setup_profiler
from sentry_sdk.tracing_utils import has_tracestate_enabled, reinflate_tracestate

from sentry_sdk._types import MYPY

if MYPY:
    from typing import Any
    from typing import Callable
    from typing import Dict
    from typing import Optional

    from sentry_sdk.scope import Scope
    from sentry_sdk._types import Event, Hint
    from sentry_sdk.session import Session


_client_init_debug = ContextVar("client_init_debug")


SDK_INFO = {
    "name": "sentry.python",  # SDK name will be overridden after integrations have been loaded with sentry_sdk.integrations.setup_integrations()
    "version": VERSION,
    "packages": [{"name": "pypi:sentry-sdk", "version": VERSION}],
}


def _get_options(*args, **kwargs):
    # type: (*Optional[str], **Any) -> Dict[str, Any]
    if args and (isinstance(args[0], (text_type, bytes, str)) or args[0] is None):
        dsn = args[0]  # type: Optional[str]
        args = args[1:]
    else:
        dsn = None

    if len(args) > 1:
        raise TypeError("Only single positional argument is expected")

    rv = dict(DEFAULT_OPTIONS)
    options = dict(*args, **kwargs)
    if dsn is not None and options.get("dsn") is None:
        options["dsn"] = dsn

    for key, value in iteritems(options):
        if key not in rv:
            raise TypeError("Unknown option %r" % (key,))
        rv[key] = value

    if rv["dsn"] is None:
        rv["dsn"] = os.environ.get("SENTRY_DSN")

    if rv["release"] is None:
        rv["release"] = get_default_release()

    if rv["environment"] is None:
        rv["environment"] = os.environ.get("SENTRY_ENVIRONMENT") or "production"

    if rv["server_name"] is None and hasattr(socket, "gethostname"):
        rv["server_name"] = socket.gethostname()

    if rv["instrumenter"] is None:
        rv["instrumenter"] = INSTRUMENTER.SENTRY

    return rv


class _Client(object):
    """The client is internally responsible for capturing the events and
    forwarding them to sentry through the configured transport.  It takes
    the client options as keyword arguments and optionally the DSN as first
    argument.
    """

    def __init__(self, *args, **kwargs):
        # type: (*Any, **Any) -> None
        self.options = get_options(*args, **kwargs)  # type: Dict[str, Any]
        self._init_impl()

    def __getstate__(self):
        # type: () -> Any
        return {"options": self.options}

    def __setstate__(self, state):
        # type: (Any) -> None
        self.options = state["options"]
        self._init_impl()

    def _init_impl(self):
        # type: () -> None
        old_debug = _client_init_debug.get(False)

        def _capture_envelope(envelope):
            # type: (Envelope) -> None
            if self.transport is not None:
                self.transport.capture_envelope(envelope)

        try:
            _client_init_debug.set(self.options["debug"])
            self.transport = make_transport(self.options)

            self.session_flusher = SessionFlusher(capture_func=_capture_envelope)

            request_bodies = ("always", "never", "small", "medium")
            if self.options["request_bodies"] not in request_bodies:
                raise ValueError(
                    "Invalid value for request_bodies. Must be one of {}".format(
                        request_bodies
                    )
                )

            self.integrations = setup_integrations(
                self.options["integrations"],
                with_defaults=self.options["default_integrations"],
                with_auto_enabling_integrations=self.options[
                    "auto_enabling_integrations"
                ],
            )

            sdk_name = get_sdk_name(list(self.integrations.keys()))
            SDK_INFO["name"] = sdk_name
            logger.debug("Setting SDK name to '%s'", sdk_name)

        finally:
            _client_init_debug.set(old_debug)

        profiles_sample_rate = self.options["_experiments"].get("profiles_sample_rate")
        if profiles_sample_rate is not None and profiles_sample_rate > 0:
            try:
                setup_profiler(self.options)
            except ValueError as e:
                logger.debug(str(e))

    @property
    def dsn(self):
        # type: () -> Optional[str]
        """Returns the configured DSN as string."""
        return self.options["dsn"]

    def _prepare_event(
        self,
        event,  # type: Event
        hint,  # type: Hint
        scope,  # type: Optional[Scope]
    ):
        # type: (...) -> Optional[Event]

        if event.get("timestamp") is None:
            event["timestamp"] = datetime.utcnow()

        if scope is not None:
            is_transaction = event.get("type") == "transaction"
            event_ = scope.apply_to_event(event, hint)

            # one of the event/error processors returned None
            if event_ is None:
                if self.transport:
                    self.transport.record_lost_event(
                        "event_processor",
                        data_category=("transaction" if is_transaction else "error"),
                    )
                return None

            event = event_

        if (
            self.options["attach_stacktrace"]
            and "exception" not in event
            and "stacktrace" not in event
            and "threads" not in event
        ):
            with capture_internal_exceptions():
                event["threads"] = {
                    "values": [
                        {
                            "stacktrace": current_stacktrace(
                                self.options["with_locals"]
                            ),
                            "crashed": False,
                            "current": True,
                        }
                    ]
                }

        for key in "release", "environment", "server_name", "dist":
            if event.get(key) is None and self.options[key] is not None:
                event[key] = text_type(self.options[key]).strip()
        if event.get("sdk") is None:
            sdk_info = dict(SDK_INFO)
            sdk_info["integrations"] = sorted(self.integrations.keys())
            event["sdk"] = sdk_info

        if event.get("platform") is None:
            event["platform"] = "python"

        event = handle_in_app(
            event, self.options["in_app_exclude"], self.options["in_app_include"]
        )

        # Postprocess the event here so that annotated types do
        # generally not surface in before_send
        if event is not None:
            event = serialize(
                event,
                smart_transaction_trimming=self.options["_experiments"].get(
                    "smart_transaction_trimming"
                ),
            )

        before_send = self.options["before_send"]
        if before_send is not None and event.get("type") != "transaction":
            new_event = None
            with capture_internal_exceptions():
                new_event = before_send(event, hint or {})
            if new_event is None:
                logger.info("before send dropped event (%s)", event)
                if self.transport:
                    self.transport.record_lost_event(
                        "before_send", data_category="error"
                    )
            event = new_event  # type: ignore

        before_send_transaction = self.options["before_send_transaction"]
        if before_send_transaction is not None and event.get("type") == "transaction":
            new_event = None
            with capture_internal_exceptions():
                new_event = before_send_transaction(event, hint or {})
            if new_event is None:
                logger.info("before send transaction dropped event (%s)", event)
                if self.transport:
                    self.transport.record_lost_event(
                        "before_send", data_category="transaction"
                    )
            event = new_event  # type: ignore

        return event

    def _is_ignored_error(self, event, hint):
        # type: (Event, Hint) -> bool
        exc_info = hint.get("exc_info")
        if exc_info is None:
            return False

        error = exc_info[0]
        error_type_name = get_type_name(exc_info[0])
        error_full_name = "%s.%s" % (exc_info[0].__module__, error_type_name)

        for ignored_error in self.options["ignore_errors"]:
            # String types are matched against the type name in the
            # exception only
            if isinstance(ignored_error, string_types):
                if ignored_error == error_full_name or ignored_error == error_type_name:
                    return True
            else:
                if issubclass(error, ignored_error):
                    return True

        return False

    def _should_capture(
        self,
        event,  # type: Event
        hint,  # type: Hint
        scope=None,  # type: Optional[Scope]
    ):
        # type: (...) -> bool
        # Transactions are sampled independent of error events.
        is_transaction = event.get("type") == "transaction"
        if is_transaction:
            return True

        ignoring_prevents_recursion = scope is not None and not scope._should_capture
        if ignoring_prevents_recursion:
            return False

        ignored_by_config_option = self._is_ignored_error(event, hint)
        if ignored_by_config_option:
            return False

        return True

    def _should_sample_error(
        self,
        event,  # type: Event
    ):
        # type: (...) -> bool
        not_in_sample_rate = (
            self.options["sample_rate"] < 1.0
            and random.random() >= self.options["sample_rate"]
        )
        if not_in_sample_rate:
            # because we will not sample this event, record a "lost event".
            if self.transport:
                self.transport.record_lost_event("sample_rate", data_category="error")

            return False

        return True

    def _update_session_from_event(
        self,
        session,  # type: Session
        event,  # type: Event
    ):
        # type: (...) -> None

        crashed = False
        errored = False
        user_agent = None

        exceptions = (event.get("exception") or {}).get("values")
        if exceptions:
            errored = True
            for error in exceptions:
                mechanism = error.get("mechanism")
                if mechanism and mechanism.get("handled") is False:
                    crashed = True
                    break

        user = event.get("user")

        if session.user_agent is None:
            headers = (event.get("request") or {}).get("headers")
            for (k, v) in iteritems(headers or {}):
                if k.lower() == "user-agent":
                    user_agent = v
                    break

        session.update(
            status="crashed" if crashed else None,
            user=user,
            user_agent=user_agent,
            errors=session.errors + (errored or crashed),
        )

    def capture_event(
        self,
        event,  # type: Event
        hint=None,  # type: Optional[Hint]
        scope=None,  # type: Optional[Scope]
    ):
        # type: (...) -> Optional[str]
        """Captures an event.

        :param event: A ready-made event that can be directly sent to Sentry.

        :param hint: Contains metadata about the event that can be read from `before_send`, such as the original exception object or a HTTP request object.

        :returns: An event ID. May be `None` if there is no DSN set or of if the SDK decided to discard the event for other reasons. In such situations setting `debug=True` on `init()` may help.
        """
        if disable_capture_event.get(False):
            return None

        if self.transport is None:
            return None
        if hint is None:
            hint = {}
        event_id = event.get("event_id")
        hint = dict(hint or ())  # type: Hint

        if event_id is None:
            event["event_id"] = event_id = uuid.uuid4().hex
        if not self._should_capture(event, hint, scope):
            return None

        profile = event.pop("profile", None)

        event_opt = self._prepare_event(event, hint, scope)
        if event_opt is None:
            return None

        # whenever we capture an event we also check if the session needs
        # to be updated based on that information.
        session = scope._session if scope else None
        if session:
            self._update_session_from_event(session, event)

        is_transaction = event_opt.get("type") == "transaction"

        if not is_transaction and not self._should_sample_error(event):
            return None

        attachments = hint.get("attachments")

        # this is outside of the `if` immediately below because even if we don't
        # use the value, we want to make sure we remove it before the event is
        # sent
        raw_tracestate = (
            event_opt.get("contexts", {}).get("trace", {}).pop("tracestate", "")
        )

        dynamic_sampling_context = (
            event_opt.get("contexts", {})
            .get("trace", {})
            .pop("dynamic_sampling_context", {})
        )

        # Transactions or events with attachments should go to the /envelope/
        # endpoint.
        if is_transaction or attachments:

            headers = {
                "event_id": event_opt["event_id"],
                "sent_at": format_timestamp(datetime.utcnow()),
            }

            if has_tracestate_enabled():
                tracestate_data = raw_tracestate and reinflate_tracestate(
                    raw_tracestate.replace("sentry=", "")
                )

                if tracestate_data:
                    headers["trace"] = tracestate_data
            elif dynamic_sampling_context:
                headers["trace"] = dynamic_sampling_context

            envelope = Envelope(headers=headers)

            if is_transaction:
                if profile is not None:
                    envelope.add_profile(profile.to_json(event_opt, self.options))
                envelope.add_transaction(event_opt)
            else:
                envelope.add_event(event_opt)

            for attachment in attachments or ():
                envelope.add_item(attachment.to_envelope_item())
            self.transport.capture_envelope(envelope)
        else:
            # All other events go to the /store/ endpoint.
            self.transport.capture_event(event_opt)
        return event_id

    def capture_session(
        self, session  # type: Session
    ):
        # type: (...) -> None
        if not session.release:
            logger.info("Discarded session update because of missing release")
        else:
            self.session_flusher.add_session(session)

    def close(
        self,
        timeout=None,  # type: Optional[float]
        callback=None,  # type: Optional[Callable[[int, float], None]]
    ):
        # type: (...) -> None
        """
        Close the client and shut down the transport. Arguments have the same
        semantics as :py:meth:`Client.flush`.
        """
        if self.transport is not None:
            self.flush(timeout=timeout, callback=callback)
            self.session_flusher.kill()
            self.transport.kill()
            self.transport = None

    def flush(
        self,
        timeout=None,  # type: Optional[float]
        callback=None,  # type: Optional[Callable[[int, float], None]]
    ):
        # type: (...) -> None
        """
        Wait for the current events to be sent.

        :param timeout: Wait for at most `timeout` seconds. If no `timeout` is provided, the `shutdown_timeout` option value is used.

        :param callback: Is invoked with the number of pending events and the configured timeout.
        """
        if self.transport is not None:
            if timeout is None:
                timeout = self.options["shutdown_timeout"]
            self.session_flusher.flush()
            self.transport.flush(timeout=timeout, callback=callback)

    def __enter__(self):
        # type: () -> _Client
        return self

    def __exit__(self, exc_type, exc_value, tb):
        # type: (Any, Any, Any) -> None
        self.close()


from sentry_sdk._types import MYPY

if MYPY:
    # Make mypy, PyCharm and other static analyzers think `get_options` is a
    # type to have nicer autocompletion for params.
    #
    # Use `ClientConstructor` to define the argument types of `init` and
    # `Dict[str, Any]` to tell static analyzers about the return type.

    class get_options(ClientConstructor, Dict[str, Any]):  # noqa: N801
        pass

    class Client(ClientConstructor, _Client):
        pass

else:
    # Alias `get_options` for actual usage. Go through the lambda indirection
    # to throw PyCharm off of the weakly typed signature (it would otherwise
    # discover both the weakly typed signature of `_init` and our faked `init`
    # type).

    get_options = (lambda: _get_options)()
    Client = (lambda: _Client)()
