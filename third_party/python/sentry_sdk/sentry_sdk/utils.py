import base64
import json
import linecache
import logging
import os
import re
import subprocess
import sys
import threading
import time
from datetime import datetime
from functools import partial

try:
    from functools import partialmethod

    _PARTIALMETHOD_AVAILABLE = True
except ImportError:
    _PARTIALMETHOD_AVAILABLE = False

import sentry_sdk
from sentry_sdk._compat import PY2, PY33, PY37, implements_str, text_type, urlparse
from sentry_sdk._types import MYPY

if MYPY:
    from types import FrameType, TracebackType
    from typing import (
        Any,
        Callable,
        ContextManager,
        Dict,
        Iterator,
        List,
        Optional,
        Set,
        Tuple,
        Type,
        Union,
    )

    from sentry_sdk._types import EndpointType, ExcInfo


epoch = datetime(1970, 1, 1)


# The logger is created here but initialized in the debug support module
logger = logging.getLogger("sentry_sdk.errors")

MAX_STRING_LENGTH = 1024
BASE64_ALPHABET = re.compile(r"^[a-zA-Z0-9/+=]*$")


def json_dumps(data):
    # type: (Any) -> bytes
    """Serialize data into a compact JSON representation encoded as UTF-8."""
    return json.dumps(data, allow_nan=False, separators=(",", ":")).encode("utf-8")


def _get_debug_hub():
    # type: () -> Optional[sentry_sdk.Hub]
    # This function is replaced by debug.py
    pass


def get_default_release():
    # type: () -> Optional[str]
    """Try to guess a default release."""
    release = os.environ.get("SENTRY_RELEASE")
    if release:
        return release

    with open(os.path.devnull, "w+") as null:
        try:
            release = (
                subprocess.Popen(
                    ["git", "rev-parse", "HEAD"],
                    stdout=subprocess.PIPE,
                    stderr=null,
                    stdin=null,
                )
                .communicate()[0]
                .strip()
                .decode("utf-8")
            )
        except (OSError, IOError):
            pass

        if release:
            return release

    for var in (
        "HEROKU_SLUG_COMMIT",
        "SOURCE_VERSION",
        "CODEBUILD_RESOLVED_SOURCE_VERSION",
        "CIRCLE_SHA1",
        "GAE_DEPLOYMENT_ID",
    ):
        release = os.environ.get(var)
        if release:
            return release
    return None


def get_sdk_name(installed_integrations):
    # type: (List[str]) -> str
    """Return the SDK name including the name of the used web framework."""

    # Note: I can not use for example sentry_sdk.integrations.django.DjangoIntegration.identifier
    # here because if django is not installed the integration is not accessible.
    framework_integrations = [
        "django",
        "flask",
        "fastapi",
        "bottle",
        "falcon",
        "quart",
        "sanic",
        "starlette",
        "chalice",
        "serverless",
        "pyramid",
        "tornado",
        "aiohttp",
        "aws_lambda",
        "gcp",
        "beam",
        "asgi",
        "wsgi",
    ]

    for integration in framework_integrations:
        if integration in installed_integrations:
            return "sentry.python.{}".format(integration)

    return "sentry.python"


class CaptureInternalException(object):
    __slots__ = ()

    def __enter__(self):
        # type: () -> ContextManager[Any]
        return self

    def __exit__(self, ty, value, tb):
        # type: (Optional[Type[BaseException]], Optional[BaseException], Optional[TracebackType]) -> bool
        if ty is not None and value is not None:
            capture_internal_exception((ty, value, tb))

        return True


_CAPTURE_INTERNAL_EXCEPTION = CaptureInternalException()


def capture_internal_exceptions():
    # type: () -> ContextManager[Any]
    return _CAPTURE_INTERNAL_EXCEPTION


def capture_internal_exception(exc_info):
    # type: (ExcInfo) -> None
    hub = _get_debug_hub()
    if hub is not None:
        hub._capture_internal_exception(exc_info)


def to_timestamp(value):
    # type: (datetime) -> float
    return (value - epoch).total_seconds()


def format_timestamp(value):
    # type: (datetime) -> str
    return value.strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def event_hint_with_exc_info(exc_info=None):
    # type: (Optional[ExcInfo]) -> Dict[str, Optional[ExcInfo]]
    """Creates a hint with the exc info filled in."""
    if exc_info is None:
        exc_info = sys.exc_info()
    else:
        exc_info = exc_info_from_error(exc_info)
    if exc_info[0] is None:
        exc_info = None
    return {"exc_info": exc_info}


