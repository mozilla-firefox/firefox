# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Note: This script was written to follow the same business logic
#  as make_incremental_update.sh and funsize logic. There are many
#  opportunities for refactoring and improving how this works.
# Some improvement ideas:
# - Only decompress the target complete mar once
# - Separate this script into a python module with multiple files (ie: download, validation, diffing)
# - Implement caching of diffs. https://bugzilla.mozilla.org/show_bug.cgi?id=1842209
# - Writing of the manifest file could be done at the very end instead of multiple writes

import argparse
import configparser
import filecmp
import functools
import glob
import hashlib
import json
import logging
import os
import re
import shutil
import subprocess
import sys
import time
import traceback
import urllib.error
import urllib.request
from concurrent.futures import ProcessPoolExecutor, as_completed
from tempfile import TemporaryDirectory

from mardor.reader import MarReader
from mardor.signing import get_keysize

# Additional flags for XZ compression
BCJ_OPTIONS = {
    "x86": ["--x86"],
    "x86_64": ["--x86"],
    "aarch64": [],
    "macos-x86_64-aarch64": [],
}
ROOT_URL = os.environ.get(
    "TASKCLUSTER_ROOT_URL", "https://firefox-ci-tc.services.mozilla.com"
)
QUEUE_PREFIX = f"{ROOT_URL}/api/queue/"
ALLOWED_URL_PREFIXES = (
    "http://download.cdn.mozilla.net/pub/mozilla.org/firefox/nightly/",
    "http://download.cdn.mozilla.net/pub/firefox/nightly/",
    "http://ftp.mozilla.org/",
    "http://download.mozilla.org/",
    "https://archive.mozilla.org/",
    "http://archive.mozilla.org/",
    QUEUE_PREFIX,
)
STAGING_URL_PREFIXES = (
    # TODO: Update release_history to force stage urls as https. https://bugzilla.mozilla.org/show_bug.cgi?id=2010915
    "http://ftp.stage.mozaws.net/",
    "https://ftp.stage.mozaws.net/",
)

# Download retry tuning. Sleep grows linearly per attempt, capped at the max.
DOWNLOAD_ATTEMPTS = 5
DOWNLOAD_SLEEPTIME = 5
DOWNLOAD_MAX_SLEEPTIME = 30

logging.basicConfig(level=logging.INFO)


class DownloadError(Exception):
    """Raised when a file download fails after exhausting all retries."""


# TODO: use logging context instead of this
#   https://docs.python.org/3/howto/logging-cookbook.html#context-info
def log(msg, func=""):
    logging.info(f"[pid: {os.getpid()}] {func}: {msg}")


def xz_compression_options(arch):
    return (
        "--compress",
        "-T1",
        "-7e",
        *BCJ_OPTIONS.get(arch, []),
        "--lzma2",
        "--format=xz",
        "--check=crc64",
        "--force",
    )


# Copied from scriptworker
def get_hash(path, algo="sha512"):
    h = hashlib.new(algo)
    with open(path, "rb") as f:
        for chunk in iter(functools.partial(f.read, 4096), b""):
            h.update(chunk)
    return h.hexdigest()


def _rel_posix(path, start):
    return os.path.relpath(path, start).replace(os.sep, "/")


def _hash_dir(directory):
    """Return {rel_path: sha256_hex} for every file under directory."""
    result = {}
    for root, _, filenames in os.walk(directory):
        for filename in filenames:
            abs_path = os.path.join(root, filename)
            result[_rel_posix(abs_path, directory)] = get_hash(abs_path, "sha256")
    return result


def _extract_cache_mar(patch_cache_dir, target_mar, workdir):
    """Extract the cached partial mar matching target_mar's basename into
    workdir/cache_extracted. Returns the extracted dir on success, or None
    if the cached mar is missing or extraction fails."""
    dest_mar_basename = os.path.basename(target_mar)
    cached_mar = os.path.join(patch_cache_dir, dest_mar_basename)
    if not os.path.exists(cached_mar):
        log(
            f"Cached mar not found: {cached_mar}",
            "_extract_cache_mar",
        )
        return None
    cache_extracted_dir = os.path.join(workdir, "cache_extracted")
    try:
        mar_extract(cached_mar, cache_extracted_dir)
    except Exception as e:
        log(
            f"Failed to extract cached mar {cached_mar}: {e}",
            "_extract_cache_mar",
        )
        return None
    return cache_extracted_dir


