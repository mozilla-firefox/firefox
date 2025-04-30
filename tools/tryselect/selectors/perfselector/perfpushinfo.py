# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


class PerfPushInfo:
    """Used to store, and pass information about the perf try pushes."""

    def __init__(
        self,
        base_revision=None,
        new_revision=None,
        framework=None,
        base_hash=None,
        new_hash=None,
    ):
        self.base_revision = base_revision
        self.base_hash = base_hash
        self.new_revision = new_revision
        self.new_hash = new_hash
        self.framework = framework
        self.finished_run = False

    @property
    def base_revision(self):
        return self._base_revision

    @base_revision.setter
    def base_revision(self, base_revision):
        self._base_revision = base_revision

    @property
    def new_revision(self):
        return self._new_revision

    @new_revision.setter
    def new_revision(self, new_revision):
        self._new_revision = new_revision
        self.finished_run = True

    @property
    def base_hash(self):
        return self._base_hash

    @base_hash.setter
    def base_hash(self, base_hash):
        self._base_hash = base_hash

    @property
    def new_hash(self):
        return self._new_hash

    @new_hash.setter
    def new_hash(self, new_hash):
        self._new_hash = new_hash
        self.finished_run = True

    def get_perfcompare_settings(self):
        """Returns all the settings required to setup a perfcompare URL."""
        return (
            self.base_revision,
            self.new_revision,
            self.framework,
        )

    def get_perfcompare_settings_git(self):
        """Returns all the settings required to setup a perfcompare URL after migration to git."""
        return (
            self.base_hash,
            self.new_hash,
            self.framework,
        )
