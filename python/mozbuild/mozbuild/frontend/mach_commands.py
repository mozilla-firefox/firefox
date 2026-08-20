# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import re
import sys
import time
import urllib.request
from collections import defaultdict

import mozpack.path as mozpath
from mach.decorators import Command, CommandArgument, SubCommand
from mozpack.files import FileFinder

TOPSRCDIR = os.path.abspath(os.path.join(__file__, "../../../../../"))

# Herald rules as scraped from Phabricator by the reviewer-selector tool. We use
# them to replicate the reviewers Herald would automatically add for a set of
# files. This dump is a bootstrapping measure: its format and freshness are not
# guaranteed, and this should eventually move to querying the reviewer-selector
# tool (or mots) directly rather than parsing the dump.
HERALD_RULES_URL = (
    "https://raw.githubusercontent.com/mozilla-conduit/reviewer-selector/"
    "main/herald_rules.json"
)
# Re-download the cached copy once it gets older than this.
HERALD_RULES_MAX_AGE = 24 * 60 * 60

# Number of recent commits to inspect when falling back to version control
# history to find past reviewers for a file.
RECENT_REVIEWERS_COMMIT_LIMIT = 100


class InvalidPathException(Exception):
    """Represents an error due to an invalid path."""


@Command(
    "mozbuild-reference",
    category="build-dev",
    description="View reference documentation on mozbuild files.",
    virtualenv_name="docs",
)
@CommandArgument(
    "symbol",
    default=None,
    nargs="*",
    help="Symbol to view help on. If not specified, all will be shown.",
)
@CommandArgument(
    "--name-only",
    "-n",
    default=False,
    action="store_true",
    help="Print symbol names only.",
)
def reference(command_context, symbol, name_only=False):
    import mozbuild.frontend.context as m
    from mozbuild.sphinx import (
        format_module,
        function_reference,
        special_reference,
        variable_reference,
    )

    if name_only:
        for s in sorted(m.VARIABLES.keys()):
            print(s)

        for s in sorted(m.FUNCTIONS.keys()):
            print(s)

        for s in sorted(m.SPECIAL_VARIABLES.keys()):
            print(s)

        return 0

    if len(symbol):
        for s in symbol:
            if s in m.VARIABLES:
                for line in variable_reference(s, *m.VARIABLES[s]):
                    print(line)
                continue
            elif s in m.FUNCTIONS:
                for line in function_reference(s, *m.FUNCTIONS[s]):
                    print(line)
                continue
            elif s in m.SPECIAL_VARIABLES:
                for line in special_reference(s, *m.SPECIAL_VARIABLES[s]):
                    print(line)
                continue

            print("Could not find symbol: %s" % s)
            return 1

        return 0

    for line in format_module(m):
        print(line)

    return 0


@Command(
    "file-info", category="build-dev", description="Query for metadata about files."
)
def file_info(command_context):
    """Show files metadata derived from moz.build files.

    moz.build files contain "Files" sub-contexts for declaring metadata
    against file patterns. This command suite is used to query that data.
    """


@SubCommand(
    "file-info",
    "bugzilla-component",
    "Show Bugzilla component info for files listed.",
)
@CommandArgument("-r", "--rev", help="Version control revision to look up info from")
@CommandArgument(
    "--format",
    choices={"json", "plain"},
    default="plain",
    help="Output format",
    dest="fmt",
)
@CommandArgument("paths", nargs="+", help="Paths whose data to query")
def file_info_bugzilla(command_context, paths, rev=None, fmt=None):
    """Show Bugzilla component for a set of files.

    Given a requested set of files (which can be specified using
    wildcards), print the Bugzilla component for each file.
    """
    components = defaultdict(set)
    try:
        for p, m in _get_files_info(command_context, paths, rev=rev).items():
            components[m.get("BUG_COMPONENT")].add(p)
    except InvalidPathException as e:
        print(e)
        return 1

    if fmt == "json":
        data = {}
        for component, files in components.items():
            if not component:
                continue
            for f in files:
                data[f] = [component.product, component.component]

        json.dump(data, sys.stdout, sort_keys=True, indent=2)
        return
    elif fmt == "plain":
        comp_to_file = sorted(
            (
                (
                    "UNKNOWN"
                    if component is None
                    else "%s :: %s" % (component.product, component.component)
                ),
                sorted(files),
            )
            for component, files in components.items()
        )
        for component, files in comp_to_file:
            print(component)
            for f in files:
                print("  %s" % f)
    else:
        print("unhandled output format: %s" % fmt)
        return 1


