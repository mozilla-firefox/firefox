# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import os
import shutil
import subprocess
import sys

import mozunit
import pytest

from mozbuild.action.tooltool import clean_path


def test_clean_path_removes_leftovers(tmp_path):
    toolchain = tmp_path / "toolchain"
    toolchain.mkdir()
    (toolchain / "file").touch()
    (tmp_path / "toolchain.old-leftover").mkdir()

    clean_path(str(toolchain))

    assert list(tmp_path.iterdir()) == []


@pytest.mark.skipif(sys.platform != "win32", reason="Windows file locking")
@pytest.mark.parametrize("directory_rename_fails", (False, True))
def test_clean_path_with_running_executable(
    tmp_path, monkeypatch, directory_rename_fails
):
    toolchain = tmp_path / "toolchain"
    toolchain.mkdir()
    exe = toolchain / "cmd.exe"
    shutil.copy(os.environ.get("COMSPEC", r"C:\Windows\System32\cmd.exe"), exe)
    proc = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL)
    try:
        with pytest.raises(PermissionError):
            exe.unlink()

        real_rename = os.rename

        def rename(src, dst):
            if directory_rename_fails and src == str(toolchain):
                raise PermissionError
            real_rename(src, dst)

        monkeypatch.setattr(os, "rename", rename)
        clean_path(str(toolchain))

        assert not toolchain.exists()
        if directory_rename_fails:
            leftovers = [p.name for p in tmp_path.glob("toolchain.old-*/*")]
            assert leftovers == ["0-cmd.exe"]
    finally:
        proc.kill()
        proc.wait()
        proc.stdin.close()


if __name__ == "__main__":
    mozunit.main()