class BadDsn(ValueError):
    """Raised on invalid DSNs."""


@implements_str
class Dsn(object):
    """Represents a DSN."""

    def __init__(self, value):
        # type: (Union[Dsn, str]) -> None
        if isinstance(value, Dsn):
            self.__dict__ = dict(value.__dict__)
            return
        parts = urlparse.urlsplit(text_type(value))

        if parts.scheme not in ("http", "https"):
            raise BadDsn("Unsupported scheme %r" % parts.scheme)
        self.scheme = parts.scheme

        if parts.hostname is None:
            raise BadDsn("Missing hostname")

        self.host = parts.hostname

        if parts.port is None:
            self.port = self.scheme == "https" and 443 or 80  # type: int
        else:
            self.port = parts.port

        if not parts.username:
            raise BadDsn("Missing public key")

        self.public_key = parts.username
        self.secret_key = parts.password

        path = parts.path.rsplit("/", 1)

        try:
            self.project_id = text_type(int(path.pop()))
        except (ValueError, TypeError):
            raise BadDsn("Invalid project in DSN (%r)" % (parts.path or "")[1:])

        self.path = "/".join(path) + "/"

    @property
    def netloc(self):
        # type: () -> str
        """The netloc part of a DSN."""
        rv = self.host
        if (self.scheme, self.port) not in (("http", 80), ("https", 443)):
            rv = "%s:%s" % (rv, self.port)
        return rv

    def to_auth(self, client=None):
        # type: (Optional[Any]) -> Auth
        """Returns the auth info object for this dsn."""
        return Auth(
            scheme=self.scheme,
            host=self.netloc,
            path=self.path,
            project_id=self.project_id,
            public_key=self.public_key,
            secret_key=self.secret_key,
            client=client,
        )

    def __str__(self):
        # type: () -> str
        return "%s://%s%s@%s%s%s" % (
            self.scheme,
            self.public_key,
            self.secret_key and "@" + self.secret_key or "",
            self.netloc,
            self.path,
            self.project_id,
        )


class Auth(object):
    """Helper object that represents the auth info."""

    def __init__(
        self,
        scheme,
        host,
        project_id,
        public_key,
        secret_key=None,
        version=7,
        client=None,
        path="/",
    ):
        # type: (str, str, str, str, Optional[str], int, Optional[Any], str) -> None
        self.scheme = scheme
        self.host = host
        self.path = path
        self.project_id = project_id
        self.public_key = public_key
        self.secret_key = secret_key
        self.version = version
        self.client = client

    @property
    def store_api_url(self):
        # type: () -> str
        """Returns the API url for storing events.

        Deprecated: use get_api_url instead.
        """
        return self.get_api_url(type="store")

    def get_api_url(
        self, type="store"  # type: EndpointType
    ):
        # type: (...) -> str
        """Returns the API url for storing events."""
        return "%s://%s%sapi/%s/%s/" % (
            self.scheme,
            self.host,
            self.path,
            self.project_id,
            type,
        )

    def to_header(self):
        # type: () -> str
        """Returns the auth header a string."""
        rv = [("sentry_key", self.public_key), ("sentry_version", self.version)]
        if self.client is not None:
            rv.append(("sentry_client", self.client))
        if self.secret_key is not None:
            rv.append(("sentry_secret", self.secret_key))
        return "Sentry " + ", ".join("%s=%s" % (key, value) for key, value in rv)


