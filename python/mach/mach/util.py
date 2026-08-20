# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import hashlib
import os
import sys
from pathlib import Path, PurePosixPath
from typing import Optional, Union


class UserError(Exception):
    """Represents an error caused by something the user did wrong rather than
    an internal `mach` failure. Exceptions that are subclasses of this class
    will not be reported as failures to Sentry.
    """


def setenv(key, value):
    """Compatibility shim to ensure the proper string type is used with
    os.environ for the version of Python being used.
    """
    encoding = "mbcs" if sys.platform == "win32" else "utf-8"

    if isinstance(key, bytes):
        key = key.decode(encoding)
    if isinstance(value, bytes):
        value = value.decode(encoding)

    os.environ[key] = value


def get_state_dir(
    specific_to_topsrcdir=False, topsrcdir: Optional[Union[str, Path]] = None
):
    """Obtain path to a directory to hold state.

    Args:
        specific_to_topsrcdir (bool): If True, return a state dir specific to the current
            srcdir instead of the global state dir (default: False)

    Returns:
        A path to the state dir (str)
    """
    state_dir = Path(os.environ.get("MOZBUILD_STATE_PATH", Path.home() / ".mozbuild"))
    if not specific_to_topsrcdir:
        return str(state_dir)

    if not topsrcdir:
        # Only import MozbuildObject if topsrcdir isn't provided. This is to cover
        # the Mach initialization stage, where "mozbuild" isn't in the import scope.
        from mozbuild.base import MozbuildObject

        topsrcdir = Path(
            MozbuildObject.from_environment(cwd=str(Path(__file__).parent)).topsrcdir
        )

    # Ensure that the topsrcdir is a consistent string before hashing it.
    topsrcdir = Path(topsrcdir).resolve()

    # Shortening to 12 characters makes these directories a bit more manageable
    # in a terminal and is more than good enough for this purpose.
    srcdir_hash = hashlib.sha256(str(topsrcdir).encode("utf-8")).hexdigest()[:12]

    state_dir = state_dir / "srcdirs" / f"{topsrcdir.name}-{srcdir_hash}"

    if not state_dir.is_dir():
        # We create the srcdir here rather than 'mach_initialize.py' so direct
        # consumers of this function don't create the directory inconsistently.
        print(f"Creating local state directory: {state_dir}")
        state_dir.mkdir(mode=0o770, parents=True)
        # Save the topsrcdir that this state dir corresponds to so we can clean
        # it up in the event its srcdir was deleted.
        with (state_dir / "topsrcdir.txt").open(mode="w") as fh:
            fh.write(str(topsrcdir))

    return str(state_dir)


MACHRC_NAMES = ("machrc", ".machrc")


def get_global_machrc_path():
    """Return the path of the global machrc file, which may not exist yet.

    This is "$state_dir/machrc", unless MACHRC already resolves to an existing
    file, in which case that is the file mach reads settings from and thus the
    one to write them back to. MACHRC is resolved the same way as when loading
    settings: it either names the file itself, or a directory containing a
    "machrc" or ".machrc".
    """
    machrc = os.environ.get("MACHRC")
    if machrc:
        machrc = Path(machrc)
        if machrc.is_file():
            return machrc
        for name in MACHRC_NAMES:
            if (machrc / name).is_file():
                return machrc / name

    return Path(get_state_dir()) / "machrc"


def get_virtualenv_base_dir(topsrcdir):
    return os.path.join(
        get_state_dir(specific_to_topsrcdir=True, topsrcdir=topsrcdir),
        "_virtualenvs",
    )


def win_to_msys_path(path: Path):
    """Convert a windows-style path to msys-style."""
    drive, path = os.path.splitdrive(path)
    path = "/".join(path.split("\\"))
    if drive:
        if path[0] == "/":
            path = path[1:]
        path = f"/{drive[:-1]}/{path}"
    return PurePosixPath(path)


def to_optional_path(path: Optional[Path]):
    if path:
        return Path(path)
    else:
        return None


# CreateProcess on Windows rejects command lines longer than 32767 characters,
# including the executable path. Leave headroom for the tool path and for the
# subcommand arguments that precede the paths.
MAX_COMMAND_LINE_LENGTH = 31000


def batch_paths(paths: list[Union[str, Path]], budget: int = MAX_COMMAND_LINE_LENGTH):
    if not paths:
        return

    if sys.platform != "win32":
        yield paths
        return

    batch = []
    used = 0
    for path in paths:
        # Account for the separator and for possible quoting around the path.
        size = len(str(path)) + 3
        if batch and used + size > budget:
            yield batch
            batch, used = [], 0
        batch.append(path)
        used += size
    if batch:
        yield batch


def to_optional_str(path: Optional[Path]):
    if path:
        return str(path)
    else:
        return None


def strtobool(value: str):
    # Reimplementation of distutils.util.strtobool
    # https://docs.python.org/3.9/distutils/apiref.html#distutils.util.strtobool
    true_vals = ("y", "yes", "t", "true", "on", "1")
    false_vals = ("n", "no", "f", "false", "off", "0")

    value = value.lower()
    if value in true_vals:
        return 1
    if value in false_vals:
        return 0

    raise ValueError(f"Expected one of: {', '.join(true_vals + false_vals)}")
