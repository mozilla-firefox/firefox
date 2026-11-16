import gc
import json
import os
import re
import shutil
import signal
import sys
import tempfile
import threading
import types
import unittest
from unittest import mock

import mozunit

PYWIN32 = False
if os.name == "nt":
    try:
        import win32file

        PYWIN32 = True
    except ImportError:
        pass


from mozharness.base import errors, log, script
from mozharness.base.config import parse_config_file
from mozharness.base.log import CRITICAL, DEBUG, ERROR, FATAL, IGNORE, INFO, WARNING

here = os.path.dirname(os.path.abspath(__file__))

test_string = """foo
bar
baz"""


class CleanupObj(script.ScriptMixin, log.LogMixin):
    def __init__(self):
        super().__init__()
        self.log_obj = None
        self.config = {"log_level": ERROR}


def cleanup(files=None):
    files = files or []
    files.extend(("test_logs", "test_dir", "tmpfile_stdout", "tmpfile_stderr"))
    gc.collect()
    c = CleanupObj()
    for f in files:
        c.rmtree(f)


def get_debug_script_obj():
    s = script.BaseScript(
        config={"log_type": "multi", "log_level": DEBUG},
        initial_config_file="test/test.json",
    )
    return s


def _post_fatal(self, **kwargs):
    fh = open("tmpfile_stdout", "w")
    print(test_string, file=fh)
    fh.close()