class AnnotatedValue(object):
    """
    Meta information for a data field in the event payload.
    This is to tell Relay that we have tampered with the fields value.
    See:
    https://github.com/getsentry/relay/blob/be12cd49a0f06ea932ed9b9f93a655de5d6ad6d1/relay-general/src/types/meta.rs#L407-L423
    """

    __slots__ = ("value", "metadata")

    def __init__(self, value, metadata):
        # type: (Optional[Any], Dict[str, Any]) -> None
        self.value = value
        self.metadata = metadata

    @classmethod
    def removed_because_raw_data(cls):
        # type: () -> AnnotatedValue
        """The value was removed because it could not be parsed. This is done for request body values that are not json nor a form."""
        return AnnotatedValue(
            value="",
            metadata={
                "rem": [  # Remark
                    [
                        "!raw",  # Unparsable raw data
                        "x",  # The fields original value was removed
                    ]
                ]
            },
        )

    @classmethod
    def removed_because_over_size_limit(cls):
        # type: () -> AnnotatedValue
        """The actual value was removed because the size of the field exceeded the configured maximum size (specified with the request_bodies sdk option)"""
        return AnnotatedValue(
            value="",
            metadata={
                "rem": [  # Remark
                    [
                        "!config",  # Because of configured maximum size
                        "x",  # The fields original value was removed
                    ]
                ]
            },
        )

    @classmethod
    def substituted_because_contains_sensitive_data(cls):
        # type: () -> AnnotatedValue
        """The actual value was removed because it contained sensitive information."""
        from sentry_sdk.consts import SENSITIVE_DATA_SUBSTITUTE

        return AnnotatedValue(
            value=SENSITIVE_DATA_SUBSTITUTE,
            metadata={
                "rem": [  # Remark
                    [
                        "!config",  # Because of SDK configuration (in this case the config is the hard coded removal of certain django cookies)
                        "s",  # The fields original value was substituted
                    ]
                ]
            },
        )


if MYPY:
    from typing import TypeVar

    T = TypeVar("T")
    Annotated = Union[AnnotatedValue, T]


def get_type_name(cls):
    # type: (Optional[type]) -> Optional[str]
    return getattr(cls, "__qualname__", None) or getattr(cls, "__name__", None)


def get_type_module(cls):
    # type: (Optional[type]) -> Optional[str]
    mod = getattr(cls, "__module__", None)
    if mod not in (None, "builtins", "__builtins__"):
        return mod
    return None


def should_hide_frame(frame):
    # type: (FrameType) -> bool
    try:
        mod = frame.f_globals["__name__"]
        if mod.startswith("sentry_sdk."):
            return True
    except (AttributeError, KeyError):
        pass

    for flag_name in "__traceback_hide__", "__tracebackhide__":
        try:
            if frame.f_locals[flag_name]:
                return True
        except Exception:
            pass

    return False


def iter_stacks(tb):
    # type: (Optional[TracebackType]) -> Iterator[TracebackType]
    tb_ = tb  # type: Optional[TracebackType]
    while tb_ is not None:
        if not should_hide_frame(tb_.tb_frame):
            yield tb_
        tb_ = tb_.tb_next


def get_lines_from_file(
    filename,  # type: str
    lineno,  # type: int
    loader=None,  # type: Optional[Any]
    module=None,  # type: Optional[str]
):
    # type: (...) -> Tuple[List[Annotated[str]], Optional[Annotated[str]], List[Annotated[str]]]
    context_lines = 5
    source = None
    if loader is not None and hasattr(loader, "get_source"):
        try:
            source_str = loader.get_source(module)  # type: Optional[str]
        except (ImportError, IOError):
            source_str = None
        if source_str is not None:
            source = source_str.splitlines()

    if source is None:
        try:
            source = linecache.getlines(filename)
        except (OSError, IOError):
            return [], None, []

    if not source:
        return [], None, []

    lower_bound = max(0, lineno - context_lines)
    upper_bound = min(lineno + 1 + context_lines, len(source))

    try:
        pre_context = [
            strip_string(line.strip("\r\n")) for line in source[lower_bound:lineno]
        ]
        context_line = strip_string(source[lineno].strip("\r\n"))
        post_context = [
            strip_string(line.strip("\r\n"))
            for line in source[(lineno + 1) : upper_bound]
        ]
        return pre_context, context_line, post_context
    except IndexError:
        # the file may have changed since it was loaded into memory
        return [], None, []


def get_source_context(
    frame,  # type: FrameType
    tb_lineno,  # type: int
):
    # type: (...) -> Tuple[List[Annotated[str]], Optional[Annotated[str]], List[Annotated[str]]]
    try:
        abs_path = frame.f_code.co_filename  # type: Optional[str]
    except Exception:
        abs_path = None
    try:
        module = frame.f_globals["__name__"]
    except Exception:
        return [], None, []
    try:
        loader = frame.f_globals["__loader__"]
    except Exception:
        loader = None
    lineno = tb_lineno - 1
    if lineno is not None and abs_path:
        return get_lines_from_file(abs_path, lineno, loader, module)
    return [], None, []


def safe_str(value):
    # type: (Any) -> str
    try:
        return text_type(value)
    except Exception:
        return safe_repr(value)