@SubCommand(
    "file-info", "missing-bugzilla", "Show files missing Bugzilla component info"
)
@CommandArgument("-r", "--rev", help="Version control revision to look up info from")
@CommandArgument(
    "--format",
    choices={"json", "plain"},
    dest="fmt",
    default="plain",
    help="Output format",
)
@CommandArgument("paths", nargs="+", help="Paths whose data to query")
def file_info_missing_bugzilla(command_context, paths, rev=None, fmt=None):
    missing = set()

    try:
        for p, m in _get_files_info(command_context, paths, rev=rev).items():
            if "BUG_COMPONENT" not in m:
                missing.add(p)
    except InvalidPathException as e:
        print(e)
        return 1

    if fmt == "json":
        json.dump({"missing": sorted(missing)}, sys.stdout, indent=2)
        return
    elif fmt == "plain":
        for f in sorted(missing):
            print(f)
    else:
        print("unhandled output format: %s" % fmt)
        return 1


@SubCommand(
    "file-info",
    "bugzilla-automation",
    "Perform Bugzilla metadata analysis as required for automation",
)
@CommandArgument("out_dir", help="Where to write files")
def bugzilla_automation(command_context, out_dir):
    """Analyze and validate Bugzilla metadata as required by automation.

    This will write out JSON and gzipped JSON files for Bugzilla metadata.

    The exit code will be non-0 if Bugzilla metadata fails validation.
    """
    import gzip

    missing_component = set()
    seen_components = set()
    component_by_path = {}

    # TODO operate in VCS space. This requires teaching the VCS reader
    # to understand wildcards and/or for the relative path issue in the
    # VCS finder to be worked out.
    for p, m in sorted(_get_files_info(command_context, ["**"]).items()):
        if "BUG_COMPONENT" not in m:
            missing_component.add(p)
            print(
                "FileToBugzillaMappingError: Missing Bugzilla component: "
                "%s - Set the BUG_COMPONENT in the moz.build file to fix "
                "the issue." % p
            )
            continue

        c = m["BUG_COMPONENT"]
        seen_components.add(c)
        component_by_path[p] = [c.product, c.component]

    print("Examined %d files" % len(component_by_path))

    # We also have a normalized versions of the file to components mapping
    # that requires far less storage space by eliminating redundant strings.
    indexed_components = {
        i: [c.product, c.component] for i, c in enumerate(sorted(seen_components))
    }
    components_index = {tuple(v): k for k, v in indexed_components.items()}
    normalized_component = {"components": indexed_components, "paths": {}}

    for p, c in component_by_path.items():
        d = normalized_component["paths"]
        while "/" in p:
            base, p = p.split("/", 1)
            d = d.setdefault(base, {})

        d[p] = components_index[tuple(c)]

    if not os.path.exists(out_dir):
        os.makedirs(out_dir)

    components_json = os.path.join(out_dir, "components.json")
    print("Writing %s" % components_json)
    with open(components_json, "w") as fh:
        json.dump(component_by_path, fh, sort_keys=True, indent=2)

    missing_json = os.path.join(out_dir, "missing.json")
    print("Writing %s" % missing_json)
    with open(missing_json, "w") as fh:
        json.dump({"missing": sorted(missing_component)}, fh, indent=2)

    indexed_components_json = os.path.join(out_dir, "components-normalized.json")
    print("Writing %s" % indexed_components_json)
    with open(indexed_components_json, "w") as fh:
        # Don't indent so file is as small as possible.
        json.dump(normalized_component, fh, sort_keys=True)

    # Write compressed versions of JSON files.
    for p in (components_json, indexed_components_json, missing_json):
        gzip_path = "%s.gz" % p
        print("Writing %s" % gzip_path)
        with open(p, "rb") as ifh, gzip.open(gzip_path, "wb") as ofh:
            while True:
                data = ifh.read(32768)
                if not data:
                    break
                ofh.write(data)

    # Causes CI task to fail if files are missing Bugzilla annotation.
    if missing_component:
        return 1


