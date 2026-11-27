# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import hashlib
import importlib.util
import lzma
import random
import sys
import types
from pathlib import Path
from unittest import mock

import pytest
from mozunit import main

# Pre-populate sys.modules with stub modules for mardor so the script's
# top-level "from mardor.* import ..." statements resolve without needing
# the real mardor package installed in the test environment. This must
# run before the script is loaded below; a fixture would fire too late.
mardor_pkg = types.ModuleType("mardor")
mardor_reader = types.ModuleType("mardor.reader")
mardor_reader.MarReader = object
mardor_signing = types.ModuleType("mardor.signing")
mardor_signing.get_keysize = lambda _cert: 4096
sys.modules.setdefault("mardor", mardor_pkg)
sys.modules.setdefault("mardor.reader", mardor_reader)
sys.modules.setdefault("mardor.signing", mardor_signing)

# tools/update-packaging is a script directory, not a Python package, so
# `import make_incremental_zucchini` wouldn't find it. Load the file by
# absolute path through importlib instead.
SCRIPT_PATH = Path(__file__).resolve().parent.parent / "make_incremental_zucchini.py"
spec = importlib.util.spec_from_file_location("make_incremental_zucchini", SCRIPT_PATH)
mz = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mz)


def _write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def _sha256(path):
    """Compute sha256 of a file via stdlib so test fixtures stay independent
    of the script's own get_hash helper."""
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def test_hash_dir_walks_files(tmp_path):
    _write(tmp_path / "a.txt", b"alpha")
    _write(tmp_path / "sub/b.txt", b"beta")
    _write(tmp_path / "sub/nested/c.txt", b"gamma")

    hashes = mz._hash_dir(str(tmp_path))

    assert hashes == {
        "a.txt": _sha256(tmp_path / "a.txt"),
        "sub/b.txt": _sha256(tmp_path / "sub/b.txt"),
        "sub/nested/c.txt": _sha256(tmp_path / "sub/nested/c.txt"),
    }


def test_hash_dir_empty(tmp_path):
    assert mz._hash_dir(str(tmp_path)) == {}


def _make_cache_setup(tmp_path, rel_path, from_bytes, to_bytes, cache_state):
    """Build a fixture: returns (cache_entry, partials_dir, manifest_file,
    cache_extracted_dir) and writes the local from/to files.
    cache_state is "patch", "full", or "missing"."""
    local_from = tmp_path / "from" / rel_path
    local_to = tmp_path / "to" / rel_path
    _write(local_from, from_bytes)
    _write(local_to, to_bytes)

    cache_extracted_dir = tmp_path / "cache_extracted"
    cache_extracted_dir.mkdir()
    if cache_state == "patch":
        _write(cache_extracted_dir / f"{rel_path}.patch", b"PATCH-BYTES")
    elif cache_state == "full":
        _write(cache_extracted_dir / rel_path, to_bytes)
    # "missing" leaves the cache dir empty

    partials_dir = tmp_path / "partials"
    partials_dir.mkdir()
    manifest_file = tmp_path / "updatev3.manifest"
    manifest_file.write_text("")

    cache_entry = {
        "from_mar_files": {rel_path: _sha256(local_from)},
        "to_mar_files": {rel_path: _sha256(local_to)},
    }
    return (
        cache_entry,
        str(local_from),
        str(local_to),
        str(cache_extracted_dir),
        str(partials_dir),
        str(manifest_file),
    )


def test_cache_lookup_patch_hit(tmp_path):
    rel = "firefox.exe"
    (
        cache_entry,
        local_from,
        local_to,
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    ) = _make_cache_setup(tmp_path, rel, b"OLD", b"NEW", "patch")

    result = mz._maybe_use_cached_file(
        cache_entry,
        rel,
        cache_entry["from_mar_files"][rel],
        cache_entry["to_mar_files"][rel],
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    )

    assert result == f"{rel}.patch"
    assert (Path(partials_dir) / f"{rel}.patch").read_bytes() == b"PATCH-BYTES"
    assert Path(manifest_file).read_text() == f'patch "{rel}.patch" "{rel}"\n'