if PY2:

    def safe_repr(value):
        # type: (Any) -> str
        try:
            rv = repr(value).decode("utf-8", "replace")

            # At this point `rv` contains a bunch of literal escape codes, like
            # this (exaggerated example):
            #
            # u"\\x2f"
            #
            # But we want to show this string as:
            #
            # u"/"
            try:
                # unicode-escape does this job, but can only decode latin1. So we
                # attempt to encode in latin1.
                return rv.encode("latin1").decode("unicode-escape")
            except Exception:
                # Since usually strings aren't latin1 this can break. In those
                # cases we just give up.
                return rv
        except Exception:
            # If e.g. the call to `repr` already fails
            return "<broken repr>"

else:

    def safe_repr(value):
        # type: (Any) -> str
        try:
            return repr(value)
        except Exception:
            return "<broken repr>"


def filename_for_module(module, abs_path):
    # type: (Optional[str], Optional[str]) -> Optional[str]
    if not abs_path or not module:
        return abs_path

    try:
        if abs_path.endswith(".pyc"):
            abs_path = abs_path[:-1]

        base_module = module.split(".", 1)[0]
        if base_module == module:
            return os.path.basename(abs_path)

        base_module_path = sys.modules[base_module].__file__
        if not base_module_path:
            return abs_path

        return abs_path.split(base_module_path.rsplit(os.sep, 2)[0], 1)[-1].lstrip(
            os.sep
        )
    except Exception:
        return abs_path


def serialize_frame(frame, tb_lineno=None, with_locals=True):
    # type: (FrameType, Optional[int], bool) -> Dict[str, Any]
    f_code = getattr(frame, "f_code", None)
    if not f_code:
        abs_path = None
        function = None
    else:
        abs_path = frame.f_code.co_filename
        function = frame.f_code.co_name
    try:
        module = frame.f_globals["__name__"]
    except Exception:
        module = None

    if tb_lineno is None:
        tb_lineno = frame.f_lineno

    pre_context, context_line, post_context = get_source_context(frame, tb_lineno)

    rv = {
        "filename": filename_for_module(module, abs_path) or None,
        "abs_path": os.path.abspath(abs_path) if abs_path else None,
        "function": function or "<unknown>",
        "module": module,
        "lineno": tb_lineno,
        "pre_context": pre_context,
        "context_line": context_line,
        "post_context": post_context,
    }  # type: Dict[str, Any]
    if with_locals:
        rv["vars"] = frame.f_locals

    return rv


def current_stacktrace(with_locals=True):
    # type: (bool) -> Any
    __tracebackhide__ = True
    frames = []

    f = sys._getframe()  # type: Optional[FrameType]
    while f is not None:
        if not should_hide_frame(f):
            frames.append(serialize_frame(f, with_locals=with_locals))
        f = f.f_back

    frames.reverse()

    return {"frames": frames}


def get_errno(exc_value):
    # type: (BaseException) -> Optional[Any]
    return getattr(exc_value, "errno", None)


def single_exception_from_error_tuple(
    exc_type,  # type: Optional[type]
    exc_value,  # type: Optional[BaseException]
    tb,  # type: Optional[TracebackType]
    client_options=None,  # type: Optional[Dict[str, Any]]
    mechanism=None,  # type: Optional[Dict[str, Any]]
):
    # type: (...) -> Dict[str, Any]
    if exc_value is not None:
        errno = get_errno(exc_value)
    else:
        errno = None

    if errno is not None:
        mechanism = mechanism or {"type": "generic"}
        mechanism.setdefault("meta", {}).setdefault("errno", {}).setdefault(
            "number", errno
        )

    if client_options is None:
        with_locals = True
    else:
        with_locals = client_options["with_locals"]

    frames = [
        serialize_frame(tb.tb_frame, tb_lineno=tb.tb_lineno, with_locals=with_locals)
        for tb in iter_stacks(tb)
    ]

    rv = {
        "module": get_type_module(exc_type),
        "type": get_type_name(exc_type),
        "value": safe_str(exc_value),
        "mechanism": mechanism,
    }

    if frames:
        rv["stacktrace"] = {"frames": frames}

    return rv


HAS_CHAINED_EXCEPTIONS = hasattr(Exception, "__suppress_context__")

