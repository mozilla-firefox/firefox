# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import importlib.util
import os
import sys
from pathlib import Path

MACH_METRICS_PATH = (Path(__file__) / ".." / ".." / "metrics.yaml").resolve()
SITE_DIR = (Path(__file__) / ".." / ".." / ".." / "sites").resolve()


def create_telemetry_from_environment(settings):
    """Creates and a Telemetry instance based on system details.

    If telemetry isn't enabled or Glean can't be imported, then a "mock" telemetry
    instance is returned that doesn't set or record any data. This allows consumers
    to optimistically set telemetry data without needing to specifically handle the
    case where the current system doesn't support it.
    """

    from mach.site import MozSiteMetadata
    from mach.telemetry_interface import GleanTelemetry, NoopTelemetry
    from mach.util import get_state_dir

    active_metadata = MozSiteMetadata.from_runtime()
    mach_sites = [site_path.stem for site_path in SITE_DIR.glob("*.txt")]
    is_a_mach_virtualenv = active_metadata and active_metadata.site_name in mach_sites

    if not (
        is_applicable_telemetry_environment()
        # If not using a mach virtualenv (e.g.: bootstrap uses native python)
        # then we can't guarantee that the glean package that we import is a
        # compatible version. Therefore, don't use glean.
        and is_a_mach_virtualenv
    ):
        return NoopTelemetry(False)

    is_enabled = is_telemetry_enabled(settings)

    if importlib.util.find_spec("glean") is None:
        return NoopTelemetry(is_enabled)

    # We must initialize Glean even if telemetry is disabled to ensure a deletion ping is sent.
    # This deletes any previously collected data if the user opts out of telemetry.
    telemetry_interface = GleanTelemetry(
        upload_enabled=is_enabled, data_dir=Path(get_state_dir()) / "glean"
    )

    return telemetry_interface


def report_invocation_metrics(telemetry, command):
    from mozbuild.base import BuildEnvironmentNotFoundException, MozbuildObject
    from mozbuild.telemetry import filter_args

    metrics = telemetry.metrics(MACH_METRICS_PATH)
    metrics.mach.command.set(command)

    try:
        instance = MozbuildObject.from_environment()
    except BuildEnvironmentNotFoundException:
        # Mach may be invoked with the state dir as the current working
        # directory, in which case we're not able to find the topsrcdir (so
        # we can't create a MozbuildObject instance).
        # Without this information, we're unable to filter argv paths, so
        # we skip submitting them to telemetry.
        return
    metrics.mach.argv.set(
        filter_args(command, sys.argv, instance.topsrcdir, instance.topobjdir)
    )


def is_applicable_telemetry_environment():
    if os.environ.get("MACH_MAIN_PID") != str(os.getpid()):
        # This is a child mach process. Since we're collecting telemetry for the parent,
        # we don't want to collect telemetry again down here.
        return False

    return True


def is_telemetry_enabled(settings):
    if os.environ.get("DISABLE_TELEMETRY") == "1":
        return False

    return settings.mach_telemetry.is_enabled


def arcrc_path():
    if sys.platform.startswith("win32") or sys.platform.startswith("msys"):
        return Path(os.environ.get("APPDATA", "")) / ".arcrc"
    else:
        return Path("~/.arcrc").expanduser()


def resolve_setting_from_arcconfig(topsrcdir: Path, setting):
    import subprocess

    from mozfile import json

    git_path = topsrcdir / ".git"
    if git_path.is_file():
        git_path = subprocess.check_output(
            ["git", "rev-parse", "--git-common-dir"],
            cwd=str(topsrcdir),
            universal_newlines=True,
        ).rstrip()
        git_path = Path(git_path)

    for arcconfig_path in [
        topsrcdir / ".hg" / ".arcconfig",
        git_path / ".arcconfig",
        topsrcdir / ".arcconfig",
    ]:
        try:
            with open(arcconfig_path) as arcconfig_file:
                arcconfig = json.load(arcconfig_file)
        except (json.JSONDecodeError, FileNotFoundError):
            continue

        value = arcconfig.get(setting)
        if value:
            return value


def resolve_is_employee_by_credentials(topsrcdir: Path):
    import urllib.parse as urllib_parse

    import requests
    from mozfile import json

    try:
        phabricator_uri = resolve_setting_from_arcconfig(topsrcdir, "phabricator.uri")

        if not phabricator_uri:
            return None

        with arcrc_path().open() as arcrc_file:
            arcrc = json.load(arcrc_file)

        phabricator_token = (
            arcrc
            .get("hosts", {})
            .get(urllib_parse.urljoin(phabricator_uri, "api/"), {})
            .get("token")
        )

        if not phabricator_token:
            return None

        bmo_uri = (
            resolve_setting_from_arcconfig(topsrcdir, "bmo_url")
            or "https://bugzilla.mozilla.org"
        )
        bmo_api_url = urllib_parse.urljoin(bmo_uri, "rest/whoami")
        bmo_result = requests.get(
            bmo_api_url, headers={"X-PHABRICATOR-TOKEN": phabricator_token}
        )

        return "mozilla-employee-confidential" in bmo_result.json().get("groups", [])
    except (
        FileNotFoundError,
        json.JSONDecodeError,
        requests.exceptions.RequestException,
    ):
        return None