def _maybe_use_cached_file(
    cache_entry,
    rel_path,
    local_from_sha,
    local_to_sha,
    cache_extracted_dir,
    partials_dir,
    manifest_file,
):
    """Reuse a precomputed patch/full file from a previously-built partial mar
    when the (from, to) sha256 pair for rel_path matches a cache entry.
    Returns the archivefile name on hit, None on miss."""
    if local_from_sha is None or local_to_sha is None:
        return None
    expected_from = cache_entry.get("from_mar_files", {}).get(rel_path)
    expected_to = cache_entry.get("to_mar_files", {}).get(rel_path)
    if expected_from != local_from_sha or expected_to != local_to_sha:
        return None

    src_patch = os.path.join(cache_extracted_dir, f"{rel_path}.patch")
    src_full = os.path.join(cache_extracted_dir, rel_path)
    try:
        if os.path.exists(src_patch):
            dest = os.path.join(partials_dir, f"{rel_path}.patch")
            os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
            shutil.copy2(src_patch, dest)
            make_patch_instruction(rel_path, manifest_file)
            log(f"Cache hit (patch): {rel_path}", "_maybe_use_cached_file")
            return f"{rel_path}.patch"
        if os.path.exists(src_full):
            # The cached partial mar stored the full file (not a .patch)
            # likely because the patch was bigger than the file itself. Reuse
            # that decision so we don't re-run the diff just to throw it away.
            dest = os.path.join(partials_dir, rel_path)
            os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
            shutil.copy2(src_full, dest)
            make_add_instruction(rel_path, manifest_file)
            log(f"Cache hit (add): {rel_path}", "_maybe_use_cached_file")
            return rel_path
    except OSError as e:
        log(f"Cache copy failed for {rel_path}: {e}", "_maybe_use_cached_file")
        return None
    log(
        f"Cache hashes match but file missing in upstream mar: {rel_path}",
        "_maybe_use_cached_file",
    )
    return None


# The thread-safety of this function should be ok, given that each thread only reads it's own from_mar
#   and the main thread reads the to_mar
@functools.cache
def get_text_from_compressed(path):
    proc = subprocess.run(
        ("xz", "-d", "-c", path),
        capture_output=True,
        text=True,
        check=True,
    )
    return proc.stdout


def get_option_from_compressed(directory, filename, section, option):
    """Gets an option from an XZ compressed config file"""
    log(
        f"Extracting [{section}]: {option} from {directory}/**/{filename}",
        "get_option_from_compressed",
    )
    files = list(glob.glob(f"{directory}/**/{filename}", recursive=True))
    if not files:
        raise Exception(f"Could not find {filename} in {directory}")
    f = files.pop()
    contents = get_text_from_compressed(f)
    config = configparser.ConfigParser()
    config.read_string(contents)
    rv = config.get(section, option)
    log(f"Found {section}.{option}: {rv}", "get_option_from_compressed")
    return rv


def check_for_forced_update(force_list, file_path):
    """Check for files that are forced to update. Note: .chk files are always force updated"""
    # List of files that are always force updated
    always_force_updated = (
        "precomplete",
        "Contents/Resources/precomplete",
        "removed-files",
        "Contents/Resources/removed-files",
        "Contents/CodeResources",
    )
    return (
        file_path in always_force_updated
        or file_path.endswith(".chk")
        or file_path in force_list
    )


def list_files_and_dirs(dir_path):
    files = []
    dirs = []
    for root, directories, filenames in os.walk(dir_path):
        for directory in directories:
            dirs.append(_rel_posix(os.path.join(root, directory), dir_path))
        for filename in filenames:
            files.append(_rel_posix(os.path.join(root, filename), dir_path))
    # Reverse code-point order, matching "sort -r" under LC_ALL=C. It makes the
    # manifest reproducible and puts a directory after each of its children,
    # which RemoveDir needs since it only removes empty directories.
    return sorted(files, reverse=True), sorted(dirs, reverse=True)


def extension_root_dir(filename):
    match = re.match(r"(.*distribution/extensions/[^/]*)/", filename)
    return match.group(1) if match else None


