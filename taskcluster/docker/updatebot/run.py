#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

import sys

sys.path.append("/builds/worker/checkouts/gecko/third_party/python")
sys.path.append(".")

import base64
import os
import platform
import signal
import stat
import subprocess

import requests

import taskcluster

# Bump this number when you need to cause a commit for the job to re-run: 23

if len(sys.argv) < 3:
    print("Usage:", sys.argv[0], "gecko-dev-path updatebot-path [moz-fetches-dir]")
    sys.exit(1)

GECKO_DEV_PATH = sys.argv[1].replace("/", os.path.sep)
UPDATEBOT_PATH = sys.argv[2].replace("/", os.path.sep)

# Only needed on Windows
if len(sys.argv) > 3:
    FETCHES_PATH = sys.argv[3].replace("/", os.path.sep)
else:
    FETCHES_PATH = None

HOME_PATH = os.path.expanduser("~")

OPERATING_MODE = (
    "prod"
    if os.environ.get("GECKO_HEAD_REPOSITORY", "")
    == "https://hg.mozilla.org/mozilla-central"
    else "dev"
)

DEV_PHAB_URL = "https://phabricator-dev.allizom.org/"
PROD_PHAB_URL = "https://phabricator.services.mozilla.com/"

phabricator_url = DEV_PHAB_URL if OPERATING_MODE == "dev" else PROD_PHAB_URL


def log(*args):
    print(*args)


def get_secret(name):
    secret = None
    if "TASK_ID" in os.environ:
        secrets_url = (
            os.environ.get("TASKCLUSTER_PROXY_URL", "http://taskcluster")
            + "/secrets/v1/secret/project/updatebot/"
            + ("3" if OPERATING_MODE == "prod" else "2")
            + "/"
            + name
        )
        res = requests.get(secrets_url)
        res.raise_for_status()
        secret = res.json()
    else:
        secrets = taskcluster.Secrets(taskcluster.optionsFromEnvironment())
        secret = secrets.get("project/updatebot/" + OPERATING_MODE + "/" + name)
    secret = secret["secret"] if "secret" in secret else None
    secret = secret["value"] if "value" in secret else None
    return secret


# Get TC Secrets =======================================
log("Operating mode is ", OPERATING_MODE)
log("Getting secrets...")
bugzilla_api_key = get_secret("bugzilla-api-key")
phabricator_token = get_secret("phabricator-token")
try_sshkey = get_secret("try-sshkey")
database_config = get_secret("database-password")
sentry_url = get_secret("sentry-url")
sql_proxy_config = get_secret("sql-proxy-config")
ai_api_key = get_secret("ai-api-key")

# Update Updatebot =======================================
if OPERATING_MODE == "dev":
    """
    If we are in development mode, we will update from github.
    (This command will probably only work if we checkout a branch FWIW.)

    This allows us to iterate faster by committing to github and
    re-running the cron job on Taskcluster, without rebuilding the
    Docker image.

    However, this mechanism is bypassing the security feature we
    have in-tree, where upstream out-of-tree code is fixed at a known
    revision and cannot be changed without a commit to m-c.

    Therefore, we only do this in dev mode when running on try.
    """

    os.chdir(UPDATEBOT_PATH)
    log("Performing git repo update...")
    command = ["git", "symbolic-ref", "-q", "HEAD"]

    r = subprocess.run(command, check=False)
    if r.returncode == 0:
        # This indicates we are on a branch, and not a specific revision
        subprocess.check_call(["git", "pull", "origin"])

# Set Up SSH & Phabricator ==============================
os.chdir(HOME_PATH)
log("Setting up ssh and phab keys...")
with open("id_rsa", "w") as sshkey:
    sshkey.write(try_sshkey)
os.chmod("id_rsa", stat.S_IRUSR | stat.S_IWUSR)