if HAS_CHAINED_EXCEPTIONS:

    def walk_exception_chain(exc_info):
        # type: (ExcInfo) -> Iterator[ExcInfo]
        exc_type, exc_value, tb = exc_info

        seen_exceptions = []
        seen_exception_ids = set()  # type: Set[int]

        while (
            exc_type is not None
            and exc_value is not None
            and id(exc_value) not in seen_exception_ids
        ):
            yield exc_type, exc_value, tb

            # Avoid hashing random types we don't know anything
            # about. Use the list to keep a ref so that the `id` is
            # not used for another object.
            seen_exceptions.append(exc_value)
            seen_exception_ids.add(id(exc_value))

            if exc_value.__suppress_context__:
                cause = exc_value.__cause__
            else:
                cause = exc_value.__context__
            if cause is None:
                break
            exc_type = type(cause)
            exc_value = cause
            tb = getattr(cause, "__traceback__", None)

else:

    def walk_exception_chain(exc_info):
        # type: (ExcInfo) -> Iterator[ExcInfo]
        yield exc_info


def exceptions_from_error_tuple(
    exc_info,  # type: ExcInfo
    client_options=None,  # type: Optional[Dict[str, Any]]
    mechanism=None,  # type: Optional[Dict[str, Any]]
):
    # type: (...) -> List[Dict[str, Any]]
    exc_type, exc_value, tb = exc_info
    rv = []
    for exc_type, exc_value, tb in walk_exception_chain(exc_info):
        rv.append(
            single_exception_from_error_tuple(
                exc_type, exc_value, tb, client_options, mechanism
            )
        )

    rv.reverse()

    return rv


def to_string(value):
    # type: (str) -> str
    try:
        return text_type(value)
    except UnicodeDecodeError:
        return repr(value)[1:-1]


def iter_event_stacktraces(event):
    # type: (Dict[str, Any]) -> Iterator[Dict[str, Any]]
    if "stacktrace" in event:
        yield event["stacktrace"]
    if "threads" in event:
        for thread in event["threads"].get("values") or ():
            if "stacktrace" in thread:
                yield thread["stacktrace"]
    if "exception" in event:
        for exception in event["exception"].get("values") or ():
            if "stacktrace" in exception:
                yield exception["stacktrace"]


def iter_event_frames(event):
    # type: (Dict[str, Any]) -> Iterator[Dict[str, Any]]
    for stacktrace in iter_event_stacktraces(event):
        for frame in stacktrace.get("frames") or ():
            yield frame


def handle_in_app(event, in_app_exclude=None, in_app_include=None):
    # type: (Dict[str, Any], Optional[List[str]], Optional[List[str]]) -> Dict[str, Any]
    for stacktrace in iter_event_stacktraces(event):
        handle_in_app_impl(
            stacktrace.get("frames"),
            in_app_exclude=in_app_exclude,
            in_app_include=in_app_include,
        )

    return event


def handle_in_app_impl(frames, in_app_exclude, in_app_include):
    # type: (Any, Optional[List[str]], Optional[List[str]]) -> Optional[Any]
    if not frames:
        return None

    any_in_app = False
    for frame in frames:
        in_app = frame.get("in_app")
        if in_app is not None:
            if in_app:
                any_in_app = True
            continue

        module = frame.get("module")
        if not module:
            continue
        elif _module_in_set(module, in_app_include):
            frame["in_app"] = True
            any_in_app = True
        elif _module_in_set(module, in_app_exclude):
            frame["in_app"] = False

    if not any_in_app:
        for frame in frames:
            if frame.get("in_app") is None:
                frame["in_app"] = True

    return frames


def exc_info_from_error(error):
    # type: (Union[BaseException, ExcInfo]) -> ExcInfo
    if isinstance(error, tuple) and len(error) == 3:
        exc_type, exc_value, tb = error
    elif isinstance(error, BaseException):
        tb = getattr(error, "__traceback__", None)
        if tb is not None:
            exc_type = type(error)
            exc_value = error
        else:
            exc_type, exc_value, tb = sys.exc_info()
            if exc_value is not error:
                tb = None
                exc_value = error
                exc_type = type(error)

    else:
        raise ValueError("Expected Exception object to report, got %s!" % type(error))

    return exc_type, exc_value, tb


