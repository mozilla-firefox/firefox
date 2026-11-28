# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Map Cargo.lock onto SBOM records (see sbom.py for the record shape).

The Rust dependencies are the one part of the tree that already knows its own
dependency graph, its exact versions and, for registry crates, a checksum of
the published artifact. moz.yaml knows none of that: `third_party/rust` is
vendored wholesale, so without this the crates appear only as whatever
about:license attributes to them.

Like sbom.py this module does not import cyclonedx, so the mapping rules can be
unit tested from sites that do not vendor it.
"""

import collections
import json
import os
import re
import subprocess

import mozpack.path as mozpath
import toml

VENDOR_DIR = "third_party/rust"

# Cargo once took "MIT/Apache-2.0" to mean "MIT OR Apache-2.0", and crates
# published back then still say so. It is not an SPDX expression.
RE_LEGACY_LICENSE_SEPARATOR = re.compile(r"\s*/\s*")

# Workspace members that build or test the product rather than being part of
# it. A crate reached only from these, or only as a dev dependency, is a
# `tooling` crate. A member missing here counts as shipped, which overstates
# the product rather than hiding part of it.
TOOLING_MEMBERS = (
    "netwerk/test/http3server",
    "security/manager/ssl/tests/unit/pkcs11testmodule",
    "security/manager/ssl/tests/unit/test_trust_anchors",
    "services/app-services-tools/embedded-uniffi-bindgen",
    "services/app-services-tools/nimbus-fml",
    "testing/geckodriver",
    "third_party/application-services/components/example",
    "third_party/application-services/components/support/find-places-db",
    "third_party/application-services/components/support/restmail-client",
    "third_party/application-services/components/support/text-table",
    "third_party/application-services/tools/embedded-uniffi-bindgen",
    "third_party/application-services/tools/uniffi-bindgen-library-mode",
    "toolkit/components/uniffi-bindgen-gecko-js",
    "toolkit/components/uniffi-bindgen-gecko-js/test-fixtures",
    "toolkit/library/gtest/rust",
)

# The crates.io index. Anything else is a git dependency, whose version is the
# crate's own and therefore not a crates.io release.
REGISTRY_SOURCE = "registry+https://github.com/rust-lang/crates.io-index"


def parse_cargo_lock(path):
    """Return the third-party packages of a Cargo.lock, keyed by (name, version).

    Workspace members have no ``source`` and are dropped: they are Firefox's
    own crates, not third-party code, and the graph reads the same without
    them because every crate they pull in becomes a root of the forest.
    """
    document = toml.load(path)
    return {
        (package["name"], package["version"]): package
        for package in document.get("package", [])
        if package.get("source")
    }


def _resolve(dependency, packages, by_name):
    """Resolve one Cargo.lock dependency string to a package key.

    Cargo writes a bare ``name`` when the name is unambiguous and
    ``name version`` when several versions coexist.
    """
    parts = dependency.split()
    name = parts[0]
    if len(parts) > 1:
        return (name, parts[1]) if (name, parts[1]) in packages else None
    candidates = by_name.get(name, ())
    return candidates[0] if len(candidates) == 1 else None


def _vendored_metadata(topsrcdir, name):
    """Read the vendored crate's own Cargo.toml, as (package table, error).

    Cargo.lock carries no license, description or URL, so the manifest
    `mach vendor rust` checked in next to the code is the only source for them.
    A manifest that does not parse yields an error rather than an empty table:
    dropped silently, it would leave the SBOM reporting a crate that carries no
    license at all.
    """
    manifest = mozpath.join(topsrcdir, VENDOR_DIR, name, "Cargo.toml")
    if not os.path.exists(manifest):
        return {}, None
    try:
        return toml.load(manifest).get("package", {}), None
    except (OSError, UnicodeDecodeError, toml.TomlDecodeError) as error:
        return {}, str(error)


def _purl(package):
    qualifiers = {}
    if package["source"] != REGISTRY_SOURCE:
        qualifiers["vcs_url"] = package["source"]
    return ("cargo", None, package["name"], package["version"], qualifiers)


def member_directory(metadata, package):
    """A workspace member's directory, relative to the workspace root."""
    root = mozpath.normsep(metadata.get("workspace_root") or "")
    directory = mozpath.dirname(mozpath.normsep(package.get("manifest_path") or ""))
    return mozpath.relpath(directory, root) if root else directory


