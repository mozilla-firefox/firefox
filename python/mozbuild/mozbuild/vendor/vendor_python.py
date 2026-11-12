# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

import mozfile
import toml
from mozfile import TemporaryDirectory
from mozpack.files import FileFinder

from mozbuild.base import MozbuildObject

EXCLUDED_PACKAGES = {
    # dlmanager's package on PyPI only has metadata, but is missing the code.
    # https://github.com/parkouss/dlmanager/issues/1
    "dlmanager",
    # gyp's package on PyPI doesn't have any downloadable files.
    "gyp",
    # We keep some wheels vendored in "_venv" for use in Mozharness
    "_venv",
    # We manage vendoring "vsdownload" with a moz.yaml file (there is no module
    # on PyPI).
    "vsdownload",
    # The moz.build file isn't a vendored module, so don't delete it.
    "moz.build",
    # Support files needed for vendoring
    "uv.lock",
    "uv.lock.hash",
    "pyproject.toml",
    "requirements.txt",
    # The ansicon package contains DLLs and we don't want to arbitrarily vendor
    # them since they could be unsafe. This module should rarely be used in practice
    # (it's a fallback for old versions of windows). We've intentionally vendored a
    # modified 'dummy' version of it so that the dependency checks still succeed, but
    # if it ever is attempted to be used, it will fail gracefully.
    "ansicon",
    # jsonschema 4.17.3 is incompatible with Python 3.14+,
    # but later versions use a dependency with Rust components, which we thus can't vendor.
    # For now we apply the minimal patch to jsonschema to make it work again.
    "jsonschema",
    # filelock is temporarily excluded from the mach site to avoid conflicts with other sites
    # that require a newer version via PyPI. It is instead loaded directly via the mach.filelock wrapper.
    "filelock",
}


