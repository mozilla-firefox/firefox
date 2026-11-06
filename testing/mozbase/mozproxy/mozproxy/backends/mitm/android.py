"""Functions to download, install, setup, and use the mitmproxy playback tool"""

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import glob
import os
import shutil
import subprocess
import sys
import tempfile
from subprocess import PIPE

import mozinfo

from mozproxy.backends.mitm.mitm import Mitmproxy
from mozproxy.utils import LOG, download_file_from_url, tooltool_download

# path for mitmproxy certificate, generated auto after mitmdump is started
# on local machine it is 'HOME', however it is different on production machines
try:
    DEFAULT_CERT_PATH = os.path.join(
        os.getenv("HOME"), ".mitmproxy", "mitmproxy-ca-cert.cer"
    )
except Exception:
    DEFAULT_CERT_PATH = os.path.join(
        os.getenv("HOMEDRIVE"),
        os.getenv("HOMEPATH"),
        ".mitmproxy",
        "mitmproxy-ca-cert.cer",
    )

# On Windows, deal with mozilla-build having forward slashes in $HOME:
if os.name == "nt" and "/" in DEFAULT_CERT_PATH:
    DEFAULT_CERT_PATH = DEFAULT_CERT_PATH.replace("/", "\\")

FIREFOX_ANDROID_APPS = [
    "geckoview",
    "refbrow",
    "fenix",
]