def event_from_exception(
    exc_info,  # type: Union[BaseException, ExcInfo]
    client_options=None,  # type: Optional[Dict[str, Any]]
    mechanism=None,  # type: Optional[Dict[str, Any]]
):
    # type: (...) -> Tuple[Dict[str, Any], Dict[str, Any]]
    exc_info = exc_info_from_error(exc_info)
    hint = event_hint_with_exc_info(exc_info)
    return (
        {
            "level": "error",
            "exception": {
                "values": exceptions_from_error_tuple(
                    exc_info, client_options, mechanism
                )
            },
        },
        hint,
    )


def _module_in_set(name, set):
    # type: (str, Optional[List[str]]) -> bool
    if not set:
        return False
    for item in set or ():
        if item == name or name.startswith(item + "."):
            return True
    return False


def strip_string(value, max_length=None):
    # type: (str, Optional[int]) -> Union[AnnotatedValue, str]
    # TODO: read max_length from config
    if not value:
        return value

    if max_length is None:
        # This is intentionally not just the default such that one can patch `MAX_STRING_LENGTH` and affect `strip_string`.
        max_length = MAX_STRING_LENGTH

    length = len(value.encode("utf-8"))

    if length > max_length:
        return AnnotatedValue(
            value=value[: max_length - 3] + "...",
            metadata={
                "len": length,
                "rem": [["!limit", "x", max_length - 3, max_length]],
            },
        )
    return value


def _is_contextvars_broken():
    # type: () -> bool
    """
    Returns whether gevent/eventlet have patched the stdlib in a way where thread locals are now more "correct" than contextvars.
    """
    try:
        import gevent  # type: ignore
        from gevent.monkey import is_object_patched  # type: ignore

        # Get the MAJOR and MINOR version numbers of Gevent
        version_tuple = tuple(
            [int(part) for part in re.split(r"a|b|rc|\.", gevent.__version__)[:2]]
        )
        if is_object_patched("threading", "local"):
            # Gevent 20.9.0 depends on Greenlet 0.4.17 which natively handles switching
            # context vars when greenlets are switched, so, Gevent 20.9.0+ is all fine.
            # Ref: https://github.com/gevent/gevent/blob/83c9e2ae5b0834b8f84233760aabe82c3ba065b4/src/gevent/monkey.py#L604-L609
            # Gevent 20.5, that doesn't depend on Greenlet 0.4.17 with native support
            # for contextvars, is able to patch both thread locals and contextvars, in
            # that case, check if contextvars are effectively patched.
            if (
                # Gevent 20.9.0+
                (sys.version_info >= (3, 7) and version_tuple >= (20, 9))
                # Gevent 20.5.0+ or Python < 3.7
                or (is_object_patched("contextvars", "ContextVar"))
            ):
                return False

            return True
    except ImportError:
        pass

    try:
        from eventlet.patcher import is_monkey_patched  # type: ignore

        if is_monkey_patched("thread"):
            return True
    except ImportError:
        pass

    return False


def _make_threadlocal_contextvars(local):
    # type: (type) -> type
    class ContextVar(object):
        # Super-limited impl of ContextVar

        def __init__(self, name):
            # type: (str) -> None
            self._name = name
            self._local = local()

        def get(self, default):
            # type: (Any) -> Any
            return getattr(self._local, "value", default)

        def set(self, value):
            # type: (Any) -> None
            self._local.value = value

    return ContextVar


def _get_contextvars():
    # type: () -> Tuple[bool, type]
    """
    Figure out the "right" contextvars installation to use. Returns a
    `contextvars.ContextVar`-like class with a limited API.

    See https://docs.sentry.io/platforms/python/contextvars/ for more information.
    """
    if not _is_contextvars_broken():
        # aiocontextvars is a PyPI package that ensures that the contextvars
        # backport (also a PyPI package) works with asyncio under Python 3.6
        #
        # Import it if available.
        if sys.version_info < (3, 7):
            # `aiocontextvars` is absolutely required for functional
            # contextvars on Python 3.6.
            try:
                from aiocontextvars import ContextVar

                return True, ContextVar
            except ImportError:
                pass
        else:
            # On Python 3.7 contextvars are functional.
            try:
                from contextvars import ContextVar

                return True, ContextVar
            except ImportError:
                pass

    # Fall back to basic thread-local usage.

    from threading import local

    return False, _make_threadlocal_contextvars(local)


HAS_REAL_CONTEXTVARS, ContextVar = _get_contextvars()