def make_add_instruction(filename, manifest):
    """Adds an instruction to the update manifest file."""
    extension_dir = extension_root_dir(filename)

    if extension_dir is not None:
        with open(manifest, "a") as file:
            file.write(f'add-if "{extension_dir}" "{filename}"\n')
    else:
        with open(manifest, "a") as file:
            file.write(f'add "{filename}"\n')


def check_for_add_if_not_update(filename):
    basename = os.path.basename(filename)
    return (
        basename in {"channel-prefs.js", "update-settings.ini", "distribution.ini"}
        or re.search(r"(^|/)ChannelPrefs\.framework/", filename)
        or re.search(r"(^|/)UpdateSettings\.framework/", filename)
    )


def make_patch_instruction(filename, manifest):
    extension_dir = extension_root_dir(filename)
    with open(manifest, "a") as manifest_file:
        if extension_dir is not None:
            manifest_file.write(
                f'patch-if "{extension_dir}" "{filename}.patch" "{filename}"\n'
            )
        else:
            manifest_file.write(f'patch "{filename}.patch" "{filename}"\n')


def add_remove_instructions(remove_array, manifest):
    with open(manifest, "a") as manifest_file:
        for file in remove_array:
            manifest_file.write(f'remove "{file}"\n')


def make_add_if_not_instruction(filename, manifest):
    with open(manifest, "a") as manifest_file:
        manifest_file.write(f'add-if-not "{filename}" "{filename}"\n')


def append_rmdir_instructions(olddirs, newdirs, manifest):
    newdirs = set(newdirs)
    with open(manifest, "a") as manifest_file:
        for rel_path in olddirs:
            if rel_path not in newdirs:
                manifest_file.write(f'rmdir "{rel_path}/"\n')


def append_remove_instructions(newdir, manifest):
    removed_files_path = os.path.join(newdir, "removed-files")
    if not os.path.isfile(removed_files_path):
        # On mac the file only exists at the bundle-relative location.
        removed_files_path = os.path.join(
            newdir, "Contents", "Resources", "removed-files"
        )
    if os.path.isfile(removed_files_path):
        removed_files = sorted(
            get_text_from_compressed(removed_files_path).splitlines(), reverse=True
        )
        with open(manifest, "a") as manifest_file:
            for line in removed_files:
                entry = line.strip()
                if not entry or entry.startswith("#"):
                    continue
                if entry.endswith("/"):
                    manifest_file.write(f'rmdir "{entry}"\n')
                elif entry.endswith("/*"):
                    manifest_file.write(f'rmrfdir "{entry[:-1]}"\n')
                else:
                    manifest_file.write(f'remove "{entry}"\n')


def get_app_name(mar_dir):
    return get_option_from_compressed(mar_dir, "application.ini", "App", "Name").lower()


def _instruction_member(line):
    """The member path an instruction acts on, which is always its last quoted
    argument."""
    return line.rsplit('"', 2)[-2]


def reorder_instructions(manifest, appname):
    with open(manifest) as manifest_file:
        lines = manifest_file.read().splitlines()

    rank = {
        "dependentlibs.list": -1,
        # Any other member gets 0.
        "xul.dll": 1,
        "browser/omni.ja": 2,
        "omni.ja": 3,
        "mozglue.dll": 4,
        f"{appname}.exe": 5,
    }

    # The sort is stable, so the other instructions keep their order.
    lines[1:] = sorted(
        lines[1:], key=lambda line: rank.get(_instruction_member(line), 0)
    )

    with open(manifest, "w") as manifest_file:
        manifest_file.write("\n".join(lines) + "\n")


def validate_mar_channel_id(mar_path, mar_channel_id):
    """Validate MAR has correct channel id"""
    channel_ids = set(mar_channel_id.split(","))

    product_info = MarReader(open(mar_path, "rb")).productinfo
    if not isinstance(product_info, tuple):
        raise Exception(f"Malformed product info in mar: {mar_path}")

    found_channel_ids = set(product_info[1].split(","))
    if not found_channel_ids.issubset(channel_ids):
        raise Exception(
            f"MAR_CHANNEL_ID mismatch, {product_info[1]} not in {channel_ids}"
        )

    log(f"Validated {mar_path}: channel {product_info[1]} in {mar_channel_id}")
    return True


