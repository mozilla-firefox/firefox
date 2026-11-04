# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

import fnmatch
import functools
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import uuid
from functools import partial
from pprint import pprint

import attr
import mozpack.path as mozpath
import sentry_sdk
import yaml
from mach.decorators import Command, CommandArgument, SubCommand
from mach.registrar import Registrar
from mozbuild.util import cpu_count
from mozfile import load_source
from mozlint.result import Issue, IssueEncoder, ResultSummary

here = os.path.abspath(os.path.dirname(__file__))
topsrcdir = os.path.abspath(os.path.dirname(os.path.dirname(here)))
DOC_ROOT = os.path.join(topsrcdir, "docs")
BASE_LINK = "http://gecko-docs.mozilla.org-l1.s3-website.us-west-2.amazonaws.com/"
PUBLISHED_LINK = "https://firefox-source-docs.mozilla.org/"


# Helps manage in-tree documentation.


@Command(
    "doc",
    category="devenv",
    virtualenv_name="docs",
    description="Generate and serve documentation from the tree.",
)
@CommandArgument(
    "path",
    default=None,
    metavar="DIRECTORY",
    nargs="?",
    help="Path to documentation to build and display.",
)
@CommandArgument(
    "--format", default="html", dest="fmt", help="Documentation format to write."
)
@CommandArgument(
    "--outdir", default=None, metavar="DESTINATION", help="Where to write output."
)
@CommandArgument(
    "--archive",
    action="store_true",
    help="Write a gzipped tarball of generated docs.",
)
@CommandArgument(
    "--no-open",
    dest="auto_open",
    default=True,
    action="store_false",
    help="Don't automatically open HTML docs in a browser.",
)
@CommandArgument(
    "--no-serve",
    dest="serve",
    default=True,
    action="store_false",
    help="Don't serve the generated docs after building.",
)
@CommandArgument(
    "--http",
    default="localhost:5500",
    metavar="ADDRESS",
    help="Serve documentation on the specified host and port, "
    'default "localhost:5500".',
)
@CommandArgument("--upload", action="store_true", help="Upload generated files to S3.")
@CommandArgument(
    "-j",
    "--jobs",
    default=str(cpu_count()),
    dest="jobs",
    help="Distribute the build over N processes in parallel.",
)
@CommandArgument("--write-url", default=None, help="Write S3 Upload URL to text file")
@CommandArgument(
    "--linkcheck", action="store_true", help="Check if the links are still valid"
)
@CommandArgument(
    "--dump-trees", default=None, help="Dump the Sphinx trees to specified file."
)
@CommandArgument(
    "--disable-warnings-check",
    dest="disable_warnings_check",
    action="store_true",
    help="Disable checking for warnings",
)
@CommandArgument("--verbose", action="store_true", help="Run Sphinx in verbose mode")
@CommandArgument(
    "--no-autodoc",
    action="store_true",
    help="Disable generating Python/JS API documentation",
)
@CommandArgument(
    "--errors-file",
    dest="errors_file",
    default=None,
    help="File to store errors in, in JSON format. Typically used for code review bot.",
)
def build_docs(
    command_context,
    path=None,
    fmt="html",
    outdir=None,
    auto_open=True,
    serve=True,
    http=None,
    archive=False,
    upload=False,
    jobs=None,
    write_url=None,
    linkcheck=None,
    dump_trees=None,
    disable_warnings_check=False,
    verbose=None,
    no_autodoc=False,
    errors_file=None,
):
    # TODO: Bug 1704891 - move the ESLint setup tools to a shared place.
    import setup_helper
    from mozbuild.nodeutil import check_node_executables_valid

    setup_helper.set_project_root(command_context.topsrcdir)

    if not check_node_executables_valid():
        return 1

    setup_helper.eslint_maybe_setup()

    # Set the path so that Sphinx can find jsdoc, unfortunately there isn't
    # a way to pass this to Sphinx itself at the moment.
    os.environ["PATH"] = (
        mozpath.join(command_context.topsrcdir, "node_modules", ".bin")
        + os.pathsep
        + _node_path()
        + os.pathsep
        + os.environ["PATH"]
    )

    import webbrowser

    from livereload import Server

    from moztreedocs.package import create_tarball

    unique_id = f"{project()}/{str(uuid.uuid1())}"

    outdir = outdir or os.path.join(command_context.topobjdir, "docs")
    savedir = os.path.join(outdir, fmt)

    if path is None:
        path = command_context.topsrcdir
        if os.environ.get("MOZ_AUTOMATION") != "1":
            print(
                "\nBuilding the full documentation tree.\n"
                "Did you mean to only build part of the documentation?\n"
                "For a faster command, consider running:\n"
                " ./mach doc path/to/docs\n"
            )
    path = os.path.normpath(os.path.abspath(path))

    docdir = _find_doc_dir(path)
    if not docdir:
        print(_dump_sphinx_backtrace())
        return die(
            "failed to generate documentation:\n"
            f"{path}: could not find docs at this location"
        )

    if linkcheck:
        # We want to verify if the links are valid or not
        fmt = "linkcheck"
    if no_autodoc:
        if disable_warnings_check:
            return die(
                "'--no-autodoc' flag may not be used with '--disable-warnings-check'"
            )
        toggle_no_autodoc()

    status, warnings = _run_sphinx(docdir, savedir, fmt=fmt, jobs=jobs, verbose=verbose)
    if status != 0:
        print(_dump_sphinx_backtrace())
        return die(
            f"failed to generate documentation:\n{path}: sphinx return code {status}"
        )
    else:
        print(f"\nGenerated documentation:\n{savedir}")

    if not disable_warnings_check:
        with open(os.path.join(DOC_ROOT, "config.yml")) as fh:
            docs_config = yaml.safe_load(fh)

        [fatal_errors, known_errors] = _check_sphinx_warnings(warnings, docs_config)

        log_results(fatal_errors, known_errors, command_context.topsrcdir, errors_file)
        if len(fatal_errors):
            return 1

    # Upload the artifact containing the link to S3
    # This would be used by code-review to post the link to Phabricator
    if write_url is not None:
        if _publishes(project()):
            unique_link = PUBLISHED_LINK + "index.html"
        else:
            unique_link = BASE_LINK + unique_id + "/index.html"
        with open(write_url, "w") as fp:
            fp.write(unique_link)
            fp.flush()
        print("Generated " + write_url)

    if dump_trees is not None:
        parent = os.path.dirname(dump_trees)
        if parent and not os.path.isdir(parent):
            os.makedirs(parent)
        with open(dump_trees, "w") as fh:
            json.dump(manager().trees, fh)

    if archive:
        archive_path = os.path.join(outdir, f"{project()}.tar.gz")
        create_tarball(archive_path, savedir)
        print(f"Archived to {archive_path}")

    if upload:
        _s3_upload(savedir, project(), unique_id, version())

    if not serve:
        index_path = os.path.join(savedir, "index.html")
        if auto_open and os.path.isfile(index_path):
            webbrowser.open(index_path)
        return

    # Create livereload server. Any files modified in the specified docdir
    # will cause a re-build and refresh of the browser (if open).
    try:
        host, port = http.split(":", 1)
        port = int(port)
    except ValueError:
        return die(f"invalid address: {http}")

    server = Server()

    sphinx_trees = manager().trees or {savedir: docdir}
    for _, src in sphinx_trees.items():
        run_sphinx = partial(
            _run_sphinx, src, savedir, fmt=fmt, jobs=jobs, verbose=verbose
        )
        server.watch(src, run_sphinx)
    server.serve(
        host=host,
        port=port,
        root=savedir,
        open_url_delay=0.1 if auto_open else None,
    )