# TestScript {{{1
class TestScript(unittest.TestCase):
    def setUp(self):
        cleanup()
        self.s = None
        self.tmpdir = tempfile.mkdtemp(suffix=".mozharness")

    def tearDown(self):
        # Close the logfile handles, or windows can't remove the logs
        if hasattr(self, "s") and isinstance(self.s, object):
            del self.s
        cleanup([self.tmpdir])

    # test _dump_config_hierarchy() when --dump-config-hierarchy is passed
    def test_dump_config_hierarchy_valid_files_len(self):
        try:
            self.s = script.BaseScript(
                initial_config_file="test/test.json",
                option_args=["--cfg", "test/test_override.py,test/test_override2.py"],
                config={"dump_config_hierarchy": True},
            )
        except SystemExit:
            local_cfg_files = parse_config_file("test_logs/localconfigfiles.json")
            # first let's see if the correct number of config files were
            # realized
            self.assertEqual(
                len(local_cfg_files),
                4,
                msg="--dump-config-hierarchy dumped wrong number of config files",
            )

    def test_dump_config_hierarchy_keys_unique_and_valid(self):
        try:
            self.s = script.BaseScript(
                initial_config_file="test/test.json",
                option_args=["--cfg", "test/test_override.py,test/test_override2.py"],
                config={"dump_config_hierarchy": True},
            )
        except SystemExit:
            local_cfg_files = parse_config_file("test_logs/localconfigfiles.json")
            # now let's see if only unique items were added from each config
            t_override = local_cfg_files.get("test/test_override.py", {})
            self.assertTrue(
                t_override.get("keep_string") == "don't change me"
                and len(t_override.keys()) == 1,
                msg="--dump-config-hierarchy dumped wrong keys/value for "
                "`test/test_override.py`. There should only be one "
                "item and it should be unique to all the other "
                "items in test_log/localconfigfiles.json.",
            )

    def test_dump_config_hierarchy_matches_self_config(self):
        try:
            ######
            # we need temp_cfg because self.s will be gcollected (NoneType) by
            # the time we get to SystemExit exception
            # temp_cfg will differ from self.s.config because of
            # 'dump_config_hierarchy'. we have to make a deepcopy because
            # config is a locked dict
            temp_s = script.BaseScript(
                initial_config_file="test/test.json",
                option_args=["--cfg", "test/test_override.py,test/test_override2.py"],
            )
            from copy import deepcopy

            temp_cfg = deepcopy(temp_s.config)
            temp_cfg.update({"dump_config_hierarchy": True})
            ######
            self.s = script.BaseScript(
                initial_config_file="test/test.json",
                option_args=["--cfg", "test/test_override.py,test/test_override2.py"],
                config={"dump_config_hierarchy": True},
            )
        except SystemExit:
            local_cfg_files = parse_config_file("test_logs/localconfigfiles.json")
            # finally let's just make sure that all the items added up, equals
            # what we started with: self.config
            target_cfg = {}
            for cfg_file in local_cfg_files:
                target_cfg.update(local_cfg_files[cfg_file])
            self.assertEqual(
                target_cfg,
                temp_cfg,
                msg="all of the items (combined) in each cfg file dumped via "
                "--dump-config-hierarchy does not equal self.config ",
            )

    # test _dump_config() when --dump-config is passed
    def test_dump_config_equals_self_config(self):
        try:
            ######
            # we need temp_cfg because self.s will be gcollected (NoneType) by
            # the time we get to SystemExit exception
            # temp_cfg will differ from self.s.config because of
            # 'dump_config_hierarchy'. we have to make a deepcopy because
            # config is a locked dict
            temp_s = script.BaseScript(
                initial_config_file="test/test.json",
                option_args=["--cfg", "test/test_override.py,test/test_override2.py"],
            )
            from copy import deepcopy

            temp_cfg = deepcopy(temp_s.config)
            temp_cfg.update({"dump_config": True})
            ######
            self.s = script.BaseScript(
                initial_config_file="test/test.json",
                option_args=["--cfg", "test/test_override.py,test/test_override2.py"],
                config={"dump_config": True},
            )
        except SystemExit:
            target_cfg = parse_config_file("test_logs/localconfig.json")
            self.assertEqual(
                target_cfg,
                temp_cfg,
                msg="all of the items (combined) in each cfg file dumped via "
                "--dump-config does not equal self.config ",
            )

    def test_nonexistent_mkdir_p(self):
        self.s = script.BaseScript(initial_config_file="test/test.json")
        self.s.mkdir_p("test_dir/foo/bar/baz")
        self.assertTrue(os.path.isdir("test_dir/foo/bar/baz"), msg="mkdir_p error")

    def test_existing_mkdir_p(self):
        self.s = script.BaseScript(initial_config_file="test/test.json")
        os.makedirs("test_dir/foo/bar/baz")
        self.s.mkdir_p("test_dir/foo/bar/baz")
        self.assertTrue(
            os.path.isdir("test_dir/foo/bar/baz"), msg="mkdir_p error when dir exists"
        )

    def test_chdir(self):
        self.s = script.BaseScript(initial_config_file="test/test.json")
        cwd = os.getcwd()
        self.s.chdir(self.tmpdir)
        self.assertEqual(self.tmpdir, os.getcwd(), msg="chdir error")
        self.s.chdir(cwd)

    def test_run_perfherder_extra_options(self):
        class Script(script.BaseScript):
            def ran(self):
                pass

            def skipped(self):
                pass

        self.s = Script(
            config={"log_type": "multi"},
            initial_config_file="test/test.json",
            all_actions=["ran", "skipped"],
            default_actions=["ran"],
        )
        upload_dir = os.path.join(self.tmpdir, "upload")
        env = {
            "MOZ_AUTOMATION": "1",
            "UPLOAD_DIR": upload_dir,
            "PERFHERDER_EXTRA_OPTIONS": "mochitest-plain xorig",
        }
        with mock.patch.dict(os.environ, env):
            self.s.run()
        with open(
            os.path.join(upload_dir, "perfherder-data-mozharness-actions.json")
        ) as fh:
            data = json.load(fh)
        self.assertEqual(data["framework"], {"name": "mozharness"})
        self.assertEqual([suite["name"] for suite in data["suites"]], ["ran"])
        self.assertEqual(
            data["suites"][0]["extraOptions"], ["mochitest-plain", "xorig"]
        )

    def test_chdir_relative(self):
        subdir = os.path.join(self.tmpdir, "subdir")
        os.mkdir(subdir)
        self.s = script.BaseScript(initial_config_file="test/test.json")
        cwd = os.getcwd()
        os.chdir(self.tmpdir)
        try:
            self.s.chdir("subdir")
            self.assertEqual(subdir, os.getcwd(), msg="chdir relative error")
        finally:
            os.chdir(cwd)

    def _test_log_helper(self, obj):
        obj.debug("Testing DEBUG")
        obj.warning("Testing WARNING")
        obj.error("Testing ERROR")
        obj.critical("Testing CRITICAL")
        try:
            obj.fatal("Testing FATAL")
        except SystemExit:
            pass
        else:
            self.assertTrue(False, msg="fatal() didn't SystemExit!")

    def test_log(self):
        self.s = get_debug_script_obj()
        self.s.log_obj = None
        self._test_log_helper(self.s)
        del self.s
        self.s = script.BaseScript(initial_config_file="test/test.json")
        self._test_log_helper(self.s)

    def test_run_nonexistent_command(self):
        self.s = get_debug_script_obj()
        self.s.run_command(
            command="this_cmd_should_not_exist --help",
            env={"GARBLE": "FARG"},
            error_list=errors.PythonErrorList,
        )
        error_logsize = os.path.getsize("test_logs/test_info.log")
        self.assertTrue(error_logsize > 0, msg="command not found error not hit")

    def test_run_command_in_bad_dir(self):
        self.s = get_debug_script_obj()
        self.s.run_command(
            command="ls",
            cwd="/this_dir_should_not_exist",
            error_list=errors.PythonErrorList,
        )
        error_logsize = os.path.getsize("test_logs/test_error.log")
        self.assertTrue(error_logsize > 0, msg="bad dir error not hit")

    @unittest.skipIf(os.name != "posix", "SIGTERM can't be handled on Windows")
    def test_run_command_sigterm(self):
        self.s = get_debug_script_obj()
        done = os.path.join(self.tmpdir, "done")
        command = [
            sys.executable,
            "-c",
            "import pathlib, time; time.sleep(2); pathlib.Path(%r).touch()" % done,
        ]
        previous_handler = signal.getsignal(signal.SIGTERM)
        sigterm = threading.Timer(0.5, os.kill, (os.getpid(), signal.SIGTERM))
        sigterm.start()
        self.assertEqual(self.s.run_command(command), 0)
        sigterm.join()
        self.assertTrue(os.path.exists(done), msg="command was interrupted")
        self.assertEqual(signal.getsignal(signal.SIGTERM), previous_handler)
        self.assertEqual(self.s.run_command([sys.executable, "-c", "pass"]), -1)
        error_logsize = os.path.getsize("test_logs/test_error.log")
        self.assertTrue(error_logsize > 0, msg="SIGTERM error not hit")

    def test_get_output_from_command_in_bad_dir(self):
        self.s = get_debug_script_obj()
        self.s.get_output_from_command(command="ls", cwd="/this_dir_should_not_exist")
        error_logsize = os.path.getsize("test_logs/test_error.log")
        self.assertTrue(error_logsize > 0, msg="bad dir error not hit")

    def test_get_output_from_command_with_missing_file(self):
        self.s = get_debug_script_obj()
        self.s.get_output_from_command(command="ls /this_file_should_not_exist")
        error_logsize = os.path.getsize("test_logs/test_error.log")
        self.assertTrue(error_logsize > 0, msg="bad file error not hit")

    def test_get_output_from_command_with_missing_file2(self):
        self.s = get_debug_script_obj()
        self.s.run_command(
            command="cat mozharness/base/errors.py",
            error_list=[
                {"substr": "error", "level": ERROR},
                {
                    "regex": re.compile(",$"),
                    "level": IGNORE,
                },
                {
                    "substr": "]$",
                    "level": WARNING,
                },
            ],
        )
        error_logsize = os.path.getsize("test_logs/test_error.log")
        self.assertTrue(error_logsize > 0, msg="error list not working properly")

    def test_download_unpack(self):
        # NOTE: The action is called *download*, however, it can work for files in disk
        self.s = get_debug_script_obj()

        archives_path = os.path.join(here, "helper_files", "archives")

        # Test basic decompression
        for archive in (
            "archive.tar",
            "archive.tar.bz2",
            "archive.tar.gz",
            "archive.tar.xz",
            "archive.zip",
        ):
            self.s.download_unpack(
                url=os.path.join(archives_path, archive), extract_to=self.tmpdir
            )
            self.assertIn("script.sh", os.listdir(os.path.join(self.tmpdir, "bin")))
            self.assertIn("lorem.txt", os.listdir(self.tmpdir))
            shutil.rmtree(self.tmpdir)

        # Test permissions for extracted entries from zip archive
        self.s.download_unpack(
            url=os.path.join(archives_path, "archive.zip"),
            extract_to=self.tmpdir,
        )
        file_stats = os.stat(os.path.join(self.tmpdir, "bin", "script.sh"))
        orig_fstats = os.stat(
            os.path.join(archives_path, "reference", "bin", "script.sh")
        )
        self.assertEqual(file_stats.st_mode, orig_fstats.st_mode)
        shutil.rmtree(self.tmpdir)

        # Test unzip specific dirs only
        self.s.download_unpack(
            url=os.path.join(archives_path, "archive.zip"),
            extract_to=self.tmpdir,
            extract_dirs=["bin/*"],
        )
        self.assertIn("bin", os.listdir(self.tmpdir))
        self.assertNotIn("lorem.txt", os.listdir(self.tmpdir))
        shutil.rmtree(self.tmpdir)

        # Test for invalid filenames (Windows only)
        if PYWIN32:
            self.s.download_unpack(
                url=os.path.join(archives_path, "archive_invalid_filename.zip"),
                extract_to=self.tmpdir,
            )
            self.assertTrue(os.path.exists(os.path.join(self.tmpdir, "_^_$.txt")))
            shutil.rmtree(self.tmpdir)

        for archive in (
            "archive-setuid.tar",
            "archive-escape.tar",
            "archive-link.tar",
            "archive-link-abs.tar",
            "archive-double-link.tar",
        ):
            with self.assertRaises(Exception):
                self.s.download_unpack(
                    url=os.path.join(archives_path, archive),
                    extract_to=self.tmpdir,
                )

    def test_unpack(self):
        self.s = get_debug_script_obj()

        archives_path = os.path.join(here, "helper_files", "archives")

        # Test basic decompression
        for archive in (
            "archive.tar",
            "archive.tar.bz2",
            "archive.tar.gz",
            "archive.tar.xz",
            "archive.zip",
        ):
            self.s.unpack(os.path.join(archives_path, archive), self.tmpdir)
            self.assertIn("script.sh", os.listdir(os.path.join(self.tmpdir, "bin")))
            self.assertIn("lorem.txt", os.listdir(self.tmpdir))
            shutil.rmtree(self.tmpdir)

        # Test permissions for extracted entries from zip archive
        self.s.unpack(os.path.join(archives_path, "archive.zip"), self.tmpdir)
        file_stats = os.stat(os.path.join(self.tmpdir, "bin", "script.sh"))
        orig_fstats = os.stat(
            os.path.join(archives_path, "reference", "bin", "script.sh")
        )
        self.assertEqual(file_stats.st_mode, orig_fstats.st_mode)
        shutil.rmtree(self.tmpdir)

        # Test extract specific dirs only
        self.s.unpack(
            os.path.join(archives_path, "archive.zip"),
            self.tmpdir,
            extract_dirs=["bin/*"],
        )
        self.assertIn("bin", os.listdir(self.tmpdir))
        self.assertNotIn("lorem.txt", os.listdir(self.tmpdir))
        shutil.rmtree(self.tmpdir)

        # Test for invalid filenames (Windows only)
        if PYWIN32:
            with self.assertRaises(IOError):
                self.s.unpack(
                    os.path.join(archives_path, "archive_invalid_filename.zip"),
                    self.tmpdir,
                )

        for archive in (
            "archive-setuid.tar",
            "archive-escape.tar",
            "archive-link.tar",
            "archive-link-abs.tar",
            "archive-double-link.tar",
        ):
            with self.assertRaises(Exception):
                self.s.unpack(os.path.join(archives_path, archive), self.tmpdir)


