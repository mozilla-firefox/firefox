# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Implements Auth0 Device Code flow and Lando try submission.

See https://auth0.com/blog/securing-a-python-cli-application-with-auth0/ for more.
"""

from __future__ import annotations

import base64
import configparser
import json
import os
import time
import webbrowser
from dataclasses import (
    dataclass,
    field,
)
from pathlib import Path
from typing import Union

import requests
from mach.util import get_state_dir
from mozbuild.base import MozbuildObject
from mozversioncontrol import (
    GitRepository,
    HgRepository,
    JujutsuRepository,
)

# Instances that keep using the legacy shared token file so existing users
# are not forced to re-authenticate. This was the default instance back when
# the token was stored in a single shared file.
LEGACY_TOKEN_FILE_NAME = "lando_auth0_user_token.json"
LEGACY_TOKEN_FILE_INSTANCES = {"lando-prod-2025"}


def get_token_file(instance_id: str) -> Path:
    """Return the path to the Auth0 token cache file for a Lando instance.

    Tokens are stored per-instance since each Lando instance uses a distinct
    Auth0 audience, so a token issued for one instance is not valid for another.
    """
    state_dir = Path(get_state_dir(specific_to_topsrcdir=False))
    if instance_id in LEGACY_TOKEN_FILE_INSTANCES:
        return state_dir / LEGACY_TOKEN_FILE_NAME
    return state_dir / f"lando_auth0_user_token_{instance_id}.json"


LAUNCH_BROWSER = True

# The supported variants of `Repository` for this workflow.
SupportedVcsRepository = Union[GitRepository, HgRepository, JujutsuRepository]

here = os.path.abspath(os.path.dirname(__file__))
build = MozbuildObject.from_environment(cwd=here)


def convert_bytes_patch_to_base64(patch_bytes: bytes) -> str:
    """Return a base64 encoded `str` representing the passed `bytes` patch."""
    return base64.b64encode(patch_bytes).decode("ascii")


def load_token_from_disk(token_file: Path) -> dict | None:
    """Load and validate an existing Auth0 token from disk.

    Return the token as a `dict` if it can be validated, or return `None`
    if any error was encountered.
    """
    if not token_file.exists():
        print("No existing Auth0 token found.")
        return None

    try:
        user_token = json.loads(token_file.read_bytes())
    except json.JSONDecodeError:
        print("Existing Auth0 token could not be decoded as JSON.")
        return None

    return user_token


def get_stack_info(
    vcs: SupportedVcsRepository, head: str | None
) -> tuple[str, str, list[str]]:
    """Retrieve information about the current stack for submission via Lando.

    Returns a tuple of the current public base commit as a Mercurial SHA,
    and a list of ordered base64 encoded patches.
    """
    # Get the appropriate base commit hash format.
    # Use `git` for Git-native checkouts of Firefox.
    # Use `hg` for Mercurial repos and `git-cinnabar` clones.
    base_commit_vcs = (
        "git" if vcs.name in ("git", "jj") and not vcs.is_cinnabar_repo() else "hg"
    )

    base_commit = (
        vcs.base_ref_as_hg() if base_commit_vcs == "hg" else vcs.base_ref_as_commit()
    )
    if not base_commit:
        raise ValueError("Could not determine base commit hash for submission.")
    print("Using", base_commit, f"as the {base_commit_vcs} base commit.")

    # Reuse the base revision when on Mercurial to avoid multiple calls to `hg log`.
    get_commits_kwargs = {}
    if isinstance(vcs, HgRepository):
        get_commits_kwargs["base_ref"] = base_commit

    nodes = vcs.get_commits(head, **get_commits_kwargs)
    if not nodes:
        raise ValueError("Could not find any commit hashes for submission.")
    elif len(nodes) == 1:
        print("Submitting a single try config commit.")
    elif len(nodes) == 2:
        print("Submitting 1 node and the try commit.")
    else:
        print("Submitting stack of", len(nodes) - 1, "nodes and the try commit.")

    patches = vcs.get_commit_patches(nodes)
    base64_patches = [
        convert_bytes_patch_to_base64(patch_bytes) for patch_bytes in patches
    ]

    return base_commit, base_commit_vcs, base64_patches


@dataclass
class Auth0Config:
    """Helper class to interact with Auth0."""

    domain: str
    client_id: str
    audience: str
    scope: str
    algorithms: list[str] = field(default_factory=lambda: ["RS256"])

    @property
    def base_url(self) -> str:
        """Auth0 base URL."""
        return f"https://{self.domain}"

    @property
    def device_code_url(self) -> str:
        """URL of the Device Code API endpoint."""
        return f"{self.base_url}/oauth/device/code"

    @property
    def issuer(self) -> str:
        """Token issuer URL."""
        return f"{self.base_url}/"

    @property
    def jwks_url(self) -> str:
        """URL of the JWKS file."""
        return f"{self.base_url}/.well-known/jwks.json"

    @property
    def oauth_token_url(self) -> str:
        """URL of the OAuth Token endpoint."""
        return f"{self.base_url}/oauth/token"

    def request_device_code(self) -> dict:
        """Request authorization from Auth0 using the Device Code Flow.

        See https://auth0.com/docs/api/authentication#get-device-code for more.
        """
        response = requests.post(
            self.device_code_url,
            headers={"Content-Type": "application/x-www-form-urlencoded"},
            data={
                "audience": self.audience,
                "client_id": self.client_id,
                "scope": self.scope,
            },
        )

        response.raise_for_status()

        return response.json()

    def validate_token(self, user_token: dict) -> dict | None:
        """Verify the given user token is valid.

        Validate the ID token, and validate the access token's expiration claim.
        """
        # Import `auth0-python` here to avoid `ImportError` in tests, since
        # the `python-test` site won't have `auth0-python` installed.
        import jwt
        from auth0.authentication.token_verifier import (
            AsymmetricSignatureVerifier,
            TokenVerifier,
        )
        from auth0.exceptions import (
            TokenValidationError,
        )

        signature_verifier = AsymmetricSignatureVerifier(self.jwks_url)
        token_verifier = TokenVerifier(
            audience=self.client_id,
            issuer=self.issuer,
            signature_verifier=signature_verifier,
        )

        try:
            token_verifier.verify(user_token["id_token"])
        except TokenValidationError as e:
            if "Expiration Time (exp) claim error" in str(e):
                # This is the most common error, and the default one is very technical
                # and verbose:
                # Could not validate existing Auth0 ID token: Expiration Time (exp)
                # claim error in the ID token; current time (1750343040.7194722) is
                # after expiration time (1699120554)
                # Instead of that mess, print something clear and concise.
                print("Your Auth0 token has expired.")
            else:
                print("Could not validate existing Auth0 ID token:", str(e))
            return None

        decoded_access_token = jwt.decode(
            user_token["access_token"],
            algorithms=self.algorithms,
            options={"verify_signature": False},
        )

        access_token_expiration = decoded_access_token["exp"]

        # Assert that the access token isn't expired or expiring within a minute.
        if time.time() > access_token_expiration + 60:
            print("Access token is expired.")
            return None

        user_token.update(
            jwt.decode(
                user_token["id_token"],
                algorithms=self.algorithms,
                options={"verify_signature": False},
            )
        )
        return user_token

    def device_authorization_flow(self) -> dict:
        """Perform the Device Authorization Flow.

        See https://auth0.com/docs/get-started/authentication-and-authorization-flow/device-authorization-flow
        for more.
        """
        start = time.perf_counter()

        device_code_data = self.request_device_code()
        print(
            "1. On your computer or mobile device navigate to:",
            device_code_data["verification_uri_complete"],
        )
        print("2. Enter the following code:", device_code_data["user_code"])

        auth_msg = f"Auth0 token validation required at: {device_code_data['verification_uri_complete']}"
        build.notify(auth_msg)

        if LAUNCH_BROWSER:
            try:
                webbrowser.open(device_code_data["verification_uri_complete"])
            except webbrowser.Error:
                print("Could not automatically open the web browser.")

        device_code_lifetime_s = device_code_data["expires_in"]

        # Print successive periods on the same line to avoid moving the link
        # while the user is trying to click it.
        print("Waiting...", end="", flush=True)
        while time.perf_counter() - start < device_code_lifetime_s:
            response = requests.post(
                self.oauth_token_url,
                data={
                    "client_id": self.client_id,
                    "device_code": device_code_data["device_code"],
                    "grant_type": "urn:ietf:params:oauth:grant-type:device_code",
                    "scope": self.scope,
                },
            )
            response_data = response.json()

            if response.status_code == 200:
                # Terminate the in-progress "Waiting......" line.
                print()
                return response_data

            if response_data["error"] not in ("authorization_pending", "slow_down"):
                raise RuntimeError(response_data["error_description"])

            time.sleep(device_code_data["interval"])
            print(".", end="", flush=True)

        raise ValueError("Timed out waiting for Auth0 device code authentication!")

    def get_cached_token(self, token_file: Path) -> dict | None:
        """Return the token cached in `token_file` if it can be validated."""
        cached_token = load_token_from_disk(token_file)
        return self.validate_token(cached_token) if cached_token else None

    def login(self, token_file: Path) -> dict:
        """Request a new token with the Device Code Flow and save it to `token_file`."""
        new_token = self.device_authorization_flow()
        user_token = self.validate_token(new_token)

        if not user_token:
            raise ValueError("Could not get an Auth0 token.")

        # Save token to disk.
        with token_file.open("w") as f:
            json.dump(user_token, f, indent=2, sort_keys=True)

        return user_token


class LandoAPIException(Exception):
    """Raised when Lando throws an exception."""

    def __init__(self, detail: str | None = None, status_code: int | None = None):
        super().__init__(detail or "")
        self.status_code = status_code


@dataclass
class LandoAPI:
    """Helper class to interact with Lando-API."""

    access_token: str
    api_url: str
    instance_id: str
    verify_tls: bool = True
    auth0: Auth0Config | None = None
    token_file: Path | None = None
    token_from_cache: bool = False

    @property
    def lando_try_api_url(self) -> str:
        """URL of the Lando Try endpoint."""
        return f"https://{self.api_url}/try/patches"

    def lando_try_status_api_url(self, job_id: int) -> str:
        """URL of the Lando Try Job Status JSON endpoint."""
        return f"https://{self.api_url}/landing_jobs/{job_id}"

    def lando_try_status_url(self, job_id: int) -> str:
        """URL of the Lando Try Job Status HTML endpoint in new Lando."""
        return f"https://{self.api_url}/landings/{job_id}"

    @property
    def api_headers(self) -> dict[str, str]:
        """Headers for use accessing and authenticating against the API."""
        return {
            "Authorization": f"Bearer {self.access_token}",
            "Content-Type": "application/json",
        }

    @classmethod
    def from_lando_config_file(cls, config_path: Path, section: str) -> LandoAPI:
        """Build a `LandoConfig` from `section` in the file at `config_path`."""
        if not config_path.exists():
            raise ValueError(f"Could not find a Lando config file at `{config_path}`.")

        lando_ini_contents = config_path.read_text()

        parser = configparser.ConfigParser(delimiters="=")
        parser.read_string(lando_ini_contents)

        if not parser.has_section(section):
            raise ValueError(f"Lando config file does not have a {section} section.")

        auth0 = Auth0Config(
            domain=parser.get(section, "auth0_domain"),
            client_id=parser.get(section, "auth0_client_id"),
            audience=parser.get(section, "auth0_audience"),
            scope=parser.get(section, "auth0_scope"),
        )

        instance_id = parser.get(section, "instance_id", fallback=section)
        token_file = get_token_file(instance_id)
        token = auth0.get_cached_token(token_file)
        token_from_cache = token is not None
        if not token:
            token = auth0.login(token_file)

        return LandoAPI(
            api_url=parser.get(section, "api_domain"),
            access_token=token["access_token"],
            instance_id=instance_id,
            verify_tls=parser.getboolean(section, "verify_tls", fallback=True),
            auth0=auth0,
            token_file=token_file,
            token_from_cache=token_from_cache,
        )

    def post(self, url: str, body: dict) -> dict:
        """Make a POST request to Lando.

        If Lando refuses the request, the token may predate a change
        in the user's access, so discard it. If it came from the cache, log in
        again and retry.
        """
        try:
            return self._post(url, body)
        except LandoAPIException as exc:
            if exc.status_code != 403:
                raise
            if self.token_file:
                self.token_file.unlink(missing_ok=True)
            if not (self.auth0 and self.token_file and self.token_from_cache):
                raise
            print(f"{exc}\nRe-authenticating to refresh your Lando permissions.")
            token = self.auth0.login(self.token_file)
            self.access_token = token["access_token"]
            self.token_from_cache = False
            return self.post(url, body)

    def _post(self, url: str, body: dict) -> dict:
        response = requests.post(
            url, headers=self.api_headers, json=body, verify=self.verify_tls
        )

        try:
            response_json = response.json()
        except json.JSONDecodeError:
            # The server didn't send back a valid JSON object. This commonly
            # happens on transient server-side errors (e.g. a 503 returns an
            # HTML error page rather than JSON). Surface a readable message
            # rather than letting a raw `HTTPError` traceback escape.
            if response.status_code >= 400:
                detail = (
                    f"Lando returned HTTP {response.status_code} "
                    f"({response.reason}) for {url}."
                )
                if response.status_code >= 500:
                    detail += (
                        " This is likely a transient server-side issue; "
                        "please try again in a few moments."
                    )
                raise LandoAPIException(detail=detail, status_code=response.status_code)

            # The response wasn't JSON but the status was valid.
            raise LandoAPIException(
                detail="Response was not valid JSON yet status was valid."
            )

        if response.status_code >= 400:
            raise LandoAPIException(
                detail=response_json["detail"], status_code=response.status_code
            )

        return response_json

    def post_try_push_patches(
        self,
        patches: list[str],
        patch_format: str,
        base_commit: str,
        base_commit_vcs: str,
        repo_name: str = "try",
    ) -> dict:
        """Send try push contents to Lando.

        Send the list of base64-encoded `patches` in `patch_format` to Lando, to be applied to
        the Mercurial `base_commit`, using the Auth0 `access_token` for authorization.
        """
        request_json_body = {
            "repo_name": repo_name,
            "base_commit": base_commit,
            "base_commit_vcs": base_commit_vcs,
            "patch_format": patch_format,
            "patches": patches,
        }

        response_json = self.post(self.lando_try_api_url, request_json_body)

        return response_json


def push_to_lando_try(
    vcs: SupportedVcsRepository,
    commit_message: str,
    changed_files: dict,
    metrics,
    *,
    force_old_lando: bool = False,
    repo_name: str = "try",
):
    """Push a set of patches to Lando's try endpoint."""

    metrics.mach_try.vcs_prep.start()
    # Map `Repository` subclasses to the `patch_format` value Lando expects.
    PATCH_FORMAT_STRING_MAPPING = {
        GitRepository: "git-format-patch",
        HgRepository: "hgexport",
        JujutsuRepository: "git-format-patch",
    }
    patch_format = PATCH_FORMAT_STRING_MAPPING.get(type(vcs))
    if not patch_format:
        # Other VCS types (namely `src`) are unsupported.
        raise ValueError(f"Try push via Lando is not supported for `{vcs.name}`.")

    # Load Auth0 config from `.lando.ini`.
    lando_api = get_lando_api_config(vcs)

    # Get the time when the push was initiated, not including Auth0 login time.
    push_start_time = time.perf_counter()

    head, cleanup_fn = vcs.prepare_try_push(commit_message, changed_files)

    def cleanup():
        metrics.mach_try.vcs_cleanup.start()
        cleanup_fn()
        metrics.mach_try.vcs_cleanup.stop()

    try:
        base_commit, base_commit_vcs, patches = get_stack_info(vcs, head)
    except ValueError as exc:
        metrics.mach_try.vcs_prep.stop()
        error_msg = "abort: error gathering patches for submission."
        print(error_msg)
        print(str(exc))
        build.notify(error_msg)
        cleanup()
        return

    metrics.mach_try.vcs_prep.stop()
    try:
        metrics.mach_try.vcs_push.start()
        # Make the try request to Lando.
        response_json = lando_api.post_try_push_patches(
            patches,
            patch_format,
            base_commit,
            base_commit_vcs,
            repo_name=repo_name,
        )
    except LandoAPIException as exc:
        metrics.mach_try.vcs_push.stop()
        error_msg = "abort: error submitting patches to Lando."
        print(error_msg)
        print(str(exc))
        build.notify(error_msg)
        cleanup()
        return

    metrics.mach_try.vcs_push.stop()
    cleanup()
    duration = time.perf_counter() - push_start_time

    job_id = response_json["id"]

    # Send a notification only if the push took an unexpectedly long time
    if duration > 30:
        build.notify(f"try submission success in {duration:.1f}s")

    return {
        "lando_instance": lando_api.instance_id,
        "lando_job_id": job_id,
        "duration": duration,
    }