def _get_files_info(command_context, paths, rev=None):
    reader = command_context.mozbuild_reader(config_mode="empty", vcs_revision=rev)

    # Normalize to relative from topsrcdir.
    relpaths = []
    finder = FileFinder(
        command_context.topsrcdir, find_dotfiles=True, ignore=[".hg", ".git"]
    )
    for path in paths:
        for p, _ in finder.find(path):
            a = mozpath.abspath(p)
            if not mozpath.basedir(a, [command_context.topsrcdir]):
                raise InvalidPathException("path is outside topsrcdir: %s" % p)

            relpaths.append(mozpath.relpath(a, command_context.topsrcdir))

    # Expand wildcards.
    # One variable is for ordering. The other for membership tests.
    # (Membership testing on a list can be slow.)
    allpaths = []
    all_paths_set = set()
    for p in relpaths:
        if "*" not in p:
            if p not in all_paths_set:
                if not os.path.exists(mozpath.join(command_context.topsrcdir, p)):
                    print("(%s does not exist; ignoring)" % p, file=sys.stderr)
                    continue

                all_paths_set.add(p)
                allpaths.append(p)
            continue

        if rev:
            raise InvalidPathException("cannot use wildcard in version control mode")

        # finder is rooted at / for now.
        # TODO bug 1171069 tracks changing to relative.
        search = mozpath.join(command_context.topsrcdir, p)[1:]
        for path, f in reader.finder.find(search):
            path = path[len(command_context.topsrcdir) :]
            if path not in all_paths_set:
                all_paths_set.add(path)
                allpaths.append(path)

    return reader.files_info(allpaths)


@SubCommand(
    "file-info", "schedules", "Show the combined SCHEDULES for the files listed."
)
@CommandArgument("paths", nargs="+", help="Paths whose data to query")
def file_info_schedules(command_context, paths):
    """Show what is scheduled by the given files.

    Given a requested set of files (which can be specified using
    wildcards), print the total set of scheduled components.
    """
    from mozbuild.frontend.reader import BuildReader, EmptyConfig

    config = EmptyConfig(TOPSRCDIR)
    reader = BuildReader(config)
    schedules = set()
    for p, m in reader.files_info(paths).items():
        schedules |= set(m["SCHEDULES"].components)

    print(", ".join(schedules))


def _load_herald_rules(offline=False):
    """Return the parsed herald_rules.json, fetching and caching it as needed.

    The file is cached under the mozbuild state directory and re-downloaded once
    the cached copy gets older than HERALD_RULES_MAX_AGE. When offline (or the
    download fails) any cached copy is used regardless of age; if none exists,
    None is returned.
    """
    from mach.util import get_state_dir

    cache_path = os.path.join(get_state_dir(), "herald_rules.json")

    fresh = False
    if os.path.exists(cache_path):
        age = time.time() - os.path.getmtime(cache_path)
        fresh = age < HERALD_RULES_MAX_AGE

    if not offline and not fresh:
        try:
            with urllib.request.urlopen(HERALD_RULES_URL, timeout=30) as fh:
                data = fh.read()
            rules = json.loads(data)
            with open(cache_path, "wb") as fh:
                fh.write(data)
            return rules
        except Exception as e:
            print(
                f"(could not update herald rules cache from {HERALD_RULES_URL}: "
                f"{e}; using cached copy if available)",
                file=sys.stderr,
            )

    if not os.path.exists(cache_path):
        return None

    with open(cache_path) as fh:
        return json.load(fh)