# TestHelperFunctions {{{1
class TestHelperFunctions(unittest.TestCase):
    temp_file = "test_dir/mozilla"

    def setUp(self):
        cleanup()
        self.s = None

    def tearDown(self):
        # Close the logfile handles, or windows can't remove the logs
        if hasattr(self, "s") and isinstance(self.s, object):
            del self.s
        cleanup()

    def _create_temp_file(self, contents=test_string):
        os.mkdir("test_dir")
        fh = open(self.temp_file, "w+")
        fh.write(contents)
        fh.close

    def test_mkdir_p(self):
        self.s = script.BaseScript(initial_config_file="test/test.json")
        self.s.mkdir_p("test_dir")
        self.assertTrue(os.path.isdir("test_dir"), msg="mkdir_p error")

    def test_get_output_from_command(self):
        self._create_temp_file()
        self.s = script.BaseScript(initial_config_file="test/test.json")
        contents = self.s.get_output_from_command([
            "bash",
            "-c",
            "cat %s" % self.temp_file,
        ])
        self.assertEqual(
            test_string,
            contents,
            msg="get_output_from_command('cat file') differs from fh.write",
        )

    def test_run_command(self):
        self._create_temp_file()
        self.s = script.BaseScript(initial_config_file="test/test.json")
        temp_file_name = os.path.basename(self.temp_file)
        self.assertEqual(
            self.s.run_command("cat %s" % temp_file_name, cwd="test_dir"),
            0,
            msg="run_command('cat file') did not exit 0",
        )

    def test_move1(self):
        self._create_temp_file()
        self.s = script.BaseScript(initial_config_file="test/test.json")
        temp_file2 = "%s2" % self.temp_file
        self.s.move(self.temp_file, temp_file2)
        self.assertFalse(
            os.path.exists(self.temp_file),
            msg="%s still exists after move()" % self.temp_file,
        )

    def test_move2(self):
        self._create_temp_file()
        self.s = script.BaseScript(initial_config_file="test/test.json")
        temp_file2 = "%s2" % self.temp_file
        self.s.move(self.temp_file, temp_file2)
        self.assertTrue(
            os.path.exists(temp_file2), msg="%s doesn't exist after move()" % temp_file2
        )

    def test_copyfile(self):
        self._create_temp_file()
        self.s = script.BaseScript(initial_config_file="test/test.json")
        temp_file2 = "%s2" % self.temp_file
        self.s.copyfile(self.temp_file, temp_file2)
        self.assertEqual(
            os.path.getsize(self.temp_file),
            os.path.getsize(temp_file2),
            msg="%s and %s are different sizes after copyfile()"
            % (self.temp_file, temp_file2),
        )

    def test_existing_rmtree(self):
        self._create_temp_file()
        self.s = script.BaseScript(initial_config_file="test/test.json")
        self.s.mkdir_p("test_dir/foo/bar/baz")
        self.s.rmtree("test_dir")
        self.assertFalse(os.path.exists("test_dir"), msg="rmtree unsuccessful")

    def test_nonexistent_rmtree(self):
        self.s = script.BaseScript(initial_config_file="test/test.json")
        status = self.s.rmtree("test_dir")
        self.assertFalse(status, msg="nonexistent rmtree error")

    @unittest.skipUnless(PYWIN32, "PyWin32 specific")
    def test_long_dir_rmtree(self):
        self.s = script.BaseScript(initial_config_file="test/test.json")
        # create a very long path that the command-prompt cannot delete
        # by using unicode format (max path length 32000)
        path = "\\\\?\\%s\\test_dir" % os.getcwd()
        win32file.CreateDirectoryExW(".", path)

        for x in range(0, 20):
            print("path=%s" % path)
            path = path + "\\%sxxxxxxxxxxxxxxxxxxxx" % x
            win32file.CreateDirectoryExW(".", path)
        self.s.rmtree("test_dir")
        self.assertFalse(os.path.exists("test_dir"), msg="rmtree unsuccessful")

    @unittest.skipUnless(PYWIN32, "PyWin32 specific")
    def test_chmod_rmtree(self):
        self._create_temp_file()
        win32file.SetFileAttributesW(self.temp_file, win32file.FILE_ATTRIBUTE_READONLY)
        self.s = script.BaseScript(initial_config_file="test/test.json")
        self.s.rmtree("test_dir")
        self.assertFalse(os.path.exists("test_dir"), msg="rmtree unsuccessful")

    @unittest.skipIf(os.name == "nt", "Not for Windows")
    def test_chmod(self):
        self._create_temp_file()
        self.s = script.BaseScript(initial_config_file="test/test.json")
        self.s.chmod(self.temp_file, 0o100700)
        self.assertEqual(os.stat(self.temp_file)[0], 33216, msg="chmod unsuccessful")

    def test_env_normal(self):
        self.s = script.BaseScript(initial_config_file="test/test.json")
        script_env = self.s.query_env()
        self.assertEqual(
            script_env,
            os.environ,
            msg="query_env() != env\n%s\n%s" % (script_env, os.environ),
        )

    def test_env_normal2(self):
        self.s = script.BaseScript(initial_config_file="test/test.json")
        self.s.query_env()
        script_env = self.s.query_env()
        self.assertEqual(
            script_env,
            os.environ,
            msg="Second query_env() != env\n%s\n%s" % (script_env, os.environ),
        )

    def test_env_partial(self):
        self.s = script.BaseScript(initial_config_file="test/test.json")
        script_env = self.s.query_env(partial_env={"foo": "bar"})
        self.assertTrue("foo" in script_env and script_env["foo"] == "bar")

    def test_env_path(self):
        self.s = script.BaseScript(initial_config_file="test/test.json")
        partial_path = "yaddayadda:%(PATH)s"
        full_path = partial_path % {"PATH": os.environ["PATH"]}
        script_env = self.s.query_env(partial_env={"PATH": partial_path})
        self.assertEqual(script_env["PATH"], full_path)

    def test_query_exe(self):
        self.s = script.BaseScript(
            initial_config_file="test/test.json",
            config={"exes": {"foo": "bar"}},
        )
        path = self.s.query_exe("foo")
        self.assertEqual(path, "bar")

    def test_query_exe_string_replacement(self):
        self.s = script.BaseScript(
            initial_config_file="test/test.json",
            config={
                "base_work_dir": "foo",
                "work_dir": "bar",
                "exes": {"foo": os.path.join("%(abs_work_dir)s", "baz")},
            },
        )
        path = self.s.query_exe("foo")
        self.assertEqual(path, os.path.join("foo", "bar", "baz"))

    def test_read_from_file(self):
        self._create_temp_file()
        self.s = script.BaseScript(initial_config_file="test/test.json")
        contents = self.s.read_from_file(self.temp_file)
        self.assertEqual(contents, test_string)

    def test_read_from_nonexistent_file(self):
        self.s = script.BaseScript(initial_config_file="test/test.json")
        contents = self.s.read_from_file("nonexistent_file!!!")
        self.assertEqual(contents, None)