def get_lando_instance_id(
    vcs: SupportedVcsRepository, section_name: str | None = None
) -> str:
    """Return the lando instance ID from the given config section, with default."""
    lando_api = get_lando_api_config(vcs, section_name)
    return lando_api.instance_id


def get_lando_api_config(
    vcs: SupportedVcsRepository, section_name: str | None = None
) -> LandoAPI:
    """Initialise a LandoAPI object from the .lando.ini for the given section_name"""
    lando_ini_path = Path(vcs.path) / ".lando.ini"
    if not lando_ini_path.exists():
        # In comm-central, `tools/` is under the `mozilla/`
        # subdirectory, while the VCS root is under `comm/`
        lando_ini_path = Path(vcs.path) / ".." / ".lando.ini"

    section_name = section_name or get_lando_config_section_name(vcs)

    return LandoAPI.from_lando_config_file(lando_ini_path, section_name)


def get_lando_config_section_name(vcs: SupportedVcsRepository) -> str:
    """Determine which lando config section to use.

    This is based on defaults and overrides such as the LANDO_TRY_CONFIG env variable.

    The new Lando does not support pushing to try from a Mercurial checkout, so
    Mercurial repos default to the old `lando-prod` instance.
    """
    if "LANDO_TRY_CONFIG" in os.environ:
        return os.environ["LANDO_TRY_CONFIG"]

    if vcs.name == "hg":
        raise LandoAPIException(
            "Mercurial is not supported for Try pushes via Lando.\n"
            + "Please migrate your clone to git.\n"
            + "Please see https://bit.ly/fx-git-migration for guidance on how to migrate."
        )

    return "lando-prod"
