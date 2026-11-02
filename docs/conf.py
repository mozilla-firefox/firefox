# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import gc
import os
import sys
from pathlib import Path

import yaml

# Set up Python environment to load build system packages.
OUR_DIR = Path(__file__).parent
topsrcdir = OUR_DIR.parent

# Escapes $, [, ] and 3 dots in copy button
copybutton_prompt_text = r">>> |\.\.\. |\$ |In \[\d*\]: | {2,5}\.\.\.: | {5,8}: "
copybutton_prompt_is_regexp = True

EXTRA_PATHS = (
    "layout/tools/reftest",
    "python/mach",
    "python/mozbuild",
    "python/mozversioncontrol",
    "testing/mozbase/manifestparser",
    "testing/mozbase/mozfile",
    "testing/mozbase/mozprocess",
    "testing/mozbase/moznetwork",
    "third_party/python/jsmin",
    "third_party/python/which",
    "docs/_addons",
    "taskcluster/gecko_taskgraph/test",
)

sys.path[:0] = [str(topsrcdir / p) for p in EXTRA_PATHS]
sys.path.insert(0, str(OUR_DIR))

extensions = [
    "myst_parser",
    "sphinx.ext.autodoc",
    "sphinx.ext.doctest",
    "sphinx.ext.graphviz",
    "sphinx.ext.napoleon",
    "sphinx.ext.todo",
    "mozbuild.sphinx",
    "sphinx_js",
    "js_typedef",
    "sphinxcontrib.jquery",
    "sphinxcontrib.mermaid",
    "sphinx_copybutton",
    "sphinx_design",
    "bzlink",
    "etp_matrix",
    "staging_paths",
    "dark_mode",
    "mermaid_wrapped_option",
]

myst_enable_extensions = [
    "colon_fence",
    "smartquotes",
    "deflist",
    "html_admonition",
    "fieldlist",
]

# A diagram renders at the size mermaid laid it out at, which needs both
# halves: useMaxWidth below gives the SVG an intrinsic size, without which a
# content-sized box collapses to the CSS default object size of 300px, and
# custom_theme.css overrides the extension's own stylesheet, which stretches
# the SVG to the width of its container. A diagram wider than the column
# scrolls there.
mermaid_width = "fit-content"
mermaid_height = "auto"

mermaid_version = "12.2.0"

# sphinxcontrib-mermaid always hands Mermaid a theme for the page's color
# scheme, "default" or "dark" unless set here, which overrides Mermaid's own
# default of "redux-color".
mermaid_light_theme = "redux-color"
mermaid_dark_theme = "redux-dark-color"

# startOnLoad must stay off: the extension renders via mermaid.run() itself.
# Only theme-neutral values belong here, as this config is shared by the light
# and dark themes.
mermaid_init_config = {
    "startOnLoad": False,
    # Mermaid gives flowchart and state nodes a 120px minimum width and
    # wrapping width. The docs size nodes to their labels.
    "themeVariables": {
        "fontSize": "18px",
    },
    # useMaxWidth is configured per diagram type; these are the types the tree
    # uses.
    "flowchart": {"useMaxWidth": False, "wrappingWidth": 200, "minNodeWidth": 0},
    "sequence": {"useMaxWidth": False},
    "class": {"useMaxWidth": False},
    "state": {"useMaxWidth": False, "wrappingWidth": 200, "minNodeWidth": 0},
    "gantt": {"useMaxWidth": False},
    "er": {"useMaxWidth": False},
}

# ZenUML sequence diagrams. The plugin is fetched only on a page that has one.
mermaid_include_zenuml = True
mermaid_zenuml_version = "1.0.1"

# The paths are loaded from config.yml so they can be shared with a CI
# optimization strategy that ensures the doc task runs when these files change.
with open(OUR_DIR / "config.yml") as fh:
    config = yaml.safe_load(fh)
    js_source_path = [f"../{path}" for path in config["js_source_paths"]]

root_for_relative_js_paths = ".."
jsdoc_config_path = "jsdoc.json"

templates_path = ["_templates"]
source_suffix = [".md"]
autodoc_preserve_defaults = True
master_doc = "index"
project = "Firefox Source Docs"

# Override the search box to use Google instead of
# sphinx search on firefox-source-docs.mozilla.org
if (
    os.environ.get("MOZ_SOURCE_DOCS_USE_GOOGLE") == "1"
    and os.environ.get("MOZ_SCM_LEVEL") == "3"
):
    templates_path.append("_search_template")

html_sidebars = {
    "**": [
        "searchbox.html",
    ]
}
html_logo = str(topsrcdir / "browser/branding/nightly/content/firefox-wordmark.svg")
html_favicon = str(topsrcdir / "browser/branding/nightly/firefox.ico")

exclude_patterns = ["_build", "_staging", "_venv", "**security/nss/legacy/**"]
pygments_style = "sphinx"
# generate label “slugs” for header anchors so that
# we can reference them from markdown links.
myst_heading_anchors = 5

# We need to perform some adjustment of the settings and environment
# when running on Read The Docs.
on_rtd = os.environ.get("READTHEDOCS", None) == "True"

if on_rtd:
    # SHELL isn't set on RTD and mach.mixin.process's import raises if a
    # shell-related environment variable can't be found. Set the variable here
    # to hack us into working on RTD.
    assert "SHELL" not in os.environ
    os.environ["SHELL"] = "/bin/bash"