# TestScriptLogging {{{1
class TestScriptLogging(unittest.TestCase):
    # I need a log watcher helper function, here and in test_log.
    def setUp(self):
        cleanup()
        self.s = None

    def tearDown(self):
        # Close the logfile handles, or windows can't remove the logs
        if hasattr(self, "s") and isinstance(self.s, object):
            del self.s
        cleanup()

    def test_info_logsize(self):
        self.s = script.BaseScript(
            config={"log_type": "multi"}, initial_config_file="test/test.json"
        )
        info_logsize = os.path.getsize("test_logs/test_info.log")
        self.assertTrue(info_logsize > 0, msg="initial info logfile missing/size 0")

    def test_add_summary_info(self):
        self.s = script.BaseScript(
            config={"log_type": "multi"}, initial_config_file="test/test.json"
        )
        info_logsize = os.path.getsize("test_logs/test_info.log")
        self.s.add_summary("one")
        info_logsize2 = os.path.getsize("test_logs/test_info.log")
        self.assertTrue(
            info_logsize < info_logsize2, msg="add_summary() info not logged"
        )

    def test_add_summary_warning(self):
        self.s = script.BaseScript(
            config={"log_type": "multi"}, initial_config_file="test/test.json"
        )
        warning_logsize = os.path.getsize("test_logs/test_warning.log")
        self.s.add_summary("two", level=WARNING)
        warning_logsize2 = os.path.getsize("test_logs/test_warning.log")
        self.assertTrue(
            warning_logsize < warning_logsize2,
            msg="add_summary(level=%s) not logged in warning log" % WARNING,
        )

    def test_summary(self):
        self.s = script.BaseScript(
            config={"log_type": "multi"}, initial_config_file="test/test.json"
        )
        self.s.add_summary("one")
        self.s.add_summary("two", level=WARNING)
        info_logsize = os.path.getsize("test_logs/test_info.log")
        warning_logsize = os.path.getsize("test_logs/test_warning.log")
        self.s.summary()
        info_logsize2 = os.path.getsize("test_logs/test_info.log")
        warning_logsize2 = os.path.getsize("test_logs/test_warning.log")
        msg = ""
        if info_logsize >= info_logsize2:
            msg += "summary() didn't log to info!\n"
        if warning_logsize >= warning_logsize2:
            msg += "summary() didn't log to warning!\n"
        self.assertEqual(msg, "", msg=msg)

    def _test_log_level(self, log_level, log_level_file_list):
        self.s = script.BaseScript(
            config={"log_type": "multi"}, initial_config_file="test/test.json"
        )
        if log_level != FATAL:
            self.s.log("testing", level=log_level)
        else:
            self.s._post_fatal = types.MethodType(_post_fatal, self.s)
            try:
                self.s.fatal("testing")
            except SystemExit:
                contents = None
                if os.path.exists("tmpfile_stdout"):
                    fh = open("tmpfile_stdout")
                    contents = fh.read()
                    fh.close()
                self.assertEqual(contents.rstrip(), test_string, "_post_fatal failed!")
        del self.s
        msg = ""
        for level in log_level_file_list:
            log_path = "test_logs/test_%s.log" % level
            if not os.path.exists(log_path):
                msg += "%s doesn't exist!\n" % log_path
            else:
                filesize = os.path.getsize(log_path)
                if not filesize > 0:
                    msg += "%s is size 0!\n" % log_path
        self.assertEqual(msg, "", msg=msg)

    def test_debug(self):
        self._test_log_level(DEBUG, [])

    def test_ignore(self):
        self._test_log_level(IGNORE, [])

    def test_info(self):
        self._test_log_level(INFO, [INFO])

    def test_warning(self):
        self._test_log_level(WARNING, [INFO, WARNING])

    def test_error(self):
        self._test_log_level(ERROR, [INFO, WARNING, ERROR])

    def test_critical(self):
        self._test_log_level(CRITICAL, [INFO, WARNING, ERROR, CRITICAL])

    def test_fatal(self):
        self._test_log_level(FATAL, [INFO, WARNING, ERROR, CRITICAL, FATAL])


