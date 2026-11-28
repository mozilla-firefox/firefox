# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Generate a CycloneDX software bill of materials for the configured tree.

``generate_file`` and ``generate_build_tooling_file`` are the GENERATED_FILES
entry points the build uses, so the documents are produced from the objdir that
built the product, by the same build graph that produces everything else. `mach sbom` calls ``generate()``, which is
also how an unconfigured tree gets the moz.yaml-only subset.
"""

import os
import sys


class SbomError(Exception):
    """A condition that must not silently shrink the document."""


def _crate_records(topsrcdir, topobjdir, substs, log):
    """The vendored crates, split by whether they ship in the product.

    Cargo.lock describes third_party/rust exactly: versions, checksums and the
    crate-to-crate graph, none of which moz.yaml has. `cargo metadata` adds
    what Cargo.lock cannot express: whether a crate is reached as a normal, a
    build or a dev dependency, and so whether it ships at all.

    Returns (shipped, tooling, edges, kinds). Without `cargo metadata` nothing
    can be told apart, so every crate counts as shipped and ``kinds`` is empty.
    """
    from mozbuild.vendor.sbom_cargo import (
        collect_dependency_kinds,
        crate_records,
        is_tooling,
    )

    kinds = collect_dependency_kinds(
        topsrcdir,
        topobjdir,
        substs.get("CARGO"),
        log=log,
        rustc=substs.get("RUSTC"),
    )
    crates, edges = crate_records(topsrcdir, kinds=kinds)
    shipped, tooling = [], []
    for crate in crates:
        (tooling if is_tooling(crate) else shipped).append(crate)
    return shipped, tooling, edges, kinds


def _manifest_records(repo, topsrcdir, log):
    """The moz.yaml records, split by whether they ship in the product.

    Returns (shipped, tooling, errors). The vendored Python packages and the
    test suites, benchmarks and fuzzers build or test the product rather than
    ship in it.
    """
    from mozbuild.vendor.sbom import collect_records, is_tooling_path
    from mozbuild.vendor.sbom_python import VENDOR_DIR as PYTHON_VENDOR_DIR

    records, errors = collect_records(repo, topsrcdir, log=log)

    shipped, tooling = [], []
    for record in records:
        directory = record["bom_ref"]
        if directory.startswith(PYTHON_VENDOR_DIR + "/"):
            # sbom_python describes these, vsdownload's moz.yaml included.
            continue
        (tooling if is_tooling_path(directory) else shipped).append(record)
    return shipped, tooling, errors


def _source_revision_and_time(repo):
    from mozbuild.vendor.sbom_cyclonedx import utc_timestamp

    # head_rev, not head_ref: the latter is a branch name under git, which
    # would make the BOM serial number move with the branch.
    source_revision = repo.head_rev

    # Default to the head commit time rather than the wall clock, so that two
    # runs over the same checkout produce byte-identical output.
    # SOURCE_DATE_EPOCH wins where release engineering sets it.
    source_date_epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if source_date_epoch:
        try:
            commit_time = int(source_date_epoch)
        except ValueError:
            raise SbomError(
                "SOURCE_DATE_EPOCH must be an integer number of seconds since "
                f"the epoch, not {source_date_epoch!r}."
            )
    else:
        # A source tarball has no VCS, so no commit time to fall back on.
        commit_time = repo.get_commit_time() or 0
    return source_revision, utc_timestamp(commit_time)


def _serialize(records, version, repo, log, **bom_arguments):
    """Sort the records and return the CycloneDX document as a JSON string."""
    from mozbuild.vendor.sbom_cyclonedx import build_bom, to_json

    source_revision, timestamp = _source_revision_and_time(repo)
    unrecognized = []
    records.sort(key=lambda record: record["bom_ref"])
    document = to_json(
        build_bom(
            records,
            version,
            source_revision,
            timestamp,
            unrecognized=unrecognized,
            **bom_arguments,
        )
    )
    if unrecognized:
        log(
            f"{len(unrecognized)} license value(s) are neither an SPDX id nor "
            "an expression and are recorded as free text: "
            f"{', '.join(sorted(set(unrecognized)))}."
        )
    return document


def _product_identity(topsrcdir, substs, product_name, version):
    """Default the root component's name and version from the configuration."""
    # MOZ_APP_BASENAME, not MOZ_APP_DISPLAYNAME: the display name is the
    # branding, which is "Firefox" for both desktop and Android official builds
    # and moves with the channel otherwise. The basename is "Firefox" or
    # "Fennec", which is the distinction the SBOM needs.
    if product_name is None:
        product_name = substs.get("MOZ_APP_BASENAME") or "Firefox"

    if version is None:
        version = substs.get("MOZ_APP_VERSION_DISPLAY") or substs.get("MOZ_APP_VERSION")
    if version is None:
        with open(
            os.path.join(topsrcdir, "browser", "config", "version_display.txt"),
            encoding="utf-8",
        ) as version_file:
            version = version_file.read().strip()
    return product_name, version