class VendorPython(MozbuildObject):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, virtualenv_name="uv", **kwargs)
        self.removed = []
        self.added = []

    def vendor(
        self,
        keep_extra_files=False,
        add=None,
        remove=None,
        upgrade=False,
        upgrade_package=None,
        force=False,
    ):
        self.populate_logger()
        self.log_manager.enable_unstructured()

        topsrcdir = Path(self.topsrcdir)

        self.sites_dir = topsrcdir / "python" / "sites"
        vendor_dir = topsrcdir / "third_party" / "python"
        requirements_file_name = "requirements.txt"
        requirements_path = vendor_dir / requirements_file_name
        uv_lock_file = vendor_dir / "uv.lock"
        vendored_lock_file_hash_file = vendor_dir / "uv.lock.hash"

        original_package_set = self.load_package_names(uv_lock_file)

        # Make the venv used by UV match the one set my Mach for the 'vendor' site
        os.environ["UV_PROJECT_ENVIRONMENT"] = os.environ.get("VIRTUAL_ENV", None)

        if add:
            for package in add:
                subprocess.check_call(["uv", "add", package], cwd=vendor_dir)

        if remove:
            for package in remove:
                subprocess.check_call(["uv", "remove", package], cwd=vendor_dir)

        lock_command = ["uv", "lock"]

        if upgrade:
            lock_command.extend(["-U"])

        if upgrade_package:
            for package in upgrade_package:
                lock_command.extend(["-P", package])

        subprocess.check_call(lock_command, cwd=vendor_dir)

        updated_package_set = self.load_package_names(uv_lock_file)

        self.added = sorted(updated_package_set - original_package_set)
        self.removed = sorted(original_package_set - updated_package_set)

        if not force:
            vendored_lock_file_hash_value = vendored_lock_file_hash_file.read_text(
                encoding="utf-8"
            ).strip()
            new_lock_file_hash_value = hash_file_text(uv_lock_file)

            if vendored_lock_file_hash_value == new_lock_file_hash_value:
                print(
                    "No changes detected in `uv.lock` since last vendor. Nothing to do. "
                    "(You can re-run this command with '--force' to force vendoring)"
                )
                return False

            print("Changes detected in `uv.lock`.")

        print("Re-vendoring all dependencies.\n")

        # Add "-q" so that the contents of the "requirements.txt" aren't printed
        subprocess.check_call(
            [
                "uv",
                "export",
                "--format",
                "requirements-txt",
                "-o",
                requirements_file_name,
                "-q",
            ],
            cwd=vendor_dir,
        )

        # Vendoring packages is only viable if it's possible to have a single
        # set of packages that work regardless of which environment they're used in.
        # So, we scrub environment markers, so that we essentially ask pip to
        # download "all dependencies for all environments". Pip will then either
        # fetch them as requested, or intelligently raise an error if that's not
        # possible (e.g.: if different versions of Python would result in different
        # packages/package versions).
        remove_environment_markers_from_requirements_txt(requirements_path)

        with TemporaryDirectory() as tmp:
            # use requirements.txt to download archived source distributions of all
            # packages
            subprocess.check_call([
                sys.executable,
                "-m",
                "pip",
                "download",
                "-r",
                str(requirements_path),
                "--no-deps",
                "--dest",
                tmp,
                "--abi",
                "none",
                "--platform",
                "any",
            ])
            _purge_vendor_dir(vendor_dir)
            self._extract(tmp, vendor_dir, keep_extra_files)

        vendored_lock_file_hash_file.write_text(
            encoding="utf-8", data=hash_file_text(uv_lock_file)
        )

        self.repository.add_remove_files(vendor_dir)
        # explicitly add the content of the egg-info directory as it is
        # covered by the hgignore pattern.
        egg_info_files = list(vendor_dir.glob("**/*.egg-info/*"))
        if egg_info_files:
            self.repository.add_remove_files(*egg_info_files, force=True)

        self._update_site_files()

        if self.added:
            added_relative_paths, packages_not_found = self.get_vendor_package_paths(
                vendor_dir
            )
            added_list = "\n ".join(str(p) for p in added_relative_paths)

            print(
                "\nNewly added package(s) that require manual addition to one or more <site>.txt files:\n",
                added_list,
            )
            if packages_not_found:
                print(
                    f"Could not locate directories for the following added package(s) under {vendor_dir}:\n"
                    + "\n ".join(packages_not_found)
                )
            print(
                "\n You must add each to the appropriate site(s)."
                f"\n Site directory: {self.sites_dir.as_posix()}"
                "\n Do not simply add them to the 'mach.txt' site unless Mach itself depends on it."
            )

        return True

    def get_vendor_package_paths(self, vendor_dir: Path):
        topsrcdir = Path(self.topsrcdir)
        relative_paths = []
        missing = []

        for pkg in sorted(self.added):
            candidates = [
                vendor_dir / pkg,
                vendor_dir / pkg.replace("-", "_"),
                vendor_dir / pkg.replace("_", "-"),
            ]
            for path in candidates:
                if path.is_dir():
                    try:
                        rel = path.relative_to(topsrcdir)
                    except ValueError:
                        raise ValueError(f"path {path} must be relative to {topsrcdir}")
                    relative_paths.append(rel)
                    break
            else:
                missing.append(pkg)
        return relative_paths, missing

    def load_package_names(self, lockfile_path: Path):
        with lockfile_path.open("r", encoding="utf-8") as f:
            data = toml.load(f)
        return {pkg["name"] for pkg in data.get("package", [])}

    def _update_site_files(self):
        if not self.removed:
            return

        print("\nRemoving references to removed package(s):")
        for pkg in sorted(self.removed):
            print(f"  - {pkg}")
        print(
            f"\nScanning all “.txt” site files in {self.sites_dir.as_posix()} for references to those packages.\n"
        )

        cand_to_pkg = {}
        for pkg in self.removed:
            cand_to_pkg[pkg] = pkg
            cand_to_pkg[pkg.replace("-", "_")] = pkg
        rm_candidates = set(cand_to_pkg)

        packages_removed_from_sites = set()

        for site_file in self.sites_dir.glob("*.txt"):
            lines = site_file.read_text().splitlines()
            potential_output = []
            removed_lines = []
            updated_needed = False

            for line in lines:
                if line.startswith(("vendored:", "vendored-fallback:")):
                    for cand in rm_candidates:
                        marker = f"third_party/python/{cand}"
                        if marker in line:
                            removed_lines.append(line)
                            packages_removed_from_sites.add(cand_to_pkg[cand])
                            updated_needed = True
                            break
                    else:
                        potential_output.append(line)
                else:
                    potential_output.append(line)

            if updated_needed:
                updated_site_contents = "\n".join(potential_output) + "\n"
                with site_file.open("w", encoding="utf-8", newline="\n") as f:
                    f.write(updated_site_contents)

                print(f"-- {site_file.as_posix()} updated:")
                for line in removed_lines:
                    print(f" removed: {line}")

        references_not_removed_automatically = (
            set(self.removed) - packages_removed_from_sites
        )
        if references_not_removed_automatically:
            output = ", ".join(sorted(references_not_removed_automatically))
            print(
                f"No references were found for the following package(s) removed by mach vendor python: {output}\n"
                f"You may need to do a manual removal."
            )

    def _extract(self, src, dest, keep_extra_files=False):
        """extract source distribution into vendor directory"""

        ignore = ()
        if not keep_extra_files:
            ignore = ("*/doc", "*/docs", "*/test", "*/tests", "**/.git")
        finder = FileFinder(src)
        for archive, _ in finder.find("*"):
            _, ext = os.path.splitext(archive)
            archive_path = os.path.join(finder.base, archive)
            if ext == ".whl":
                # Archive is named like "$package-name-1.0-py2.py3-none-any.whl", and should
                # have four dashes that aren't part of the package name.
                package_name, version, spec, abi, platform_and_suffix = archive.rsplit(
                    "-", 4
                )

                if package_name in EXCLUDED_PACKAGES:
                    print(
                        f"'{package_name}' is on the exclusion list and will not be vendored."
                    )
                    continue

                target_package_dir = os.path.join(dest, package_name)
                os.mkdir(target_package_dir)

                # Extract all the contents of the wheel into the package subdirectory.
                # We're expecting at least a code directory and a ".dist-info" directory,
                # though there may be a ".data" directory as well.
                mozfile.extract(archive_path, target_package_dir, ignore=ignore)
                _denormalize_symlinks(target_package_dir)
            else:
                # Archive is named like "$package-name-1.0.tar.gz", and the rightmost
                # dash should separate the package name from the rest of the archive
                # specifier.
                package_name, archive_postfix = archive.rsplit("-", 1)
                package_dir = os.path.join(dest, package_name)

                if package_name in EXCLUDED_PACKAGES:
                    print(
                        f"'{package_name}' is on the exclusion list and will not be vendored."
                    )
                    continue

                # The archive should only contain one top-level directory, which has
                # the source files. We extract this directory directly to
                # the vendor directory.
                extracted_files = mozfile.extract(archive_path, dest, ignore=ignore)
                assert len(extracted_files) == 1
                extracted_package_dir = extracted_files[0]

                # The extracted package dir includes the version in the name,
                # which we don't we don't want.
                mozfile.move(extracted_package_dir, package_dir)
                _denormalize_symlinks(package_dir)