def mar_extract(source_mar, destination):
    os.makedirs(destination, exist_ok=True)
    cmd = ("mar", "-C", os.path.abspath(destination), "-x", os.path.abspath(source_mar))
    log(f"Running mar extract command: {cmd}", "mar_extract")
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as e:
        log(f"Error extracting mar: {e.stderr}", "mar_extract")
        raise Exception(f"Mar failed with code {e.returncode}")


def xz_cmd(cmd, source_file, destination_file):
    """Run xz command via pipes to avoid file extension checks."""
    os.makedirs(os.path.dirname(destination_file), exist_ok=True)
    with open(destination_file, "wb") as dest_fd, open(source_file, "rb") as source_fd:
        try:
            subprocess.run(("xz", *cmd), stdin=source_fd, stdout=dest_fd, check=True)
        except subprocess.CalledProcessError as e:
            log(
                f"XZ Failure running xz {cmd} on {source_file} to {destination_file}: {e.stderr}",
                "xz_cmd",
            )
            raise Exception(f"XZ exited with code {e.returncode}")


def xz_decompress(scratch_dir, *xz_files):
    """Decompress xz_files into scratch_dir and return the decompressed paths."""
    paths = [os.path.join(scratch_dir, str(i)) for i in range(len(xz_files))]
    for xz_file, path in zip(xz_files, paths):
        xz_cmd(("--decompress",), xz_file, path)
    return paths


def create_patch(old_raw, new_raw, scratch_dir):
    """Create a patch between 2 non-empty decompressed files in scratch_dir and
    return the path of its xz compressed version."""
    patch_raw = os.path.join(scratch_dir, "patch")
    args = ["zucchini", "-gen", old_raw, new_raw, patch_raw]
    try:
        subprocess.run(args, check=True)
    except subprocess.CalledProcessError as e:
        log(f"Zucchini failed to create patch:\n{e.stderr}", "create_patch")
        raise Exception(f"Zucchini exited with code: {e.returncode}")
    patch_xz = f"{patch_raw}.xz"
    xz_cmd(("--compress", "-7", "-e", "-c"), patch_raw, patch_xz)
    return patch_xz


def package_member_in_full(rel_path, new_xz, partials_dir, manifest_file, reason):
    log(f"{reason}, packaging {rel_path} in full", "package_member_in_full")
    make_add_instruction(rel_path, manifest_file)
    shutil.copy2(new_xz, os.path.join(partials_dir, rel_path))
    return rel_path


def package_member(rel_path, old_xz, new_xz, partials_dir, manifest_file):
    """Package the smaller of a patch and the full file for a member that
    differs between old_xz and new_xz. Return the archive file name, or None if
    the decompressed contents are identical."""
    if filecmp.cmp(old_xz, new_xz, shallow=False):
        return None
    with TemporaryDirectory() as scratch_dir:
        # TODO: Potentially don't decompress to_mar files once per thread?
        old_raw, new_raw = xz_decompress(scratch_dir, old_xz, new_xz)
        if filecmp.cmp(old_raw, new_raw, shallow=False):
            return None
        # zucchini fails to generate a patch if either side is empty, by design
        if os.path.getsize(old_raw) == 0 or os.path.getsize(new_raw) == 0:
            return package_member_in_full(
                rel_path, new_xz, partials_dir, manifest_file, "Empty member"
            )
        log(f"{old_xz} -> {rel_path}.patch", "package_member")
        patch_xz = create_patch(old_raw, new_raw, scratch_dir)
        if os.path.getsize(patch_xz) >= os.path.getsize(new_xz):
            return package_member_in_full(
                rel_path, new_xz, partials_dir, manifest_file, "Patch is not smaller"
            )
        shutil.move(patch_xz, os.path.join(partials_dir, f"{rel_path}.patch"))
        make_patch_instruction(rel_path, manifest_file)
        return f"{rel_path}.patch"


