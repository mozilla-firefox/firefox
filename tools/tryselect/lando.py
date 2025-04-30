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
from typing import (
    List,
    Optional,
    Tuple,
    Union,
)

import requests
from mach.util import get_state_dir
from mozbuild.base import MozbuildObject
from mozversioncontrol import (
    GitRepository,
    HgRepository,
    JujutsuRepository,
)

TOKEN_FILE = (
    Path(get_state_dir(specific_to_topsrcdir=False)) / "lando_auth0_user_token.json"
)

# The supported variants of `Repository` for this workflow.
SupportedVcsRepository = Union[GitRepository, HgRepository, JujutsuRepository]

here = os.path.abspath(os.path.dirname(__file__))
build = MozbuildObject.from_environment(cwd=here)


def convert_bytes_patch_to_base64(patch_bytes: bytes) -> str:
    """Return a base64 encoded `str` representing the passed `bytes` patch."""
    return base64.b64encode(patch_bytes).decode("ascii")


def load_token_from_disk() -> Optional[dict]:
    """Load and validate an existing Auth0 token from disk.

    Return the token as a `dict` if it can be validated, or return `None`
    if any error was encountered.
    """
    if not TOKEN_FILE.exists():
        print("No existing Auth0 token found.")
        return None

    try:
        user_token = json.loads(TOKEN_FILE.read_bytes())
    except json.JSONDecodeError:
        print("Existing Auth0 token could not be decoded as JSON.")
        return None

    return user_token


def get_stack_info(
    vcs: SupportedVcsRepository, head: Optional[str]
) -> Tuple[str, List[str]]:
    """Retrieve information about the current stack for submission via Lando.

    Returns a tuple of the current public base commit as a Mercurial SHA,
    and a list of ordered base64 encoded patches.
    """
    base_commit = vcs.base_ref_as_hg()
    if not base_commit:
        raise ValueError(
            "Could not determine base Mercurial commit hash for submission."
        )
    print("Using", base_commit, "as the hg base commit.")

    # Reuse the base revision when on Mercurial to avoid multiple calls to `hg log`.
    branch_nodes_kwargs = {}
    if isinstance(vcs, HgRepository):
        branch_nodes_kwargs["base_ref"] = base_commit

    nodes = vcs.get_branch_nodes(head, **branch_nodes_kwargs)
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
    print("Patches gathered for submission.")

    return base_commit, base64_patches


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

    def validate_token(self, user_token: dict) -> Optional[dict]:
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
        print("Auth0 token validated.")
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
                print("\nLogin successful.")
                return response_data

            if response_data["error"] not in ("authorization_pending", "slow_down"):
                raise RuntimeError(response_data["error_description"])

            time.sleep(device_code_data["interval"])
            print(".", end="", flush=True)

        raise ValueError("Timed out waiting for Auth0 device code authentication!")

    def get_token(self) -> dict:
        """Retrieve an access token for authentication.

        If a cached token is found and can be confirmed to be valid, return it.
        Otherwise, perform the Device Code Flow authorization to request a new
        token, validate it and save it to disk.
        """
        # Load a cached token and validate it if one is available.
        cached_token = load_token_from_disk()
        user_token = self.validate_token(cached_token) if cached_token else None

        # Login with the Device Authorization Flow if an existing token isn't found.
        if not user_token:
            new_token = self.device_authorization_flow()
            user_token = self.validate_token(new_token)

        if not user_token:
            raise ValueError("Could not get an Auth0 token.")

        # Save token to disk.
        with TOKEN_FILE.open("w") as f:
            json.dump(user_token, f, indent=2, sort_keys=True)

        return user_token


class LandoAPIException(Exception):
    """Raised when Lando throws an exception."""

    def __init__(self, detail: Optional[str] = None):
        super().__init__(detail or "")


@dataclass
class LandoAPI:
    """Helper class to interact with Lando-API."""

    access_token: str
    api_url: str

    @property
    def lando_try_api_url(self) -> str:
        """URL of the Lando Try endpoint."""
        return f"https://{self.api_url}/try/patches"

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

        token = auth0.get_token()

        return LandoAPI(
            api_url=parser.get(section, "api_domain"),
            access_token=token["access_token"],
        )

    def post(self, url: str, body: dict) -> dict:
        """Make a POST request to Lando."""
        response = requests.post(url, headers=self.api_headers, json=body)

        try:
            response_json = response.json()
        except json.JSONDecodeError:
            # If the server didn't send back a valid JSON object, raise a stack
            # trace to the terminal which includes error details.
            response.raise_for_status()

            # Raise `ValueError` if the response wasn't JSON and we didn't raise
            # from an invalid status.
            raise LandoAPIException(
                detail="Response was not valid JSON yet status was valid."
            )

        if response.status_code >= 400:
            raise LandoAPIException(detail=response_json["detail"])

        return response_json

    def post_try_push_patches(
        self,
        patches: List[str],
        patch_format: str,
        base_commit: str,
    ) -> dict:
        """Send try push contents to Lando.

        Send the list of base64-encoded `patches` in `patch_format` to Lando, to be applied to
        the Mercurial `base_commit`, using the Auth0 `access_token` for authorization.
        """
        request_json_body = {
            "base_commit": base_commit,
            "patch_format": patch_format,
            "patches": patches,
        }

        print("Submitting patches to Lando.")
        response_json = self.post(self.lando_try_api_url, request_json_body)

        return response_json


def push_to_lando_try(
    vcs: SupportedVcsRepository, commit_message: str, changed_files: dict
):
    """Push a set of patches to Lando's try endpoint."""
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

    # Use Lando Prod unless the `LANDO_TRY_USE_DEV` environment variable is defined.
    lando_config_section = (
        "lando-prod" if not os.getenv("LANDO_TRY_USE_DEV") else "lando-dev"
    )

    # Load Auth0 config from `.lando.ini`.
    lando_ini_path = Path(vcs.path) / ".lando.ini"
    lando_api = LandoAPI.from_lando_config_file(lando_ini_path, lando_config_section)

    # Get the time when the push was initiated, not including Auth0 login time.
    push_start_time = time.perf_counter()

    with vcs.try_commit(commit_message, changed_files) as head:
        try:
            base_commit, patches = get_stack_info(vcs, head)
        except ValueError as exc:
            error_msg = "abort: error gathering patches for submission."
            print(error_msg)
            print(str(exc))
            build.notify(error_msg)
            return

        try:
            # Make the try request to Lando.
            response_json = lando_api.post_try_push_patches(
                patches, patch_format, base_commit
            )
        except LandoAPIException as exc:
            error_msg = "abort: error submitting patches to Lando."
            print(error_msg)
            print(str(exc))
            build.notify(error_msg)
            return

    duration = time.perf_counter() - push_start_time

    job_id = response_json["id"]
    success_msg = (
        f"Lando try submission success, took {duration:.1f} seconds. "
        f"Landing job id: {job_id}."
    )
    print(success_msg)
    # Send a notification only if the push took an unexpectedly long time
    if duration > 30:
        build.notify(success_msg)