else:
    # We only need to set the RTD theme when not on RTD because the RTD
    # environment handles this otherwise.

    html_theme = "sphinx_rtd_theme"

# As we parse the error messages and they can be translated, force
# the english locale
os.environ["LANG"] = "C"

html_static_path = ["_static"]
htmlhelp_basename = "FirefoxTreeDocs"

moz_project_name = "main"

html_show_copyright = False

# GitHub integration for "View page source" links
html_context = {
    "display_github": True,
    "github_user": "mozilla-firefox",
    "github_repo": "firefox",
    "github_version": "main",
}


def install_sphinx_design(app, pagename, templatename, context, doctree):
    if "perfdocs" in pagename:
        app.add_js_file("sphinx_design.js")
        app.add_css_file("sphinx_design.css")


def add_github_source_link(app, pagename, templatename, context, doctree):
    """Add the original source file path to the context for GitHub links.

    Docs are staged from various source locations (e.g. gfx/docs/, js/src/doc/)
    into a flat structure (e.g. gfx/, js/) for building. We need to reverse this
    mapping so GitHub links point to the actual source files in the repo.
    """
    from moztreedocs import manager

    if not manager.trees:
        return

    source_suffix = context.get("page_source_suffix", "")
    staging_relpath = pagename + source_suffix

    # manager.trees maps staging prefixes to source prefixes,
    # e.g. {"gfx": "gfx/docs", "js": "js/src/doc"}.
    # Replace the staging prefix with the original source prefix to recover
    # the real repo path, e.g. "gfx/Silk.md" -> "gfx/docs/Silk.md".
    for staging_prefix, original_prefix in manager.trees.items():
        if staging_relpath.startswith(staging_prefix + "/"):
            # Strip the staging prefix and re-attach the original source prefix.
            # e.g. "gfx/Silk.md" -> strip "gfx" -> "Silk.md" -> "gfx/docs/Silk.md"
            rel = staging_relpath[len(staging_prefix) + 1 :]
            context["github_source_path"] = original_prefix + "/" + rel
            return

    # Files directly in docs/ don't go through SPHINX_TREES staging
    context["github_source_path"] = "docs/" + staging_relpath


def make_sphinx_js_skip_missing_objects():
    """Skip sphinx-js directives whose object jsdoc didn't produce.

    The ``js:autoclass``/``js:autofunction`` directives abort the whole build
    (``SphinxError``, sphinx return code 2) when the jsdoc analysis didn't yield
    a doclet for the referenced object. That analysis runs over the entire JS
    source tree and is intermittently incomplete in CI, which makes these
    directives fail non-deterministically -- most often on the first one read,
    ``browser/urlbar/UrlbarController``. Skip the offending directive (and log
    it) instead of failing the whole documentation build; the page still renders
    in full whenever the doclet is available.
    """
    try:
        from sphinx.errors import SphinxError
        from sphinx.util import logging as sphinx_logging
        from sphinx_js.renderers import JsRenderer
    except ImportError:
        return

    logger = sphinx_logging.getLogger("sphinx_js.renderers")
    original_rst_nodes = JsRenderer.rst_nodes

    def rst_nodes(self):
        try:
            return original_rst_nodes(self)
        except SphinxError as exc:
            if "No documentation was found for object" not in str(exc):
                raise
            logger.info(f"{exc} Skipping directive.")
            return []

    JsRenderer.rst_nodes = rst_nodes


make_sphinx_js_skip_missing_objects()


def freeze_gc(app, *args):
    # Sphinx forks a worker per chunk of documents. Collect before that and
    # freeze the survivors so that later collections don't touch them, which
    # keeps their pages shared between the main process and the workers.
    gc.collect()
    gc.freeze()


def reattach_nested_rst_nodes(app, doctree):
    """Point nodes parsed from eval-rst blocks back at their own doctree.

    MyST parses each eval-rst block into a temporary document sharing the
    original settings object, and the moved nodes keep a reference to it. Sphinx
    clears settings.env only on a copy, so without this every such doctree
    pickle would carry the whole build environment.
    """
    for node in doctree.findall():
        if node.document is not doctree:
            node.document = doctree


def disable_gc(app, *args):
    """Turn off the cyclic garbage collector for the rest of the build.

    Automatic full collections over the growing environment and doctrees took
    a large share of the build, and the worker processes forked afterwards
    inherit the setting. collect_garbage() reclaims the cycles left by merging
    the environments of the parallel readers.
    """
    gc.disable()


def collect_garbage(app, env):
    gc.collect()


def enable_gc(app, exception):
    gc.enable()


def setup(app):
    app.connect("env-before-read-docs", freeze_gc)
    app.connect("write-started", freeze_gc)
    # sphinx-js does not declare it, but it only reads the jsdoc analysis that
    # runs once before the sources are read.
    app.extensions["sphinx_js"].parallel_read_safe = True
    app.extensions["sphinx_js"].parallel_write_safe = True
    app.add_css_file("custom_theme.css")
    app.add_js_file("scrollable_regions.js")
    app.connect("html-page-context", install_sphinx_design)
    app.connect("html-page-context", add_github_source_link)
    app.connect("doctree-read", reattach_nested_rst_nodes)
    app.connect("env-before-read-docs", disable_gc, priority=900)
    app.connect("env-updated", collect_garbage)
    app.connect("build-finished", enable_gc)
