# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import io
import json
import runpy
import sys
from unittest import mock

import mozunit
import pytest
from tryselect.selectors.perfselector import utils
from tryselect.selectors.perfselector.utils import LogProcessor


def _import_perf_preview_restoring_sys_path_shadowing_taskcluster():
    saved_path = list(sys.path)
    try:
        from tryselect.selectors import perf_preview
    finally:
        sys.path[:] = saved_path
    return perf_preview


perf_preview = _import_perf_preview_restoring_sys_path_shadowing_taskcluster()


@pytest.fixture
def processor():
    out = io.StringIO()
    with mock.patch.object(utils.sys, "__stdout__", out):
        proc = LogProcessor()
    return proc, out


def test_log_processor_starts_without_revision_and_flushes_cleanly(processor):
    proc, _ = processor
    assert proc.revision is None
    proc.flush()


def test_log_processor_buffers_partial_lines_until_newline(processor):
    proc, out = processor
    proc.write("hello ")
    assert out.getvalue() == ""
    proc.write("world\nnext")
    assert out.getvalue() == "hello world\n"
    proc.write("\n")
    assert out.getvalue() == "hello world\nnext\n"


def test_log_processor_skips_blank_and_whitespace_lines(processor):
    proc, out = processor
    proc.write("\n  \nline\n")
    assert out.getvalue() == "line\n"


def test_log_processor_extracts_revision_from_treeherder_url(processor):
    proc, _ = processor
    proc.write(
        "remote:   https://treeherder.mozilla.org/jobs?repo=try&revision=abc123\n"
    )
    assert proc.revision == "abc123"


def test_log_processor_extracts_revision_from_hg_urls(processor):
    proc, _ = processor
    proc.write("remote:   https://hg.mozilla.org/try/rev/def456\n")
    assert proc.revision == "def456"
    proc.write("remote:   https://hg.mozilla.org/try/pushloghtml?changeset=0a1b2c\n")
    assert proc.revision == "0a1b2c"


def test_log_processor_prefers_treeherder_revision(processor):
    proc, _ = processor
    proc.write(
        "remote:   https://hg.mozilla.org/try/rev/def456\n"
        "remote:   https://treeherder.mozilla.org/jobs?repo=try&revision=abc123\n"
        "remote:   https://hg.mozilla.org/try/rev/789abc\n"
    )
    assert proc.revision == "abc123"


def test_log_processor_ignores_lines_without_revision(processor):
    proc, _ = processor
    proc.write("something else\n")
    assert proc.revision is None


def test_process_args_defaults_to_none():
    with mock.patch.object(sys, "argv", ["perf_preview.py"]):
        args = perf_preview.process_args()
    assert (args.tasklist, args.description, args.line) == (None, None, None)


def test_process_args_reads_tasklist_description_and_line():
    argv = ["perf_preview.py", "-t", "tasks", "-d", "desc", "-l", "line"]
    with mock.patch.object(sys, "argv", argv):
        args = perf_preview.process_args()
    assert (args.tasklist, args.description, args.line) == ("tasks", "desc", "line")


@pytest.fixture
def preview_files(tmp_path):
    tasks = tmp_path / "tasks.txt"
    tasks.write_text("task-b\ntask-a\n")
    desc = tmp_path / "desc.json"
    desc.write_text(json.dumps({"task-a": "Description of A"}))
    return str(tasks), str(desc)


def test_plain_display_lists_sorted_tasks_without_description(preview_files, capsys):
    tasks, _ = preview_files
    perf_preview.plain_display(tasks, None, None)
    assert capsys.readouterr().out == "task-a\ntask-b\n"


def test_plain_display_hides_description_when_no_line_given(preview_files, capsys):
    tasks, desc = preview_files
    perf_preview.plain_display(tasks, desc, None)
    assert capsys.readouterr().out == "task-a\ntask-b\n"


def test_plain_display_appends_description_of_matching_line(preview_files, capsys):
    tasks, desc = preview_files
    perf_preview.plain_display(tasks, desc, "'task-a'")
    assert capsys.readouterr().out.endswith("\n* Desc:\nDescription of A\n")


def test_plain_display_hides_description_for_unknown_line(preview_files, capsys):
    tasks, desc = preview_files
    perf_preview.plain_display(tasks, desc, "missing")
    assert capsys.readouterr().out == "task-a\ntask-b\n"


def test_perf_preview_script_prints_description_of_line(preview_files, capsys):
    tasks, desc = preview_files
    argv = ["perf_preview.py", "-t", tasks, "-d", desc, "-l", "task-a"]
    with mock.patch.object(sys, "argv", argv):
        with mock.patch.object(sys, "path", list(sys.path)):
            runpy.run_path(perf_preview.__file__, run_name="__main__")
    assert "Description of A" in capsys.readouterr().out


if __name__ == "__main__":
    mozunit.main()