def make_partial(
    from_mar_url,
    to_mar_dir,
    target_mar,
    mar_channel_id,
    workdir,
    signing_cert,
    arch="",
    force=None,
    staging=False,
    compute_hashes=False,
    cache_entry=None,
    to_hashes=None,
    patch_cache_dir=None,
):
    # Download from_mar
    from_mar = os.path.join(workdir, "from.mar")
    download_file(from_mar_url, from_mar, staging, signing_cert)

    requested_forced_updates = force or []
    # MacOS firefox binary is always forced update
    requested_forced_updates.append("Contents/MacOS/firefox")
    appname = get_app_name(to_mar_dir)
    manifest_file = os.path.join(workdir, "updatev3.manifest")

    # Holds the relative path to all archive files to be added to the partial
    archivefiles = []

    # Mar extract
    from_mar_dir = os.path.join(workdir, "from_mar")
    mar_extract(from_mar, from_mar_dir)

    # Log current version for easier referencing
    from_version = get_option_from_compressed(
        from_mar_dir, "application.ini", "App", "Version"
    )
    log(f"Processing from_mar: {from_version}", "make_partial")

    from_hashes = _hash_dir(from_mar_dir) if compute_hashes else None

    # Extract upstream enUS partial mar once for cache reuse
    cache_extracted_dir = None
    if cache_entry is not None and patch_cache_dir is not None:
        cache_extracted_dir = _extract_cache_mar(patch_cache_dir, target_mar, workdir)

    partials_dir = os.path.abspath(os.path.join(workdir, "partials"))
    os.makedirs(partials_dir, exist_ok=True)

    # List files and directories
    oldfiles, olddirs = list_files_and_dirs(from_mar_dir)
    newfiles, newdirs = list_files_and_dirs(to_mar_dir)

    for newdir in newdirs:
        os.makedirs(os.path.join(partials_dir, newdir), exist_ok=True)

    # Check if precomplete file exists in the new directory
    if not os.path.exists(
        os.path.join(to_mar_dir, "precomplete")
    ) and not os.path.exists(
        os.path.join(to_mar_dir, "Contents/Resources/precomplete")
    ):
        log("precomplete file is missing!", "make_partial")
        raise Exception("precomplete file is missing!")

    # Create update manifest
    with open(manifest_file, "w") as manifest_fd:
        manifest_fd.write('type "partial"\n')

    remove_array = []

    # Process files for patching
    # Note: these files are already XZ compressed
    for rel_path in oldfiles:
        # updatev3.manifest is the partial manifest, not a patch[able] file
        if rel_path == "updatev3.manifest":
            continue
        new_file_abs = os.path.join(to_mar_dir, rel_path)
        old_file_abs = os.path.join(from_mar_dir, rel_path)

        if os.path.exists(new_file_abs):
            patch_file = os.path.join(partials_dir, rel_path)
            if check_for_add_if_not_update(old_file_abs):
                make_add_if_not_instruction(rel_path, manifest_file)
                shutil.copy2(new_file_abs, patch_file)
                archivefiles.append(rel_path)
            elif check_for_forced_update(requested_forced_updates, rel_path):
                make_add_instruction(rel_path, manifest_file)
                shutil.copy2(new_file_abs, patch_file)
                archivefiles.append(rel_path)
            else:
                archive = None
                if cache_extracted_dir is not None and cache_entry is not None:
                    archive = _maybe_use_cached_file(
                        cache_entry,
                        rel_path,
                        from_hashes[rel_path],
                        to_hashes[rel_path],
                        cache_extracted_dir,
                        partials_dir,
                        manifest_file,
                    )
                if archive is None:
                    archive = package_member(
                        rel_path,
                        old_file_abs,
                        new_file_abs,
                        partials_dir,
                        manifest_file,
                    )
                if archive is not None:
                    archivefiles.append(archive)

        else:
            remove_array.append(rel_path)

    # Newly added files
    for newfile_rel in newfiles:
        new_file_abs = os.path.join(to_mar_dir, newfile_rel)
        if newfile_rel not in oldfiles:
            patch_file = os.path.join(partials_dir, newfile_rel)
            if check_for_add_if_not_update(newfile_rel):
                make_add_if_not_instruction(newfile_rel, manifest_file)
            else:
                make_add_instruction(newfile_rel, manifest_file)
            archivefiles.append(newfile_rel)
            shutil.copy2(new_file_abs, patch_file)

    # Remove files
    add_remove_instructions(remove_array, manifest_file)

    # Add directory removal instructions from removed-files
    append_remove_instructions(to_mar_dir, manifest_file)

    append_rmdir_instructions(olddirs, newdirs, manifest_file)

    reorder_instructions(manifest_file, appname)

    # Compress manifest file and add to list of archived files
    compressed_manifest = os.path.join(partials_dir, "updatev3.manifest")
    xz_cmd(xz_compression_options(arch), manifest_file, compressed_manifest)
    archivefiles.append("updatev3.manifest")

    version = get_option_from_compressed(
        to_mar_dir, "application.ini", "App", "Version"
    )

    log(f"Archive files: {' '.join(archivefiles)}", "make_partial")

    mar_cmd = (
        "mar",
        "-H",
        mar_channel_id,
        "-V",
        version,
        "-c",
        target_mar,
        *archivefiles,
    )
    log(f"Running mar command with: {' '.join(mar_cmd)}", "make_partial")
    try:
        subprocess.run(mar_cmd, cwd=partials_dir, check=True)
    except subprocess.CalledProcessError as e:
        log(f"Error creating mar:\n{e.stderr}")
        raise Exception(f"Mar exited with code {e.returncode}")

    mar_manifest = {
        "MAR_CHANNEL_ID": mar_channel_id,
        "appName": get_option_from_compressed(
            from_mar_dir, filename="application.ini", section="App", option="Name"
        ),
        "from_size": os.path.getsize(from_mar),
        "from_hash": get_hash(from_mar),
        "from_buildid": get_option_from_compressed(
            from_mar_dir, filename="application.ini", section="App", option="BuildID"
        ),
        "mar": os.path.basename(target_mar),
        "size": os.path.getsize(target_mar),
        "from_mar": from_mar_url,
    }
    return mar_manifest, from_hashes