def build_tooling_document(
    topsrcdir,
    topobjdir,
    repo,
    substs=None,
    version=None,
    product_name=None,
    strict=False,
    log=None,
):
    """Return the SBOM of what builds and tests the product, as a JSON string.

    The product document describes what ships; this one describes the
    third-party code the tree runs to get there, which ships in nothing but is
    as much a supply-chain input. Raises ``SbomError`` where ``strict`` asks
    for a hard failure.
    """
    from mozbuild.vendor.sbom_npm import npm_records
    from mozbuild.vendor.sbom_python import python_records

    substs = substs or {}
    log = log or (lambda message: None)

    # The same lockfiles the product document reads, less the runtime closure
    # it reports: webpack, babel and the rest of what builds the bundles.
    records, dependencies = npm_records(topsrcdir, dev=True)
    log(f"{len(records)} npm packages that only build the bundles.")

    _, manifests, errors = _manifest_records(repo, topsrcdir, log)
    if errors and strict:
        raise SbomError(f"{len(errors)} manifest(s) failed to load.")
    records.extend(manifests)
    log(f"{len(manifests)} vendored test, benchmark and tooling libraries.")

    # What mach, the build system and the test harnesses run on.
    packages, python_edges = python_records(topsrcdir)
    records.extend(packages)
    dependencies.update(python_edges)
    log(f"{len(packages)} vendored Python packages.")

    # The crates only the tests and the tools build, which the product
    # document leaves out.
    _, crates, crate_edges, kinds = _crate_records(topsrcdir, topobjdir, substs, log)
    records.extend(crates)
    dependencies.update(crate_edges)
    if kinds:
        log(f"{len(crates)} test and tooling crates.")
    elif strict:
        raise SbomError(
            "cargo metadata unavailable; tooling crates cannot be told apart."
        )
    else:
        log("cargo metadata unavailable; tooling crates cannot be told apart.")

    product_name, version = _product_identity(topsrcdir, substs, product_name, version)
    return _serialize(
        records,
        version,
        repo,
        log,
        product_name=product_name,
        dependencies=dependencies,
        build_tooling=True,
    )


def build_gradle_document(
    runtime_dependencies, repo, product_name, version=None, log=None
):
    """Return the SBOM of a Gradle application's runtime closure as a JSON string."""
    from mozbuild.vendor.sbom_gradle import GradleSbomError, gradle_records

    log = log or (lambda message: None)
    try:
        records, edges, gradle_version = gradle_records(runtime_dependencies)
    except GradleSbomError as error:
        raise SbomError(str(error))

    version = version or gradle_version
    if not version:
        raise SbomError(f"{runtime_dependencies} names no version; pass one.")

    document = _serialize(
        records, version, repo, log, product_name=product_name, dependencies=edges
    )
    log(f"{len(records)} Maven packages in {product_name} {version}.")
    return document


def build_document(
    topsrcdir,
    topobjdir,
    repo,
    substs=None,
    version=None,
    product_name=None,
    strict=False,
    log=None,
):
    """Return the SBOM as a JSON string.

    ``topobjdir`` may name a directory that holds no licenses.json, in which
    case the document is built from the moz.yaml manifests alone. ``substs``
    is empty for an unconfigured tree. Raises ``SbomError`` where ``strict``
    asks for a hard failure.
    """
    from mozbuild.vendor.sbom import (
        components_for_unmatched,
        load_license_notices,
        merge_license_notices,
        unattached_notices,
    )
    from mozbuild.vendor.sbom_gradle import (
        RUNTIME_DEPENDENCIES,
        GradleSbomError,
        gradle_records,
    )
    from mozbuild.vendor.sbom_npm import npm_records, upgrade_manifest_purls

    substs = substs or {}
    log = log or (lambda message: None)

    # The test suites, benchmarks and vendored Python packages build or test
    # the product rather than ship in it; the build tooling document has them.
    records, manifest_tooling, errors = _manifest_records(repo, topsrcdir, log)
    if errors and strict:
        raise SbomError(f"{len(errors)} manifest(s) failed to load.")
    log(
        f"{len(records)} vendored libraries; "
        f"{len(manifest_tooling)} test and tooling libraries left to the build "
        "tooling document."
    )

    # A crate only tests or tools use ships in nothing; the build tooling
    # document has it.
    crates, tooling, dependencies, kinds = _crate_records(
        topsrcdir, topobjdir, substs, log
    )
    records.extend(crates)

    if kinds:
        shipped = sum(1 for c in crates if {"normal", "build"} & set(c["kinds"]))
        log(
            f"{len(crates)} crates: {shipped} built into the product, "
            f"{len(crates) - shipped} not reached by cargo metadata; "
            f"{len(tooling)} test and tooling crate(s) left to the build tooling "
            "document.",
        )
    else:
        log(
            "cargo metadata unavailable; crate dependency kinds not collected.",
        )

    # The npm lockfiles of the bundles webpack folds into the product. Their
    # packages leave no directory of their own, so nothing else describes them.
    packages, npm_edges = npm_records(topsrcdir)
    records.extend(packages)
    dependencies.update(npm_edges)

    # A vendored npm library has a moz.yaml, but the purl derived from it names
    # a git repository rather than the registry the advisories are keyed to.
    upgraded = upgrade_manifest_purls(records, topsrcdir)

    log(
        f"{len(packages)} npm packages bundled into the product "
        "(dev-only dependencies excluded); "
        f"{upgraded} vendored manifest(s) given a pkg:npm purl.",
    )

    # GeckoView's Maven dependencies, as Gradle resolved them earlier in the
    # same build. Only an Android build has them, and there an SBOM without
    # them would silently describe less than the AAR brings in.
    maven = []
    if substs.get("MOZ_BUILD_APP") == "mobile/android":
        try:
            maven, maven_edges, _ = gradle_records(
                os.path.join(topobjdir, RUNTIME_DEPENDENCIES)
            )
        except GradleSbomError as error:
            if strict:
                raise SbomError(str(error))
            log(f"{error}; build the tree for the Maven dependencies.")
        else:
            records.extend(maven)
            dependencies.update(maven_edges)
            log(f"{len(maven)} Maven packages in GeckoView's runtime closure.")

    # The build backend writes this from the tree-wide moz.build LICENSES
    # declarations. It is absent in an unconfigured tree, in which case the
    # SBOM is built from moz.yaml alone.
    notices = load_license_notices(os.path.join(topobjdir, "licenses.json"))
    if notices:
        merge_license_notices(records, notices)
        # Licensed code with no moz.yaml still has to appear, or the SBOM would
        # describe less than about:license does.
        records.extend(
            components_for_unmatched(
                records,
                notices,
                lambda path: os.path.isfile(os.path.join(topsrcdir, path)),
                elsewhere=manifest_tooling + tooling,
            )
        )
        product_notices = unattached_notices(records, notices)
    else:
        product_notices = []
        log(
            "licenses.json not found; run ./mach build-backend for license data.",
        )

    product_name, version = _product_identity(topsrcdir, substs, product_name, version)

    document = _serialize(
        records,
        version,
        repo,
        log,
        product_notices=product_notices,
        product_name=product_name,
        dependencies=dependencies,
    )
    log(
        f"{len(records)} components ({len(crates)} crates, "
        f"{len(packages)} npm packages, {len(maven)} Maven packages, "
        f"{len(notices)} license notices).",
    )
    return document


