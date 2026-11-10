# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

# Utility package for working with moz.yaml files.
#
# Requires `pyyaml` and `voluptuous`
# (both are in-tree under third_party/python)

import errno
import os
import re

import voluptuous
import yaml
from voluptuous import (
    All,
    Boolean,
    FqdnUrl,
    In,
    Invalid,
    Length,
    Match,
    Msg,
    Required,
    Schema,
    Unique,
)
from yaml.error import MarkedYAMLError

# TODO ensure this matches the approved list of licenses
VALID_LICENSES = [
    # Standard Licenses (as per https://spdx.org/licenses/)
    "Apache-2.0",
    "BSD-2-Clause",
    "BSD-3-Clause",
    "BSD-3-Clause-Clear",
    "BSL-1.0",
    "CC0-1.0",
    "FTL",
    "ICU",
    "IJG",
    "ISC",
    "LGPL-2.1",
    "LGPL-3.0",
    "MIT",
    "MPL-1.1",
    "MPL-2.0",
    "Public Domain",
    "Unlicense",
    "WTFPL",
    "Zlib",
    # Unique Licenses
    "ACE",  # http://www.cs.wustl.edu/~schmidt/ACE-copying.html
    "Anti-Grain-Geometry",  # http://www.antigrain.com/license/index.html
    "JPNIC",  # https://www.nic.ad.jp/ja/idn/idnkit/download/index.html
    "Khronos",  # https://www.khronos.org/openmaxdl
    "libpng",  # http://www.libpng.org/pub/png/src/libpng-LICENSE.txt
    "Unicode",  # http://www.unicode.org/copyright.html
]

VALID_SOURCE_HOSTS = [
    "gitlab",
    "googlesource",
    "github",
    "angle",
    "codeberg",
    "git",
    "yaml-dir",
]

RE_SECTION = re.compile(r"^(\S[^:]*):").search
RE_FIELD = re.compile(r"^\s\s([^:]+):\s+(\S+)$").search


class MozYamlVerifyError(Exception):
    def __init__(self, filename, error):
        self.filename = filename
        self.error = error

    def __str__(self):
        return "%s: %s" % (self.filename, self.error)


def load_moz_yaml(filename, verify=True, require_license_file=True):
    """Loads and verifies the specified manifest."""

    # Load and parse YAML.
    try:
        with open(filename) as f:
            manifest = yaml.load(f, Loader=yaml.BaseLoader)
    except OSError as e:
        if e.errno == errno.ENOENT:
            raise MozYamlVerifyError(filename, "Failed to find manifest: %s" % filename)
        raise
    except MarkedYAMLError as e:
        raise MozYamlVerifyError(filename, e)

    if not verify:
        return manifest

    # Verify schema.
    if "schema" not in manifest:
        raise MozYamlVerifyError(filename, 'Missing manifest "schema"')
    if manifest["schema"] == "1":
        schema = _schema_1()
        schema_additional = _schema_1_additional
    else:
        raise MozYamlVerifyError(filename, "Unsupported manifest schema")

    try:
        manifest = schema(manifest)
        schema_additional(filename, manifest, require_license_file=require_license_file)
    except (voluptuous.Error, ValueError) as e:
        raise MozYamlVerifyError(filename, e)

    return manifest


