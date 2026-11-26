# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import http.client
import json
import os
import tempfile
import unittest

import mozunit
from pgoserver import REPORT_PATH, PGOServer


class TestPGOServer(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.items = []
        root = self.tmpdir.name
        self.files = {
            "docroot/index.html": "root index",
            "docroot/talosx/index.html": "talosx",
            "talos/index.html": "talos index",
            "talos/tests/page.html": "talos page",
            "webkit/Speedometer3/index.html": "speedometer3",
            "secret.txt": "secret",
        }
        for name, content in self.files.items():
            path = os.path.join(root, name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write(content)
        self.server = PGOServer(
            os.path.join(root, "docroot"),
            0,
            {
                "/talos": os.path.join(root, "talos"),
                "/webkit/PerformanceTests": os.path.join(root, "webkit"),
            },
            {"/pgo-item": self.items.append},
        )
        self.server.start()
        self.port = self.server.httpd.server_address[1]

    def tearDown(self):
        self.server.stop()
        self.tmpdir.cleanup()

    def request(self, path, method="GET", body=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port)
        try:
            conn.request(method, path, body=body)
            response = conn.getresponse()
            return response.status, response.read().decode()
        finally:
            conn.close()

    def assertServes(self, path, content):
        self.assertEqual(self.request(path), (200, content))

    def test_docroot(self):
        self.assertServes("/index.html", "root index")
        self.assertServes("/", "root index")

    def test_mapped_prefix(self):
        self.assertServes("/talos/tests/page.html", "talos page")
        self.assertServes("/talos/", "talos index")
        self.assertServes(
            "/webkit/PerformanceTests/Speedometer3/index.html?startAutomatically=true",
            "speedometer3",
        )

    def test_mapped_prefix_without_slash(self):
        status, _ = self.request("/talos")
        self.assertEqual(status, 301)
        status, _ = self.request("/talos?foo=bar")
        self.assertEqual(status, 301)

    def test_prefix_is_not_a_partial_match(self):
        self.assertServes("/talosx/index.html", "talosx")

    def test_no_escape_from_mapped_directory(self):
        status, _ = self.request("/talos/../secret.txt")
        self.assertEqual(status, 404)
        status, _ = self.request("/talos/%2e%2e/secret.txt")
        self.assertEqual(status, 404)

    def test_mime_types(self):
        conn = http.client.HTTPConnection("127.0.0.1", self.port)
        try:
            for name, mime_type in (
                ("a.js", "text/javascript"),
                ("a.mjs", "text/javascript"),
                ("a.wasm", "application/wasm"),
            ):
                with open(os.path.join(self.tmpdir.name, "docroot", name), "w"):
                    pass
                conn.request("GET", "/" + name)
                response = conn.getresponse()
                response.read()
                self.assertEqual(response.getheader("Content-Type"), mime_type)
        finally:
            conn.close()

    def test_training_errors(self):
        self.assertEqual(
            self.server.training_errors(),
            ["build/pgo/index.html did not finish training"],
        )
        self.assertEqual(
            self.request(REPORT_PATH, "POST", json.dumps({"errors": []}))[0], 204
        )
        self.assertEqual(self.server.training_errors(), [])
        self.request(REPORT_PATH, "POST", json.dumps({"errors": ["a: failed"]}))
        self.assertEqual(self.server.training_errors(), ["a: failed"])

    def test_handlers(self):
        item = {"url": "a.html", "ms": 10, "outcome": "done"}
        self.assertEqual(self.request("/pgo-item", "POST", json.dumps(item))[0], 204)
        self.assertEqual(self.items, [item])
        self.assertEqual(
            self.server.training_errors(),
            ["build/pgo/index.html did not finish training"],
        )

    def test_post_elsewhere(self):
        for body in ("{}", "not a training report"):
            with self.subTest(body=body):
                self.assertEqual(self.request("/index.html", "POST", body)[0], 404)
        self.assertEqual(
            self.server.training_errors(),
            ["build/pgo/index.html did not finish training"],
        )


if __name__ == "__main__":
    mozunit.main()