arc_filename = ".arcrc"
if platform.system() == "Windows":
    arc_path = os.path.join(FETCHES_PATH, "..", "AppData", "Roaming")
    os.makedirs(arc_path, exist_ok=True)
    os.chdir(arc_path)
    log("Writing %s to %s" % (arc_filename, arc_path))
else:
    os.chdir(HOME_PATH)

arcrc = open(arc_filename, "w")
towrite = """
{
  "hosts": {
    "PHAB_URL_HERE": {
      "token": "TOKENHERE"
    }
  }
}
""".replace("TOKENHERE", phabricator_token).replace(
    "PHAB_URL_HERE", phabricator_url + "api/"
)
arcrc.write(towrite)
arcrc.close()
os.chmod(arc_filename, stat.S_IRUSR | stat.S_IWUSR)

# Set up the Cloud SQL Proxy =============================
os.chdir(HOME_PATH)
log("Setting up cloud_sql_proxy...")
with open("sql-proxy-key", "w") as proxy_key_file:
    proxy_key_file.write(
        base64.b64decode(sql_proxy_config["key-value"]).decode("utf-8")
    )

instance_name = sql_proxy_config["instance-name"]
if platform.system() == "Linux":
    sql_proxy_command = "cloud_sql_proxy"
else:
    sql_proxy_command = os.path.join(UPDATEBOT_PATH, "..", "cloud_sql_proxy.exe")

sql_proxy_command += (
    " -instances=" + instance_name + "=tcp:3306 -credential_file=sql-proxy-key"
)
sql_proxy_args = {
    "stdout": subprocess.PIPE,
    "stderr": subprocess.PIPE,
    "shell": True,
    "start_new_session": True,
}

if platform.system() == "Windows":
    si = subprocess.STARTUPINFO()
    si.dwFlags = subprocess.CREATE_NEW_PROCESS_GROUP

    sql_proxy_args["startupinfo"] = si

sql_proxy = subprocess.Popen((sql_proxy_command), **sql_proxy_args)

try:
    (stdout, stderr) = sql_proxy.communicate(input=None, timeout=2)
    log("sql proxy stdout:", stdout.decode("utf-8"))
    log("sql proxy stderr:", stderr.decode("utf-8"))
except subprocess.TimeoutExpired:
    log("no sqlproxy output in 2 seconds, this means it probably didn't error.")
    log("sqlproxy pid:", sql_proxy.pid)

database_config["host"] = "127.0.0.1"

# Vendor =================================================
log("Getting Updatebot ready...")
os.chdir(UPDATEBOT_PATH)

# AI conflict-resolution debug output; /builds/worker/artifacts is uploaded as task artifacts.
ai_debug_dir = os.path.join(HOME_PATH, "artifacts", "ai-debug")
os.makedirs(ai_debug_dir, exist_ok=True)

localconfig = {
    "General": {
        "env": OPERATING_MODE,
        "gecko-path": GECKO_DEV_PATH,
        "soft_timeout": 3600,
    },
    "Logging": {
        "local": True,
        "sentry": True,
        "sentry_config": {"url": sentry_url, "debug": False},
    },
    "Database": database_config,
    "Bugzilla": {
        "apikey": bugzilla_api_key,
    },
    "AI": {
        "apikey": ai_api_key,
        "debug-output-dir": ai_debug_dir,
    },
    "Taskcluster": {
        "url_treeherder": "https://treeherder.mozilla.org/",
        "url_taskcluster": "http://taskcluster/",
    },
}

log("Writing local config file")
config = open("localconfig.py", "w")
config.write("localconfig = " + str(localconfig))
config.close()

log("Running updatebot")
# On Windows, Updatebot is run by windows-setup.sh
if platform.system() == "Linux":
    subprocess.check_call(["python3", "-m", "poetry", "run", "./automation.py"])

    # Clean up ===============================================
    log("Killing cloud_sql_proxy")
    os.kill(sql_proxy.pid, signal.SIGTERM)
