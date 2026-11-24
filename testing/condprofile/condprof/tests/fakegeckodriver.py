#!/usr/bin/env python3
import argparse
import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from uuid import uuid4

_SESSIONS = {}


class Window:
    def __init__(self, handle, title="about:blank"):
        self.handle = handle
        self.title = title

    def visit_url(self, url):
        print(f"Visiting {url}")
        # XXX todo, load the URL for real
        self.url = url


class Session:
    def __init__(self, uuid):
        self.session_id = uuid
        self.autoinc = 0
        self.windows = {}
        self.active_handle = self.new_window()

    def visit(self, url):
        self.windows[self.active_handle].visit_url(url)

    def new_window(self):
        w = Window(self.autoinc)
        self.windows[w.handle] = w
        self.autoinc += 1
        return w.handle


class RequestHandler(BaseHTTPRequestHandler):
    def _set_headers(self, status=200):
        self.send_response(status)
        self.send_header("Content-type", "application/json")
        self.end_headers()

    def _send_response(self, status=200, data=None):
        if data is None:
            data = {}
        data = json.dumps(data).encode("utf8")
        self._set_headers(status)
        self.wfile.write(data)

    def _parse_path(self):
        path = self.path.lstrip("/")
        sections = path.split("/")
        session = None
        action = []
        if len(sections) > 1:
            session_id = sections[1]
            if session_id in _SESSIONS:
                session = _SESSIONS[session_id]
                action = sections[2:]
        return session, action

    def do_GET(self):
        print("GET " + self.path)
        if self.path == "/status":
            return self._send_response(data={"ready": "OK"})

        session, action = self._parse_path()
        if action == ["window", "handles"]:
            data = {"value": list(session.windows.keys())}
            return self._send_response(data=data)

        if action == ["moz", "context"]:
            data = {"value": "chrome"}
            return self._send_response(data=data)

        return self._send_response(status=404)

    def do_POST(self):
        print("POST " + self.path)
        content_length = int(self.headers["Content-Length"])
        post_data = json.loads(self.rfile.read(content_length))

        # new session
        if self.path == "/session":
            uuid = str(uuid4())
            _SESSIONS[uuid] = Session(uuid)
            return self._send_response(data={"sessionId": uuid})

        session, action = self._parse_path()
        if action == ["url"]:
            session.visit(post_data["url"])
            return self._send_response()

        if action == ["window", "new"]:
            if session is None:
                return self._send_response(404)
            handle = session.new_window()
            return self._send_response(data={"handle": handle, "type": "tab"})

        if action == ["timeouts"]:
            return self._send_response()

        if action == ["execute", "async"]:
            return self._send_response(data={"logs": []})

        # other commands not supported yet, we just return 200s
        return self._send_response()

    def do_DELETE(self):
        return self._send_response()
        session, action = self._parse_path()
        if session is not None:
            del _SESSIONS[session.session_id]
            return self._send_response()
        return self._send_response(status=404)


VERSION = """\
geckodriver 0.24.0 ( 2019-01-28)

The source code of this program is available from
testing/geckodriver in https://hg.mozilla.org/mozilla-central.

This program is subject to the terms of the Mozilla Public License 2.0.
You can obtain a copy of the license at https://mozilla.org/MPL/2.0/.\
"""


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="FakeGeckodriver")
    parser.add_argument("--log", type=str, default=None)
    parser.add_argument("--port", type=int, default=4444)
    parser.add_argument("--marionette-port", type=int, default=2828)
    parser.add_argument("--version", action="store_true", default=False)
    parser.add_argument("--verbose", "-v", action="count")
    args = parser.parse_args()

    if args.version:
        print(VERSION)
    else:
        HTTPServer.allow_reuse_address = True
        server = HTTPServer(("127.0.0.1", args.port), RequestHandler)
        server.serve_forever()