def _schema_1():
    """Returns Voluptuous Schema object."""

    actions_schema = All(
        VendoringActions(),
        [
            {
                Required("action"): In(
                    [
                        "copy-file",
                        "move-file",
                        "move-dir",
                        "replace-in-file",
                        "replace-in-file-regex",
                        "run-script",
                        "run-command",
                        "delete-path",
                        "vcs-add-remove-files",
                    ],
                    msg="Invalid action specified in vendoring-actions",
                ),
                "from": All(str, Length(min=1)),
                "to": All(str, Length(min=1)),
                "pattern": All(str, Length(min=1)),
                "with": All(str, Length(min=1)),
                "file": All(str, Length(min=1)),
                "script": All(str, Length(min=1)),
                "command": All(str, Length(min=1)),
                "args": All([All(str, Length(min=1))]),
                "cwd": All(str, Length(min=1)),
                "path": All(str, Length(min=1)),
            }
        ],
    )

    return Schema({
        Required("schema"): "1",
        Required("bugzilla"): {
            Required("product"): All(str, Length(min=1)),
            Required("component"): All(str, Length(min=1)),
        },
        "origin": {
            Required("name"): All(str, Length(min=1)),
            Required("description"): All(str, Length(min=1)),
            "notes": All(str, Length(min=1)),
            Required("url"): FqdnUrl(),
            Required("license"): Msg(License(), msg="Unsupported License"),
            "license-file": All(str, Length(min=1)),
            # The npm registry name, which the SBOM uses for a pkg:npm purl.
            "npm-name": All(str, Length(min=1)),
            Required("release"): All(str, Length(min=1)),
            # The following regex defines a valid git reference
            # The first group [^ ~^:?*[\]] matches 0 or more times anything
            # that isn't a Space, ~, ^, :, ?, *, or ]
            # The second group [^ ~^:?*[\]\.]+ matches 1 or more times
            # anything that isn't a Space, ~, ^, :, ?, *, [, ], or .
            "revision": Match(r"^[^ ~^:?*[\]]*[^ ~^:?*[\]\.]+$"),
        },
        "updatebot": {
            Required("maintainer-phab"): All(str, Length(min=1)),
            Required("maintainer-bz"): All(str, Length(min=1)),
            "try-preset": All(str, Length(min=1)),
            "fuzzy-query": All(str, Length(min=1)),
            "fuzzy-paths": All([str], Length(min=1)),
            "artifact-build": Boolean(),
            "tasks": All(
                UpdatebotTasks(),
                [
                    {
                        Required("type"): In(
                            ["vendoring", "commit-alert"],
                            msg="Invalid type specified in tasks",
                        ),
                        "branch": All(str, Length(min=1)),
                        "enabled": Boolean(),
                        "cc": Unique([str]),
                        "needinfo": Unique([str]),
                        "filter": In(
                            ["none", "security", "source-extensions"],
                            msg="Invalid filter value specified in tasks",
                        ),
                        "source-extensions": Unique([str]),
                        "blocking": Match(r"^[0-9]+$"),
                        "options": [str],
                        "frequency": Match(
                            r"^(every|release|[1-9][0-9]* (weeks?|days?)|[1-9][0-9]* commits?|"
                            + r"[1-9][0-9]* (weeks?|days?), ?[1-9][0-9]* commits?)$"
                        ),
                    }
                ],
            ),
        },
        "vendoring": {
            Required("url"): FqdnUrl(),
            Required("source-hosting"): All(
                str,
                Length(min=1),
                In(VALID_SOURCE_HOSTS, msg="Unsupported Source Hosting"),
            ),
            "source-host-path": str,
            "tracking": Match(r"^(commit|tag)$"),
            "release-artifact": All(str, Length(min=1)),
            "flavor": Match(r"^(regular|rust|individual-files)$"),
            "skip-vendoring-steps": Unique([str]),
            "tolerate-git-fsck-errors": Boolean(),
            "vendor-directory": All(str, Length(min=1)),
            "patches": Unique([str]),
            "keep": Unique([str]),
            "exclude": Unique([str]),
            "include": Unique([str]),
            "generated": Unique([str]),
            "individual-files": [
                {
                    Required("upstream"): All(str, Length(min=1)),
                    Required("destination"): All(str, Length(min=1)),
                }
            ],
            "individual-files-default-upstream": str,
            "individual-files-default-destination": All(str, Length(min=1)),
            "individual-files-list": Unique([str]),
            "update-actions": actions_schema,
            "post-patch-actions": actions_schema,
        },
    })


