# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import importlib
import os
from pathlib import Path

from docutils import nodes
from mots.config import FileConfig
from mots.directory import Directory
from mots.export import export_to_format
from sphinx.util.docstrings import prepare_docstring
from sphinx.util.docutils import ReferenceRole


def as_rst(lines):
    """Embed rst in the generated Markdown.

    Docstrings that ``sphinx.ext.autodoc`` also renders have to stay rst, so
    hand them to the rst parser rather than converting them.
    """
    return ["```{eval-rst}", *lines, "```", ""]


def function_reference(f, attr, args, doc):
    lines = [
        f"### {f}",
        "",
    ]

    docstring = prepare_docstring(doc)

    lines.extend([
        docstring[0],
        "",
    ])

    arg_types = []

    for t in args:
        if isinstance(t, list):
            inner_types = [t2.__name__ for t2 in t]
            arg_types.append(" | ".join(inner_types))
            continue

        arg_types.append(t.__name__)

    arg_s = f"({', '.join(arg_types)})"

    lines.extend([
        f":Arguments: {arg_s}",
        "",
    ])

    lines.extend(docstring[1:])
    lines.append("")

    return lines


def variable_reference(v, st_type, in_type, doc):
    lines = [
        f"### {v}",
        "",
    ]

    docstring = prepare_docstring(doc)

    lines.extend([
        docstring[0],
        "",
    ])

    lines.extend([
        f":Storage Type: `{st_type.__name__}`",
        f":Input Type: `{in_type.__name__}`",
        "",
    ])

    lines.extend(docstring[1:])
    lines.append("")

    return lines


def special_reference(v, func, typ, doc):
    lines = [
        f"### {v}",
        "",
    ]

    docstring = prepare_docstring(doc)

    lines.extend([
        docstring[0],
        "",
        f":Type: `{typ.__name__}`",
        "",
    ])

    lines.extend(docstring[1:])
    lines.append("")

    return lines


def format_module(m):
    lines = [
        "(mozbuild_symbols)=",
        "",
        "# mozbuild Sandbox Symbols",
        "",
        ":::{note}",
        "moz.build files' implementation includes a `Path` class.",
        "",
    ]
    path_docstring_minus_summary = prepare_docstring(m.Path.__doc__)[2:]
    lines.extend(as_rst(path_docstring_minus_summary))
    lines.extend([
        ":::",
        "",
    ])

    for subcontext, cls in sorted(m.SUBCONTEXTS.items()):
        lines.extend([
            f"(mozbuild_subcontext_{subcontext})=",
            "",
            f"## Sub-Context: {subcontext}",
            "",
        ])
        lines.extend(as_rst(prepare_docstring(cls.__doc__)))

        for k, v in sorted(cls.VARIABLES.items()):
            lines.extend(variable_reference(k, *v))

    lines.extend([
        "## Variables",
        "",
    ])

    for v in sorted(m.VARIABLES):
        lines.extend(variable_reference(v, *m.VARIABLES[v]))

    lines.extend([
        "## Functions",
        "",
    ])

    for func in sorted(m.FUNCTIONS):
        lines.extend(function_reference(func, *m.FUNCTIONS[func]))

    lines.extend([
        "## Special Variables",
        "",
    ])

    for v in sorted(m.SPECIAL_VARIABLES):
        lines.extend(special_reference(v, *m.SPECIAL_VARIABLES[v]))

    return lines


def find_mots_config_path(app):
    """Find and return mots config path if it exists."""
    base_path = Path(app.srcdir).parent
    config_path = base_path / "mots.yaml"
    if config_path.exists():
        return config_path


