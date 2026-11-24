# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import json
import os
import re

KEY_XRE = "{xre}"
DEFAULT_DURATION = 100.0


class Allowlist:
    # we need to find the root dir of the profile at runtime
    PRE_PROFILE = ""

    def __init__(
        self,
        test_name,
        paths,
        path_substitutions,
        name_substitutions,
        event_sources=None,
        init_with=None,
    ):
        self.test_name = test_name
        self.listmap = init_with if init_with else {}
        self.dependent_libs = (
            self.load_dependent_libs() if init_with and KEY_XRE in paths else {}
        )
        self.paths = paths
        self.path_substitutions = path_substitutions
        self.name_substitutions = name_substitutions
        self.expected_event_sources = event_sources or []

    def load(self, filename):
        if not self.load_dependent_libs():
            return False

        try:
            with open(filename) as fHandle:
                temp = json.load(fHandle)

            for allowlist_name in temp:
                self.listmap[allowlist_name.lower()] = temp[allowlist_name]

        except OSError as e:
            print(f"{e.filename}: {e.strerror}")
            return False
        return True

    def sanitize_filename(self, filename):
        filename = filename.lower()
        filename.replace(" (x86)", "")

        for path, subst in self.path_substitutions.items():
            parts = filename.split(path)
            if len(parts) >= 2:
                if self.PRE_PROFILE == "" and subst == "{profile}":
                    fname = self.sanitize_filename(parts[0])
                    self.listmap[fname] = {}
                    # Windows can have {appdata}\local\temp\longnamedfolder
                    # or {appdata}\local\temp\longna~1
                    self.listmap[fname] = {}
                    if not fname.endswith("~1"):
                        # parse the longname into longna~1
                        dirs = fname.split("\\")
                        dirs[-1] = f"{dirs[-1][:6]}~1"
                        # now we want to ensure that every parent dir is
                        # added since we seem to be accessing them sometimes
                        diter = 2
                        while diter < len(dirs):
                            self.listmap["\\".join(dirs[:diter])] = {}
                            diter = diter + 1
                        self.PRE_PROFILE = fname

                filename = f"{subst}{path.join(parts[1:])}"

        for old_name, new_name in self.name_substitutions.items():
            if isinstance(old_name, re.Pattern):
                filename = re.sub(old_name, new_name, filename)
            else:
                parts = filename.split(old_name)
                if len(parts) >= 2:
                    filename = f"{parts[0]}{new_name}"

        return filename.strip("/\\\\ \t")

    def check(self, test, file_name_index, event_source_index=None):
        errors = {}
        for row_key in test.keys():
            filename = self.sanitize_filename(row_key[file_name_index])

            if filename in self.listmap:
                if (
                    "ignore" in self.listmap[filename]
                    and self.listmap[filename]["ignore"]
                ):
                    continue
            elif filename in self.dependent_libs:
                continue
            elif (
                event_source_index is not None
                and row_key[event_source_index] in self.expected_event_sources
            ):
                continue
            else:
                if filename not in errors:
                    errors[filename] = []
                errors[filename].append(test[row_key])
        return errors

    def checkDuration(self, test, file_name_index, file_duration_index):
        errors = {}
        for idx, (row_key, row_value) in enumerate(test.items()):
            if row_value[file_duration_index] > DEFAULT_DURATION:
                filename = self.sanitize_filename(row_key[file_name_index])
                if (
                    filename in self.listmap
                    and "ignoreduration" in self.listmap[filename]
                ):
                    # we have defined in the json manifest max values
                    # (max found value * 2) and will ignore it
                    if (
                        row_value[file_duration_index]
                        <= self.listmap[filename]["ignoreduration"]
                    ):
                        continue

                if filename not in errors:
                    errors[filename] = []
                errors[filename].append(
                    f"Duration {row_value[file_duration_index]} > {DEFAULT_DURATION}"
                )
        return errors

    def filter(self, test, file_name_index):
        for row_key in test.keys():
            filename = self.sanitize_filename(row_key[file_name_index])
            if filename in self.listmap:
                if (
                    "ignore" in self.listmap[filename]
                    and self.listmap[filename]["ignore"]
                ):
                    del test[row_key]
                    continue
            elif filename in self.dependent_libs:
                del test[row_key]
                continue

    @staticmethod
    def get_error_strings(errors):
        error_strs = []
        for filename, data in errors.items():
            for datum in data:
                error_strs.append(
                    f"File '{filename}' was accessed and we were not"
                    f" expecting it: {datum!r}"
                )
        return error_strs

    def print_errors(self, error_strs):
        for error_msg in error_strs:
            print(f"TEST-UNEXPECTED-FAIL | {self.test_name} | {error_msg}")

    # Note that we don't store dependent libs in listmap. This makes
    # save_baseline cleaner. Since a baseline allowlist should not include
    # the dependent_libs, we would need to filter them out if everything was
    # stored in the same dict.
    def load_dependent_libs(self):
        filename = f"{self.paths[KEY_XRE]}{os.path.sep}dependentlibs.list"
        try:
            with open(filename) as f:
                libs = f.readlines()
            self.dependent_libs = {
                f"{KEY_XRE}{os.path.sep}{lib.strip()}": {"ignore": True} for lib in libs
            }
            return True
        except OSError as e:
            print(f"{e.filename}: {e.strerror}")
            return False