def _herald_reviewers_for_files(rules_data, relpaths):
    """Return the reviewers Herald would auto-add for the given files.

    Returns (groups, individuals), each a dict mapping a reviewer target to
    whether it is added as a blocking reviewer by at least one matching rule.
    Only the file-based conditions of a rule are evaluated; conditions that
    depend on revision context (author, reviewers already on the revision,
    status, etc.) are ignored, since we only have a set of files to work from.
    """
    # Phabricator presents affected files with a leading slash, which is how the
    # rules' regexps and substrings are written, so match against that form.
    candidates = ["/" + p for p in relpaths]

    def condition_matches(condition, candidates):
        op = condition["operator"]
        value = condition["value"]
        if op in ("matches-regexp", "does-not-match-regexp"):
            try:
                matched = any(re.search(value, p) for p in candidates)
            except re.error:
                # A rule we can't evaluate shouldn't match.
                return False
            return matched if op == "matches-regexp" else not matched
        if op == "contains":
            return any(value in p for p in candidates)
        if op == "does-not-contain":
            return all(value not in p for p in candidates)
        # Unknown file operator: don't match on a condition we don't understand.
        return False

    groups = {}
    individuals = {}
    for rule in rules_data.get("rules", []):
        if rule.get("status") != "active":
            continue

        file_conditions = [
            c
            for c in rule.get("conditions", [])
            if c["type"] == "differential-affected-files"
        ]
        # Require at least one positive condition that actually matches a file.
        # Rules whose only file condition is negative (e.g. does-not-contain
        # "third_party"), or that have none at all, rely on non-file conditions
        # we don't evaluate to scope themselves, so they would otherwise fire
        # for essentially any path.
        positive = [
            c
            for c in file_conditions
            if c["operator"] in ("matches-regexp", "contains")
        ]
        if not positive:
            continue
        if not all(condition_matches(c, candidates) for c in file_conditions):
            continue

        for action in rule.get("actions", []):
            if action["type"] != "add-reviewers":
                continue
            for reviewer in action["reviewers"]:
                target = reviewer["target"]
                blocking = reviewer.get("blocking", False)
                bucket = groups if reviewer.get("is_group") else individuals
                bucket[target] = bucket.get(target, False) or blocking

    return groups, individuals


def _reviewers_to_json(reviewers):
    """Serialize a {name: blocking} reviewer dict to a sorted list of dicts."""
    return [
        {"name": name, "blocking": blocking}
        for name, blocking in sorted(reviewers.items())
    ]


# Matches the "r=..." part of a commit message subject, capturing the
# comma-separated list of reviewers. A reviewer is a name made of word
# characters and hyphens, optionally prefixed with "#" (a group) and/or suffixed
# with "!" (blocking), e.g. "r=foo,#bar-reviewers,baz!".
_REVIEWER_RE = re.compile(r"\br=(#?[\w-]+!?(?:,#?[\w-]+!?)*)")


def _parse_reviewers_from_subjects(subjects):
    """Tally reviewers from a list of commit message subjects.

    Returns (individuals, groups), each an ordered list of (name, count) tuples
    sorted by descending count. Groups are distinguished by their "#" prefix,
    which is Phabricator's marker for a reviewer group.
    """
    individuals = defaultdict(int)
    groups = defaultdict(int)
    for line in subjects:
        match = _REVIEWER_RE.search(line)
        if not match:
            continue
        for entry in match.group(1).split(","):
            name = entry.rstrip("!")  # Drop the "!" blocking marker.
            if name.startswith("#"):
                groups[name[1:]] += 1
            else:
                individuals[name] += 1

    def ranked(counter):
        return sorted(counter.items(), key=lambda kv: (-kv[1], kv[0]))

    return ranked(individuals), ranked(groups)


def _recent_commit_subjects(repo, relpaths):
    """Return the subjects of recent commits touching the given files.

    Works across the supported version control systems. A jj checkout uses a git
    backing store, so its history is read through the git backend.
    """
    limit = RECENT_REVIEWERS_COMMIT_LIMIT
    paths = list(relpaths)
    if repo.name == "hg":
        out = repo._run(
            "log",
            "-l",
            str(limit),
            "-T",
            "{desc|firstline}\n",
            "--",
            *paths,
        )
    else:
        # git, and jj (through its git backing store).
        git = repo._git if repo.name == "jj" else repo
        out = git._run(
            "log",
            "-n",
            str(limit),
            "--no-merges",
            "--format=%s",
            "--",
            *paths,
        )
    return out.splitlines()


def _recent_reviewers_for_files(command_context, relpaths):
    """Tally reviewers from recent commits touching the given files.

    Reviewers are parsed from the "r=" trailers of recent commit message
    subjects. Returns (individuals, groups) as produced by
    _parse_reviewers_from_subjects.
    """
    try:
        repo = command_context.repository
        subjects = _recent_commit_subjects(repo, relpaths)
    except Exception as e:
        print(f"(could not read version control history: {e})", file=sys.stderr)
        return [], []

    return _parse_reviewers_from_subjects(subjects)