# TestRetry {{{1
class NewError(Exception):
    pass


class OtherError(Exception):
    pass


class TestRetry(unittest.TestCase):
    def setUp(self):
        self.ATTEMPT_N = 1
        self.s = script.BaseScript(initial_config_file="test/test.json")

    def tearDown(self):
        # Close the logfile handles, or windows can't remove the logs
        if hasattr(self, "s") and isinstance(self.s, object):
            del self.s
        cleanup()

    def _succeedOnSecondAttempt(self, foo=None, exception=Exception):
        if self.ATTEMPT_N == 2:
            self.ATTEMPT_N += 1
            return
        self.ATTEMPT_N += 1
        raise exception("Fail")

    def _raiseCustomException(self):
        return self._succeedOnSecondAttempt(exception=NewError)

    def _alwaysPass(self):
        self.ATTEMPT_N += 1
        return True

    def _mirrorArgs(self, *args, **kwargs):
        return args, kwargs

    def _alwaysFail(self):
        raise Exception("Fail")

    def testRetrySucceed(self):
        # Will raise if anything goes wrong
        self.s.retry(self._succeedOnSecondAttempt, attempts=2, sleeptime=0)

    def testRetryFailWithoutCatching(self):
        self.assertRaises(
            Exception, self.s.retry, self._alwaysFail, sleeptime=0, exceptions=()
        )

    def testRetryFailEnsureRaisesLastException(self):
        self.assertRaises(
            SystemExit, self.s.retry, self._alwaysFail, sleeptime=0, error_level=FATAL
        )

    def testRetrySelectiveExceptionSucceed(self):
        self.s.retry(
            self._raiseCustomException,
            attempts=2,
            sleeptime=0,
            retry_exceptions=(NewError,),
        )

    def testRetrySelectiveExceptionFail(self):
        self.assertRaises(
            NewError,
            self.s.retry,
            self._raiseCustomException,
            attempts=2,
            sleeptime=0,
            retry_exceptions=(OtherError,),
        )

    # TODO: figure out a way to test that the sleep actually happened
    def testRetryWithSleep(self):
        self.s.retry(self._succeedOnSecondAttempt, attempts=2, sleeptime=1)

    def testRetryOnlyRunOnce(self):
        """Tests that retry() doesn't call the action again after success"""
        self.s.retry(self._alwaysPass, attempts=3, sleeptime=0)
        # self.ATTEMPT_N gets increased regardless of pass/fail
        self.assertEqual(2, self.ATTEMPT_N)

    def testRetryReturns(self):
        ret = self.s.retry(self._alwaysPass, sleeptime=0)
        self.assertEqual(ret, True)

    def testRetryCleanupIsCalled(self):
        cleanup = mock.Mock()
        self.s.retry(self._succeedOnSecondAttempt, cleanup=cleanup, sleeptime=0)
        self.assertEqual(cleanup.call_count, 1)

    def testRetryArgsPassed(self):
        args = (1, "two", 3)
        kwargs = dict(foo="a", bar=7)
        ret = self.s.retry(
            self._mirrorArgs, args=args, kwargs=kwargs.copy(), sleeptime=0
        )
        print(ret)
        self.assertEqual(ret[0], args)
        self.assertEqual(ret[1], kwargs)


