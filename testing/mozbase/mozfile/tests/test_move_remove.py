#!/usr/bin/env python

import errno
import os
import shutil
import stat
import threading
import time
import unittest
from contextlib import contextmanager
from unittest import mock

import mozfile
import mozinfo
import mozunit
import stubs


def mark_readonly(path):
    """Removes all write permissions from given file/directory.

    :param path: path of directory/file of which modes must be changed
    """
    mode = os.stat(path)[stat.ST_MODE]
    os.chmod(path, mode & ~stat.S_IWUSR & ~stat.S_IWGRP & ~stat.S_IWOTH)


class FileOpenCloseThread(threading.Thread):
    """Helper thread for asynchronous file handling"""

    def __init__(self, path, delay, delete=False):
        threading.Thread.__init__(self)
        self.file_opened = threading.Event()
        self.delay = delay
        self.path = path
        self.delete = delete

    def run(self):
        with open(self.path):
            self.file_opened.set()
            time.sleep(self.delay)
        if self.delete:
            try:
                os.remove(self.path)
            except Exception:
                pass


@contextmanager
def wait_file_opened_in_thread(*args, **kwargs):
    thread = FileOpenCloseThread(*args, **kwargs)
    thread.start()
    thread.file_opened.wait()
    try:
        yield thread
    finally:
        thread.join()


RETRY_ATTEMPTS = 6


class MozfileRemoveTestCase(unittest.TestCase):
    """Test our ability to remove directories and files"""

    def setUp(self):
        # Generate a stub
        self.tempdir = stubs.create_stub()

    def tearDown(self):
        if os.path.isdir(self.tempdir):
            shutil.rmtree(self.tempdir)

    def test_remove_directory(self):
        """Test the removal of a directory"""
        self.assertTrue(os.path.isdir(self.tempdir))
        mozfile.remove(self.tempdir)
        self.assertFalse(os.path.exists(self.tempdir))

    def test_remove_directory_with_open_file(self):
        """Test removing a directory with an open file"""
        # Open a file in the generated stub
        filepath = os.path.join(self.tempdir, *stubs.files[1])
        f = open(filepath, "w")
        f.write("foo-bar")

        # keep file open and then try removing the dir-tree
        if mozinfo.isWin:
            # On the Windows family WindowsError should be raised.
            self.assertRaises(OSError, mozfile.remove, self.tempdir)
            self.assertTrue(os.path.exists(self.tempdir))
        else:
            # Folder should be deleted on all other platforms
            mozfile.remove(self.tempdir)
            self.assertFalse(os.path.exists(self.tempdir))

    def test_remove_closed_file(self):
        """Test removing a closed file"""
        # Open a file in the generated stub
        filepath = os.path.join(self.tempdir, *stubs.files[1])
        with open(filepath, "w") as f:
            f.write("foo-bar")

        # Folder should be deleted on all platforms
        mozfile.remove(self.tempdir)
        self.assertFalse(os.path.exists(self.tempdir))

    def test_removing_open_file_with_retry(self):
        """Test removing a file in use with retry"""
        filepath = os.path.join(self.tempdir, *stubs.files[1])

        with wait_file_opened_in_thread(filepath, 0.2):
            # on windows first attempt will fail,
            # and it will be retried until the thread leave the handle
            mozfile.remove(filepath)

        # Check deletion was successful
        self.assertFalse(os.path.exists(filepath))

    def test_removing_already_deleted_file_with_retry(self):
        """Test removing a meanwhile removed file with retry"""
        filepath = os.path.join(self.tempdir, *stubs.files[1])

        with wait_file_opened_in_thread(filepath, 0.2, True):
            # on windows first attempt will fail, and before
            # the retry the opened file will be deleted in the thread
            mozfile.remove(filepath)

        # Check deletion was successful
        self.assertFalse(os.path.exists(filepath))

    def test_remove_gives_up_after_max_retries(self):
        """Test that remove() stops retrying and raises after the last attempt"""
        filepath = os.path.join(self.tempdir, *stubs.files[1])
        busy = OSError(errno.EACCES, "Permission denied")

        with mock.patch("os.remove", autospec=True, side_effect=busy) as remove_mock:
            with self.assertRaises(OSError) as ctx:
                mozfile.remove(filepath)

        self.assertEqual(ctx.exception.errno, errno.EACCES)
        self.assertEqual(remove_mock.call_count, RETRY_ATTEMPTS)
        self.assertTrue(os.path.exists(filepath))

    def test_remove_readonly_tree(self):
        """Test removing a read-only directory"""

        dirpath = os.path.join(self.tempdir, "nested_tree")
        mark_readonly(dirpath)

        # However, mozfile should change write permissions and remove dir.
        mozfile.remove(dirpath)

        self.assertFalse(os.path.exists(dirpath))

    def test_remove_readonly_file(self):
        """Test removing read-only files"""
        filepath = os.path.join(self.tempdir, *stubs.files[1])
        mark_readonly(filepath)

        # However, mozfile should change write permission and then remove file.
        mozfile.remove(filepath)

        self.assertFalse(os.path.exists(filepath))

    @unittest.skipIf(mozinfo.isWin, "Symlinks are not supported on Windows")
    def test_remove_symlink(self):
        """Test removing a symlink"""
        file_path = os.path.join(self.tempdir, *stubs.files[1])
        symlink_path = os.path.join(self.tempdir, "symlink")

        os.symlink(file_path, symlink_path)
        self.assertTrue(os.path.islink(symlink_path))

        # The linked folder and files should not be deleted
        mozfile.remove(symlink_path)
        self.assertFalse(os.path.exists(symlink_path))
        self.assertTrue(os.path.exists(file_path))

    @unittest.skipIf(mozinfo.isWin, "Symlinks are not supported on Windows")
    def test_remove_symlink_in_subfolder(self):
        """Test removing a folder with an contained symlink"""
        file_path = os.path.join(self.tempdir, *stubs.files[0])
        dir_path = os.path.dirname(os.path.join(self.tempdir, *stubs.files[1]))
        symlink_path = os.path.join(dir_path, "symlink")

        os.symlink(file_path, symlink_path)
        self.assertTrue(os.path.islink(symlink_path))

        # The folder with the contained symlink will be deleted but not the
        # original linked file
        mozfile.remove(dir_path)
        self.assertFalse(os.path.exists(dir_path))
        self.assertFalse(os.path.exists(symlink_path))
        self.assertTrue(os.path.exists(file_path))

    @unittest.skipIf(mozinfo.isWin, "Symlinks are not supported on Windows")
    def test_remove_broken_symlink(self):
        """Test removing a folder with an contained symlink"""
        file_path = os.path.join(self.tempdir, "readonly.txt")
        working_link = os.path.join(self.tempdir, "link_to_readonly.txt")
        broken_link = os.path.join(self.tempdir, "broken_link")
        os.symlink(file_path, working_link)
        os.symlink(os.path.join(self.tempdir, "broken.txt"), broken_link)

        self.assertTrue(os.path.exists(file_path))
        self.assertTrue(os.path.islink(working_link))
        self.assertTrue(os.path.islink(broken_link))

        mozfile.remove(working_link)
        self.assertFalse(os.path.lexists(working_link))
        self.assertTrue(os.path.exists(file_path))

        mozfile.remove(broken_link)
        self.assertFalse(os.path.lexists(broken_link))

    @unittest.skipIf(
        mozinfo.isWin or not os.geteuid(),
        "Symlinks are not supported on Windows and cannot run test as root",
    )
    def test_remove_symlink_for_system_path(self):
        """Test removing a symlink which points to a system folder"""
        symlink_path = os.path.join(self.tempdir, "symlink")

        os.symlink(os.path.dirname(self.tempdir), symlink_path)
        self.assertTrue(os.path.islink(symlink_path))

        # The folder with the contained symlink will be deleted but not the
        # original linked file
        mozfile.remove(symlink_path)
        self.assertFalse(os.path.exists(symlink_path))

    def test_remove_path_that_does_not_exists(self):
        not_existing_path = os.path.join(self.tempdir, "I_do_not_not_exists")
        try:
            mozfile.remove(not_existing_path)
        except OSError as exc:
            if exc.errno == errno.ENOENT:
                self.fail("removing non existing path must not raise error")
            raise