def _dump_sphinx_backtrace():
    """
    If there is a sphinx dump file, read and return
    its content.
    By default, it isn't displayed.
    """
    pattern = "sphinx-err-*"
    output = ""
    tmpdir = "/tmp"

    if not os.path.isdir(tmpdir):
        # Only run it on Linux
        return
    files = os.listdir(tmpdir)
    for name in files:
        if fnmatch.fnmatch(name, pattern):
            pathFile = os.path.join(tmpdir, name)
            stat = os.stat(pathFile)
            output += f"Name: {pathFile} / Creation date: {time.ctime(stat.st_mtime)}\n"
            with open(pathFile) as f:
                output += f.read()
    return output


def _run_sphinx(docdir, savedir, config=None, fmt="html", jobs=None, verbose=None):
    import sphinx.cmd.build

    config = config or manager().conf_py_path
    # When running sphinx with sentry, it adds significant overhead
    # and makes the build generation very very very slow
    # So, disable it to generate the doc faster
    sentry_sdk.init(None)
    warn_fd, warn_path = tempfile.mkstemp()
    os.close(warn_fd)
    try:
        args = [
            "-T",
            "-b",
            fmt,
            "-c",
            os.path.dirname(config),
            "-w",
            warn_path,
            docdir,
            savedir,
        ]
        if jobs:
            args.extend(["-j", jobs])
        if verbose:
            args.extend(["-v", "-v"])
        print("Run sphinx with:")
        print(args)
        status = sphinx.cmd.build.build_main(args)
        with open(warn_path, encoding="utf-8") as warn_file:
            warnings = warn_file.readlines()
        return status, warnings
    finally:
        try:
            os.unlink(warn_path)
        except Exception as ex:
            print(ex)


