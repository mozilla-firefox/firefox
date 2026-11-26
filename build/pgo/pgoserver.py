# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import functools
import http.server
import json
import threading

REPORT_PATH = "/pgo-report"


class PGORequestHandler(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".js": "text/javascript",
        ".mjs": "text/javascript",
        ".svg": "image/svg+xml",
        ".wasm": "application/wasm",
    }

    def __init__(self, *args, path_mappings=None, **kwargs):
        self.path_mappings = path_mappings or {}
        super().__init__(*args, **kwargs)

    def translate_path(self, path):
        for prefix, directory in self.path_mappings.items():
            if path == prefix or path.startswith((prefix + "/", prefix + "?")):
                docroot = self.directory
                self.directory = directory
                try:
                    return super().translate_path(path[len(prefix) :] or "/")
                finally:
                    self.directory = docroot
        return super().translate_path(path)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        if self.path == REPORT_PATH:
            self.server.report = json.loads(body)
        elif self.path in self.server.handlers:
            self.server.handlers[self.path](json.loads(body))
        else:
            self.send_error(404)
            return
        self.send_response(204)
        self.end_headers()

    def log_message(self, format, *args):
        pass


class PGOServer:
    """HTTP/1.1 server for the PGO training corpus.

    :param docroot: Directory served at the root.
    :param path_mappings: A dict mapping URL prefixes, such as "/talos", to
        additional directories to serve them from.
    :param handlers: A dict mapping URL paths to functions called with the JSON
        body of the requests posted to them.

    build/pgo/index.html posts a report of the training run to REPORT_PATH, see
    training_errors.
    """

    def __init__(
        self, docroot, port, path_mappings=None, handlers=None, host="127.0.0.1"
    ):
        self.httpd = http.server.ThreadingHTTPServer(
            (host, port),
            functools.partial(
                PGORequestHandler, directory=docroot, path_mappings=path_mappings
            ),
        )
        self.httpd.report = None
        self.httpd.handlers = handlers or {}

    def training_errors(self):
        """Returns why training failed, from the report posted by
        build/pgo/index.html, or an empty list if it succeeded."""
        if self.httpd.report is None:
            return ["build/pgo/index.html did not finish training"]
        return self.httpd.report["errors"]

    def start(self):
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def stop(self):
        self.httpd.shutdown()
        self.httpd.server_close()