def dependency_kinds(metadata, tooling_members=TOOLING_MEMBERS, report=None):
    """Map each third-party package of a `cargo metadata` document to its kinds.

    A crate is reached from the workspace as a normal, a build or a dev
    dependency, and only the first ends up in the product: `mockall`, `cc`
    and `syn` are as vendored as `serde` but ship in nothing. Cargo.lock
    cannot answer this -- it is the flat union of all three -- so the kinds
    come from `cargo metadata`, whose resolve graph labels every edge.
    A proc-macro, and what it or a build dependency reaches, is `build`.

    The workspace also holds test servers and code generators, listed in
    ``tooling_members``. What a member reached only from those depends on is
    labelled `tooling`: hyper and tokio are normal dependencies of the
    http3server, not of the product.

    Returns {(name, version): sorted kinds}. A crate reached both ways carries
    both, which is the common case for something used in tests as well.
    ``report`` is told about a ``tooling_members`` entry that is no member.
    """
    packages = {package["id"]: package for package in metadata["packages"]}
    # Only the nodes `packages` describes: the directory of a member and the
    # name and version of a result are all read from there, so a node it does
    # not cover could not be reported on further down anyway. Restricting the
    # graph once here keeps every packages[] lookup below total.
    nodes = {
        node["id"]: node
        for node in metadata["resolve"]["nodes"]
        if node["id"] in packages
    }
    members = {m for m in metadata["workspace_members"] if m in nodes}
    proc_macros = {
        package["id"]
        for package in metadata["packages"]
        for target in package.get("targets", ())
        if "proc-macro" in target["kind"]
    }

    def edges(node_id):
        for dep in nodes[node_id]["deps"]:
            for dep_kind in dep["dep_kinds"]:
                yield dep["pkg"], dep_kind.get("kind") or "normal"

    directories = {
        member: member_directory(metadata, packages[member])
        for member in metadata["workspace_members"]
        if member in packages
    }
    # A member that moved would silently count as shipped again.
    if report:
        for stale in sorted(set(tooling_members) - set(directories.values())):
            report(f"TOOLING_MEMBERS names {stale}, which is no workspace member")
    tooling = {member for member in members if directories[member] in tooling_members}

    # A member is shipped when a shipped root reaches it, a root being a
    # member no other member builds. What only tooling members build, like
    # webdriver under geckodriver, is not.
    built_by_members = {
        target
        for member in members
        for target, kind in edges(member)
        if kind != "dev" and target in nodes
    }
    shipped = set()
    queue = collections.deque(
        member for member in sorted(members - tooling) if member not in built_by_members
    )
    while queue:
        member = queue.popleft()
        if member in shipped:
            continue
        shipped.add(member)
        queue.extend(
            target
            for target, kind in edges(member)
            if kind == "normal" and target in members - proc_macros
        )

    queue = collections.deque()
    for member in members:
        for target, edge_kind in edges(member):
            if edge_kind == "normal" and member in proc_macros:
                kind = "build"
            elif member not in shipped and edge_kind != "dev":
                kind = "tooling"
            else:
                kind = edge_kind
            queue.append((target, kind))

    kinds = collections.defaultdict(set)
    seen = set()
    while queue:
        package_id, kind = queue.popleft()
        if package_id not in nodes:
            continue
        if kind == "normal" and package_id in proc_macros:
            kind = "build"
        if (package_id, kind) in seen:
            continue
        seen.add((package_id, kind))
        kinds[package_id].add(kind)
        for target, target_kind in edges(package_id):
            # A dependency's own dev-dependencies are never built, so the walk
            # stops there rather than dragging a whole test-only subtree in.
            if target_kind != "dev":
                queue.append((target, "build" if target_kind == "build" else kind))

    return {
        (packages[package_id]["name"], packages[package_id]["version"]): sorted(values)
        for package_id, values in kinds.items()
        if packages[package_id].get("source")
    }