class MozFileMoveTestCase(unittest.TestCase):
    def setUp(self):
        # Generate a stub
        self.tempdir = stubs.create_stub()
        self.addCleanup(mozfile.rmtree, self.tempdir)

    def test_move_file(self):
        file_path = os.path.join(self.tempdir, *stubs.files[1])
        moved_path = file_path + ".moved"
        self.assertTrue(os.path.isfile(file_path))
        self.assertFalse(os.path.exists(moved_path))
        mozfile.move(file_path, moved_path)
        self.assertFalse(os.path.exists(file_path))
        self.assertTrue(os.path.isfile(moved_path))

    def test_move_file_with_retry(self):
        file_path = os.path.join(self.tempdir, *stubs.files[1])
        moved_path = file_path + ".moved"

        with wait_file_opened_in_thread(file_path, 0.2):
            # first move attempt should fail on windows and be retried
            mozfile.move(file_path, moved_path)
            self.assertFalse(os.path.exists(file_path))
            self.assertTrue(os.path.isfile(moved_path))

    def test_move_gives_up_after_max_retries(self):
        file_path = os.path.join(self.tempdir, *stubs.files[1])
        moved_path = file_path + ".moved"
        busy = OSError(errno.EACCES, "Permission denied")

        with mock.patch("shutil.move", autospec=True, side_effect=busy) as move_mock:
            with self.assertRaises(OSError) as ctx:
                mozfile.move(file_path, moved_path)

        self.assertEqual(ctx.exception.errno, errno.EACCES)
        self.assertEqual(move_mock.call_count, RETRY_ATTEMPTS)
        self.assertTrue(os.path.isfile(file_path))
        self.assertFalse(os.path.exists(moved_path))


if __name__ == "__main__":
    mozunit.main()