@SubCommand(
    "file-info",
    "reviewers",
    "Suggest reviewers for the files listed.",
)
@CommandArgument("-r", "--rev", help="Version control revision to look up info from")
@CommandArgument(
    "--format",
    choices={"json", "plain"},
    default="plain",
    help="Output format",
    dest="fmt",
)
@CommandArgument(
    "--offline",
    action="store_true",
    help="Use the cached herald rules without checking for updates",
)
@CommandArgument("paths", nargs="+", help="Paths whose reviewers to suggest")
def file_info_reviewers(command_context, paths, rev=None, fmt=None, offline=False):
    """Suggest reviewers for a set of files.

    The reviewers that Phabricator's Herald rules would automatically add for
    the files (from the reviewer-selector tool's herald_rules.json) are
    suggested first. When no rule matches, this falls back to the individuals
    and groups that reviewed recent patches touching the files, parsed from
    "r=" trailers in the version control history.
    """
    try:
        files_info = _get_files_info(command_context, paths, rev=rev)
    except InvalidPathException as e:
        print(e)
        return 1

    relpaths = sorted(files_info.keys())
    if not relpaths:
        print("(no matching files)", file=sys.stderr)
        return 1

    rules_data = _load_herald_rules(offline=offline)

    herald_groups, herald_individuals = {}, {}
    if rules_data:
        herald_groups, herald_individuals = _herald_reviewers_for_files(
            rules_data, relpaths
        )
    else:
        print(
            "(herald rules unavailable; skipping Herald suggestions)",
            file=sys.stderr,
        )

    # Parsing the version control history only to find a reviewer that Herald
    # would have added anyway is wasteful, and on a rarely-touched file the walk
    # is slow (the VCS has to traverse all of history to find the commit limit).
    # So fall back to it only when no reviewer was found from the herald rules.
    have_herald = herald_groups or herald_individuals
    recent_individuals, recent_groups = [], []
    if not have_herald:
        recent_individuals, recent_groups = _recent_reviewers_for_files(
            command_context, relpaths
        )

    if fmt == "json":
        data = {"files": relpaths}
        if have_herald:
            data["herald_groups"] = _reviewers_to_json(herald_groups)
            data["herald_individuals"] = _reviewers_to_json(herald_individuals)
        else:
            data["recent_groups"] = [
                {"name": name, "count": count} for name, count in recent_groups
            ]
            data["recent_individuals"] = [
                {"name": name, "count": count} for name, count in recent_individuals
            ]
        json.dump(data, sys.stdout, indent=2)
        print()
        return

    if have_herald:
        print("Herald reviewers (automatically added):")
        for group, blocking in sorted(herald_groups.items()):
            print(f"  #{group}{' (blocking)' if blocking else ''}")
        for name, blocking in sorted(herald_individuals.items()):
            print(f"  {name}{' (blocking)' if blocking else ''}")
        return

    print("No Herald reviewers matched.")
    print("\nRecent reviewers (from version control history):")
    if recent_individuals or recent_groups:
        for name, count in recent_groups:
            print(f"  #{name} ({count})")
        for name, count in recent_individuals:
            print(f"  {name} ({count})")
    else:
        print("  (none)")


@SubCommand(
    "file-info",
    "reviewer-groups",
    "List the known reviewer groups.",
)
@CommandArgument(
    "--format",
    choices={"json", "plain"},
    default="plain",
    help="Output format",
    dest="fmt",
)
@CommandArgument(
    "--offline",
    action="store_true",
    help="Use the cached herald rules without checking for updates",
)
def file_info_reviewer_groups(command_context, fmt=None, offline=False):
    """List the reviewer groups known to the reviewer-selector tool.

    These are the groups referenced by Phabricator's Herald rules, as scraped
    into herald_rules.json. The list is the source of truth for valid group
    names when choosing a reviewer (e.g. "#firefox-build-system-reviewers").
    """
    rules_data = _load_herald_rules(offline=offline)
    if not rules_data:
        print("(herald rules unavailable)", file=sys.stderr)
        return 1

    groups = sorted(rules_data.get("groups", {}).keys())

    if fmt == "json":
        json.dump(groups, sys.stdout, indent=2)
        print()
        return

    for group in groups:
        print(group)