def export_mots(config_path):
    """Load mots configuration and export it to file."""
    # Load from disk and initialize configuration and directory. The export
    # doesn't need the path index built by Directory.load(), which globs the
    # whole tree and takes several seconds.
    config = FileConfig(config_path)
    config.load()
    directory = Directory(config)

    # Fetch file format (i.e., "rst") and export path.
    frmt = config.config["export"]["format"]
    path = config_path.parent / config.config["export"]["path"]

    # Generate output.
    output = export_to_format(directory, frmt)

    # Create export directory if it does not exist.
    path.parent.mkdir(parents=True, exist_ok=True)

    # Remove stale exports left over from a previous format (e.g. an
    # index.rst from before the switch to Markdown). Sphinx would otherwise
    # treat both as the same document and fail with a "multiple files found"
    # warning.
    for stale in path.parent.glob(f"{path.stem}.*"):
        if stale != path:
            stale.unlink()

    # Write changes to disk.
    with path.open("w", encoding="utf-8") as f:
        f.write(output)


# Source directory of the documentation tree the reference belongs to, as
# registered in ``SPHINX_TREES``.
MOZBUILD_SYMBOLS_TREE = os.path.join("build", "docs")


def export_mozbuild_symbols(manager):
    """Write the mozbuild sandbox symbol reference to the staging directory.

    The page is generated rather than checked in, so it is written where the
    documentation is staged instead of in the source tree.
    """
    dest = next(
        (d for d, source in manager.trees.items() if source == MOZBUILD_SYMBOLS_TREE),
        None,
    )
    if dest is None:
        # The tree the reference belongs to isn't part of this build.
        return

    module = importlib.import_module("mozbuild.frontend.context")
    path = Path(manager.staging_dir) / dest / "mozbuild-symbols.md"
    output = "\n".join(format_module(module)) + "\n"

    # The staged documentation is a tree of symlinks into the source tree, so
    # a leftover link from an earlier build would be written through.
    if path.is_symlink():
        path.unlink()

    # Leave the file alone when nothing changed, so that Sphinx, which looks at
    # the modification time, doesn't rebuild the page on every run.
    if path.exists() and path.read_text(encoding="utf-8") == output:
        return

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(output, encoding="utf-8")


class Searchfox(ReferenceRole):
    """Role which links a relative path from the source to it's searchfox URL.

    Can be used like:

        See :searchfox:`browser/base/content/browser-places.js` for more details.

    Will generate a link to
    ``https://searchfox.org/firefox-main/source/browser/base/content/browser-places.js``

    The example above will use the path as the text, to use custom text:

        See :searchfox:`this file <browser/base/content/browser-places.js>` for
        more details.

    To specify a different source tree:

        See :searchfox:`firefox-beta:browser/base/content/browser-places.js`
        for more details.

    To pin to a specific revision, include ``/rev/<sha>`` in the source:

        See :searchfox:`firefox-main/rev/<sha>:browser/components/BrowserGlue.sys.mjs#42`
        for more details.
    """

    def run(self):
        if ":" in self.target:
            source, path = self.target.split(":", 1)
        else:
            source = "firefox-main"
            path = self.target

        if "/rev/" not in source:
            source = f"{source}/source"

        url = f"https://searchfox.org/{source}/{path}"

        if self.has_explicit_title:
            title = self.title
        else:
            title = path

        node = nodes.reference(self.rawtext, title, refuri=url, **self.options)
        return [node], []


def setup(app):
    from moztreedocs import manager

    app.add_role("searchfox", Searchfox())

    # Unlike typical Sphinx installs, our documentation is assembled from
    # many sources and staged in a common location. This arguably isn't a best
    # practice, but it was the easiest to implement at the time.
    #
    # Here, we invoke our custom code for staging/generating all our
    # documentation.

    # Export and write "governance" documentation to disk.
    config_path = find_mots_config_path(app)
    if config_path:
        export_mots(config_path)

    manager.generate_docs(app)

    # Write the mozbuild sandbox symbol reference to the staging directory,
    # after the static documentation has been staged there.
    export_mozbuild_symbols(manager)

    app.srcdir = Path(manager.staging_dir)

    return {"parallel_read_safe": True, "parallel_write_safe": True}