class MitmproxyAndroid(Mitmproxy):
    def setup(self):
        self.download_and_install_host_utils()
        self.install_mitmproxy_cert()

    def download_and_install_host_utils(self):
        """
        If running locally:
        1. Will use the `certutil` tool from the local Firefox desktop build

        If running in production:
        1. Get the tooltools manifest file for downloading hostutils (contains certutil)
        2. Get the `certutil` tool by downloading hostutils using the tooltool manifest
        """
        if self.config["run_local"]:
            # when running locally, it is found in the Firefox desktop build (..obj../dist/bin)
            self.certutil_path = os.path.join(os.environ["MOZ_HOST_BIN"], "certutil")
            if not (
                os.path.isfile(self.certutil_path)
                and os.access(self.certutil_path, os.X_OK)
            ):
                raise Exception(
                    f"Abort: unable to execute certutil: {self.certutil_path}"
                )
            self.certutil_path = os.environ["MOZ_HOST_BIN"]
            os.environ["LD_LIBRARY_PATH"] = self.certutil_path
        else:
            # must download certutil inside hostutils via tooltool; use this manifest:
            # mozilla-central/testing/config/tooltool-manifests/linux64/hostutils.manifest
            # after it will be found here inside the worker/bitbar container:
            # /builds/worker/workspace/build/hostutils/host-utils-66.0a1.en-US.linux-x86_64
            LOG.info("downloading certutil binary (hostutils)")

            # get path to the hostutils tooltool manifest; was set earlier in
            # mozharness/configs/raptor/android_hw_config.py, to the path i.e.
            # mozilla-central/testing/config/tooltool-manifests/linux64/hostutils.manifest
            # the bitbar container is always linux64
            if os.environ.get("GECKO_HEAD_REPOSITORY", None) is None:
                raise Exception("Abort: unable to get GECKO_HEAD_REPOSITORY")

            if os.environ.get("GECKO_HEAD_REV", None) is None:
                raise Exception("Abort: unable to get GECKO_HEAD_REV")

            if os.environ.get("HOSTUTILS_MANIFEST_PATH", None) is not None:
                manifest_url = os.path.join(
                    os.environ["GECKO_HEAD_REPOSITORY"],
                    "raw-file",
                    os.environ["GECKO_HEAD_REV"],
                    os.environ["HOSTUTILS_MANIFEST_PATH"],
                )
            else:
                raise Exception("Abort: unable to get HOSTUTILS_MANIFEST_PATH!")

            # first need to download the hostutils tooltool manifest file itself
            _dest = os.path.join(self.mozproxy_dir, "hostutils.manifest")
            have_manifest = download_file_from_url(manifest_url, _dest)
            if not have_manifest:
                raise Exception("failed to download the hostutils tooltool manifest")

            # now use the manifest to download hostutils so we can get certutil
            tooltool_download(_dest, self.config["run_local"], self.mozproxy_dir)

            # bitbar hosts are linux, the Android emulator hosts are macOS
            if mozinfo.os == "mac":
                hostutils_glob = "host-utils*.mac"
            else:
                hostutils_glob = "host-utils*[!z|checksum]"
            self.certutil_path = glob.glob(
                os.path.join(self.mozproxy_dir, hostutils_glob)
            )[0]

            # must add hostutils/certutil to the path
            os.environ["LD_LIBRARY_PATH"] = self.certutil_path

        bin_suffix = mozinfo.info.get("bin_suffix", "")
        self.certutil_path = os.path.join(self.certutil_path, "certutil" + bin_suffix)
        if os.path.isfile(self.certutil_path):
            LOG.info("certutil is found at: %s" % self.certutil_path)
        else:
            raise Exception("unable to find certutil at %s" % self.certutil_path)

    def install_mitmproxy_cert(self):
        """Install the CA certificate generated by mitmproxy, into geckoview android

        1. Create an NSS certificate database in the geckoview browser profile dir, only
           if it doesn't already exist. Use this certutil command:
           `certutil -N -d sql:<path to profile> --empty-password`
        2. Import the mitmproxy certificate into the database, i.e.:
           `certutil -A -d sql:<path to profile> -n "some nickname" -t TC,, -a -i <path to CA.pem>`
        """
        # If the app isn't in FIREFOX_ANDROID_APPS then we have to create the tempdir
        # because we don't have it available in the local_profile_dir
        if self.config["app"] in FIREFOX_ANDROID_APPS:
            tempdir = self.config["local_profile_dir"]
        else:
            tempdir = tempfile.mkdtemp()
        cert_db_location = "sql:%s/" % tempdir

        if not self.cert_db_exists(cert_db_location):
            self.create_cert_db(cert_db_location)

        # DEFAULT_CERT_PATH has local path and name of mitmproxy cert i.e.
        # /home/cltbld/.mitmproxy/mitmproxy-ca-cert.cer
        self.import_certificate_in_cert_db(cert_db_location, DEFAULT_CERT_PATH)

        # cannot continue if failed to add CA cert to Firefox, need to check
        if not self.is_mitmproxy_cert_installed(cert_db_location):
            LOG.error("Aborting: failed to install mitmproxy CA cert into Firefox")
            self.stop_mitmproxy_playback()
            sys.exit()
        if self.config["app"] not in FIREFOX_ANDROID_APPS:
            try:
                shutil.rmtree(tempdir)
            except Exception:
                LOG.warning("unable to remove directory: %s" % tempdir)

    def import_certificate_in_cert_db(self, cert_db_location, local_cert_path):
        # import mitmproxy cert into the db
        args = [
            "-A",
            "-d",
            cert_db_location,
            "-n",
            "mitmproxy-cert",
            "-t",
            "TC,,",
            "-a",
            "-i",
            local_cert_path,
        ]
        LOG.info("importing mitmproxy cert into db using command")
        self.certutil(args)

    def create_cert_db(self, cert_db_location):
        # create cert db if it doesn't already exist; it may exist already
        # if a previous pageload test ran in the same test suite
        args = ["-d", cert_db_location, "-N", "--empty-password"]

        LOG.info("creating nss cert database")

        self.certutil(args)

        if not self.cert_db_exists(cert_db_location):
            raise Exception("nss cert db creation command failed. Cert db not created.")

    def cert_db_exists(self, cert_db_location):
        # check if the nss ca cert db already exists in the device profile
        LOG.info(
            "checking if the nss cert db already exists in the android browser profile"
        )

        args = ["-d", cert_db_location, "-L"]
        cert_db_exists = self.certutil(args, raise_exception=False)

        if cert_db_exists:
            LOG.info("the nss cert db exists")
            return True
        else:
            LOG.info("nss cert db doesn't exist yet.")
            return False

    def is_mitmproxy_cert_installed(self, cert_db_location):
        """Verify mitmxproy CA cert was added to Firefox on android"""
        LOG.info("verifying that the mitmproxy ca cert is installed on android")

        # list the certifcates that are in the nss cert db (inside the browser profile dir)
        LOG.info(
            "getting the list of certs in the nss cert db in the android browser profile"
        )
        args = ["-d", cert_db_location, "-L"]

        cmd_output = self.certutil(args)

        if "mitmproxy-cert" in cmd_output.decode("utf-8"):
            LOG.info(
                "verfied the mitmproxy-cert is installed in the nss cert db on android"
            )
            return True
        return False

    def certutil(self, args, raise_exception=True):
        cmd = [self.certutil_path] + list(args)
        LOG.info("Certutil: Running command: %s" % " ".join(cmd))
        try:
            cmd_proc = subprocess.Popen(
                cmd, stdout=PIPE, stderr=PIPE, env=os.environ.copy()
            )

            cmd_output, errs = cmd_proc.communicate()
        except subprocess.SubprocessError:
            LOG.critical("could not run the certutil command")
            raise

        if cmd_proc.returncode == 0:
            # Debug purpose only remove if stable
            LOG.info("Certutil returncode: %s" % cmd_proc.returncode)
            LOG.info("Certutil output: %s" % cmd_output)
            return cmd_output
        elif raise_exception:
            LOG.critical("Certutil command failed!!")
            LOG.info("Certutil returncode: %s" % cmd_proc.returncode)
            LOG.info("Certutil output: %s" % cmd_output)
            LOG.info("Certutil error: %s" % errs)
            raise Exception("Certutil command failed!!")
        else:
            return False