class BaseScriptWithDecorators(script.BaseScript):
    def __init__(self, *args, **kwargs):
        self._tmpdir = tempfile.mkdtemp(suffix=".mozharness")
        option_args = kwargs.get("option_args", [])
        option_args.extend(["--base-work-dir", self._tmpdir])
        kwargs["option_args"] = option_args

        super().__init__(*args, **kwargs)

        self.pre_run_1_args = []
        self.raise_during_pre_run_1 = False
        self.pre_action_1_args = []
        self.raise_during_pre_action_1 = False
        self.pre_action_2_args = []
        self.pre_action_3_args = []
        self.post_action_1_args = []
        self.raise_during_post_action_1 = False
        self.post_action_2_args = []
        self.post_action_3_args = []
        self.post_run_1_args = []
        self.raise_during_post_run_1 = False
        self.post_run_2_args = []
        self.raise_during_build = False

    @script.PreScriptRun
    def pre_run_1(self, *args, **kwargs):
        self.pre_run_1_args.append((args, kwargs))

        if self.raise_during_pre_run_1:
            raise Exception(self.raise_during_pre_run_1)

    @script.PreScriptAction
    def pre_action_1(self, *args, **kwargs):
        self.pre_action_1_args.append((args, kwargs))

        if self.raise_during_pre_action_1:
            raise Exception(self.raise_during_pre_action_1)

    @script.PreScriptAction
    def pre_action_2(self, *args, **kwargs):
        self.pre_action_2_args.append((args, kwargs))

    @script.PreScriptAction("clobber")
    def pre_action_3(self, *args, **kwargs):
        self.pre_action_3_args.append((args, kwargs))

    @script.PostScriptAction
    def post_action_1(self, *args, **kwargs):
        self.post_action_1_args.append((args, kwargs))

        if self.raise_during_post_action_1:
            raise Exception(self.raise_during_post_action_1)

    @script.PostScriptAction
    def post_action_2(self, *args, **kwargs):
        self.post_action_2_args.append((args, kwargs))

    @script.PostScriptAction("build")
    def post_action_3(self, *args, **kwargs):
        self.post_action_3_args.append((args, kwargs))

    @script.PostScriptRun
    def post_run_1(self, *args, **kwargs):
        self.post_run_1_args.append((args, kwargs))

        if self.raise_during_post_run_1:
            raise Exception(self.raise_during_post_run_1)

    @script.PostScriptRun
    def post_run_2(self, *args, **kwargs):
        self.post_run_2_args.append((args, kwargs))

    def build(self):
        if self.raise_during_build:
            raise Exception(self.raise_during_build)


