from __future__ import absolute_import

from sentry_sdk import Hub
from sentry_sdk.consts import OP
from sentry_sdk.utils import capture_internal_exceptions, logger
from sentry_sdk.integrations import Integration, DidNotEnable

from sentry_sdk._types import MYPY

if MYPY:
    from typing import Any, Sequence

_SINGLE_KEY_COMMANDS = frozenset(
    ["decr", "decrby", "get", "incr", "incrby", "pttl", "set", "setex", "setnx", "ttl"]
)
_MULTI_KEY_COMMANDS = frozenset(["del", "touch", "unlink"])

#: Trim argument lists to this many values
_MAX_NUM_ARGS = 10


def patch_redis_pipeline(pipeline_cls, is_cluster, get_command_args_fn):
    # type: (Any, bool, Any) -> None
    old_execute = pipeline_cls.execute

    def sentry_patched_execute(self, *args, **kwargs):
        # type: (Any, *Any, **Any) -> Any
        hub = Hub.current

        if hub.get_integration(RedisIntegration) is None:
            return old_execute(self, *args, **kwargs)

        with hub.start_span(
            op=OP.DB_REDIS, description="redis.pipeline.execute"
        ) as span:
            with capture_internal_exceptions():
                span.set_tag("redis.is_cluster", is_cluster)
                transaction = self.transaction if not is_cluster else False
                span.set_tag("redis.transaction", transaction)

                commands = []
                for i, arg in enumerate(self.command_stack):
                    if i > _MAX_NUM_ARGS:
                        break
                    command_args = []
                    for j, command_arg in enumerate(get_command_args_fn(arg)):
                        if j > 0:
                            command_arg = repr(command_arg)
                        command_args.append(command_arg)
                    commands.append(" ".join(command_args))

                span.set_data(
                    "redis.commands",
                    {"count": len(self.command_stack), "first_ten": commands},
                )

            return old_execute(self, *args, **kwargs)

    pipeline_cls.execute = sentry_patched_execute


def _get_redis_command_args(command):
    # type: (Any) -> Sequence[Any]
    return command[0]


def _parse_rediscluster_command(command):
    # type: (Any) -> Sequence[Any]
    return command.args


def _patch_rediscluster():
    # type: () -> None
    try:
        import rediscluster  # type: ignore
    except ImportError:
        return

    patch_redis_client(rediscluster.RedisCluster, is_cluster=True)

    # up to v1.3.6, __version__ attribute is a tuple
    # from v2.0.0, __version__ is a string and VERSION a tuple
    version = getattr(rediscluster, "VERSION", rediscluster.__version__)

    # StrictRedisCluster was introduced in v0.2.0 and removed in v2.0.0
    # https://github.com/Grokzen/redis-py-cluster/blob/master/docs/release-notes.rst
    if (0, 2, 0) < version < (2, 0, 0):
        pipeline_cls = rediscluster.pipeline.StrictClusterPipeline
        patch_redis_client(rediscluster.StrictRedisCluster, is_cluster=True)
    else:
        pipeline_cls = rediscluster.pipeline.ClusterPipeline

    patch_redis_pipeline(pipeline_cls, True, _parse_rediscluster_command)


class RedisIntegration(Integration):
    identifier = "redis"

    @staticmethod
    def setup_once():
        # type: () -> None
        try:
            import redis
        except ImportError:
            raise DidNotEnable("Redis client not installed")

        patch_redis_client(redis.StrictRedis, is_cluster=False)
        patch_redis_pipeline(redis.client.Pipeline, False, _get_redis_command_args)
        try:
            strict_pipeline = redis.client.StrictPipeline  # type: ignore
        except AttributeError:
            pass
        else:
            patch_redis_pipeline(strict_pipeline, False, _get_redis_command_args)

        try:
            import rb.clients  # type: ignore
        except ImportError:
            pass
        else:
            patch_redis_client(rb.clients.FanoutClient, is_cluster=False)
            patch_redis_client(rb.clients.MappingClient, is_cluster=False)
            patch_redis_client(rb.clients.RoutingClient, is_cluster=False)

        try:
            _patch_rediscluster()
        except Exception:
            logger.exception("Error occurred while patching `rediscluster` library")


def patch_redis_client(cls, is_cluster):
    # type: (Any, bool) -> None
    """
    This function can be used to instrument custom redis client classes or
    subclasses.
    """
    old_execute_command = cls.execute_command

    def sentry_patched_execute_command(self, name, *args, **kwargs):
        # type: (Any, str, *Any, **Any) -> Any
        hub = Hub.current

        if hub.get_integration(RedisIntegration) is None:
            return old_execute_command(self, name, *args, **kwargs)

        description = name

        with capture_internal_exceptions():
            description_parts = [name]
            for i, arg in enumerate(args):
                if i > _MAX_NUM_ARGS:
                    break

                description_parts.append(repr(arg))

            description = " ".join(description_parts)

        with hub.start_span(op=OP.DB_REDIS, description=description) as span:
            span.set_tag("redis.is_cluster", is_cluster)
            if name:
                span.set_tag("redis.command", name)

            if name and args:
                name_low = name.lower()
                if (name_low in _SINGLE_KEY_COMMANDS) or (
                    name_low in _MULTI_KEY_COMMANDS and len(args) == 1
                ):
                    span.set_tag("redis.key", args[0])

            return old_execute_command(self, name, *args, **kwargs)

    cls.execute_command = sentry_patched_execute_command