def validate_url(url, allow_staging):
    allowed_url_prefixes = ALLOWED_URL_PREFIXES
    if allow_staging:
        allowed_url_prefixes += STAGING_URL_PREFIXES
    if not url.startswith(allowed_url_prefixes):
        raise Exception(f"URL not allowed: {url}")


def verify_signature(path, signing_cert):
    with open(path, "rb") as fd:
        m = MarReader(fd)
        if not m.verify(verify_key=signing_cert):
            raise Exception(f"MAR Signature invalid: {path}")


def download_file(url, save_path, allow_staging, signing_cert=None):
    """
    Downloads a file from a given URL and saves it to disk.
    Validates URL before proceeding with download.

    Args:
    url (str): The URL to download the file from.
    save_path (str): The path (including filename) where the file should be saved.
    allow_staging (bool): Allows staging URLs
    """
    validate_url(url, allow_staging)
    last_error = None
    for attempt in range(1, DOWNLOAD_ATTEMPTS + 1):
        try:
            # Download the file and save it to the specified path
            urllib.request.urlretrieve(url, save_path)
            log(f"File downloaded successfully: {save_path}", "download_file")
            break
        except Exception as e:
            last_error = e
            log(
                f"Error downloading file (attempt {attempt}/{DOWNLOAD_ATTEMPTS}): {url} -> {e}",
                "download_file",
            )
            if attempt < DOWNLOAD_ATTEMPTS:
                time.sleep(min(DOWNLOAD_SLEEPTIME * attempt, DOWNLOAD_MAX_SLEEPTIME))
    else:
        raise DownloadError(
            f"Failed to download file after {DOWNLOAD_ATTEMPTS} attempts: {url} -> {last_error}"
        )

    if signing_cert:
        verify_signature(save_path, signing_cert)


def cleanup_workdir(workdir):
    try:
        shutil.rmtree(workdir)
    except OSError as e:
        log(f"Could not remove work directory {workdir}: {e}", "cleanup_workdir")