def collect_dependency_kinds(
    topsrcdir, topobjdir=None, cargo=None, log=None, rustc=None, target=None
):
    """Run `cargo metadata` and reduce it to dependency kinds.

    ``cargo`` is the configuration's CARGO subst; automation has no bare
    `cargo` on PATH, so falling back to one only helps an unconfigured tree.
    ``rustc`` is the RUSTC subst, passed through as the `RUSTC` environment
    variable: resolving the dependency graph makes cargo run `rustc -vV` for
    the host target and cfgs, and it looks for it on PATH, where the Linux
    build tasks do not have it. Cargo reads `RUSTC` ahead of PATH. A
    ``target`` triple leaves out the dependencies only another platform's
    `cfg` selects.

    Returns {} when the answer is not available rather than failing: the
    vendored-source replacement lives in the objdir's generated cargo config,
    so an unconfigured tree, a source tarball or a checkout with no cargo
    installed simply has no kinds to report. Each of those reasons is reported
    through ``log``, so an empty result is never just silence -- a cargo that
    fails and a cargo that is missing are not the same news.
    """

    def report(message):
        if log:
            log(f"cargo metadata: {message}")

    environment = dict(os.environ)
    if topobjdir:
        cargo_home = mozpath.join(topobjdir, ".cargo")
        if not os.path.isdir(cargo_home):
            report(
                f"{cargo_home} does not exist, so the vendored sources are "
                "not configured"
            )
            return {}
        environment["CARGO_HOME"] = cargo_home
    if rustc:
        environment["RUSTC"] = rustc

    try:
        output = subprocess.run(
            [cargo or "cargo", "metadata", "--format-version", "1", "--offline"]
            + (["--filter-platform", target] if target else []),
            cwd=topsrcdir,
            env=environment,
            capture_output=True,
            check=True,
            encoding="utf-8",
        ).stdout
    except OSError as error:
        report(f"cannot run {cargo or 'cargo'}: {error}")
        return {}
    except subprocess.CalledProcessError as error:
        report(f"exited {error.returncode}: {(error.stderr or '').strip()}")
        return {}

    try:
        return dependency_kinds(json.loads(output), report=report)
    except (ValueError, KeyError) as error:
        report(f"output not understood: {type(error).__name__}: {error}")
        return {}


def crate_records(topsrcdir, lock_path=None, kinds=None):
    """Build (records, edges) for every third-party crate in Cargo.lock.

    ``edges`` maps a record's bom_ref to the bom_refs it depends on, which is
    what turns the SBOM into a graph rather than a flat list.

    ``kinds`` comes from collect_dependency_kinds(); is_tooling() uses it
    to tell what Firefox ships from what only builds its tests.
    """
    lock_path = lock_path or mozpath.join(topsrcdir, "Cargo.lock")
    if not os.path.exists(lock_path):
        return [], {}

    packages = parse_cargo_lock(lock_path)
    by_name = collections.defaultdict(list)
    for key in packages:
        by_name[key[0]].append(key)

    def bom_ref(key):
        name, version = key
        vendored = mozpath.join(VENDOR_DIR, name)
        if os.path.isdir(mozpath.join(topsrcdir, vendored)):
            return vendored
        return f"cargo:{name}@{version}"

    records = []
    edges = {}
    for key in sorted(packages):
        name, version = key
        package = packages[key]
        metadata, manifest_error = _vendored_metadata(topsrcdir, name)

        properties = {"moz:cargo.source": package["source"]}
        if manifest_error:
            properties["moz:cargo.manifest-unreadable"] = manifest_error
        licenses = []
        if metadata.get("license"):
            licenses = [RE_LEGACY_LICENSE_SEPARATOR.sub(" OR ", metadata["license"])]
        elif metadata.get("license-file"):
            properties["moz:cargo.license-file"] = metadata["license-file"]

        hashes = []
        if package.get("checksum"):
            # Cargo.lock records the SHA-256 of the .crate archive published to
            # crates.io, which is exactly what an SBOM consumer needs to tell
            # whether the vendored copy came from the published release.
            hashes = [("SHA-256", package["checksum"])]

        resolved = {
            dependency: _resolve(dependency, packages, by_name)
            for dependency in package.get("dependencies", [])
        }
        # An edge Cargo.lock names but that matches no package it also lists.
        # Recorded rather than dropped: a graph missing edges silently reads
        # like a complete one.
        unresolved = sorted(spec for spec, key in resolved.items() if key is None)
        if unresolved:
            properties["moz:cargo.unresolved-dependencies"] = ",".join(unresolved)

        ref = bom_ref(key)
        records.append({
            "bom_ref": ref,
            "name": name,
            "version": version,
            "description": metadata.get("description"),
            "purl": _purl(package),
            "licenses": licenses,
            "website": metadata.get("homepage"),
            "vcs": metadata.get("repository"),
            "bugzilla": None,
            "hashes": hashes,
            "kinds": (kinds or {}).get(key, []),
            "properties": properties,
        })

        dependencies = sorted({bom_ref(key) for key in resolved.values() if key})
        if dependencies:
            edges[ref] = dependencies

    return records, edges


def is_tooling(record):
    """Is a crate reached only as a build or dev dependency, or by tooling?

    Such a crate builds or tests the product and ships in nothing. Without
    kinds, which an unconfigured tree cannot collect, nothing is known to be.
    """
    return bool(record["kinds"]) and "normal" not in record["kinds"]