def resolve_is_employee_by_vcs(topsrcdir: Path):
    from mozversioncontrol import InvalidRepoPath, get_repository_object

    try:
        vcs = get_repository_object(str(topsrcdir))
    except InvalidRepoPath:
        return None

    email = vcs.get_user_email()
    if not email:
        return None

    return "@mozilla.com" in email


def resolve_is_employee(topsrcdir: Path, settings):
    """Detect whether the current user is a Mozilla employee.

    Checks using Bugzilla authentication, if possible. Otherwise, falls back to checking
    if email configured in VCS is "@mozilla.com". The result is cached after the first
    check to avoid repeated API calls.

    Returns True if the user could be identified as an employee, False if the user
    is confirmed as not being an employee, or None if the user couldn't be identified.
    """
    if os.environ.get("MOZ_AUTOMATION"):
        return False

    if settings.mach_telemetry.is_employee is not None:
        return settings.mach_telemetry.is_employee

    is_employee_by_creds = resolve_is_employee_by_credentials(topsrcdir)
    if is_employee_by_creds is not None:
        record_is_employee_telemetry_setting(settings, is_employee_by_creds)
        return is_employee_by_creds

    is_employee_by_vcs = resolve_is_employee_by_vcs(topsrcdir)
    if is_employee_by_vcs is not None:
        record_is_employee_telemetry_setting(settings, is_employee_by_vcs)
        return is_employee_by_vcs

    return None


def record_is_employee_telemetry_setting(settings, is_employee):
    """Records the is_employee field in the settings file."""
    import configparser

    from mach.config import ConfigSettings
    from mach.settings import MachSettings
    from mach.util import get_global_machrc_path

    settings_path = get_global_machrc_path()
    file_settings = ConfigSettings()
    file_settings.register_provider(MachSettings)

    try:
        file_settings.load_file(settings_path)
    except FileNotFoundError:
        # File doesn't exist yet, we'll create it
        pass
    except configparser.Error as error:
        print(
            f"mach configuration file at `{settings_path}` cannot be parsed:\n{error}"
        )
        raise

    file_settings.mach_telemetry.is_employee = is_employee

    with settings_path.open("w") as f:
        file_settings.write(f)

    settings.mach_telemetry.is_employee = is_employee


def record_telemetry_settings(
    main_settings,
    is_enabled,
):
    import configparser

    from mach.config import ConfigSettings
    from mach.settings import MachSettings
    from mach.util import get_global_machrc_path

    # We want to update the user's machrc file. However, the main settings object
    # contains config from "$topsrcdir/machrc" (if it exists) which we don't want
    # to accidentally include. So, we have to create a brand new mozbuild-specific
    # settings, update it, then write to it.
    settings_path = get_global_machrc_path()
    file_settings = ConfigSettings()
    file_settings.register_provider(MachSettings)
    try:
        file_settings.load_file(settings_path)
    except configparser.Error as error:
        print(
            f"Your mach configuration file at `{settings_path}` cannot be parsed:\n{error}"
        )
        return

    file_settings.mach_telemetry.is_enabled = is_enabled
    file_settings.mach_telemetry.is_set_up = True

    with open(settings_path, "w") as f:
        file_settings.write(f)

    # Telemetry will want this elsewhere in the mach process, so we'll slap the
    # new values on the main settings object.
    main_settings.mach_telemetry.is_enabled = is_enabled
    main_settings.mach_telemetry.is_set_up = True


TELEMETRY_DESCRIPTION_PREAMBLE = """
Mozilla collects data to improve the developer experience.
To learn more about the data we intend to collect, read here:
  https://firefox-source-docs.mozilla.org/build/buildsystem/telemetry.html
If you have questions, please ask in #build on Matrix:
  https://chat.mozilla.org/#/room/#build:mozilla.org
""".strip()


def print_telemetry_message_employee():
    from textwrap import dedent

    message_template = dedent(
        """
    %s

    As a Mozilla employee, telemetry has been automatically enabled.
    """
    ).strip()
    print(message_template % TELEMETRY_DESCRIPTION_PREAMBLE)
    return True


def prompt_telemetry_message_contributor():
    from textwrap import dedent

    while True:
        prompt = (
            dedent(
                """
        %s

        If you'd like to opt out of data collection, select (N) at the prompt.
        Would you like to enable build system telemetry? (Yn): """
            )
            % TELEMETRY_DESCRIPTION_PREAMBLE
        ).strip()

        choice = input(prompt)
        choice = choice.strip().lower()
        if choice == "":
            return True
        if choice not in ("y", "n"):
            print("ERROR! Please enter y or n!")
        else:
            return choice == "y"


def initialize_telemetry_setting(settings, topsrcdir: str):
    """Enables telemetry for employees or prompts the user."""
    # If the user doesn't care about telemetry for this invocation, then
    # don't make requests to Bugzilla and/or prompt for whether the
    # user wants to opt-in.

    if topsrcdir is not None:
        topsrcdir = Path(topsrcdir)

    if os.environ.get("DISABLE_TELEMETRY") == "1":
        return

    is_employee = resolve_is_employee(topsrcdir, settings)

    if is_employee:
        is_enabled = True
        print_telemetry_message_employee()
    else:
        is_enabled = prompt_telemetry_message_contributor()

    record_telemetry_settings(settings, is_enabled)