def process_single(
    update_number,
    from_mar_url,
    to_mar_dir,
    target_mar,
    mar_channel_id,
    workdir,
    signing_cert,
    arch,
    force,
    staging,
    previousVersion=None,
    previousBuildNumber=None,
    compute_hashes=False,
    cache_entry=None,
    to_hashes=None,
    patch_cache_dir=None,
):
    try:
        mar_manifest, from_hashes = make_partial(
            from_mar_url=from_mar_url,
            to_mar_dir=to_mar_dir,
            target_mar=target_mar,
            mar_channel_id=mar_channel_id,
            workdir=workdir,
            signing_cert=signing_cert,
            arch=arch,
            force=force,
            staging=staging,
            compute_hashes=compute_hashes,
            cache_entry=cache_entry,
            to_hashes=to_hashes,
            patch_cache_dir=patch_cache_dir,
        )
        mar_manifest["update_number"] = update_number
        if previousVersion:
            mar_manifest["previousVersion"] = previousVersion
        if previousBuildNumber:
            mar_manifest["previousBuildNumber"] = previousBuildNumber
        # Validate the created mar has valid channel id
        if validate_mar_channel_id(target_mar, mar_channel_id):
            # The scratch tree is only kept when something went wrong, for
            # debuggability. Peak disk would otherwise grow with the number of
            # partials.
            cleanup_workdir(workdir)
            return None, mar_manifest, from_hashes
        else:
            # Since we still want to capture the manifest, we return the exception without raising it
            return Exception("Invalid partial MAR id!"), mar_manifest, from_hashes
    except Exception as e:
        log(traceback.format_exc(), "process_single")
        return e, None, None


