# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Map in-tree moz.yaml manifests onto ecosystem-neutral SBOM records.

This module deliberately does not import cyclonedx, so the mapping rules can be
unit tested from sites that do not vendor it. See sbom_cyclonedx.py for the
serialization half.
"""

import fnmatch
import json
import re

import mozpack.path as mozpath

from mozbuild.vendor.moz_yaml import MozYamlVerifyError, load_moz_yaml

# `origin.release` is free-form prose, not a version. Real values look like
# "v1.6.58 (2026-04-15T20:23:26+03:00)." or "version 3.7.3".
RE_RELEASE_PREFIX = re.compile(r"^(?:version|tag|release)\s+", re.IGNORECASE)
RE_RELEASE_SUFFIX = re.compile(r"\s*\([^)]*\)\s*\.?\s*$")

RE_GITHUB = re.compile(
    r"^https?://(?:www\.)?github\.com/([^/]+)/([^/]+?)(?:\.git)?/?$", re.IGNORECASE
)
# Only gitlab.com: pkg:gitlab has no notion of a host, so claiming it for a
# self-hosted instance would point at the wrong project. Every gitlab-hosted
# dependency in-tree today is self-hosted (freedesktop, xiph, videolan) and
# correctly falls through to pkg:generic with a vcs_url qualifier.
RE_GITLAB = re.compile(
    r"^https?://(?:www\.)?gitlab\.com/(.+?)/([^/]+?)(?:\.git)?/?$", re.IGNORECASE
)

RE_PURL_UNSAFE = re.compile(r"[^a-z0-9._-]+")

# `mach vendor`'s updatebot-only shims describe a version bump rather than code
# vendored at this path, so they are not components.
PSEUDO_SOURCE_HOSTING = "yaml-dir"


def clean_version(release):
    """Reduce a free-form `origin.release` to something version-shaped."""
    if not release:
        return None
    version = RE_RELEASE_SUFFIX.sub("", release.strip())
    version = RE_RELEASE_PREFIX.sub("", version).strip()
    return version.rstrip(".") or None


def purl_slug(name):
    return RE_PURL_UNSAFE.sub("-", name.strip().lower()).strip("-") or "unknown"


def _purl(name, version, vendoring):
    """Derive a package URL, preferring a type that vulnerability databases index.

    pkg:generic matches nothing in OSV.dev or the GitHub Advisory Database, so
    github/gitlab URLs are recognised explicitly and everything else keeps the
    upstream repository in a vcs_url qualifier.
    """
    url = (vendoring or {}).get("url", "")
    hosting = (vendoring or {}).get("source-hosting")

    if hosting == "github":
        match = RE_GITHUB.match(url)
        if match:
            namespace, repo = match.group(1).lower(), match.group(2).lower()
            return ("github", namespace, repo, version, {})
    elif hosting == "gitlab":
        match = RE_GITLAB.match(url)
        if match:
            namespace, repo = match.group(1).lower(), match.group(2).lower()
            return ("gitlab", namespace, repo, version, {})

    qualifiers = {}
    if url:
        qualifiers["vcs_url"] = f"git+{url}" + (f"@{version}" if version else "")
    return ("generic", None, purl_slug(name), version, qualifiers)


def _licenses(origin):
    """Normalize `origin.license` to a list of strings.

    yaml.BaseLoader gives us strings for scalars, but the field is also allowed
    to be a list -- moz_yaml.py:274 makes the same distinction.
    """
    license = origin.get("license")
    if not license:
        return []
    if isinstance(license, str):
        return [license]
    return list(license)


def manifest_to_record(rel_path, manifest):
    """Build an SBOM record from one parsed moz.yaml.

    `rel_path` is the topsrcdir-relative path of the manifest itself. Returns
    None for manifests that do not describe a vendored component.
    """
    origin = manifest.get("origin")
    if not origin:
        return None

    vendoring = manifest.get("vendoring") or {}
    if vendoring.get("source-hosting") == PSEUDO_SOURCE_HOSTING:
        return None

    manifest_dir = mozpath.dirname(rel_path) or "."
    name = origin["name"]
    version = origin.get("revision") or clean_version(origin.get("release"))

    properties = {
        "moz:moz-yaml.path": rel_path,
        "moz:bugzilla.product": manifest["bugzilla"]["product"],
        "moz:bugzilla.component": manifest["bugzilla"]["component"],
    }
    for key, value in (
        # sbom_npm.py turns a declared npm name into a pkg:npm purl; it is
        # recorded here so that pass does not have to reparse every manifest.
        ("moz:npm.name", origin.get("npm-name")),
        ("moz:origin.release", origin.get("release")),
        ("moz:origin.notes", origin.get("notes")),
        ("moz:vendoring.source-hosting", vendoring.get("source-hosting")),
        ("moz:vendoring.vendor-directory", vendoring.get("vendor-directory")),
    ):
        if value:
            properties[key] = value

    licenses = _licenses(origin)
    if len(licenses) > 1:
        # moz.yaml has no AND/OR operator, and the answer genuinely differs per
        # library (libjpeg-turbo's IJG/BSD-3-Clause is AND; MIT/Apache-2.0 is
        # usually OR). Record the ambiguity rather than inventing a legal fact.
        properties["moz:license.conjunction"] = "unspecified"

    return {
        "bom_ref": manifest_dir,
        "name": name,
        "version": version,
        "description": origin.get("description"),
        "purl": _purl(name, version, vendoring),
        "licenses": licenses,
        "website": origin.get("url"),
        "vcs": vendoring.get("url"),
        "bugzilla": (
            manifest["bugzilla"]["product"],
            manifest["bugzilla"]["component"],
        ),
        "properties": properties,
    }


# Test fixtures, deliberately incomplete, so a manifest written to exercise a
# validation failure does not become one under `--strict`.
FIXTURE_PREFIXES = ("tools/lint/test/files/",)


def discover_manifests(repo, topsrcdir):
    """Yield topsrcdir-relative paths of every tracked moz.yaml, sorted.

    Driving this off version control rather than a filesystem walk keeps
    objdirs, node_modules and untracked scratch files out of the result.
    """
    finder = repo.get_tracked_files_finder(topsrcdir)
    paths = [
        path
        for path, _ in finder.find("**/moz.yaml")
        if mozpath.basename(path) == "moz.yaml"
        and not path.startswith(FIXTURE_PREFIXES)
    ]
    return sorted(paths)


def collect_records(repo, topsrcdir, log=None):
    """Load every tracked moz.yaml and map it. Returns (records, errors)."""
    records = []
    errors = []
    for rel_path in discover_manifests(repo, topsrcdir):
        full_path = mozpath.join(topsrcdir, rel_path)
        try:
            manifest = load_moz_yaml(full_path, require_license_file=False)
        except (MozYamlVerifyError, ValueError) as error:
            errors.append((rel_path, str(error)))
            if log:
                log(f"skipping {rel_path}: {error}")
            continue
        record = manifest_to_record(rel_path, manifest)
        if record:
            records.append(record)
    records.sort(key=lambda record: record["bom_ref"])
    return records, errors


def load_license_notices(path):
    """Read the licenses.json the build backend writes from moz.build LICENSES.

    Returns a list of notice dicts, or [] if the file is absent, so callers can
    generate an SBOM from an unconfigured tree.
    """
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)["licenses"]
    except FileNotFoundError:
        return []


def _covers(notice_path, component_dir):
    """Does a LICENSES path cover a component directory?

    Notice paths are directories, single files or shell-style globs, so an
    exact match, a parent-directory match and a glob match all count.
    """
    notice_path = notice_path.rstrip("/")
    if notice_path == component_dir:
        return True
    if component_dir.startswith(notice_path + "/"):
        return True
    if notice_path.startswith(component_dir + "/"):
        return True
    return fnmatch.fnmatch(component_dir, notice_path)


def _locations(record):
    """Where a record's code lives: its directory, or where an npm package was
    installed, whose bom_ref is not a path."""
    return [record["bom_ref"], *(record.get("occurrences") or ())]


def merge_license_notices(records, notices):
    """Attach moz.build license notices to the moz.yaml records they cover.

    moz.yaml carries an SPDX-ish `license` field but no notice text, and it only
    exists for vendored libraries. The LICENSES declarations cover directories
    moz.yaml knows nothing about. Where both describe the same code, the notice
    ids are recorded as a property; where moz.yaml declared no license at all,
    the notice's SPDX expression fills the gap.

    A notice flagged `subcomponent` is the exception to "moz.yaml wins": it
    describes code whose license differs from that of the library it sits
    inside, so its expression joins the component's rather than being dropped.

    Only `paths` is consulted, not `declared_in`: the declaring directory holds
    the notice text, which says nothing about the license of its own code.
    toolkit/content/licenses declares most of the shared notices and is itself
    MPL-licensed. The backend leaves it out of `paths` for the same reason.
    """
    index = [(path, notice) for notice in notices for path in notice["paths"]]

    for record in records:
        locations = _locations(record)
        covering = [
            (path.rstrip("/"), notice)
            for path, notice in index
            if any(_covers(path, location) for location in locations)
        ]
        if not covering:
            continue
        record["properties"]["moz:license.notice-ids"] = ",".join(
            sorted({notice["id"] for _, notice in covering})
        )
        # A notice naming files inside the component says more than the
        # component directory does -- nsprpub/pr/src/misc/dtoa.c rather than
        # nsprpub -- and the notice ids alone do not carry it. Record those
        # paths as evidence occurrences, the field meant for "this was found
        # here". A path that merely contains the component adds nothing it
        # does not already know.
        inside = sorted({
            path
            for path, _ in covering
            if any(path.startswith(location + "/") for location in locations)
        })
        if inside:
            record["occurrences"] = sorted(
                set(record.get("occurrences") or []) | set(inside)
            )
        if not record["licenses"]:
            record["licenses"] = sorted({
                notice["spdx"] for _, notice in covering if notice["spdx"]
            })
        # A `subcomponent` notice covers code whose license differs from the
        # enclosing library's -- the MySpell files inside hunspell -- so
        # moz.yaml having answered for the library does not answer for it.
        # Add it rather than let it drop.
        for value in sorted({
            notice["spdx"]
            for _, notice in covering
            if notice.get("subcomponent") and notice["spdx"]
        }):
            if value not in record["licenses"]:
                record["licenses"].append(value)
        record["licenses"].sort()
    return records


def components_for_unmatched(records, notices, is_file=None, elsewhere=()):
    """Build records for licensed code that no moz.yaml or crate describes.

    about:license attributes roughly 250 paths, but moz.yaml only exists for
    vendored libraries, so most of them would otherwise be absent from the SBOM
    entirely.

    One component per notice, not per path: a notice covering thirty files is
    one piece of third-party code, and emitting thirty sibling components
    buries the real libraries in noise. The paths are recorded as CycloneDX
    evidence occurrences, which is the field meant for "this component was
    found here".

    ``is_file`` decides whether a path is a file rather than a directory; it
    defaults to the filesystem-free heuristic that a basename with a suffix is
    a file, so callers with a real tree should pass ``os.path.isfile``.
    ``elsewhere`` holds records another document describes, whose code must
    not come back as a notice component.
    """
    known = {
        location for record in [*records, *elsewhere] for location in _locations(record)
    }

    def covered(path):
        """Is `path` already described by a component, exactly or within one?

        A component the path merely contains does not count. `ipc/chromium` is
        attributed to the Chromium license and the only component under it is
        ipc/chromium/src/third_party/libevent, which describes one vendored
        library rather than the directory; treating the ancestor as covered
        drops the broader path, and where it is a notice's only path -- the
        node-md5 notice on devtools/client/shared/vendor -- drops the notice.
        """
        return any(path == ref or path.startswith(ref + "/") for ref in known)

    if is_file is None:

        def is_file(path):
            return "." in mozpath.basename(path)

    extra = []
    for notice in sorted(notices, key=lambda notice: notice["id"]):
        paths = sorted({
            stripped
            for stripped in (path.rstrip("/") for path in notice["paths"])
            if stripped and not covered(stripped)
        })
        if not paths:
            continue
        extra.append({
            "bom_ref": f"license:{notice['id']}",
            "name": notice["title"],
            "version": None,
            "description": None,
            # No purl: these are files and directories in our own tree, not
            # packages any ecosystem can resolve, and a made-up
            # pkg:generic/<basename> matches nothing in any vulnerability
            # database while looking like it might.
            "purl": None,
            "type": "file" if all(is_file(path) for path in paths) else "library",
            # A license with no SPDX id, JPNIC's for one, is still a license:
            # its title is what CycloneDX's license name is for.
            "licenses": [notice["spdx"] or notice["title"]],
            "website": notice["url"],
            "vcs": None,
            "bugzilla": None,
            "occurrences": paths,
            "properties": {"moz:license.notice-ids": notice["id"]},
        })
    return extra


def unattached_notices(records, notices):
    """Notices that no component carries, so a caller can put them on the root."""
    attached = set()
    for record in records:
        ids = record["properties"].get("moz:license.notice-ids")
        if ids:
            attached.update(ids.split(","))
    return [notice for notice in notices if notice["id"] not in attached]