def test_cache_lookup_full_hit(tmp_path):
    rel = "libxul.so"
    (
        cache_entry,
        local_from,
        local_to,
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    ) = _make_cache_setup(tmp_path, rel, b"OLD", b"NEW", "full")

    result = mz._maybe_use_cached_file(
        cache_entry,
        rel,
        cache_entry["from_mar_files"][rel],
        cache_entry["to_mar_files"][rel],
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    )

    assert result == rel
    assert (Path(partials_dir) / rel).read_bytes() == b"NEW"
    assert Path(manifest_file).read_text() == f'add "{rel}"\n'


def test_cache_lookup_from_hash_miss(tmp_path):
    rel = "firefox.exe"
    (
        cache_entry,
        local_from,
        local_to,
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    ) = _make_cache_setup(tmp_path, rel, b"OLD", b"NEW", "patch")

    result = mz._maybe_use_cached_file(
        cache_entry,
        rel,
        "deadbeef" * 8,
        cache_entry["to_mar_files"][rel],
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    )

    assert result is None
    assert Path(manifest_file).read_text() == ""
    assert not (Path(partials_dir) / f"{rel}.patch").exists()


def test_cache_lookup_to_hash_miss(tmp_path):
    rel = "firefox.exe"
    (
        cache_entry,
        local_from,
        local_to,
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    ) = _make_cache_setup(tmp_path, rel, b"OLD", b"NEW", "patch")

    result = mz._maybe_use_cached_file(
        cache_entry,
        rel,
        cache_entry["from_mar_files"][rel],
        "deadbeef" * 8,
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    )

    assert result is None
    assert Path(manifest_file).read_text() == ""


def test_cache_lookup_relpath_absent_from_index(tmp_path):
    rel = "firefox.exe"
    (
        cache_entry,
        local_from,
        local_to,
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    ) = _make_cache_setup(tmp_path, rel, b"OLD", b"NEW", "patch")

    cache_entry["from_mar_files"].pop(rel)
    cache_entry["to_mar_files"].pop(rel)

    result = mz._maybe_use_cached_file(
        cache_entry,
        rel,
        "deadbeef" * 8,
        "deadbeef" * 8,
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    )

    assert result is None


def test_cache_lookup_hashes_match_but_file_missing(tmp_path):
    rel = "firefox.exe"
    (
        cache_entry,
        local_from,
        local_to,
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    ) = _make_cache_setup(tmp_path, rel, b"OLD", b"NEW", "missing")

    result = mz._maybe_use_cached_file(
        cache_entry,
        rel,
        cache_entry["from_mar_files"][rel],
        cache_entry["to_mar_files"][rel],
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    )

    assert result is None
    assert Path(manifest_file).read_text() == ""


def test_cache_lookup_none_local_hash(tmp_path):
    rel = "firefox.exe"
    (
        cache_entry,
        local_from,
        local_to,
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    ) = _make_cache_setup(tmp_path, rel, b"OLD", b"NEW", "patch")

    result = mz._maybe_use_cached_file(
        cache_entry,
        rel,
        None,
        cache_entry["to_mar_files"][rel],
        cache_extracted_dir,
        partials_dir,
        manifest_file,
    )

    assert result is None


FIXTURE_DIR = Path(__file__).resolve().parent


def test_removed_files_entries_are_classified(tmp_path):
    """Directory and recursive-directory entries get
    rmdir/rmrfdir, and blank and commented lines are skipped."""
    manifest = tmp_path / "updatev3.manifest"

    with mock.patch.object(
        mz, "get_text_from_compressed", lambda p: Path(p).read_text()
    ):
        mz.append_remove_instructions(str(FIXTURE_DIR / "to"), str(manifest))
    lines = manifest.read_text().splitlines()

    assert 'rmdir "dir/"' in lines
    assert 'rmdir "notherdir/"' in lines
    assert 'rmrfdir "recursivedir/meh/"' in lines
    assert 'remove "removed1.txt"' in lines
    assert not [line for line in lines if line == 'remove ""']
    assert not [line for line in lines if "a comment line" in line]