def main():
    parser = argparse.ArgumentParser(
        description="Generate incremental update packages with zucchini."
    )
    parser.add_argument(
        "--from-mars-json",
        help="JSON data of complete mar URLs",
        action="store",
        required=True,
    )
    parser.add_argument(
        "--branch", help="Project branch", action="store", required=True
    )
    parser.add_argument("--to-mar", help="To complete mar", required=True)
    parser.add_argument(
        "--to-mar-url",
        help="To mar URL. Only used for filling the manifest.json file.",
        action="store",
        required=False,
        default="",
    )
    parser.add_argument("--target", help="Target partial mar location", required=True)
    parser.add_argument(
        "--cert-path",
        help="Certificate path",
        type=argparse.FileType("rb"),
        required=False,
    )
    parser.add_argument(
        "--allow-staging-urls",
        help="Allows downloading from staging URLs",
        action="store_true",
    )
    parser.add_argument(
        "--workdir", help="Work directory", action="store", required=True
    )
    parser.add_argument(
        "--mar-channel-id", help="MAR channel ID", action="store", default="unknown"
    )
    parser.add_argument("--locale", help="Build locale", action="store", required=True)
    parser.add_argument(
        "--arch",
        help="Target Architecture",
        action="store",
        choices=BCJ_OPTIONS.keys(),
        required=True,
    )
    parser.add_argument(
        "--force",
        help="Clobber this file in the installation. Must be a path to a file to clobber in the partial update.",
        action="append",
    )
    parser.add_argument(
        "--generate-hashes",
        help="Write a hashes.json file alongside manifest.json with per-file sha256 metadata. Used by downstream l10n tasks for cache reuse.",
        action="store_true",
    )
    parser.add_argument(
        "--patch-cache-dir",
        help="Directory containing an upstream hashes.json plus the cached partial mars. When set, per-file diffs are reused from the upstream cache when (from, to) sha256 hashes match.",
        action="store",
        default=None,
    )

    args = parser.parse_args()

    base_workdir = os.path.abspath(args.workdir)

    if args.cert_path:
        signing_cert = args.cert_path.read()
        assert get_keysize(signing_cert) == 4096
    else:
        signing_cert = None

    # Multithread one partial per CPU
    cpus = os.cpu_count()  # This isn't optimal, but will do for now
    log(f"CPUs available for parallel computing: {cpus}", "main")

    # Create target directory with locale
    target = os.path.abspath(args.target)
    os.makedirs(target, exist_ok=True)

    # Decompress to_mar early
    to_mar_dir = os.path.join(base_workdir, "to_mar")
    mar_extract(args.to_mar, to_mar_dir)

    # Note: we can't validate "from mar" channels. See bug 1543662 for details.
    # Validate to_mar channel id
    if not validate_mar_channel_id(args.to_mar, args.mar_channel_id):
        log("Invalid MAR channel id in to-mar!", "main")
        raise Exception("Invalid MAR channel id in to-mar!")

    cache_index = {}
    if args.patch_cache_dir:
        cache_path = os.path.join(args.patch_cache_dir, "hashes.json")
        try:
            with open(cache_path) as fd:
                cache_index = json.load(fd)
            log(f"Loaded patch cache index from {cache_path}", "main")
        except (FileNotFoundError, json.JSONDecodeError) as e:
            log(
                f"Could not load patch cache index from {cache_path}: {e}; cache disabled.",
                "main",
            )

    compute_hashes = args.generate_hashes or bool(args.patch_cache_dir)
    to_hashes = _hash_dir(to_mar_dir) if compute_hashes else None

    futures_result = []

    from_data = json.loads(args.from_mars_json)
    assert isinstance(from_data, list), "Param --from-mars-json should be a JSON list."

    with ProcessPoolExecutor(cpus) as executor:
        futures = []
        for from_data_index in range(len(from_data)):
            source_data = from_data[from_data_index]
            update_number = source_data["update_number"]
            process_workdir = os.path.join(base_workdir, str(from_data_index))
            os.makedirs(process_workdir, exist_ok=True)

            target_mar = os.path.join(target, source_data["dest_mar"])

            cache_entry = cache_index.get(source_data["dest_mar"])
            if args.patch_cache_dir and cache_entry is None:
                log(
                    f"No upstream cache entry for {source_data['dest_mar']}; cache disabled for this partial.",
                    "main",
                )

            future = executor.submit(
                process_single,
                update_number=update_number,
                from_mar_url=source_data["url"],
                to_mar_dir=to_mar_dir,
                target_mar=target_mar,
                mar_channel_id=args.mar_channel_id,
                workdir=process_workdir,
                signing_cert=signing_cert,
                arch=args.arch,
                force=args.force,
                staging=args.allow_staging_urls,
                previousVersion=source_data.get("previousVersion"),
                previousBuildNumber=source_data.get("previousBuildNumber"),
                compute_hashes=compute_hashes,
                cache_entry=cache_entry,
                to_hashes=to_hashes,
                patch_cache_dir=args.patch_cache_dir,
            )
            futures.append(future)

        for future in as_completed(futures):
            result = future.result()
            futures_result.append(result)
            # A download that failed every retry is unrecoverable, so abort the
            # whole run rather than letting the remaining partials proceed.
            if isinstance(result[0], DownloadError):
                log(
                    f"Aborting: a download failed and could not be retried: {result[0]}",
                    "main",
                )
                executor.shutdown(wait=False, cancel_futures=True)
                sys.exit(1)

    log("Finished all processes.", "main")

    to_mar_info = {
        "locale": args.locale,
        # Use Gecko repo and rev from platform.ini, not application.ini
        "repo": get_option_from_compressed(
            to_mar_dir,
            filename="platform.ini",
            section="Build",
            option="SourceRepository",
        ),
        "branch": args.branch,
        "revision": get_option_from_compressed(
            to_mar_dir, filename="platform.ini", section="Build", option="SourceStamp"
        ),
        "version": get_option_from_compressed(
            to_mar_dir, filename="application.ini", section="App", option="Version"
        ),
        "to_buildid": get_option_from_compressed(
            to_mar_dir, filename="application.ini", section="App", option="BuildID"
        ),
        "to_hash": get_hash(args.to_mar),
        "to_size": os.stat(args.to_mar).st_size,
        "to_mar": args.to_mar_url,
    }

    errd = False
    results = []
    hashes_output = {}
    for error, manifest, from_hashes in futures_result:
        if manifest:
            manifest.update(to_mar_info)
            results.append(manifest)
            if args.generate_hashes and from_hashes is not None:
                hashes_output[manifest["mar"]] = {
                    "from_mar_files": from_hashes,
                    "to_mar_files": to_hashes,
                }
        else:
            errd = True
            log("Process raised an exception!", "main")
            print(error)

    # Write final task manifest
    with open(os.path.join(target, "manifest.json"), "w") as fd:
        fd.write(json.dumps(results))

    log("Finished writing final manifest.", "main")

    if args.generate_hashes:
        with open(os.path.join(target, "hashes.json"), "w") as fd:
            fd.write(json.dumps(hashes_output))
        log("Finished writing hashes.json.", "main")

    if errd:
        sys.exit(1)


if __name__ == "__main__":
    main()