def _schema_1_additional(filename, manifest, require_license_file=True):
    """Additional schema/validity checks"""

    vendor_directory = os.path.dirname(filename)
    if "vendoring" in manifest and "vendor-directory" in manifest["vendoring"]:
        vendor_directory = manifest["vendoring"]["vendor-directory"]

    # LICENSE file must exist, except for Rust crates which are exempted
    # because the license is required to be specified in the Cargo.toml file
    if require_license_file and "origin" in manifest:
        files = [f.lower() for f in os.listdir(vendor_directory)]
        if (
            not (
                "license-file" in manifest["origin"]
                and manifest["origin"]["license-file"].lower() in files
            )
            and not (
                "license" in files
                or "license.txt" in files
                or "license.rst" in files
                or "license.html" in files
                or "license.md" in files
            )
            and not (
                "vendoring" in manifest
                and manifest["vendoring"].get("flavor", "regular") == "rust"
            )
        ):
            license = manifest["origin"]["license"]
            if isinstance(license, list):
                license = "/".join(license)
            raise ValueError("Failed to find %s LICENSE file" % license)

    # Cannot vendor without an origin.
    if "vendoring" in manifest and "origin" not in manifest:
        raise ValueError('"vendoring" requires an "origin"')

    # Cannot vendor without a computer-readable revision.
    if "vendoring" in manifest and "revision" not in manifest["origin"]:
        raise ValueError(
            'If "vendoring" is present, "revision" must be present in "origin"'
        )

    # The Rust and Individual Flavor type precludes a lot of options
    # individual-files could, in theory, use several of these, but until we have a use case let's
    # disallow them so we're not worrying about whether they work. When we need them we can make
    # sure they do.
    if (
        "vendoring" in manifest
        and manifest["vendoring"].get("flavor", "regular") != "regular"
    ):
        for i in [
            "keep",
            "exclude",
            "include",
            "generated",
        ]:
            if i in manifest["vendoring"]:
                raise ValueError("A non-regular flavor of update cannot use '%s'" % i)

        if manifest["vendoring"].get("flavor", "regular") == "rust":
            for i in [
                "skip-vendoring-steps",
                "update-actions",
            ]:
                if i in manifest["vendoring"]:
                    raise ValueError("A rust flavor of update cannot use '%s'" % i)

    # Ensure that only individual-files flavor uses those options
    if (
        "vendoring" in manifest
        and manifest["vendoring"].get("flavor", "regular") != "individual-files"
    ):
        if (
            "individual-files" in manifest["vendoring"]
            or "individual-files-list" in manifest["vendoring"]
        ):
            raise ValueError(
                "Only individual-files flavor of update can use 'individual-files'"
            )

    # Ensure that release-artifact is only used with tag tracking
    if "vendoring" in manifest and "release-artifact" in manifest["vendoring"]:
        if (
            manifest["vendoring"].get("source-hosting") != "github"
            or manifest["vendoring"].get("tracking", "commit") != "tag"
        ):
            raise ValueError(
                "You can only use release-artifact with tag tracking from Github."
            )

    # Ensure that the individual-files flavor has all the correct options
    if (
        "vendoring" in manifest
        and manifest["vendoring"].get("flavor", "regular") == "individual-files"
    ):
        # Because the only way we can determine the latest tag is by doing a local clone,
        # we don't want to do that for individual-files flavors because those flavors are
        # usually on gigantic repos we don't want to clone for such a simple thing.
        if manifest["vendoring"].get("tracking", "commit") == "tag":
            raise ValueError(
                "You cannot use tag tracking with the individual-files flavor. (Sorry.)"
            )

        # We need either individual-files or individual-files-list
        if (
            "individual-files" not in manifest["vendoring"]
            and "individual-files-list" not in manifest["vendoring"]
        ):
            raise ValueError(
                "The individual-files flavor must include either "
                + "'individual-files' or 'individual-files-list'"
            )
        # For whichever we have, make sure we don't have the other and we don't have
        # options we shouldn't or lack ones we should.
        if "individual-files" in manifest["vendoring"]:
            if "individual-files-list" in manifest["vendoring"]:
                raise ValueError(
                    "individual-files-list is mutually exclusive with individual-files"
                )
            if "individual-files-default-upstream" in manifest["vendoring"]:
                raise ValueError(
                    "individual-files-default-upstream can only be used with individual-files-list"
                )
            if "individual-files-default-destination" in manifest["vendoring"]:
                raise ValueError(
                    "individual-files-default-destination can only be used "
                    + "with individual-files-list"
                )
        if "individual-files-list" in manifest["vendoring"]:
            if "individual-files" in manifest["vendoring"]:
                raise ValueError(
                    "individual-files is mutually exclusive with individual-files-list"
                )
            if "individual-files-default-upstream" not in manifest["vendoring"]:
                raise ValueError(
                    "individual-files-default-upstream must be used with individual-files-list"
                )
            if "individual-files-default-destination" not in manifest["vendoring"]:
                raise ValueError(
                    "individual-files-default-destination must be used with individual-files-list"
                )
            misplaced = []
            previous = None
            for current in manifest["vendoring"]["individual-files-list"]:
                if previous is not None and not (previous.lower() <= current.lower()):
                    misplaced.append(current)
                else:
                    previous = current
            if len(misplaced) > 0:
                raise ValueError(
                    "individual-files-list must be sorted, the following files are misplaced: {}".format(
                        ", ".join(misplaced)
                    )
                )

    if "updatebot" in manifest:
        # If there are Updatebot tasks, then certain fields must be present and
        # defaults need to be set.
        if "tasks" in manifest["updatebot"]:
            if "vendoring" not in manifest or "url" not in manifest["vendoring"]:
                raise ValueError(
                    "If Updatebot tasks are specified, a vendoring url must be included."
                )

        if "try-preset" in manifest["updatebot"]:
            for f in ["fuzzy-query", "fuzzy-paths"]:
                if f in manifest["updatebot"]:
                    raise ValueError(
                        "If 'try-preset' is specified, then %s cannot be" % f
                    )

    # Check for a simple YAML file
    with open(filename) as f:
        has_schema = False
        for line in f.readlines():
            m = RE_SECTION(line)
            if m:
                if m.group(1) == "schema":
                    has_schema = True
                    break
        if not has_schema:
            raise ValueError("Not simple YAML")