def test_append_remove_instructions_reverse_sorts_entries(tmp_path):
    to_dir = tmp_path / "to"
    _write(to_dir / "removed-files", b"a.txt\nb/\nc.txt\n")
    manifest = tmp_path / "updatev3.manifest"

    with mock.patch.object(
        mz, "get_text_from_compressed", lambda p: Path(p).read_text()
    ):
        mz.append_remove_instructions(str(to_dir), str(manifest))

    assert manifest.read_text().splitlines() == [
        'remove "c.txt"',
        'rmdir "b/"',
        'remove "a.txt"',
    ]


def test_append_remove_instructions_reads_bundle_relative_removed_files(tmp_path):
    """<to>/Contents/Resources/removed-files is used when
    <to>/removed-files is absent, which is the only location on mac."""
    manifest = tmp_path / "updatev3.manifest"

    with mock.patch.object(
        mz, "get_text_from_compressed", lambda p: Path(p).read_text()
    ):
        mz.append_remove_instructions(str(FIXTURE_DIR / "to-mac"), str(manifest))
    lines = manifest.read_text().splitlines()

    assert 'remove "Contents/Resources/removed1.txt"' in lines
    assert 'rmdir "Contents/Resources/dir/"' in lines
    assert 'rmrfdir "Contents/MacOS/recursivedir/meh/"' in lines
    assert not [line for line in lines if "a comment line" in line]


def test_append_remove_instructions_prefers_top_level_removed_files(tmp_path):
    """The fallback must not fire when <to>/removed-files exists."""
    to_dir = tmp_path / "to"
    _write(to_dir / "removed-files", b"top-level.txt\n")
    _write(to_dir / "Contents" / "Resources" / "removed-files", b"bundle.txt\n")
    manifest = tmp_path / "updatev3.manifest"

    with mock.patch.object(
        mz, "get_text_from_compressed", lambda p: Path(p).read_text()
    ):
        mz.append_remove_instructions(str(to_dir), str(manifest))

    assert manifest.read_text().splitlines() == ['remove "top-level.txt"']


def test_instruction_is_conditional_under_extensions(tmp_path):
    """A file under distribution/extensions/<id>/ gets the conditional
    instruction against that subdirectory; anything else gets the plain one."""
    manifest = tmp_path / "updatev3.manifest"
    extension_dir = "distribution/extensions/diff"
    in_extension = f"{extension_dir}/file.txt"
    loose = "distribution/extensions/loose.txt"
    outside = "extensions/diff/file.txt"

    for rel in (in_extension, loose, outside):
        mz.make_add_instruction(rel, str(manifest))
        mz.make_patch_instruction(rel, str(manifest))

    assert manifest.read_text().splitlines() == [
        f'add-if "{extension_dir}" "{in_extension}"',
        f'patch-if "{extension_dir}" "{in_extension}.patch" "{in_extension}"',
        f'add "{loose}"',
        f'patch "{loose}.patch" "{loose}"',
        f'add "{outside}"',
        f'patch "{outside}.patch" "{outside}"',
    ]


def test_check_for_add_if_not_update_matches_common_sh():
    for path in (
        "channel-prefs.js",
        "defaults/pref/channel-prefs.js",
        "update-settings.ini",
        "distribution/distribution.ini",
        "Contents/Resources/distribution/distribution.ini",
        "Contents/Frameworks/ChannelPrefs.framework/ChannelPrefs",
        "Contents/Frameworks/UpdateSettings.framework/UpdateSettings",
    ):
        assert mz.check_for_add_if_not_update(path), path


def test_check_for_add_if_not_update_rejects_other_files():
    for path in (
        "distribution/extensions/foo@mozilla.org.xpi",
        "distribution/policies.json",
        "application.ini",
        "notdistribution.ini",
        "firefox",
    ):
        assert not mz.check_for_add_if_not_update(path), path


