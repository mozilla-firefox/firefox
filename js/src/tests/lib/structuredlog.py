# produce mozlog-compatible log messages, following the spec at
# https://firefox-source-docs.mozilla.org/mozbase/mozlog.html

import json
import os
from time import time


class TestLogger:
    def __init__(self, source, threadname="main"):
        self.template = {
            "source": source,
            "thread": threadname,
            "pid": os.getpid(),
        }
        # Only write the raw JSON log in automation, where it's uploaded as an
        # artifact.
        self.fh = None
        directory = os.environ.get("MOZ_UPLOAD_DIR")
        if directory:
            self.fh = open(os.path.join(directory, threadname + "_raw.log"), "a")

    def _record(self, **kwargs):
        record = self.template.copy()
        record.update(**kwargs)
        if "time" not in record:
            record["time"] = time()
        return record

    def _log_obj(self, obj):
        if self.fh:
            print(json.dumps(obj, sort_keys=True), file=self.fh)

    def _log(self, **kwargs):
        self._log_obj(self._record(**kwargs))

    def suite_start(self):
        self._log(action="suite_start", tests=[])

    def suite_end(self):
        self._log(action="suite_end")

    def test_start(self, testname):
        self._log(action="test_start", test=testname)

    def test_end(self, testname, status):
        self._log(action="test_end", test=testname, status=status)

    def test(self, testname, status, duration, **details):
        record = self._record(
            action="test_start", test=testname, **details.get("extra", {})
        )
        end_time = record["time"]
        record["time"] -= duration
        self._log_obj(record)

        record["action"] = "test_end"
        record["time"] = end_time
        record["status"] = status
        record.update(**details)
        self._log_obj(record)

    def log_info(self, message):
        self._log(action="log", level="INFO", message=message)