def generate(
    topsrcdir,
    topobjdir,
    repo,
    substs=None,
    output=None,
    version=None,
    product_name=None,
    strict=False,
    gradle_runtime_dependencies=None,
    build_tooling=False,
):
    """Write the SBOM to ``output``, or to stdout. Returns a process exit code.

    With ``gradle_runtime_dependencies``, the document describes that Gradle
    application rather than the tree; with ``build_tooling``, what builds and
    tests the tree rather than what it ships.
    """

    def log(message):
        print(message, file=sys.stderr)

    try:
        if gradle_runtime_dependencies and build_tooling:
            raise SbomError(
                "--gradle-runtime-dependencies and --build-tooling each "
                "describe a different document."
            )
        if strict and (gradle_runtime_dependencies or build_tooling):
            raise SbomError(
                "--strict applies to the manifests the product document reads."
            )
        if build_tooling:
            document = build_tooling_document(
                topsrcdir,
                topobjdir,
                repo,
                substs=substs,
                version=version,
                product_name=product_name,
                log=log,
            )
        elif gradle_runtime_dependencies:
            if not product_name:
                raise SbomError("A Gradle application's SBOM needs a product name.")
            document = build_gradle_document(
                gradle_runtime_dependencies,
                repo,
                product_name,
                version=version,
                log=log,
            )
        else:
            document = build_document(
                topsrcdir,
                topobjdir,
                repo,
                substs=substs,
                version=version,
                product_name=product_name,
                strict=strict,
                log=log,
            )
    except SbomError as error:
        log(str(error))
        return 1

    if output:
        with open(output, "w", encoding="utf-8", newline="\n") as output_file:
            output_file.write(document)
        log(f"Wrote the SBOM to {output}.")
    else:
        sys.stdout.write(document)

    return 0


def generate_file(output):
    """GENERATED_FILES entry point, wired up from the top-level moz.build.

    Strict: a moz.yaml the SBOM cannot parse is a hole in the document, and a
    build that ships one should fail rather than describe less than it ships.
    """
    import buildconfig
    from mozversioncontrol import get_repository_object

    def log(message):
        print(message, file=sys.stderr)

    output.write(
        build_document(
            buildconfig.topsrcdir,
            buildconfig.topobjdir,
            get_repository_object(buildconfig.topsrcdir),
            substs=buildconfig.substs,
            strict=True,
            log=log,
        )
    )


def generate_build_tooling_file(output):
    """GENERATED_FILES entry point of the build tooling document.

    Strict: without `cargo metadata` the test and tooling crates cannot be told
    apart from the shipped ones, and the document would silently leave them out.
    """
    import buildconfig
    from mozversioncontrol import get_repository_object

    def log(message):
        print(message, file=sys.stderr)

    output.write(
        build_tooling_document(
            buildconfig.topsrcdir,
            buildconfig.topobjdir,
            get_repository_object(buildconfig.topsrcdir),
            substs=buildconfig.substs,
            strict=True,
            log=log,
        )
    )