def test_list_files_and_dirs_is_reverse_sorted(tmp_path):
    _write(tmp_path / "added.txt", b"a")
    _write(tmp_path / "addFeedPrefs.js", b"b")
    _write(tmp_path / "sub/nested/deep/a.txt", b"c")
    _write(tmp_path / "sub/b.txt", b"d")

    files, dirs = mz.list_files_and_dirs(str(tmp_path))

    # Byte-wise, so "added.txt" sorts above "addFeedPrefs.js" as under LC_ALL=C.
    assert files == [
        "sub/nested/deep/a.txt",
        "sub/b.txt",
        "added.txt",
        "addFeedPrefs.js",
    ]
    # Every directory follows all of its children, which RemoveDir requires.
    assert dirs == ["sub/nested/deep", "sub/nested", "sub"]


def test_append_rmdir_instructions_removes_dirs_no_longer_shipped(tmp_path):
    _write(tmp_path / "from/kept/a.txt", b"a")
    _write(tmp_path / "from/gone/nested/b.txt", b"b")
    _write(tmp_path / "from/gone/c.txt", b"c")
    _write(tmp_path / "to/kept/a.txt", b"a")
    manifest = tmp_path / "updatev3.manifest"

    _, olddirs = mz.list_files_and_dirs(str(tmp_path / "from"))
    _, newdirs = mz.list_files_and_dirs(str(tmp_path / "to"))
    mz.append_rmdir_instructions(olddirs, newdirs, str(manifest))

    assert manifest.read_text().splitlines() == [
        'rmdir "gone/nested/"',
        'rmdir "gone/"',
    ]


def test_download_file_succeeds_first_try(tmp_path):
    url = "https://archive.mozilla.org/firefox.mar"
    dest = str(tmp_path / "out.mar")

    with mock.patch.object(mz.urllib.request, "urlretrieve") as urlretrieve:
        with mock.patch.object(mz.time, "sleep") as sleep:
            mz.download_file(url, dest, allow_staging=False)

    urlretrieve.assert_called_once_with(url, dest)
    sleep.assert_not_called()


def test_download_file_retries_then_succeeds(tmp_path):
    url = "https://archive.mozilla.org/firefox.mar"
    dest = str(tmp_path / "out.mar")

    with mock.patch.object(
        mz.urllib.request,
        "urlretrieve",
        side_effect=[OSError("boom"), OSError("boom"), None],
    ) as urlretrieve:
        with mock.patch.object(mz.time, "sleep") as sleep:
            mz.download_file(url, dest, allow_staging=False)

    assert urlretrieve.call_count == 3
    assert sleep.call_count == 2


def test_download_file_raises_after_exhausting_retries(tmp_path):
    url = "https://archive.mozilla.org/firefox.mar"
    dest = str(tmp_path / "out.mar")

    with mock.patch.object(
        mz.urllib.request,
        "urlretrieve",
        side_effect=OSError("boom"),
    ) as urlretrieve:
        with mock.patch.object(mz.time, "sleep"):
            try:
                mz.download_file(url, dest, allow_staging=False)
            except mz.DownloadError:
                pass
            else:
                raise AssertionError("Expected DownloadError to be raised")

    assert urlretrieve.call_count == mz.DOWNLOAD_ATTEMPTS


def test_package_member_ignores_a_change_of_xz_options(tmp_path):
    contents = b"same contents" * 100
    old_xz = tmp_path / "old.xz"
    old_xz.write_bytes(lzma.compress(contents, preset=1))
    new_xz = tmp_path / "new.xz"
    new_xz.write_bytes(lzma.compress(contents, preset=6))
    assert old_xz.read_bytes() != new_xz.read_bytes()
    partials_dir = tmp_path / "partials"
    partials_dir.mkdir()
    manifest = tmp_path / "updatev3.manifest"

    archive = mz.package_member(
        "a.txt", str(old_xz), str(new_xz), str(partials_dir), str(manifest)
    )

    assert archive is None
    assert not manifest.exists()
    assert list(partials_dir.iterdir()) == []