CONTEXTVARS_ERROR_MESSAGE = """

With asyncio/ASGI applications, the Sentry SDK requires a functional
installation of `contextvars` to avoid leaking scope/context data across
requests.

Please refer to https://docs.sentry.io/platforms/python/contextvars/ for more information.
"""


def qualname_from_function(func):
    # type: (Callable[..., Any]) -> Optional[str]
    """Return the qualified name of func. Works with regular function, lambda, partial and partialmethod."""
    func_qualname = None  # type: Optional[str]

    # Python 2
    try:
        return "%s.%s.%s" % (
            func.im_class.__module__,  # type: ignore
            func.im_class.__name__,  # type: ignore
            func.__name__,
        )
    except Exception:
        pass

    prefix, suffix = "", ""

    if (
        _PARTIALMETHOD_AVAILABLE
        and hasattr(func, "_partialmethod")
        and isinstance(func._partialmethod, partialmethod)  # type: ignore
    ):
        prefix, suffix = "partialmethod(<function ", ">)"
        func = func._partialmethod.func  # type: ignore
    elif isinstance(func, partial) and hasattr(func.func, "__name__"):
        prefix, suffix = "partial(<function ", ">)"
        func = func.func

    if hasattr(func, "__qualname__"):
        func_qualname = func.__qualname__
    elif hasattr(func, "__name__"):  # Python 2.7 has no __qualname__
        func_qualname = func.__name__

    # Python 3: methods, functions, classes
    if func_qualname is not None:
        if hasattr(func, "__module__"):
            func_qualname = func.__module__ + "." + func_qualname
        func_qualname = prefix + func_qualname + suffix

    return func_qualname


def transaction_from_function(func):
    # type: (Callable[..., Any]) -> Optional[str]
    return qualname_from_function(func)


disable_capture_event = ContextVar("disable_capture_event")


class ServerlessTimeoutWarning(Exception):  # noqa: N818
    """Raised when a serverless method is about to reach its timeout."""

    pass


class TimeoutThread(threading.Thread):
    """Creates a Thread which runs (sleeps) for a time duration equal to
    waiting_time and raises a custom ServerlessTimeout exception.
    """

    def __init__(self, waiting_time, configured_timeout):
        # type: (float, int) -> None
        threading.Thread.__init__(self)
        self.waiting_time = waiting_time
        self.configured_timeout = configured_timeout
        self._stop_event = threading.Event()

    def stop(self):
        # type: () -> None
        self._stop_event.set()

    def run(self):
        # type: () -> None

        self._stop_event.wait(self.waiting_time)

        if self._stop_event.is_set():
            return

        integer_configured_timeout = int(self.configured_timeout)

        # Setting up the exact integer value of configured time(in seconds)
        if integer_configured_timeout < self.configured_timeout:
            integer_configured_timeout = integer_configured_timeout + 1

        # Raising Exception after timeout duration is reached
        raise ServerlessTimeoutWarning(
            "WARNING : Function is expected to get timed out. Configured timeout duration = {} seconds.".format(
                integer_configured_timeout
            )
        )


def to_base64(original):
    # type: (str) -> Optional[str]
    """
    Convert a string to base64, via UTF-8. Returns None on invalid input.
    """
    base64_string = None

    try:
        utf8_bytes = original.encode("UTF-8")
        base64_bytes = base64.b64encode(utf8_bytes)
        base64_string = base64_bytes.decode("UTF-8")
    except Exception as err:
        logger.warning("Unable to encode {orig} to base64:".format(orig=original), err)

    return base64_string


def from_base64(base64_string):
    # type: (str) -> Optional[str]
    """
    Convert a string from base64, via UTF-8. Returns None on invalid input.
    """
    utf8_string = None

    try:
        only_valid_chars = BASE64_ALPHABET.match(base64_string)
        assert only_valid_chars

        base64_bytes = base64_string.encode("UTF-8")
        utf8_bytes = base64.b64decode(base64_bytes)
        utf8_string = utf8_bytes.decode("UTF-8")
    except Exception as err:
        logger.warning(
            "Unable to decode {b64} from base64:".format(b64=base64_string), err
        )

    return utf8_string


if PY37:

    def nanosecond_time():
        # type: () -> int
        return time.perf_counter_ns()

elif PY33:

    def nanosecond_time():
        # type: () -> int

        return int(time.perf_counter() * 1e9)

else:

    def nanosecond_time():
        # type: () -> int

        raise AttributeError
