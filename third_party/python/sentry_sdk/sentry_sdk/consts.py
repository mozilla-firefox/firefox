from sentry_sdk._types import MYPY

if MYPY:
    import sentry_sdk

    from typing import Optional
    from typing import Callable
    from typing import Union
    from typing import List
    from typing import Type
    from typing import Dict
    from typing import Any
    from typing import Sequence
    from typing_extensions import TypedDict

    from sentry_sdk.integrations import Integration

    from sentry_sdk._types import (
        BreadcrumbProcessor,
        Event,
        EventProcessor,
        TracesSampler,
        TransactionProcessor,
    )

    # Experiments are feature flags to enable and disable certain unstable SDK
    # functionality. Changing them from the defaults (`None`) in production
    # code is highly discouraged. They are not subject to any stability
    # guarantees such as the ones from semantic versioning.
    Experiments = TypedDict(
        "Experiments",
        {
            "max_spans": Optional[int],
            "record_sql_params": Optional[bool],
            "smart_transaction_trimming": Optional[bool],
            "propagate_tracestate": Optional[bool],
            "custom_measurements": Optional[bool],
            "profiles_sample_rate": Optional[float],
            "profiler_mode": Optional[str],
        },
        total=False,
    )

DEFAULT_QUEUE_SIZE = 100
DEFAULT_MAX_BREADCRUMBS = 100

SENSITIVE_DATA_SUBSTITUTE = "[Filtered]"


class INSTRUMENTER:
    SENTRY = "sentry"
    OTEL = "otel"


class OP:
    DB = "db"
    DB_REDIS = "db.redis"
    EVENT_DJANGO = "event.django"
    FUNCTION = "function"
    FUNCTION_AWS = "function.aws"
    FUNCTION_GCP = "function.gcp"
    HTTP_CLIENT = "http.client"
    HTTP_CLIENT_STREAM = "http.client.stream"
    HTTP_SERVER = "http.server"
    MIDDLEWARE_DJANGO = "middleware.django"
    MIDDLEWARE_STARLETTE = "middleware.starlette"
    MIDDLEWARE_STARLETTE_RECEIVE = "middleware.starlette.receive"
    MIDDLEWARE_STARLETTE_SEND = "middleware.starlette.send"
    MIDDLEWARE_STARLITE = "middleware.starlite"
    MIDDLEWARE_STARLITE_RECEIVE = "middleware.starlite.receive"
    MIDDLEWARE_STARLITE_SEND = "middleware.starlite.send"
    QUEUE_SUBMIT_CELERY = "queue.submit.celery"
    QUEUE_TASK_CELERY = "queue.task.celery"
    QUEUE_TASK_RQ = "queue.task.rq"
    SUBPROCESS = "subprocess"
    SUBPROCESS_WAIT = "subprocess.wait"
    SUBPROCESS_COMMUNICATE = "subprocess.communicate"
    TEMPLATE_RENDER = "template.render"
    VIEW_RENDER = "view.render"
    VIEW_RESPONSE_RENDER = "view.response.render"
    WEBSOCKET_SERVER = "websocket.server"


# This type exists to trick mypy and PyCharm into thinking `init` and `Client`
# take these arguments (even though they take opaque **kwargs)
class ClientConstructor(object):
    def __init__(
        self,
        dsn=None,  # type: Optional[str]
        with_locals=True,  # type: bool
        max_breadcrumbs=DEFAULT_MAX_BREADCRUMBS,  # type: int
        release=None,  # type: Optional[str]
        environment=None,  # type: Optional[str]
        server_name=None,  # type: Optional[str]
        shutdown_timeout=2,  # type: float
        integrations=[],  # type: Sequence[Integration]  # noqa: B006
        in_app_include=[],  # type: List[str]  # noqa: B006
        in_app_exclude=[],  # type: List[str]  # noqa: B006
        default_integrations=True,  # type: bool
        dist=None,  # type: Optional[str]
        transport=None,  # type: Optional[Union[sentry_sdk.transport.Transport, Type[sentry_sdk.transport.Transport], Callable[[Event], None]]]
        transport_queue_size=DEFAULT_QUEUE_SIZE,  # type: int
        sample_rate=1.0,  # type: float
        send_default_pii=False,  # type: bool
        http_proxy=None,  # type: Optional[str]
        https_proxy=None,  # type: Optional[str]
        ignore_errors=[],  # type: List[Union[type, str]]  # noqa: B006
        request_bodies="medium",  # type: str
        before_send=None,  # type: Optional[EventProcessor]
        before_breadcrumb=None,  # type: Optional[BreadcrumbProcessor]
        debug=False,  # type: bool
        attach_stacktrace=False,  # type: bool
        ca_certs=None,  # type: Optional[str]
        propagate_traces=True,  # type: bool
        traces_sample_rate=None,  # type: Optional[float]
        traces_sampler=None,  # type: Optional[TracesSampler]
        auto_enabling_integrations=True,  # type: bool
        auto_session_tracking=True,  # type: bool
        send_client_reports=True,  # type: bool
        _experiments={},  # type: Experiments  # noqa: B006
        proxy_headers=None,  # type: Optional[Dict[str, str]]
        instrumenter=INSTRUMENTER.SENTRY,  # type: Optional[str]
        before_send_transaction=None,  # type: Optional[TransactionProcessor]
    ):
        # type: (...) -> None
        pass


def _get_default_options():
    # type: () -> Dict[str, Any]
    import inspect

    if hasattr(inspect, "getfullargspec"):
        getargspec = inspect.getfullargspec
    else:
        getargspec = inspect.getargspec  # type: ignore

    a = getargspec(ClientConstructor.__init__)
    defaults = a.defaults or ()
    return dict(zip(a.args[-len(defaults) :], defaults))


DEFAULT_OPTIONS = _get_default_options()
del _get_default_options


VERSION = "1.14.0"