class VendoringActions:
    """Voluptuous validator which verifies the vendoring actions(s) are valid."""

    def __call__(self, values):
        for v in values:
            if "action" not in v:
                raise Invalid("All file-update entries must specify a valid action")
            if v["action"] in ["copy-file", "move-file", "move-dir"]:
                if "from" not in v or "to" not in v or len(v.keys()) != 3:
                    raise Invalid(
                        "%s action must (only) specify 'from' and 'to' keys"
                        % v["action"]
                    )
            elif v["action"] in ["replace-in-file", "replace-in-file-regex"]:
                if (
                    "pattern" not in v
                    or "with" not in v
                    or "file" not in v
                    or len(v.keys()) != 4
                ):
                    raise Invalid(
                        "replace-in-file action must (only) specify "
                        + "'pattern', 'with', and 'file' keys"
                    )
            elif v["action"] == "delete-path":
                if "path" not in v or len(v.keys()) != 2:
                    raise Invalid(
                        "delete-path action must (only) specify the 'path' key"
                    )
            elif v["action"] == "vcs-add-remove-files":
                if "path" not in v or len(v.keys()) != 2:
                    raise Invalid(
                        "vcs-add-remove-files action must (only) specify the 'path' key"
                    )
            elif v["action"] == "run-script":
                if "script" not in v or "cwd" not in v:
                    raise Invalid(
                        "run-script action must specify 'script' and 'cwd' keys"
                    )
                if set(v.keys()) - set(["args", "cwd", "script", "action"]) != set():
                    raise Invalid(
                        "run-script action may only specify 'script', 'cwd', and 'args' keys"
                    )
            elif v["action"] == "run-command":
                if "command" not in v or "cwd" not in v:
                    raise Invalid(
                        "run-command action must specify 'command' and 'cwd' keys"
                    )
                if set(v.keys()) - set(["args", "cwd", "command", "action"]) != set():
                    raise Invalid(
                        "run-command action may only specify 'command', 'cwd', and 'args' keys"
                    )
            else:
                # This check occurs before the validator above, so the above is
                # redundant but we leave it to be verbose.
                raise Invalid("Supplied action " + v["action"] + " is invalid.")
        return values

    def __repr__(self):
        return "VendoringActions"


class UpdatebotTasks:
    """Voluptuous validator which verifies the updatebot task(s) are valid."""

    def __call__(self, values):
        seenTaskTypes = set()
        for v in values:
            if "type" not in v:
                raise Invalid("All updatebot tasks must specify a valid type")

            if v["type"] in seenTaskTypes:
                raise Invalid("Only one type of each task is currently supported")
            seenTaskTypes.add(v["type"])

            if v["type"] == "vendoring":
                for i in ["filter", "branch", "source-extensions"]:
                    if i in v:
                        raise Invalid(
                            "'%s' is only valid for commit-alert task types" % i
                        )
            elif v["type"] == "commit-alert":
                pass
            else:
                # This check occurs before the validator above, so the above is
                # redundant but we leave it to be verbose.
                raise Invalid("Supplied type " + v["type"] + " is invalid.")
        return values

    def __repr__(self):
        return "UpdatebotTasks"


class License:
    """Voluptuous validator which verifies the license(s) are valid as per our
    allow list."""

    def __call__(self, values):
        if isinstance(values, str):
            values = [values]
        elif not isinstance(values, list):
            raise Invalid("Must be string or list")
        for v in values:
            if v not in VALID_LICENSES:
                raise Invalid("Bad License")
        return values

    def __repr__(self):
        return "License"
