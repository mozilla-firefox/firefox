# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Check the LICENSES and LICENSED_UNDER declarations across the tree.

What is checked, and why it cannot be checked by the build backend, is
documented in docs/code-quality/lint/linters/license.md.
"""

import ast
import collections
import os
import re
from pathlib import Path

import mozpack.path as mozpath
from license_expression import ExpressionError, get_spdx_licensing
from mozbuild.licenses import covering_manifest
from mozlint import result
from mozlint.pathutils import expand_exclusions

DECLARES = "LICENSES"
REFERENCES = "LICENSED_UNDER"

# The preprocessor hook an application used to splice its own sections into the
# hand-maintained license.html with. The page is generated now, so setting
# these has no effect at all.
REMOVED_DEFINES = (
    "APP_LICENSE_BLOCK",
    "APP_LICENSE_LIST_BLOCK",
    "APP_LICENSE_BODY_BLOCK",
)

# `LicenseRef-<name>` and `DocumentRef-<doc>:LicenseRef-<name>` are valid SPDX
# expressions, but name a license the SPDX list does not carry, so
# license_expression rejects them in validating mode. Substituting a known id
# keeps the rest of the expression under validation.
LICENSE_REF = re.compile(r"(?:DocumentRef-[A-Za-z0-9.-]+:)?LicenseRef-[A-Za-z0-9.-]+")


class NonLiteral(Exception):
    """A statement whose license ids are not plain string literals."""


def _string_literal(node):
    """The string literal `node` has to be."""
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return node.value
    raise NonLiteral(f"{ast.unparse(node)} is not a string literal")


def _string_items(node):
    """The string literals the list or tuple `node` has to hold."""
    if not isinstance(node, (ast.List, ast.Tuple)):
        raise NonLiteral(f"{ast.unparse(node)} is not a list of string literals")
    return [_string_literal(element) for element in node.elts]


def _target(node):
    """Describe an assignment target as (variable, key node, flag).

    `LICENSES["mit"].title` is ("LICENSES", <"mit">, "title") and a bare
    `LICENSES` is ("LICENSES", None, None). The key is left unresolved so that
    only the variables this linter owns have to hold a literal.
    """
    flag = node.attr if isinstance(node, ast.Attribute) else None
    while isinstance(node, (ast.Attribute, ast.Subscript)):
        if isinstance(node, ast.Subscript) and isinstance(node.value, ast.Name):
            return node.value.id, node.slice, flag
        node = node.value
    if isinstance(node, ast.Name):
        return node.id, None, flag
    return None, None, None


def _assignment_targets(node):
    """The targets of `node`, if it assigns to anything at all."""
    if isinstance(node, ast.AugAssign):
        return [node.target]
    if isinstance(node, ast.Assign):
        return node.targets
    return []


class Declarations:
    """What one moz.build says about the license notices.

    Gathered in a single walk, because lint() needs all of it for every file.
    """

    def __init__(self, tree):
        # id -> the first line naming it under `LICENSES`, declaration or flag.
        self.named = {}
        self.referenced = {}
        # (id, flag) -> the assigned constants, one per assignment: a flag set
        # twice is two values to check, not one.
        self.flags = collections.defaultdict(list)
        self.removed_defines = {}
        # The lines whose ids this cannot read, reported rather than skipped.
        self.non_literals = {}

        for node in ast.walk(tree):
            for target in _assignment_targets(node):
                self._add(node, *_target(target))

    def _add(self, node, variable, key, flag):
        if variable == "DEFINES":
            try:
                define = _string_literal(key) if key is not None else None
            except NonLiteral:
                return
            if define in REMOVED_DEFINES:
                self.removed_defines.setdefault(define, node.lineno)
            return

        if variable not in (DECLARES, REFERENCES):
            return

        try:
            ids = (
                [_string_literal(key)] if key is not None else _string_items(node.value)
            )
        except NonLiteral as e:
            self.non_literals.setdefault(node.lineno, str(e))
            return

        for license_id in ids:
            if variable == REFERENCES:
                self.referenced.setdefault(license_id, node.lineno)
                continue
            self.named.setdefault(license_id, node.lineno)
            if key is None:
                continue
            if flag is not None and isinstance(node.value, ast.Constant):
                self.flags[(license_id, flag)].append((node.value.value, node.lineno))

    def flag(self, license_id, name, of_type=str):
        """The values of one flag, in source order, skipping the other types."""
        return [
            (value, lineno)
            for value, lineno in self.flags.get((license_id, name), ())
            if isinstance(value, of_type)
        ]


def _declarations(path):
    """The declarations of one moz.build, raising OSError or SyntaxError."""
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    return Declarations(tree)


def _on_disk_spelling(root, path):
    """`path` with each component in its on disk case, or as given if none matches."""
    current = root
    for part in mozpath.normsep(os.path.relpath(path, root)).split("/"):
        if part in ("", "."):
            continue
        try:
            names = os.listdir(current)
        except OSError:
            names = []
        if part in names:
            name = part
        else:
            name = next((n for n in names if n.lower() == part.lower()), part)
        current = mozpath.join(current, name)
    return mozpath.normpath(current)


def lint(paths, config, **lintargs):
    root = lintargs["root"]

    # A manifest is only read for the moz.build files at or below it, so a
    # change to one only needs those linted, not the rest of the tree.
    targets = {
        _on_disk_spelling(
            root, mozpath.dirname(p) if mozpath.basename(p) == "moz.yaml" else p
        )
        for p in paths
    }
    prefixes = tuple(f"{p.rstrip('/')}/" for p in targets if os.path.isdir(p))

    def linted(path):
        path = mozpath.normpath(path)
        return path in targets or path.startswith(prefixes)

    licensing = get_spdx_licensing()
    manifests = {}
    results = []

    def report(path, lineno, message, hint):
        results.append(
            result.from_config(
                config,
                path=path,
                lineno=lineno,
                level="error",
                message=message,
                hint=hint,
            )
        )

    parsed = {}
    for path in expand_exclusions([root], config, root):
        if path.endswith(".yaml"):
            continue
        try:
            parsed[path] = _declarations(Path(path))
        except SyntaxError as e:
            if linted(path):
                report(
                    path,
                    e.lineno or 1,
                    f"this file does not parse as Python: {e.msg}.",
                    "The license declarations are read from the syntax tree, "
                    "and the build system cannot execute this file either.",
                )
        except OSError as e:
            if linted(path):
                report(
                    path,
                    1,
                    f"this file cannot be read: {e.strerror}.",
                    "The license declarations are read from every moz.build "
                    "in the tree, so this one has to be readable.",
                )

    # Gathered from every moz.build rather than from the ones being linted,
    # because a notice is usually declared far from the code referencing it: the
    # shared texts in toolkit/content/licenses cover the whole tree. A file that
    # cannot be read leaves the set short, and is reported above when it is
    # linted.
    declared_in_tree = set().union(*(d.named for d in parsed.values()))

    for path in sorted(parsed):
        if not linted(path):
            continue
        declarations = parsed[path]
        problems = []

        for license_id, lineno in declarations.referenced.items():
            if license_id in declared_in_tree:
                continue
            problems.append((
                lineno,
                f'LICENSED_UNDER["{license_id}"] has no matching '
                f'LICENSES["{license_id}"] declaration anywhere in the tree.',
                "Declare the notice once, with a title and a text file, "
                "in the moz.build that owns the license text.",
            ))

        for define, lineno in declarations.removed_defines.items():
            problems.append((
                lineno,
                f'DEFINES["{define}"] no longer reaches about:license.',
                "The page is generated from the LICENSES declarations rather "
                "than preprocessed, so this define is dead and the section it "
                "names is not rendered. Generate the application's own "
                "license.html the way browser/base/moz.build does, passing "
                "the block as an extra input to gen_license_html.py.",
            ))

        for lineno, reason in declarations.non_literals.items():
            problems.append((
                lineno,
                f"the license id of this declaration is not a literal: {reason}.",
                f"{DECLARES} and {REFERENCES} are read without executing the "
                "moz.build, so a computed id cannot be checked against the "
                "rest of the tree. Spell the id out.",
            ))

        directory = mozpath.dirname(mozpath.normpath(path))
        covering = covering_manifest(directory, root, manifests)
        for license_id in declarations.named:
            spdx = declarations.flag(license_id, "spdx")
            for expression, lineno in spdx:
                try:
                    licensing.parse(
                        LICENSE_REF.sub("MIT", expression), validate=True, strict=True
                    )
                except (ExpressionError, ValueError) as e:
                    problems.append((
                        lineno,
                        f'"{expression}" is not a valid SPDX license expression: {e}',
                        "Use an id from https://spdx.org/licenses/, or "
                        "LicenseRef-<name> for a license that has none. "
                        "Combine ids with AND, OR and WITH.",
                    ))

            if covering is None:
                continue
            subcomponent = any(
                value
                for value, _ in declarations.flag(license_id, "subcomponent", bool)
            )
            if spdx and covering.license and not subcomponent:
                for _, lineno in spdx:
                    problems.append((
                        lineno,
                        f'LICENSES["{license_id}"].spdx repeats `origin.license` '
                        f"from {mozpath.relpath(covering.path, root)}, which "
                        f"declares {covering.license}.",
                        "moz.yaml owns the license of a vendored library, and "
                        "the SBOM reads it from there rather than from this "
                        "flag, so the two are free to drift apart. Drop the "
                        "flag; or, if this notice covers code whose license "
                        "differs from the library's own, set "
                        f'LICENSES["{license_id}"].subcomponent = True.',
                    ))

            for text, lineno in declarations.flag(license_id, "text"):
                declared_file = covering.license_file
                if not declared_file or mozpath.normpath(
                    mozpath.join(directory, text)
                ) != mozpath.normpath(declared_file):
                    continue
                problems.append((
                    lineno,
                    f'LICENSES["{license_id}"].text repeats `origin.license-file` '
                    f"from {mozpath.relpath(covering.path, root)}.",
                    "A notice with no `text` is read from the file that "
                    "manifest names, so the two cannot come to name different "
                    "files. Drop the flag.",
                ))

        for lineno, message, hint in sorted(problems):
            report(path, lineno, message, hint)

    return results
