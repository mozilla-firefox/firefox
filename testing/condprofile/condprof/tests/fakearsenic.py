# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import sys
import types
from json import JSONDecodeError
from typing import Any, Tuple

import attr


class UnknownError(Exception):
    pass


class UnknownArsenicError(Exception):
    pass


@attr.s
class Geckodriver:
    log_file = attr.ib(default=os.devnull)
    binary = attr.ib(default="geckodriver")
    version_checked = attr.ib(default=False)

    async def _check_version(self):
        self.version_checked = True


class Firefox:
    def __init__(self, **kw):
        self.kw = kw


FREE_PORT = 4567
SERVICE_CALLS = []


def free_port():
    return FREE_PORT


async def subprocess_based_service(cmd, service_url, log_file):
    SERVICE_CALLS.append((cmd, service_url, log_file))
    return service_url


class FakeSession:
    def __init__(self, handles=(0, 1, 2), context="chrome", script_results=None):
        self.handles = list(handles)
        self.context = context
        self.script_results = list(script_results or [])
        self.requests = []
        self.visited = []
        self.switched = []
        self.get_errors = []

    async def _request(self, url, method, data=None):
        self.requests.append((url, method, data))
        if url == "/moz/context" and method == "GET":
            return self.context
        if url == "/execute/async":
            if self.script_results:
                result = self.script_results.pop(0)
                if isinstance(result, Exception):
                    raise result
                return result
            return {"logs": []}
        return None

    async def get(self, url):
        if self.get_errors:
            error = self.get_errors.pop(0)
            if error is not None:
                raise error
        self.visited.append(url)

    async def get_window_handles(self):
        return list(self.handles)

    async def switch_to_window(self, handle):
        self.switched.append(handle)

    async def close(self):
        pass


class FakeSessionContext:
    def __init__(self, session):
        self.session = session

    async def __aenter__(self):
        return self.session

    async def __aexit__(self, *exc):
        return False


SESSION_FACTORY = [lambda service, browser: FakeSession()]


def get_session(service, browser):
    return FakeSessionContext(SESSION_FACTORY[0](service, browser))


class _Log:
    def __init__(self):
        self.calls = []

    def info(self, *args, **kw):
        self.calls.append(("info", args, kw))

    def error(self, *args, **kw):
        self.calls.append(("error", args, kw))


class Connection:
    pass


def ensure_task(func):
    return func


def strip_auth(url):
    return url


def wrap_screen(data):
    data.setdefault("wrapped", True)


def _module(name, **attrs):
    mod = types.ModuleType(name)
    for key, value in attrs.items():
        setattr(mod, key, value)
    return mod


def install():
    connection = _module(
        "arsenic.connection",
        Connection=Connection,
        ensure_task=ensure_task,
        strip_auth=strip_auth,
        wrap_screen=wrap_screen,
        log=_Log(),
        json=json,
        JSONDecodeError=JSONDecodeError,
        Tuple=Tuple,
        Any=Any,
    )
    services = _module(
        "arsenic.services",
        Geckodriver=Geckodriver,
        free_port=free_port,
        subprocess_based_service=subprocess_based_service,
    )
    browsers = _module("arsenic.browsers", Firefox=Firefox)
    errors = _module(
        "arsenic.errors",
        UnknownError=UnknownError,
        UnknownArsenicError=UnknownArsenicError,
    )
    arsenic = _module(
        "arsenic",
        get_session=get_session,
        connection=connection,
        services=services,
        browsers=browsers,
        errors=errors,
    )
    arsenic.__path__ = []
    sys.modules.update({
        "arsenic": arsenic,
        "arsenic.connection": connection,
        "arsenic.services": services,
        "arsenic.browsers": browsers,
        "arsenic.errors": errors,
    })


install()