class TestScriptDecorators(unittest.TestCase):
    def setUp(self):
        cleanup()
        self.s = None

    def tearDown(self):
        if isinstance(getattr(self, "s", None), BaseScriptWithDecorators):
            cleanup([self.s._tmpdir])

        if hasattr(self, "s") and isinstance(self.s, object):
            del self.s

        cleanup()

    def test_decorators_registered(self):
        self.s = BaseScriptWithDecorators(initial_config_file="test/test.json")

        self.assertEqual(len(self.s._listeners["pre_run"]), 1)
        self.assertEqual(len(self.s._listeners["pre_action"]), 3)
        self.assertEqual(len(self.s._listeners["post_action"]), 3)
        self.assertEqual(len(self.s._listeners["post_run"]), 2)

    def test_pre_post_fired(self):
        self.s = BaseScriptWithDecorators(initial_config_file="test/test.json")
        self.s.run()

        self.assertEqual(len(self.s.pre_run_1_args), 1)
        self.assertEqual(len(self.s.pre_action_1_args), 2)
        self.assertEqual(len(self.s.pre_action_2_args), 2)
        self.assertEqual(len(self.s.pre_action_3_args), 1)
        self.assertEqual(len(self.s.post_action_1_args), 2)
        self.assertEqual(len(self.s.post_action_2_args), 2)
        self.assertEqual(len(self.s.post_action_3_args), 1)
        self.assertEqual(len(self.s.post_run_1_args), 1)

        self.assertEqual(self.s.pre_run_1_args[0], ((), {}))

        self.assertEqual(self.s.pre_action_1_args[0], (("clobber",), {}))
        self.assertEqual(self.s.pre_action_1_args[1], (("build",), {}))

        # pre_action_3 should only get called for the action it is registered
        # with.
        self.assertEqual(self.s.pre_action_3_args[0], (("clobber",), {}))

        self.assertEqual(self.s.post_action_1_args[0][0], ("clobber",))
        self.assertEqual(self.s.post_action_1_args[0][1], dict(success=True))
        self.assertEqual(self.s.post_action_1_args[1][0], ("build",))
        self.assertEqual(self.s.post_action_1_args[1][1], dict(success=True))

        # post_action_3 should only get called for the action it is registered
        # with.
        self.assertEqual(self.s.post_action_3_args[0], (("build",), dict(success=True)))

        self.assertEqual(self.s.post_run_1_args[0], ((), {}))

    def test_post_always_fired(self):
        self.s = BaseScriptWithDecorators(initial_config_file="test/test.json")
        self.s.raise_during_build = "Testing post always fired."

        with self.assertRaises(SystemExit):
            self.s.run()

        self.assertEqual(len(self.s.pre_run_1_args), 1)
        self.assertEqual(len(self.s.pre_action_1_args), 2)
        self.assertEqual(len(self.s.post_action_1_args), 2)
        self.assertEqual(len(self.s.post_action_2_args), 2)
        self.assertEqual(len(self.s.post_run_1_args), 1)
        self.assertEqual(len(self.s.post_run_2_args), 1)

        self.assertEqual(self.s.post_action_1_args[0][1], dict(success=True))
        self.assertEqual(self.s.post_action_1_args[1][1], dict(success=False))
        self.assertEqual(self.s.post_action_2_args[1][1], dict(success=False))

    def test_pre_run_exception(self):
        self.s = BaseScriptWithDecorators(initial_config_file="test/test.json")
        self.s.raise_during_pre_run_1 = "Error during pre run 1"

        with self.assertRaises(SystemExit):
            self.s.run()

        self.assertEqual(len(self.s.pre_run_1_args), 1)
        self.assertEqual(len(self.s.pre_action_1_args), 0)
        self.assertEqual(len(self.s.post_run_1_args), 1)
        self.assertEqual(len(self.s.post_run_2_args), 1)

    def test_pre_action_exception(self):
        self.s = BaseScriptWithDecorators(initial_config_file="test/test.json")
        self.s.raise_during_pre_action_1 = "Error during pre 1"

        with self.assertRaises(SystemExit):
            self.s.run()

        self.assertEqual(len(self.s.pre_run_1_args), 1)
        self.assertEqual(len(self.s.pre_action_1_args), 1)
        self.assertEqual(len(self.s.pre_action_2_args), 0)
        self.assertEqual(len(self.s.post_action_1_args), 1)
        self.assertEqual(len(self.s.post_action_2_args), 1)
        self.assertEqual(len(self.s.post_run_1_args), 1)
        self.assertEqual(len(self.s.post_run_2_args), 1)

    def test_post_action_exception(self):
        self.s = BaseScriptWithDecorators(initial_config_file="test/test.json")
        self.s.raise_during_post_action_1 = "Error during post 1"

        with self.assertRaises(SystemExit):
            self.s.run()

        self.assertEqual(len(self.s.pre_run_1_args), 1)
        self.assertEqual(len(self.s.post_action_1_args), 1)
        self.assertEqual(len(self.s.post_action_2_args), 1)
        self.assertEqual(len(self.s.post_run_1_args), 1)
        self.assertEqual(len(self.s.post_run_2_args), 1)

    def test_post_run_exception(self):
        self.s = BaseScriptWithDecorators(initial_config_file="test/test.json")
        self.s.raise_during_post_run_1 = "Error during post run 1"

        with self.assertRaises(SystemExit):
            self.s.run()

        self.assertEqual(len(self.s.post_run_1_args), 1)
        self.assertEqual(len(self.s.post_run_2_args), 1)


# main {{{1
if __name__ == "__main__":
    mozunit.main()
