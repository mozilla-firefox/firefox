# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import re

import mozpack.path as mozpath
from mach.config import ConfigSettings
from mach.logging import LoggingManager
from mozpack.copier import FileRegistry
from mozpack.files import PreprocessedFile
from mozpack.manifests import InstallManifest

from mozbuild.backend.common import CommonBackend
from mozbuild.base import MozbuildObject
from mozbuild.frontend.data import (
    ChromeManifestEntry,
    FinalTargetFiles,
    FinalTargetPreprocessedFiles,
)

from .manifest_handler import ChromeManifestHandler

_line_comment_re = re.compile(r'^//@line (\d+) "(.+)"$')


def generate_pp_info(path, topsrcdir):
    with open(path, encoding="utf-8") as fh:
        # (start, end) -> (included_source, start)
        section_info = dict()

        this_section = None

        def finish_section(pp_end):
            pp_start, inc_source, inc_start = this_section
            section_info[str(pp_start) + "," + str(pp_end)] = inc_source, inc_start

        for count, line in enumerate(fh):
            # Regex are quite slow, so bail out early.
            if not line.startswith("//@line"):
                continue
            m = re.match(_line_comment_re, line)
            if m:
                if this_section:
                    finish_section(count + 1)
                inc_start, inc_source = m.groups()

                # Special case to handle $SRCDIR prefixes
                src_dir_prefix = "$SRCDIR"
                parts = mozpath.split(inc_source)
                if parts[0] == src_dir_prefix:
                    inc_source = mozpath.join(*parts[1:])
                else:
                    inc_source = mozpath.relpath(inc_source, topsrcdir)

                pp_start = count + 2
                this_section = pp_start, inc_source, int(inc_start)

        if this_section:
            finish_section(count + 2)

        return section_info


# This build backend is assuming the build to have happened already, as it is parsing
# built preprocessed files to generate data to map them to the original sources.


class ChromeMapBackend(CommonBackend):
    def _init(self):
        CommonBackend._init(self)

        log_manager = LoggingManager()
        self._cmd = MozbuildObject(
            self.environment.topsrcdir,
            ConfigSettings(),
            log_manager,
            self.environment.topobjdir,
        )
        self._install_mapping = {}
        self.manifest_handler = ChromeManifestHandler()

    def consume_object(self, obj):
        if isinstance(obj, ChromeManifestEntry):
            self.manifest_handler.handle_manifest_entry(obj.entry)
        if isinstance(obj, (FinalTargetFiles, FinalTargetPreprocessedFiles)):
            self._handle_final_target_files(obj)
        return True

    def _handle_final_target_files(self, obj):
        for path, files in obj.files.walk():
            for f in files:
                if isinstance(obj, FinalTargetPreprocessedFiles):
                    basename = FinalTargetPreprocessedFiles.get_obj_basename(f)
                else:
                    basename = f.target_basename

                dest = mozpath.join(obj.install_target, path, basename)
                obj_path = mozpath.join(self.environment.topobjdir, dest)

                if isinstance(obj, FinalTargetPreprocessedFiles):
                    assert os.path.exists(obj_path), "%s should exist" % obj_path
                    pp_info = generate_pp_info(obj_path, obj.topsrcdir)
                else:
                    pp_info = None

                base = (
                    obj.topobjdir
                    if f.full_path.startswith(obj.topobjdir)
                    else obj.topsrcdir
                )
                self._install_mapping[dest] = (
                    mozpath.relpath(f.full_path, base),
                    pp_info,
                )

    def consume_finished(self):
        mp = os.path.join(
            self.environment.topobjdir, "_build_manifests", "install", "_tests"
        )
        install_manifest = InstallManifest(mp)
        reg = FileRegistry()
        install_manifest.populate_registry(reg)

        for dest, src in reg:
            if not hasattr(src, "path"):
                continue

            if not os.path.isabs(dest):
                dest = "_tests/" + dest

            obj_path = mozpath.join(self.environment.topobjdir, dest)
            if isinstance(src, PreprocessedFile):
                assert os.path.exists(obj_path), "%s should exist" % obj_path
                pp_info = generate_pp_info(obj_path, self.environment.topsrcdir)
            else:
                pp_info = None

            rel_src = mozpath.relpath(src.path, self.environment.topsrcdir)
            self._install_mapping[dest] = rel_src, pp_info

        # Our result has four parts:
        #  A map from url prefixes to objdir directories:
        #  { "chrome://mozapps/content/": [ "dist/bin/chrome/toolkit/content/mozapps" ], ... }
        #  A map of overrides.
        #  A map from objdir paths to sourcedir paths, and an object storing mapping
        #    information for preprocessed files:
        #  { "dist/bin/browser/chrome/browser/content/browser/aboutSessionRestore.js":
        #    [ "$topsrcdir/browser/components/sessionstore/content/aboutSessionRestore.js", {} ],
        #    ... }
        #  An object containing build configuration information.
        outputfile = os.path.join(self.environment.topobjdir, "chrome-map.json")
        with self._write_file(outputfile) as fh:
            chrome_mapping = self.manifest_handler.chrome_mapping
            overrides = self.manifest_handler.overrides
            json.dump(
                [
                    {k: list(v) for k, v in chrome_mapping.items()},
                    overrides,
                    self._install_mapping,
                    {
                        "topobjdir": mozpath.normpath(self.environment.topobjdir),
                        "MOZ_APP_NAME": self.environment.substs.get("MOZ_APP_NAME"),
                        "OMNIJAR_NAME": self.environment.substs.get("OMNIJAR_NAME"),
                        "MOZ_MACBUNDLE_NAME": self.environment.substs.get(
                            "MOZ_MACBUNDLE_NAME"
                        ),
                    },
                ],
                fh,
                sort_keys=True,
                indent=2,
            )
