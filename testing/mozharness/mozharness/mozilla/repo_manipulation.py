# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import six

# pylint --py3k: W1648
if six.PY2:
    from ConfigParser import ConfigParser
else:
    from configparser import ConfigParser

import json
import os

from mozharness.base.errors import HgErrorList
from mozharness.base.log import FATAL, INFO
from mozharness.base.vcs.mercurial import MercurialVCS


class MercurialRepoManipulationMixin:
    def get_version(self, repo_root, version_file="browser/config/version.txt"):
        version_path = os.path.join(repo_root, version_file)
        contents = self.read_from_file(version_path, error_level=FATAL)
        lines = [l for l in contents.splitlines() if l and not l.startswith("#")]
        return lines[-1].split(".")

    def replace(self, file_name, from_, to_):
        """Replace text in a file."""
        text = self.read_from_file(file_name, error_level=FATAL)
        new_text = text.replace(from_, to_)
        if text == new_text:
            self.fatal("Cannot replace '%s' to '%s' in '%s'" % (from_, to_, file_name))
        self.write_to_file(file_name, new_text, error_level=FATAL)

    def query_hg_revision(self, path):
        """Avoid making 'pull' a required action every run, by being able
        to fall back to figuring out the revision from the cloned repo
        """
        m = MercurialVCS(log_obj=self.log_obj, config=self.config)
        revision = m.get_revision_from_path(path)
        return revision

    def hg_commit(self, cwd, message, user=None, ignore_no_changes=False):
        """Commit changes to hg."""
        cmd = self.query_exe("hg", return_type="list") + ["commit", "-m", message]
        if user:
            cmd.extend(["-u", user])
        success_codes = [0]
        if ignore_no_changes:
            success_codes.append(1)
        self.run_command(
            cmd,
            cwd=cwd,
            error_list=HgErrorList,
            halt_on_failure=True,
            success_codes=success_codes,
        )
        return self.query_hg_revision(cwd)

    def clean_repos(self):
        """We may end up with contaminated local repos at some point, but
        we don't want to have to clobber and reclone from scratch every
        time.

        This is an attempt to clean up the local repos without needing a
        clobber.
        """
        dirs = self.query_abs_dirs()
        hg = self.query_exe("hg", return_type="list")
        hg_repos = self.query_repos()
        hg_strip_error_list = [
            {
                "substr": r"""abort: empty revision set""",
                "level": INFO,
                "explanation": "Nothing to clean up; we're good!",
            }
        ] + HgErrorList
        for repo_config in hg_repos:
            repo_name = repo_config["dest"]
            repo_path = os.path.join(dirs["abs_work_dir"], repo_name)
            if os.path.exists(repo_path):
                # hg up -C to discard uncommitted changes
                self.run_command(
                    hg + ["up", "-C", "-r", repo_config["branch"]],
                    cwd=repo_path,
                    error_list=HgErrorList,
                    halt_on_failure=True,
                )
                # discard unpushed commits
                status = self.retry(
                    self.run_command,
                    args=(
                        hg
                        + [
                            "--config",
                            "extensions.mq=",
                            "strip",
                            "--no-backup",
                            "outgoing()",
                        ],
                    ),
                    kwargs={
                        "cwd": repo_path,
                        "error_list": hg_strip_error_list,
                        "return_type": "num_errors",
                        "success_codes": (0, 255),
                    },
                )
                if status not in [0, 255]:
                    self.fatal("Issues stripping outgoing revisions!")
                # 2nd hg up -C to make sure we're not on a stranded head
                # which can happen when reverting debugsetparents
                self.run_command(
                    hg + ["up", "-C", "-r", repo_config["branch"]],
                    cwd=repo_path,
                    error_list=HgErrorList,
                    halt_on_failure=True,
                )

    def commit_changes(self):
        """Do the commit."""
        hg = self.query_exe("hg", return_type="list")
        for cwd in self.query_commit_dirs():
            self.run_command(hg + ["diff"], cwd=cwd)
            self.hg_commit(
                cwd,
                user=self.config["hg_user"],
                message=self.query_commit_message(),
                ignore_no_changes=self.config.get("ignore_no_changes", False),
            )
        self.info(
            "Now verify |hg out| and |hg out --patch| if you're paranoid, and --push"
        )

    def hg_tag(
        self,
        cwd,
        tags,
        user=None,
        message=None,
        revision=None,
        force=None,
        halt_on_failure=True,
    ):
        if isinstance(tags, str):
            tags = [tags]
        cmd = self.query_exe("hg", return_type="list") + ["tag"]
        if not message:
            message = "No bug - Tagging %s" % os.path.basename(cwd)
            if revision:
                message = "%s %s" % (message, revision)
            message = "%s with %s" % (message, ", ".join(tags))
            message += " a=release DONTBUILD CLOSED TREE"
        self.info(message)
        cmd.extend(["-m", message])
        if user:
            cmd.extend(["-u", user])
        if revision:
            cmd.extend(["-r", revision])
        if force:
            cmd.append("-f")
        cmd.extend(tags)
        return self.run_command(
            cmd, cwd=cwd, halt_on_failure=halt_on_failure, error_list=HgErrorList
        )

    def query_existing_tags(self, cwd, halt_on_failure=True):
        cmd = self.query_exe("hg", return_type="list") + ["tags"]
        existing_tags = {}
        output = self.get_output_from_command(
            cmd, cwd=cwd, halt_on_failure=halt_on_failure
        )
        for line in output.splitlines():
            parts = line.split(" ")
            if len(parts) > 1:
                # existing_tags = {TAG: REVISION, ...}
                existing_tags[parts[0]] = parts[-1].split(":")[-1]
        self.info(
            f"existing_tags:\n{json.dumps(existing_tags, sort_keys=True, indent=4)}"
        )
        return existing_tags

    def push(self):
        """"""
        error_message = """Push failed!  If there was a push race, try rerunning
the script (--clean-repos --pull --migrate).  The second run will be faster."""
        hg = self.query_exe("hg", return_type="list")
        for cwd in self.query_push_dirs():
            if not cwd:
                self.warning("Skipping %s" % cwd)
                continue
            push_cmd = hg + ["push"] + self.query_push_args(cwd)
            if self.config.get("push_dest"):
                push_cmd.append(self.config["push_dest"])
            status = self.run_command(
                push_cmd,
                cwd=cwd,
                error_list=HgErrorList,
                success_codes=[0, 1],
            )
            if status == 1:
                self.warning("No changes for %s!" % cwd)
            elif status:
                self.fatal(error_message)

    def edit_repo_hg_rc(self, cwd, section, key, value):
        hg_rc = self.read_repo_hg_rc(cwd)
        hg_rc.set(section, key, value)

        with open(self._get_hg_rc_path(cwd), "wb") as f:
            hg_rc.write(f)

    def read_repo_hg_rc(self, cwd):
        hg_rc = ConfigParser()
        hg_rc.read(self._get_hg_rc_path(cwd))
        return hg_rc

    def _get_hg_rc_path(self, cwd):
        return os.path.join(cwd, ".hg", "hgrc")