def test_package_member_does_not_decompress_identical_members(tmp_path):
    xz_bytes = lzma.compress(b"same contents")
    old_xz = tmp_path / "old.xz"
    old_xz.write_bytes(xz_bytes)
    new_xz = tmp_path / "new.xz"
    new_xz.write_bytes(xz_bytes)
    partials_dir = tmp_path / "partials"
    partials_dir.mkdir()
    manifest = tmp_path / "updatev3.manifest"

    with mock.patch.object(mz, "xz_cmd") as xz_cmd:
        archive = mz.package_member(
            "a.txt", str(old_xz), str(new_xz), str(partials_dir), str(manifest)
        )

    assert archive is None
    xz_cmd.assert_not_called()
    assert not manifest.exists()
    assert list(partials_dir.iterdir()) == []


def test_package_member_packages_an_emptied_member_in_full(tmp_path):
    old_xz = tmp_path / "old.xz"
    old_xz.write_bytes(lzma.compress(b"contents"))
    new_xz = tmp_path / "new.xz"
    new_xz.write_bytes(lzma.compress(b""))
    partials_dir = tmp_path / "partials"
    partials_dir.mkdir()
    manifest = tmp_path / "updatev3.manifest"

    archive = mz.package_member(
        "a.txt", str(old_xz), str(new_xz), str(partials_dir), str(manifest)
    )

    assert archive == "a.txt"
    assert manifest.read_text().splitlines() == ['add "a.txt"']
    assert (partials_dir / "a.txt").read_bytes() == new_xz.read_bytes()


def _xz_of_size(size):
    """Generate valid xz data of the given size, which must be a multiple of 4 and at least 60."""
    # Use a deterministic seeded payload on every run. Random bytes don't compress,
    # so the output grows steadily with the payload and hits every valid size.
    payload = random.Random(0).randbytes(size)
    for n in range(size):
        xz_bytes = lzma.compress(payload[:n])
        if len(xz_bytes) == size:
            return xz_bytes
    raise ValueError(f"No xz stream of {size} bytes")


# xz file sizes are always multiples of 4.
@pytest.mark.parametrize(
    "size_delta, expected_archive, expected_instruction",
    [
        (-4, "a.txt.patch", 'patch "a.txt.patch" "a.txt"'),
        (0, "a.txt", 'add "a.txt"'),
        (4, "a.txt", 'add "a.txt"'),
    ],
)
def test_package_member_packages_the_smaller_of_patch_and_full_file(
    tmp_path, size_delta, expected_archive, expected_instruction
):
    old_xz = tmp_path / "old.xz"
    old_xz.write_bytes(lzma.compress(b"old contents"))
    new_xz = tmp_path / "new.xz"
    new_xz.write_bytes(lzma.compress(b"new contents"))
    partials_dir = tmp_path / "partials"
    partials_dir.mkdir()
    manifest = tmp_path / "updatev3.manifest"
    patch_bytes = _xz_of_size(new_xz.stat().st_size + size_delta)

    def fake_create_patch(old_raw, new_raw, scratch_dir):
        patch_xz = Path(scratch_dir) / "patch.xz"
        patch_xz.write_bytes(patch_bytes)
        return str(patch_xz)

    with mock.patch.object(mz, "create_patch", side_effect=fake_create_patch):
        archive = mz.package_member(
            "a.txt", str(old_xz), str(new_xz), str(partials_dir), str(manifest)
        )

    assert archive == expected_archive
    assert manifest.read_text().splitlines() == [expected_instruction]
    assert [p.name for p in partials_dir.iterdir()] == [expected_archive]
    expected_bytes = patch_bytes if size_delta < 0 else new_xz.read_bytes()
    assert (partials_dir / expected_archive).read_bytes() == expected_bytes


