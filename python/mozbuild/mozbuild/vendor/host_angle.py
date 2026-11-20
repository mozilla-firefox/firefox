# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import annotations

import ast
import os
import re
import tarfile
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

import mozfile

from mozbuild.vendor.host_googlesource import GoogleSourceHost
from mozbuild.vendor.vendor_manifest import safe_extract_tar

# List of additional dependencies that should be included when creating a snapshot.
DEPENDENCIES: tuple[str, ...] = (
    "third_party/zlib",
    "third_party/spirv-headers/src",
    "third_party/spirv-tools/src",
    "third_party/vulkan-headers/src",
    "third_party/vulkan_memory_allocator",
)
GOOGLESOURCE_HOST_SUFFIX = ".googlesource.com"


# Parses ANGLE's python-like DEPS file to determine the repository URL and
# revision of our required dependencies. This supports only the required subset
# of expressions in order to retrieve the information we require.
#
# Currently this consists of a top-level `vars` declaration, which importantly
# contains the `chromium_git` URL, followed by a top-level `deps` declaration.
# Each dep entry is a dict containing a "url" field. The URL may be in either
# of the following forms:
# * Var('chromium_git') + '/chromium/src/third_party/zlib@3246f1b60849cc505e231c5d19d0cbf358093555'
# * '{chromium_git}/external/github.com/KhronosGroup/SPIRV-Tools@d344926654a5bcf837479e9a7417b58a3ea19c74'
class DepsParser:
    def __init__(self, deps_path: Path) -> None:
        self._raw_vars: dict[str, ast.AST] = {}
        self._raw_deps: dict[str, ast.AST] = {}
        self._vars: dict[str, Any] = {}

        # Parse the top-level `vars` and `deps` dict assignments and save their
        # raw values for later use.
        tree = ast.parse(deps_path.read_text(encoding="utf-8"), filename=str(deps_path))
        for node in tree.body:
            if not isinstance(node, ast.Assign):
                continue
            if len(node.targets) != 1:
                continue
            target = node.targets[0]
            if not isinstance(target, ast.Name):
                continue

            if target.id == "vars":
                for key_node, value_node in zip(node.value.keys, node.value.values):
                    self._raw_vars[key_node.value] = value_node
            elif target.id == "deps":
                for key_node, value_node in zip(node.value.keys, node.value.values):
                    self._raw_deps[key_node.value] = value_node

    # Evaluate an AST node. Supports only the subset we require.
    def _eval(self, node: ast.AST) -> Any:
        # Dicts with str keys, used for each deps entry.
        if isinstance(node, ast.Dict):
            result = {}
            for key_node, value_node in zip(node.keys, node.values):
                assert isinstance(key_node.value, str)
                result[key_node.value] = self._eval(value_node)
            return result

        # String constants, may contain a "{var}" which requires resolving and
        # substitution the var.
        if isinstance(node, ast.Constant):
            if isinstance(node.value, str):
                return re.sub(
                    r"{([A-Za-z_][A-Za-z0-9_]*)}",
                    lambda match: self._resolve_var(match.group(1)),
                    node.value,
                )
            return node.value

        # `Var(name)` function calls used to resolve vars.
        if (
            isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "Var"
            and len(node.args) == 1
            and not node.keywords
        ):
            return self._resolve_var(node.args[0].value)

        # String concatenation, e.g. `Var(name) + "str"`.
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            left = self._eval(node.left)
            right = self._eval(node.right)
            return left + right

        raise Exception(f"Unsupported DEPS expression {type(node).__name__}")

    # Resolves var `name` from `self._raw_vars`, caching the result.
    def _resolve_var(self, name: str) -> Any:
        if name in self._vars:
            return self._vars[name]

        node = self._raw_vars[name]
        value = self._eval(node)
        self._vars[name] = value
        return value

    # Resolves the repository URL and revision of the specified dependency
    # `dep`.
    def resolve_dep(self, dep: str) -> tuple[str, str]:
        entry = self._eval(self._raw_deps[dep])
        url = entry["url"]

        repository, _sep, revision = url.rpartition("@")
        return repository, revision


class AngleHost(GoogleSourceHost):
    def upstream_commit(self, revision):
        def _chromium_beta_angle_revision() -> str:
            response = self.session.get(
                "https://chromiumdash.appspot.com/fetch_releases",
                params={"channel": "Beta", "platform": "Windows", "num": 1},
            )
            response.raise_for_status()
            return response.json()[0]["hashes"]["angle"]

        # If no specific revision specified, use the current ANGLE version used
        # by Chromium's Beta channel.
        if revision == "HEAD":
            revision = _chromium_beta_angle_revision()

        return super().upstream_commit(revision)

    def upstream_snapshot(self, revision):
        def download_and_extract(url: str, target: Path):
            with mozfile.NamedTemporaryFile() as tmpfile:
                req = self.session.get(url, stream=True)
                req.raise_for_status()
                for data in req.iter_content(4096):
                    tmpfile.write(data)
                tmpfile.seek(0)
                with tarfile.open(tmpfile.name) as tar:
                    safe_extract_tar(tar, target)

        with mozfile.TemporaryDirectory() as workdir:
            workdir_path = Path(workdir)

            download_and_extract(super().upstream_snapshot(revision), workdir_path)

            deps = DepsParser(workdir_path / "DEPS")
            for dep in DEPENDENCIES:
                repository, revision = deps.resolve_dep(dep)
                if not urlparse(repository).hostname.endswith(GOOGLESOURCE_HOST_SUFFIX):
                    raise Exception(
                        f"ANGLE dependency {dep!r} must come from googlesource, got {repository!r}"
                    )
                host = GoogleSourceHost({"vendoring": {"url": repository}})
                dep_dir = workdir_path / dep
                dep_dir.mkdir(parents=True, exist_ok=True)
                download_and_extract(host.upstream_snapshot(revision), dep_dir)

            self._archive = mozfile.NamedTemporaryFile(suffix=".tar.gz")
            with tarfile.open(self._archive.name, "w:gz") as tar:
                for root, dirs, files in os.walk(workdir_path):
                    dirs.sort()
                    for name in sorted(files):
                        path = Path(root, name)
                        tar.add(path, arcname=str(path.relative_to(workdir_path)))
        return "file://" + self._archive.name
