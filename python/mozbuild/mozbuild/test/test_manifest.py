# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import unittest

import mozfile
from mozunit import main

from mozbuild.vendor.moz_yaml import MozYamlVerifyError, load_moz_yaml


class TestManifest(unittest.TestCase):
    def process_test_vectors(self, test_vectors):
        index = 0
        for vector in test_vectors:
            print("Testing index", index)
            expected, yaml = vector
            with mozfile.NamedTemporaryFile() as tf:
                tf.write(yaml)
                tf.flush()
                if expected == "exception":
                    with self.assertRaises(MozYamlVerifyError):
                        load_moz_yaml(tf.name, require_license_file=False)
                else:
                    self.assertDictEqual(
                        load_moz_yaml(tf.name, require_license_file=False), expected
                    )
            index += 1

    # ===========================================================================================
    def test_simple(self):
        simple_dict = {
            "schema": "1",
            "origin": {
                "description": "2D Graphics Library",
                "license": ["MPL-1.1", "LGPL-2.1"],
                "name": "cairo",
                "release": "version 1.6.4",
                "revision": "AA001122334455",
                "url": "https://www.cairographics.org/",
            },
            "bugzilla": {"component": "Graphics", "product": "Core"},
        }

        self.process_test_vectors([
            (
                simple_dict,
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
bugzilla:
  product: Core
  component: Graphics
            """.strip(),
            ),
            (
                simple_dict,
                b"""
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
bugzilla:
  product: Core
  component: Graphics
            """.strip(),
            ),
        ])

    # ===========================================================================================
    def test_boolean(self):
        def build_input(input):
            return (
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  fuzzy-query: "!linux64"
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: commit-alert
      enabled: %s
            """.strip()
                % input
            )

        def build_expected(expected):
            return {
                "schema": "1",
                "origin": {
                    "description": "2D Graphics Library",
                    "license": ["MPL-1.1", "LGPL-2.1"],
                    "name": "cairo",
                    "release": "version 1.6.4",
                    "revision": "AA001122334455",
                    "url": "https://www.cairographics.org/",
                },
                "bugzilla": {"component": "Graphics", "product": "Core"},
                "vendoring": {
                    "url": "https://example.com",
                    "source-hosting": "gitlab",
                },
                "updatebot": {
                    "maintainer-phab": "tjr",
                    "maintainer-bz": "a@example.com",
                    "fuzzy-query": "!linux64",
                    "tasks": [{"type": "commit-alert", "enabled": expected}],
                },
            }

        true_values = [b"1", b"true", b"yes", b"on", b"enable"]
        false_values = [b"0", b"false", b"no", b"off", b"disable"]
        malformed_values = [b"existential dread", b"", b"2", b"!@#$%^"]
        test_vectors = (
            [(build_expected(True), build_input(v)) for v in true_values]
            + [(build_expected(False), build_input(v)) for v in false_values]
            + [("exception", build_input(v)) for v in malformed_values]
        )

        self.process_test_vectors(test_vectors)

    # ===========================================================================================
    def test_updatebot(self):
        self.process_test_vectors([
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "updatebot": {
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "updatebot": {
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: 001122334455
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "updatebot": {
                        "try-preset": "foo",
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: 001122334455
bugzilla:
  product: Core
  component: Graphics
updatebot:
  try-preset: foo
  maintainer-phab: tjr
  maintainer-bz: a@example.com
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "updatebot": {
                        "try-preset": "foo",
                        "artifact-build": True,
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: 001122334455
bugzilla:
  product: Core
  component: Graphics
updatebot:
  try-preset: foo
  artifact-build: true
  maintainer-phab: tjr
  maintainer-bz: a@example.com
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                    },
                    "updatebot": {
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                        "fuzzy-query": "!linux64",
                        "tasks": [{"type": "commit-alert"}],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  fuzzy-query: "!linux64"
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: commit-alert
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  try-preset: foo
  fuzzy-query: "!linux64"
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: commit-alert
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                    },
                    "updatebot": {
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                        "fuzzy-paths": ["dir1/", "dir2"],
                        "tasks": [{"type": "commit-alert"}],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  fuzzy-paths:
    - dir1/
    - dir2
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: commit-alert
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                    },
                    "updatebot": {
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                        "fuzzy-paths": ["dir1/"],
                        "tasks": [{"type": "commit-alert"}],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  fuzzy-paths: ['dir1/']
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: commit-alert
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                        "tracking": "commit",
                        "flavor": "rust",
                    },
                    "updatebot": {
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                        "tasks": [
                            {"type": "commit-alert", "frequency": "release"},
                            {
                                "type": "vendoring",
                                "enabled": False,
                                "cc": ["b@example.com"],
                                "needinfo": ["c@example.com"],
                                "frequency": "1 weeks",
                                "options": ["build-flag"],
                            },
                        ],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  tracking: commit
  source-hosting: gitlab
  flavor: rust
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: commit-alert
      frequency: release
    - type: vendoring
      enabled: False
      cc: ["b@example.com"]
      needinfo: ["c@example.com"]
      frequency: 1 weeks
      options: ["build-flag"]
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                        "tracking": "tag",
                        "flavor": "rust",
                    },
                    "updatebot": {
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                        "tasks": [
                            {"type": "commit-alert", "frequency": "release"},
                            {
                                "type": "vendoring",
                                "enabled": False,
                                "cc": ["b@example.com"],
                                "needinfo": ["c@example.com"],
                                "frequency": "1 weeks, 4 commits",
                            },
                        ],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  tracking: tag
  source-hosting: gitlab
  flavor: rust
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: commit-alert
      frequency: release
    - type: vendoring
      enabled: False
      cc: ["b@example.com"]
      needinfo: ["c@example.com"]
      frequency: 1 weeks, 4 commits
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",  # rust flavor cannot use update-actions
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  tracking: tag
  source-hosting: gitlab
  flavor: rust
  update-actions:
    - action: move-file
      from: foo
      to: bar
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: commit-alert
      frequency: release
    - type: vendoring
      enabled: False
      cc: ["b@example.com"]
      needinfo: ["c@example.com"]
      frequency: 1 weeks, 4 commits
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                    },
                    "updatebot": {
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                        "tasks": [
                            {
                                "type": "vendoring",
                                "enabled": False,
                                "cc": ["b@example.com", "c@example.com"],
                                "needinfo": ["d@example.com", "e@example.com"],
                                "frequency": "every",
                            },
                            {
                                "type": "commit-alert",
                                "filter": "none",
                                "source-extensions": [".c", ".cpp"],
                                "frequency": "2 weeks",
                            },
                        ],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
      needinfo:
        - d@example.com
        - e@example.com
      frequency: every
    - type: commit-alert
      filter: none
      frequency: 2 weeks
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                    },
                    "updatebot": {
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                        "tasks": [
                            {
                                "type": "vendoring",
                                "enabled": False,
                                "frequency": "1 day",
                            },
                            {
                                "type": "commit-alert",
                                "frequency": "3 days, 2 commits",
                            },
                        ],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      frequency: 1 day
    - type: commit-alert
      frequency: 3 days, 2 commits
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                    },
                    "updatebot": {
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                        "tasks": [
                            {
                                "type": "vendoring",
                                "enabled": False,
                                "cc": ["b@example.com", "c@example.com"],
                                "needinfo": ["d@example.com", "e@example.com"],
                                "frequency": "every",
                            },
                            {
                                "type": "commit-alert",
                                "filter": "none",
                                "source-extensions": [".c", ".cpp"],
                                "frequency": "2 commits",
                            },
                        ],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
      needinfo:
        - d@example.com
        - e@example.com
      frequency: every
    - type: commit-alert
      filter: none
      frequency: 2 commits
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                    },
                    "updatebot": {
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                        "tasks": [
                            {
                                "type": "vendoring",
                                "enabled": False,
                                "cc": ["b@example.com", "c@example.com"],
                                "needinfo": ["d@example.com", "e@example.com"],
                                "frequency": "every",
                                "blocking": "1234",
                                "options": ["option1", "option2"],
                            },
                            {
                                "type": "commit-alert",
                                "filter": "none",
                                "source-extensions": [".c", ".cpp"],
                                "frequency": "2 commits",
                            },
                        ],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
      needinfo:
        - d@example.com
        - e@example.com
      frequency: every
      blocking: 1234
      options: ["option1", "option2"]
    - type: commit-alert
      filter: none
      frequency: 2 commits
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      branch: foo
      enabled: False
      cc:
        - b@example.com
        - c@example.com
      needinfo:
        - d@example.com
        - e@example.com
      frequency: every
      blocking: 1234
    - type: commit-alert
      filter: none
      frequency: 2 commits
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "description": "2D Graphics Library",
                        "url": "https://www.cairographics.org/",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                        "flavor": "individual-files",
                        "individual-files": [{"upstream": "foo", "destination": "bar"}],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
  flavor: individual-files
  individual-files:
    - upstream: foo
      destination: bar
bugzilla:
  product: Core
  component: Graphics
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "description": "2D Graphics Library",
                        "url": "https://www.cairographics.org/",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                        "flavor": "individual-files",
                        "individual-files": [{"upstream": "foo", "destination": "bar"}],
                        "update-actions": [
                            {"action": "move-file", "from": "foo", "to": "bar"}
                        ],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
  flavor: individual-files
  individual-files:
    - upstream: foo
      destination: bar
  update-actions:
    - action: move-file
      from: foo
      to: bar
bugzilla:
  product: Core
  component: Graphics
            """.strip(),
            ),
            # -------------------------------------------------
            (
                {
                    "schema": "1",
                    "origin": {
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "description": "2D Graphics Library",
                        "url": "https://www.cairographics.org/",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                        "flavor": "individual-files",
                        "individual-files-default-destination": "bar",
                        "individual-files-default-upstream": "foo",
                        "individual-files-list": ["bar", "foo"],
                        "update-actions": [
                            {"action": "move-file", "from": "foo", "to": "bar"}
                        ],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
  flavor: individual-files
  individual-files-default-upstream: foo
  individual-files-default-destination: bar
  individual-files-list:
    - bar
    - foo
  update-actions:
    - action: move-file
      from: foo
      to: bar
bugzilla:
  product: Core
  component: Graphics
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",  # can't have both types of indidivudal-files list
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
  flavor: individual-files
  individual-files-list:
    - foo
  individual-files:
    - upstream: foo
      destination: bar
  update-actions:
    - action: move-file
      from: foo
      to: bar
bugzilla:
  product: Core
  component: Graphics
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",  # can't have indidivudal-files-default-upstream
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
  flavor: individual-files
  indidivudal-files-default-upstream: foo
  individual-files:
    - upstream: foo
      destination: bar
  update-actions:
    - action: move-file
      from: foo
      to: bar
bugzilla:
  product: Core
  component: Graphics
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",  # must have indidivudal-files-default-upstream
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
  flavor: individual-files
  indidivudal-files-default-destination: foo
  individual-files-list:
    - bar
    - foo
  update-actions:
    - action: move-file
      from: foo
      to: bar
bugzilla:
  product: Core
  component: Graphics
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
  tracking: tag
  flavor: individual-files
  individual-files:
    - upstream-src: foo
      dst: bar
bugzilla:
  product: Core
  component: Graphics
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
  flavor: individual-files
bugzilla:
  product: Core
  component: Graphics
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
  flavor: rust
  individual-files:
    - upstream: foo
      destination: bar
bugzilla:
  product: Core
  component: Graphics
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
  flavor: rust
  include:
    - foo
bugzilla:
  product: Core
  component: Graphics
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
      needinfo:
        - d@example.com
        - e@example.com
      frequency: every
      blocking: foo
    - type: commit-alert
      filter: none
      frequency: 2 commits
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  fuzzy-paths: "must-be-array"
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
      needinfo:
        - d@example.com
        - e@example.com
      frequency: every
    - type: commit-alert
      filter: none
      frequency: 2 commits
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
      needinfo:
        - d@example.com
        - e@example.com
      frequency: every
    - type: commit-alert
      filter: none
      frequency: 2 commits, 4 weeks
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
      needinfo:
        - d@example.com
        - e@example.com
      frequency: every
    - type: commit-alert
      filter: none
      frequency: 4 weeks, 2 commits, 3 weeks
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
  flavor: chocolate
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
      needinfo:
        - d@example.com
        - e@example.com
      frequency: every
    - type: commit-alert
      filter: none
      frequency: 2 weeks
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
  flavor: chocolate
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
      needinfo:
        - d@example.com
        - e@example.com
      frequency: every
    - type: commit-alert
      filter: none
      frequency: 01 commits
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
      needinfo:
        - d@example.com
        - e@example.com
      frequency: every
    - type: commit-alert
      filter: none
      frequency: 2 weeks
      platform: linux  # Updatebot only runs on Linux; platform is no longer a valid key
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
    - type: commit-alert
      filter: none
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
bugzilla:
  product: Core
  component: Graphics
vendoring:
  url: https://example.com
  source-hosting: gitlab
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
    - type: commit-alert
      filter: none
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      filter: none
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: foo
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      source-extensions:
        - .c
        - .cpp
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: commit-alert
      filter: hogwash
            """.strip(),
            ),
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
bugzilla:
  product: Core
  component: Graphics
vendoring:
  url: https://example.com
  source-hosting: gitlab
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
    - type: commit-alert
    - type: commit-alert
      filter: none
      source-extensions:
        - .c
        - .cpp""".strip(),
            ),
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
bugzilla:
  product: Core
  component: Graphics
vendoring:
  url: https://example.com
  source-hosting: gitlab
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
    - type: vendoring
    - type: commit-alert
      filter: none
      source-extensions:
        - .c
        - .cpp""".strip(),
            ),
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
bugzilla:
  product: Core
  component: Graphics
vendoring:
  url: https://example.com
  source-hosting: gitlab
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
    - type: commit-alert
      frequency: every-release
      filter: none
      source-extensions:
        - .c
        - .cpp""".strip(),
            ),
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
bugzilla:
  product: Core
  component: Graphics
vendoring:
  url: https://example.com
  source-hosting: gitlab
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: vendoring
      enabled: False
      cc:
        - b@example.com
        - c@example.com
      frequency: 2 months
    - type: commit-alert
      filter: none
      source-extensions:
        - .c
        - .cpp""".strip(),
            ),
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: commit-alert
      frequency: 0 weeks
                  """.strip(),
            ),
        ])

    # ===========================================================================================
    def test_updatebot_options(self):
        self.process_test_vectors([
            # options is accepted as an array of strings
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "AA001122334455",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                    "vendoring": {
                        "url": "https://example.com",
                        "source-hosting": "gitlab",
                    },
                    "updatebot": {
                        "maintainer-phab": "tjr",
                        "maintainer-bz": "a@example.com",
                        "tasks": [{"type": "commit-alert", "options": ["one", "two"]}],
                    },
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: commit-alert
      options:
        - one
        - two
            """.strip(),
            ),
            # -------------------------------------------------
            (
                "exception",  # options must be an array, not a scalar
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
vendoring:
  url: https://example.com
  source-hosting: gitlab
bugzilla:
  product: Core
  component: Graphics
updatebot:
  maintainer-phab: tjr
  maintainer-bz: a@example.com
  tasks:
    - type: commit-alert
      options: not-an-array
            """.strip(),
            ),
        ])

    # ===========================================================================================
    def test_malformed(self):
        with mozfile.NamedTemporaryFile() as tf:
            tf.write(b"blah")
            tf.flush()
            with self.assertRaises(MozYamlVerifyError):
                load_moz_yaml(tf.name, require_license_file=False)

    def test_schema(self):
        with mozfile.NamedTemporaryFile() as tf:
            tf.write(b"schema: 99")
            tf.flush()
            with self.assertRaises(MozYamlVerifyError):
                load_moz_yaml(tf.name, require_license_file=False)

    def test_json(self):
        with mozfile.NamedTemporaryFile() as tf:
            tf.write(
                b'{"origin": {"release": "version 1.6.4", "url": "https://w'
                b'ww.cairographics.org/", "description": "2D Graphics Libra'
                b'ry", "license": ["MPL-1.1", "LGPL-2.1"], "name": "cairo"}'
                b', "bugzilla": {"product": "Core", "component": "Graphics"'
                b'}, "schema": 1}'
            )
            tf.flush()
            with self.assertRaises(MozYamlVerifyError):
                load_moz_yaml(tf.name, require_license_file=False)

    def test_revision(self):
        self.process_test_vectors([
            (
                {
                    "schema": "1",
                    "origin": {
                        "description": "2D Graphics Library",
                        "license": ["MPL-1.1", "LGPL-2.1"],
                        "name": "cairo",
                        "release": "version 1.6.4",
                        "revision": "v1.6.37",
                        "url": "https://www.cairographics.org/",
                    },
                    "bugzilla": {"component": "Graphics", "product": "Core"},
                },
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: v1.6.37
bugzilla:
  product: Core
  component: Graphics""".strip(),
            ),
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: 4.0.0.
bugzilla:
  product: Core
  component: Graphics""".strip(),
            ),
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: 4.^.0
bugzilla:
  product: Core
  component: Graphics""".strip(),
            ),
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: " "
bugzilla:
  product: Core
  component: Graphics""".strip(),
            ),
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: ???
bugzilla:
  product: Core
  component: Graphics""".strip(),
            ),
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: ]
bugzilla:
  product: Core
  component: Graphics""".strip(),
            ),
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
bugzilla:
  product: Core
  component: Graphics
vendoring:
  url: https://example.com
  source-hosting: gitlab
  update-actions:
    - action: run-script
      cwd: '{cwd}'
      script: 'script.py'
      args: ['hi']
      pattern: 'hi'
""".strip(),
            ),
            (
                "exception",
                b"""
---
schema: 1
origin:
  name: cairo
  description: 2D Graphics Library
  url: https://www.cairographics.org/
  release: version 1.6.4
  license:
    - MPL-1.1
    - LGPL-2.1
  revision: AA001122334455
bugzilla:
  product: Core
  component: Graphics
vendoring:
  url: https://example.com
  source-hosting: gitlab
  update-actions:
    - action: run-script
      cwd: '{cwd}'
      args: ['hi']
""".strip(),
            ),
        ])


if __name__ == "__main__":
    main()