def _check_sphinx_warnings(warnings, docs_config):
    allowed_warnings_regex = [
        re.compile(item) for item in docs_config["allowed_warnings"]
    ]
    # warnings file contains other strings as well
    errors = []
    known_errors = []
    for warning in warnings:
        stripped = warning.strip()
        # Replace slashes so that we can do matching against the Windows paths
        # whilst not having to change the regexps.
        stripped_and_slashes_changed = stripped.replace("\\", "/")

        if len(stripped) and any(
            x in stripped for x in ["ERROR", "CRITICAL", "WARNING"]
        ):
            if not (
                any(
                    item.search(stripped_and_slashes_changed)
                    for item in allowed_warnings_regex
                )
            ):
                errors.append(stripped)
            else:
                known_errors.append(stripped)

    return [errors, known_errors]


def manager():
    from moztreedocs import manager

    return manager


def toggle_no_autodoc():
    import moztreedocs

    moztreedocs._SphinxManager.NO_AUTODOC = True


@functools.cache
def _read_project_properties():
    path = os.path.normpath(manager().conf_py_path)
    conf = load_source("doc_conf", path)

    # Prefer the Mozilla project name, falling back to Sphinx's
    # default variable if it isn't defined.
    project = getattr(conf, "moz_project_name", None)
    if not project:
        project = conf.project.replace(" ", "_")

    return {"project": project, "version": getattr(conf, "version", None)}


def project():
    return _read_project_properties()["project"]


def version():
    return _read_project_properties()["version"]


def _node_path():
    from mozbuild.nodeutil import find_node_executable

    node, _ = find_node_executable()

    return os.path.dirname(node)


def _find_doc_dir(path):
    if os.path.isfile(path):
        return

    valid_doc_dirs = ("doc", "docs")
    for d in valid_doc_dirs:
        p = os.path.join(path, d)
        if os.path.isdir(p):
            path = p

    if os.path.exists(os.path.join(path, "index.md")):
        return path


def _publishes(project):
    return project == "main" and os.environ.get("MOZ_SCM_LEVEL") == "3"


def _s3_upload(root, project, unique_id, version=None):
    # Workaround the issue
    # BlockingIOError: [Errno 11] write could not complete without blocking
    # https://github.com/travis-ci/travis-ci/issues/8920
    import fcntl

    from moztreedocs.package import distribution_files
    from moztreedocs.upload import s3_set_redirects, s3_upload

    fcntl.fcntl(1, fcntl.F_SETFL, 0)

    # The main docs go to the bucket root on level 3, which is the published
    # site, and to a per run prefix otherwise, which the review bot links to.
    # A <project>/<version> copy is added when the docs set a version.

    files = list(distribution_files(root))

    # Until we redirect / to main/latest, upload the main docs
    # to the root of the production bucket.
    publish = _publishes(project)
    dest = "" if publish else unique_id
    s3_upload(files, dest)
    if version:
        s3_upload(files, f"{project}/{version}")

    if publish:
        # Only the root gets redirects. The "version" or "uuid" prefixes would
        # exceed a 50 redirect limit and external things are unlikely to link
        # there anyway (see bug 1614908).
        with open(os.path.join(DOC_ROOT, "config.yml")) as fh:
            redirects = yaml.safe_load(fh)["redirects"]

        redirects = {k.strip("/"): v.strip("/") for k, v in redirects.items()}

        print("Redirects currently staged")
        pprint(redirects, indent=1)

        s3_set_redirects(redirects)
    else:
        unique_link = BASE_LINK + dest + "/index.html"
        print("Uploaded documentation can be accessed here " + unique_link)


@SubCommand(
    "doc",
    "mach-telemetry",
    description="Generate documentation from Glean metrics.yaml files",
)
def generate_telemetry_docs(command_context):
    args = [
        sys.executable,
        "-mglean_parser",
        "translate",
        "-f",
        "markdown",
        "-o",
        os.path.join(topsrcdir, "python/mach/docs/"),
        os.path.join(topsrcdir, "python/mach/pings.yaml"),
        os.path.join(topsrcdir, "python/mach/metrics.yaml"),
        os.path.join(topsrcdir, "python/mozbuild/metrics.yaml"),
    ]
    metrics_paths = [
        handler.metrics_path
        for handler in Registrar.command_handlers.values()
        if handler.metrics_path is not None
    ]
    args.extend([
        os.path.join(command_context.topsrcdir, path) for path in set(metrics_paths)
    ])
    subprocess.check_call(args)