def test_reorder_instructions_uses_safer_order(tmp_path):
    manifest = tmp_path / "updatev3.manifest"
    manifest.write_text(
        "\n".join([
            'type "partial"',
            'add "omni.ja"',
            'patch "firefox.exe.patch" "firefox.exe"',
            'add "mozglue.dll"',
            'add "zz.txt"',
            'patch "xul.dll.patch" "xul.dll"',
            'add "dependentlibs.list"',
            'add "aa.txt"',
            'add "browser/omni.ja"',
            'remove "gone.txt"',
        ])
        + "\n"
    )

    mz.reorder_instructions(str(manifest), "firefox")

    assert manifest.read_text().splitlines() == [
        'type "partial"',
        'add "dependentlibs.list"',
        'add "zz.txt"',
        'add "aa.txt"',
        'remove "gone.txt"',
        'patch "xul.dll.patch" "xul.dll"',
        'add "browser/omni.ja"',
        'add "omni.ja"',
        'add "mozglue.dll"',
        'patch "firefox.exe.patch" "firefox.exe"',
    ]


def test_reorder_instructions_buckets_by_path_not_verb(tmp_path):
    """Bash greps on the member path, so a removal of one of the big members
    moves with it and can land after later add instructions. Reproduced
    deliberately."""
    manifest = tmp_path / "updatev3.manifest"
    manifest.write_text('type "partial"\nremove "omni.ja"\nadd "aa.txt"\n')

    mz.reorder_instructions(str(manifest), "firefox")

    assert manifest.read_text().splitlines() == [
        'type "partial"',
        'add "aa.txt"',
        'remove "omni.ja"',
    ]


def test_reorder_instructions_uses_the_app_name(tmp_path):
    manifest = tmp_path / "updatev3.manifest"
    manifest.write_text('type "partial"\nadd "thunderbird.exe"\nadd "zz.txt"\n')

    mz.reorder_instructions(str(manifest), "thunderbird")

    assert manifest.read_text().splitlines() == [
        'type "partial"',
        'add "zz.txt"',
        'add "thunderbird.exe"',
    ]


def test_get_app_name_is_lowercased(tmp_path):
    with mock.patch.object(
        mz, "get_text_from_compressed", lambda p: Path(p).read_text()
    ):
        assert mz.get_app_name(str(FIXTURE_DIR / "to")) == "martest"


def _process_single(tmp_path, make_partial, valid_channel_id=True, **kwargs):
    workdir = tmp_path / "0"
    (workdir / "from_mar").mkdir(parents=True)
    (workdir / "from.mar").write_bytes(b"from")

    with mock.patch.object(mz, "make_partial", make_partial):
        with mock.patch.object(
            mz, "validate_mar_channel_id", lambda *_: valid_channel_id
        ):
            error, mar_manifest, _ = mz.process_single(
                update_number=1,
                from_mar_url="http://ftp.mozilla.org/from.mar",
                to_mar_dir=str(tmp_path / "to_mar"),
                target_mar=str(tmp_path / "target" / "partial.mar"),
                mar_channel_id="test",
                workdir=str(workdir),
                signing_cert=None,
                arch="x86",
                force=None,
                staging=False,
                **kwargs,
            )
    return workdir, error, mar_manifest


def test_process_single_removes_the_workdir_on_success(tmp_path):
    workdir, error, _ = _process_single(tmp_path, lambda **kwargs: ({}, None))

    assert error is None
    assert not workdir.exists()


def test_process_single_keeps_the_workdir_on_failure(tmp_path):
    def failing(**kwargs):
        raise Exception("boom")

    workdir, error, _ = _process_single(tmp_path, failing)

    assert isinstance(error, Exception)
    assert (workdir / "from_mar").is_dir()


def test_process_single_keeps_the_workdir_on_invalid_channel_id(tmp_path):
    workdir, error, _ = _process_single(
        tmp_path, lambda **kwargs: ({}, None), valid_channel_id=False
    )

    assert isinstance(error, Exception)
    assert (workdir / "from_mar").is_dir()


def test_process_single_records_the_previous_build_fields(tmp_path):
    _, error, mar_manifest = _process_single(
        tmp_path,
        lambda **kwargs: ({}, None),
        previousVersion="142.0",
        previousBuildNumber=3,
    )

    assert error is None
    assert mar_manifest == {
        "update_number": 1,
        "previousVersion": "142.0",
        "previousBuildNumber": 3,
    }


if __name__ == "__main__":
    main()