def remove_environment_markers_from_requirements_txt(requirements_txt: Path):
    with requirements_txt.open(mode="r", newline="\n") as f:
        lines = f.readlines()
    markerless_lines = []
    continuation_token = " \\"
    for line in lines:
        line = line.rstrip()

        if not line.startswith(" ") and not line.startswith("#") and ";" in line:
            has_continuation_token = line.endswith(continuation_token)
            # The first line of each requirement looks something like:
            #   package-name==X.Y; python_version>=3.7
            # We can scrub the environment marker by splitting on the semicolon
            line = line.split(";")[0]
            if has_continuation_token:
                line += continuation_token
            markerless_lines.append(line)
        else:
            markerless_lines.append(line)

    with requirements_txt.open(mode="w", newline="\n") as f:
        f.write("\n".join(markerless_lines))


def _purge_vendor_dir(vendor_dir):
    for child in Path(vendor_dir).iterdir():
        if child.name not in EXCLUDED_PACKAGES:
            mozfile.remove(str(child))


def _denormalize_symlinks(target):
    # If any files inside the vendored package were symlinks, turn them into normal files
    # because hg.mozilla.org forbids symlinks in the repository.
    link_finder = FileFinder(target)
    for _, f in link_finder.find("**"):
        if os.path.islink(f.path):
            link_target = os.path.realpath(f.path)
            os.unlink(f.path)
            shutil.copyfile(link_target, f.path)


def hash_file_text(file_path):
    hash_func = hashlib.new("sha256")
    file_content = file_path.read_text(encoding="utf-8")
    hash_func.update(file_content.encode("utf-8"))
    return hash_func.hexdigest()