@SubCommand(
    "doc",
    "show-targets",
    description="List all reference targets. Requires the docs to have been built.",
)
@CommandArgument(
    "--format", default="html", dest="fmt", help="Documentation format used."
)
@CommandArgument(
    "--outdir", default=None, metavar="DESTINATION", help="Where output was written."
)
def show_reference_targets(command_context, fmt="html", outdir=None):
    command_context.activate_virtualenv()
    command_context.virtualenv_manager.install_pip_requirements(
        os.path.join(here, "requirements.txt")
    )

    import sphinx.ext.intersphinx

    outdir = outdir or os.path.join(command_context.topobjdir, "docs")
    inv_path = os.path.join(outdir, fmt, "objects.inv")

    if not os.path.exists(inv_path):
        return die(
            "object inventory not found: {inv_path}.\n"
            "Rebuild the docs and rerun this command"
        )
    sphinx.ext.intersphinx.inspect_main([inv_path])


def die(msg, exit_code=1):
    msg = f"{sys.argv[0]} {sys.argv[1]}: {msg}"
    print(msg, file=sys.stderr)
    return exit_code


@functools.cache
def transform_error_regexp():
    # Lazily created, because we need to wait to get the staging_dir.

    # This regexp matches a couple of styles of message:
    #
    # path/to/simpletest.md: WARNING: document isn't included in any toctree
    # path/to/index.md:2: WARNING: Unknown directive type.
    #
    # The distinction is that some of them give a line number and some of them don't.
    # We need to replace the path, and split the text of the message so
    # that we can insert a pipe for automation to detect.
    return re.compile(
        re.escape(os.path.join(manager().staging_dir, "")) + r"(.*?)(:[0-9]*)?:\s*(.*)"
    )


def transform_error(msg, level):
    match = transform_error_regexp().match(msg)
    if match:
        filePath = match.group(1)
        for staging_path, original_path in manager().trees.items():
            if staging_path != os.path.dirname(filePath):
                continue

            return {
                "linter": "source-test-doc-upload",
                "path": os.path.join(
                    str(manager().topsrcdir),
                    filePath.replace(staging_path, original_path),
                ),
                "relpath": filePath.replace(staging_path, original_path),
                # Remove the first character, as it'll be the :
                "lineno": (
                    int(match.group(2)[1:])
                    if match.group(2) is not None and match.group(2)[1:].isdigit()
                    else None
                ),
                "level": level,
                "message": match.group(3),
            }

    return {
        "linter": "source-test-doc-upload",
        "level": level,
        "message": msg,
    }


def print_result_to_stderr(known_or_unexpected, result_details):
    lineno = (
        (f":{str(result_details['lineno'])}")
        if "lineno" in result_details and result_details["lineno"] is not None
        else ""
    )
    path = (
        f"{result_details['relpath']}{lineno} |" if "relpath" in result_details else ""
    )

    print(
        f"TEST-{known_or_unexpected}-FAIL | {sys.argv[0]} {sys.argv[1]} | {path} {result_details['message']}",
        file=sys.stderr,
    )


@attr.s(slots=True, kw_only=True)
class AnalysisFormatIssue(Issue):
    """
    Adapter for the Issue class to produce results in a code review bot
    compatible manner. Namely, use of line rather than lineno, and `path` as
    the relative path rather than absolute.
    """

    line = attr.ib(default=None)

    def __attrs_post_init__(self):
        root = ResultSummary.root
        assert root is not None, "Missing ResultSummary.root"
        if os.path.isabs(self.path):
            self.path = mozpath.relpath(self.path, root)
        else:
            self.path = mozpath.normpath(self.path)
        self.line = self.lineno


def create_issue(result):
    """Creates an AnalysisFormatIssue from the given result"""
    args = {}
    for arg in attr.fields(AnalysisFormatIssue):
        if arg.init:
            args[arg.name] = result.get(arg.name)
    return AnalysisFormatIssue(**args)


def log_results(fatal_errors, known_errors, root, error_file=None):
    """
    This will always output to stdout, but optionally also dump messages
    to error_file in the JSON format needed for the review bot.

    Ideally we should reuse mozlint's logger here.
    """

    result = ResultSummary(root)

    for m in known_errors:
        # We log known errors as warnings for mozlint, so that they'll still
        # be raised in the Code Review UI, and developers can see if they can
        # fix them.
        result_details = transform_error(m, "warning")
        print_result_to_stderr("KNOWN", result_details)
        if "relpath" in result_details:
            result.issues[result_details["relpath"]].append(
                create_issue(result_details)
            )

    print(f"Known Failures: {len(known_errors)}")

    for m in fatal_errors:
        result_details = transform_error(m, "error")
        print_result_to_stderr("UNEXPECTED", result_details)
        if "relpath" in result_details:
            result.issues[result_details["relpath"]].append(
                create_issue(result_details)
            )

    print(f"Failures: {len(fatal_errors)}")

    if error_file:
        with open(error_file, "w") as fh:
            json.dump(result.issues, fh, cls=IssueEncoder)
