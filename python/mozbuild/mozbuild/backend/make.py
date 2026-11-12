# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import mozpack.path as mozpath
from mozshellutil import quote as shell_quote

from mozbuild.frontend.data import GeneratedFile

from .common import CommonBackend


class MakeBackend(CommonBackend):
    """Class encapsulating logic for backends that use Make."""

    def _init(self):
        CommonBackend._init(self)

    def _format_statements_for_generated_file(self, obj, tier, extra_dependencies=""):
        """Return the list of statements to write to the Makefile for this
        GeneratedFile.

        This function will invoke _format_generated_file_input_name and
        _format_generated_file_output_name to munge the input/output filenames
        before sending them to the output.
        """
        assert isinstance(obj, GeneratedFile)

        # Localized generated files can use {AB_CD} and {AB_rCD} in their
        # output paths.
        if obj.localized:
            substs = {"AB_CD": "$(AB_CD)", "AB_rCD": "$(AB_rCD)"}
        else:
            substs = {}

        outputs = []
        needs_AB_rCD = False
        for o in obj.outputs:
            needs_AB_rCD = needs_AB_rCD or ("AB_rCD" in o)
            try:
                outputs.append(
                    self._format_generated_file_output_name(o.format(**substs), obj)
                )
            except KeyError as e:
                raise ValueError(
                    "%s not in %s is not a valid substitution in %s"
                    % (e.args[0], ", ".join(sorted(substs.keys())), o)
                )

        first_output = outputs[0]
        dep_file = mozpath.join(
            mozpath.dirname(first_output),
            "$(MDDEPDIR)",
            "%s.pp" % mozpath.basename(first_output),
        )
        # The stub target file needs to go in MDDEPDIR so that it doesn't
        # get written into generated Android resource directories, breaking
        # Gradle tooling and/or polluting the Android packages.
        stub_file = mozpath.join(
            mozpath.dirname(first_output),
            "$(MDDEPDIR)",
            "%s.stub" % mozpath.basename(first_output),
        )

        if obj.inputs:
            inputs = [
                self._format_generated_file_input_name(f, obj) for f in obj.inputs
            ]
        else:
            inputs = []

        # extra_deps are make prerequisites only (not passed to the script as
        # positional args). Use this for dependencies the recipe needs to
        # exist on disk but that the script discovers itself at run time
        # (e.g. preprocessor `#include @TOPOBJDIR@/foo.h`).
        extra_deps = [
            self._format_generated_file_input_name(d, obj) for d in obj.extra_deps
        ]

        force = ""
        if obj.force:
            force = " FORCE"
        elif obj.localized:
            force = " $(if $(IS_LANGUAGE_REPACK),FORCE)"

        ret = []

        if obj.script:
            # If we are doing an artifact build, we don't run compiler, so
            # we can skip generated files that are needed during compile,
            # or let the rule run as the result of something depending on
            # it.
            if (
                not (obj.required_before_compile or obj.required_during_compile)
                or not self.environment.is_artifact_build
            ):
                if tier and not needs_AB_rCD:
                    # Android localized resources have special Makefile
                    # handling.

                    # Double-colon tiers via a variable that the backend adds as a dependency
                    # later. See https://bugzilla.mozilla.org/show_bug.cgi?id=1645986#c0 as
                    # to why.
                    if tier in ("export", "pre-compile", "libs", "misc"):
                        dep = "%s_TARGETS" % tier.replace("-", "_").upper()
                        ret.append("%s += %s" % (dep, stub_file))
                    else:
                        ret.append("%s: %s" % (tier, stub_file))
            for output in outputs:
                ret.append("%s: %s ;" % (output, stub_file))
            ret.append("EXTRA_MDDEPEND_FILES += %s" % dep_file)

            ret.append(
                (
                    """{stub}: {script}{inputs}{extra_deps}{backend}{force}
\t$(REPORT_BUILD)
\t$(call py_action,file_generate {output},{locale}{script} """  # wrap for E501
                    """{method} {output} {dep_file} {stub}{inputs}{flags})
\t@$(TOUCH) $@
"""
                ).format(
                    stub=stub_file,
                    output=first_output,
                    dep_file=dep_file,
                    inputs=" " + " ".join(inputs) if inputs else "",
                    extra_deps=" " + " ".join(extra_deps) if extra_deps else "",
                    flags=(
                        " " + " ".join(shell_quote(f) for f in obj.flags)
                        if obj.flags
                        else ""
                    ),
                    backend=" " + extra_dependencies if extra_dependencies else "",
                    # Locale repacks repack multiple locales from a single configured objdir,
                    # so standard mtime dependencies won't work properly when the build is re-run
                    # with a different locale as input. IS_LANGUAGE_REPACK will reliably be set
                    # in this situation, so simply force the generation to run in that case.
                    force=force,
                    locale="--locale=$(AB_CD) " if obj.localized else "",
                    script=obj.script,
                    method=obj.method,
                )
            )

        return ret

    def _format_generated_file_input_name(self, path, obj):
        raise NotImplementedError("Subclass must implement")

    def _format_generated_file_output_name(self, path, obj):
        raise NotImplementedError("Subclass must implement")
