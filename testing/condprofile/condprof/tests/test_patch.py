# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import asyncio
import json

import mozunit
import pytest

from condprof.tests import fakearsenic

from condprof import patch  # isort: skip  # noqa


class Response:
    status = 200

    def __init__(self, body):
        self.body = body

    async def read(self):
        return self.body

    async def __aenter__(self):
        return self

    async def __aexit__(self, *exc):
        return False


class HTTPSession:
    def __init__(self, body):
        self.body = body
        self.calls = []

    def request(self, **kw):
        self.calls.append(kw)
        return Response(self.body)


class Conn:
    prefix = "http://localhost:4444/session/1"

    def __init__(self, body):
        self.session = HTTPSession(body)


def request(conn, **kw):
    return asyncio.run(fakearsenic.Connection.request(conn, **kw))


@pytest.mark.parametrize("method", ["POST", "PUT"])
def test_request_with_body(method):
    conn = Conn(b'{"value": 1}')
    status, data = request(conn, url="/url", method=method, data={"url": "x"})
    assert status == 200
    assert data == {"value": 1, "wrapped": True}
    assert conn.session.calls == [
        {
            "url": "http://localhost:4444/session/1/url",
            "method": method,
            "data": json.dumps({"url": "x"}),
            "headers": {"Content-Type": "application/json"},
            "timeout": None,
        }
    ]


def test_request_post_without_data():
    conn = Conn(b"{}")
    request(conn, url="/window/new", method="POST", timeout=3)
    (call,) = conn.session.calls
    assert call["data"] == "{}"
    assert call["timeout"] == 3


def test_request_get_drops_data():
    conn = Conn(b'{"value": []}')
    status, data = request(conn, url="/window/handles", method="GET", data={"a": 1})
    (call,) = conn.session.calls
    assert call["data"] is None
    assert call["headers"] == {}
    assert data["value"] == []


def test_request_invalid_json():
    conn = Conn(b"<html>")
    status, data = request(conn, url="/status", method="GET")
    assert data["error"] == "!internal"
    assert data["stacktrace"] == ""
    assert data["message"]


if __name__ == "__main__":
    mozunit.main()
