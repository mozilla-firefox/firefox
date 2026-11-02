# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""L10n manifest data structures and helpers.

The l10n manifest is written during configure to
<topobjdir>/l10n-manifest.json and read during command time.

All data structures of interest are children of the L10nManifest
class.

jar.mn entries are captured with AB_CD=MOZ_L10N_AB_CD_PLACEHOLDER
so the manifest stays locale-independent, and are resolved during staging.
"""

from __future__ import annotations

import json
from collections.abc import Iterator
from dataclasses import asdict, dataclass, field, replace
from pathlib import Path
from typing import Optional

import mozpack.path as mozpath

from mozbuild.frontend.context import SourcePath
from mozbuild.frontend.data import ContextDerived
from mozbuild.frontend.jar_manifest import (
    JarEntry,
    JarSection,
    jar_manifest_defines,
    parse_jar_manifest,
)
from mozbuild.frontend.reader import SandboxValidationError

MANIFEST_VERSION = 3
MOZ_L10N_AB_CD_PLACEHOLDER = "MOZ_L10N_AB_CD_PLACEHOLDER"


def locale_pp_placeholder(name: str) -> str:
    """Per-key placeholder for a LOCALE_PP_DEFINES preprocessor
    variable, so stage time can resolve or skip each key independently.

    e.g.
    ANDROID_MARKETPLACE_AB_CD ->
    MOZ_L10N_DEFINE_ANDROID_MARKETPLACE_AB_CD_PLACEHOLDER.
    """
    return f"MOZ_L10N_DEFINE_{name}_PLACEHOLDER"


@dataclass
class LocalizedFileGroup:
    """One LOCALIZED_FILES/LOCALIZED_PP_FILES group for a context."""

    subpath: str
    sources: list[str]


@dataclass
class LocalizedGenScript:
    """One LOCALIZED_GENERATED_FILES script invocation, locale-templated."""

    script: str
    method: str
    inputs: list[str]
    outputs: list[str]
    flags: list[str]
    force: bool


@dataclass
class L10nManifestContextData:
    """Per-moz.build-context l10n data.

    relsrcdir is topsrcdir-relative and locates en-US sources.
    locale_relsrcdir is rooted at the directory the l10n repository
    mirrors and locates merge-tree sources; the two only differ for
    comm-central, which localizes from commtopsrcdir.
    """

    relsrcdir: str
    install_subdir: str
    defines: dict[str, object]
    locale_pp_defines: dict[str, dict[str, str]]
    locale_relsrcdir: str
    jar_sections: list[JarSection] = field(default_factory=list)
    localized_files: list[LocalizedFileGroup] = field(default_factory=list)
    localized_pp_files: list[LocalizedFileGroup] = field(default_factory=list)
    localized_generated_files: list[LocalizedGenScript] = field(default_factory=list)


@dataclass
class L10nManifest:
    """topobjdir l10n manifest."""

    version: int
    moz_app_id: str
    moz_app_version: str
    moz_app_displayname: str
    moz_build_app: str
    contexts: list[L10nManifestContextData] = field(default_factory=list)


def write_l10n_manifest(manifest: L10nManifest, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(asdict(manifest), f, indent=2, sort_keys=True)


def load_l10n_manifest(path: Path) -> L10nManifest:
    with path.open(encoding="utf-8") as f:
        raw = json.load(f)
    if raw.get("version") != MANIFEST_VERSION:
        raise ValueError(
            f"Unsupported l10n manifest version: {raw.get('version')!r} "
            f"(expected {MANIFEST_VERSION})"
        )
    contexts = [
        L10nManifestContextData(
            relsrcdir=c["relsrcdir"],
            locale_relsrcdir=c["locale_relsrcdir"],
            install_subdir=c["install_subdir"],
            defines=c["defines"],
            locale_pp_defines=c["locale_pp_defines"],
            jar_sections=[
                JarSection(
                    name=s["name"],
                    base=s["base"],
                    relativesrcdir=s["relativesrcdir"],
                    entries=tuple(JarEntry(**e) for e in s["entries"]),
                    chrome_manifests=tuple(s["chrome_manifests"]),
                )
                for s in c["jar_sections"]
            ],
            localized_files=[LocalizedFileGroup(**g) for g in c["localized_files"]],
            localized_pp_files=[
                LocalizedFileGroup(**g) for g in c["localized_pp_files"]
            ],
            localized_generated_files=[
                LocalizedGenScript(**g) for g in c["localized_generated_files"]
            ],
        )
        for c in raw["contexts"]
    ]
    return L10nManifest(
        version=raw["version"],
        moz_app_id=raw["moz_app_id"],
        moz_app_version=raw["moz_app_version"],
        moz_app_displayname=raw["moz_app_displayname"],
        moz_build_app=raw["moz_build_app"],
        contexts=contexts,
    )


def _in_manifest_roots(relsrcdir: str, roots: list[str]) -> bool:
    return not roots or mozpath.basedir(relsrcdir, roots) is not None


def _scoped_contexts(
    context_data_list: list[L10nManifestContextData], roots: list[str]
) -> Iterator[L10nManifestContextData]:
    """Drop the chrome content of contexts outside the app's roots.

    The roots say which directories supply the app's chrome, so a
    directory outside them contributes no jar.mn entries. Its localized
    files and generated files keep their existing behavior, which is how
    a repack still gets toolkit/locales' default.locale.
    """
    for data in context_data_list:
        if _in_manifest_roots(data.relsrcdir, roots):
            yield data
            continue
        scoped = replace(data, jar_sections=[])
        if (
            scoped.localized_files
            or scoped.localized_pp_files
            or scoped.localized_generated_files
        ):
            yield scoped


def build_l10n_manifest_from_substs(
    substs: dict[str, object],
    context_data_list: list[L10nManifestContextData],
) -> L10nManifest:
    roots = substs.get("MOZ_L10N_CHROME_ROOTS") or []
    return L10nManifest(
        version=MANIFEST_VERSION,
        moz_app_id=substs.get("MOZ_APP_ID") or "",
        moz_app_version=substs.get("MOZ_APP_VERSION") or "",
        moz_app_displayname=substs.get("MOZ_APP_DISPLAYNAME") or "",
        moz_build_app=substs.get("MOZ_BUILD_APP") or "",
        contexts=list(_scoped_contexts(context_data_list, roots)),
    )


class L10nManifestContext(ContextDerived):
    """Emitter-yielded wrapper around an L10nManifestContextData."""

    __slots__ = ("data",)

    def __init__(self, context, data: L10nManifestContextData) -> None:
        ContextDerived.__init__(self, context)
        self.data = data


def emit_l10n_manifest_contexts(emitter, contexts) -> list[L10nManifestContext]:
    """Return one L10nManifestContext per moz.build context that declares
    localized content (JAR_MANIFESTS, LOCALIZED_FILES, etc.).
    """
    result = []
    for context in contexts.values():
        context_data = _extract_l10n_manifest_context_data(emitter, context)
        if context_data is not None:
            result.append(L10nManifestContext(context, context_data))
    return result


def _has_walkable(files) -> bool:
    return bool(files and any(files.walk()))


def _extract_l10n_manifest_context_data(
    emitter, context
) -> Optional[L10nManifestContextData]:
    has_jar = bool(context.get("JAR_MANIFESTS"))
    localized_files = context.get("LOCALIZED_FILES")
    localized_pp_files = context.get("LOCALIZED_PP_FILES")
    has_localized_files = _has_walkable(localized_files)
    has_localized_pp_files = _has_walkable(localized_pp_files)
    has_localized_gen = bool(context.get("LOCALIZED_GENERATED_FILES"))
    has_locale_pp_defines = bool(context.get("LOCALE_PP_DEFINES"))

    if not (
        has_jar
        or has_localized_files
        or has_localized_pp_files
        or has_localized_gen
        or has_locale_pp_defines
    ):
        return None

    context_data = L10nManifestContextData(
        relsrcdir=str(context.relsrcdir),
        locale_relsrcdir=_locale_relsrcdir(emitter.config, str(context.relsrcdir)),
        install_subdir="",
        defines=_normalize_defines(context.get("DEFINES")),
        locale_pp_defines={
            k: dict(v) for k, v in (context.get("LOCALE_PP_DEFINES") or {}).items()
        },
    )

    if has_jar:
        for path in context["JAR_MANIFESTS"]:
            context_data.jar_sections.extend(_extract_jar_sections(context, path))

    if has_localized_files:
        context_data.localized_files = _extract_localized_files(localized_files)

    if has_localized_pp_files:
        context_data.localized_pp_files = _extract_localized_files(localized_pp_files)

    if has_localized_gen:
        context_data.localized_generated_files = _extract_localized_generated(context)

    # Skip contexts that ended up with no actual locale content (e.g. a
    # JAR_MANIFESTS context whose jar.mn has no locale entries).
    if not (
        context_data.jar_sections
        or context_data.localized_files
        or context_data.localized_pp_files
        or context_data.localized_generated_files
        or context_data.locale_pp_defines
    ):
        return None

    final_target = str(context["FINAL_TARGET"])
    if not mozpath.basedir(final_target, ("dist/bin",)):
        raise SandboxValidationError(
            f"Cannot stage localized content installed to '{final_target}'; "
            "localized content must install under dist/bin.",
            context,
        )
    context_data.install_subdir = mozpath.relpath(final_target, "dist/bin")
    return context_data


def _locale_relsrcdir(config, relsrcdir: str) -> str:
    """Return relsrcdir relative to the directory the l10n repository
    mirrors. That is commtopsrcdir for directories under it and
    topsrcdir for everything else, so comm paths lose the leading comm/.
    """
    commtopsrcdir = config.substs.get("commtopsrcdir")
    if not commtopsrcdir:
        return relsrcdir
    comm_reldir = mozpath.relpath(commtopsrcdir, config.topsrcdir)
    if mozpath.basedir(relsrcdir, [comm_reldir]) is None:
        return relsrcdir
    return mozpath.relpath(relsrcdir, comm_reldir)


def _normalize_defines(defines) -> dict[str, object]:
    """Normalize a DEFINES value to a JSON-friendly dict, stringifying any
    values that aren't natively JSON-encodable (e.g. UnquotedString).
    """
    if not defines:
        return {}
    out = {}
    for k, v in defines.items():
        if isinstance(v, (str, int, float)) or v is None:
            out[k] = v
        else:
            out[k] = str(v)
    return out


def _extract_jar_sections(context, path) -> Iterator[JarSection]:
    """Parse a jar.mn and yield JarSections for groups that contain at
    least one locale entry, a locale-marked chrome.manifest line, or a
    [localization] block.

    Runs the preprocessor with AB_CD=MOZ_L10N_AB_CD_PLACEHOLDER so
    locale substitution is deferred to stage time.

    When LOCALE_PP_DEFINES is set, runs the preprocessor twice. The
    first pass leaves the defines unset (capturing #else branches);
    the second sets each define to its placeholder (capturing #ifdef
    branches). Stage time resolves the placeholder to the locale's
    value, or skips the entry when the placeholder doesn't resolve
    (so unsupported locales fall back to the first pass's #else).
    """
    yield from _run_jar_pp(context, path, locale_pp_overrides=None)
    locale_pp_defines = context.get("LOCALE_PP_DEFINES") or {}
    if locale_pp_defines:
        overrides = {k: locale_pp_placeholder(k) for k in locale_pp_defines}
        yield from _run_jar_pp(context, path, locale_pp_overrides=overrides)


def _is_locale_aware(entry) -> bool:
    """A jar.mn entry contributes locale content when its source is
    %-prefixed (is_locale=True) or rooted in en-US (en-US/... or
    .../locales/en-US/...).

    Other sources are en-US-only and not repacked per locale.
    """
    if entry.is_locale:
        return True
    src = entry.source or ""
    return src.startswith("en-US/") or "/locales/en-US/" in src


def _run_jar_pp(
    context,
    path,
    *,
    locale_pp_overrides: Optional[dict[str, str]],
) -> Iterator[JarSection]:
    defines = jar_manifest_defines(context)
    if locale_pp_overrides:
        defines.update(locale_pp_overrides)
    sections, inputs = parse_jar_manifest(
        path.full_path, defines, MOZ_L10N_AB_CD_PLACEHOLDER
    )
    # Reading the manifest happens outside the sandbox, so the paths it read
    # are recorded on the context the l10n object is derived from.
    for read in inputs:
        context.add_source(mozpath.normsep(read))

    for jarinfo in sections:
        has_locale_aware_entry = any(_is_locale_aware(e) for e in jarinfo.entries)
        has_locale_manifest = any(
            m.lstrip().startswith("locale ") for m in jarinfo.chrome_manifests
        )
        if not (has_locale_aware_entry or has_locale_manifest):
            continue
        yield jarinfo


def _extract_localized_files(files) -> list[LocalizedFileGroup]:
    """Flatten a LOCALIZED_FILES/LOCALIZED_PP_FILES hierarchical string
    list into a list of LocalizedFileGroup, one per install subpath.
    """
    groups = []
    for subpath, entries in files.walk():
        if entries:
            sources = [str(f) for f in entries]
            groups.append(LocalizedFileGroup(subpath=subpath, sources=sources))
    return groups


def _extract_localized_generated(context) -> list[LocalizedGenScript]:
    """Convert LOCALIZED_GENERATED_FILES into LocalizedGenScripts.

    Outputs keep their locale placeholders for stage time to resolve.
    Script paths are resolved to absolute paths.
    """
    table = context["LOCALIZED_GENERATED_FILES"]
    out = []
    for entry in table:
        flags = table[entry]
        if not flags.script:
            continue
        outputs = list(entry) if isinstance(entry, tuple) else [entry]
        # script has the form "path/to/script.py" or
        # "path/to/script.py:function_name" (defaults to "main").
        script = flags.script
        method = "main"
        if ":" in script:
            script, method = script.rsplit(":", 1)
        script = SourcePath(context, script).full_path
        out.append(
            LocalizedGenScript(
                script=script,
                method=method,
                inputs=[str(i) for i in (flags.inputs or [])],
                outputs=outputs,
                flags=list(flags.flags or []),
                force=bool(flags.force),
            )
        )
    return out
