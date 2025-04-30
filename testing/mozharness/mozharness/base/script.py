# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.
"""Generic script objects.

script.py, along with config.py and log.py, represents the core of
mozharness.
"""

import codecs
import datetime
import errno
import fnmatch
import functools
import gzip
import hashlib
import inspect
import itertools
import os
import platform
import pprint
import re
import shutil
import socket
import ssl
import stat
import subprocess
import sys
import tarfile
import threading
import time
import traceback
import zipfile
import zlib
from contextlib import contextmanager
from io import BytesIO
from queue import Empty, Queue

import mozinfo
import six
from six import binary_type

from mozharness.base.config import BaseConfig
from mozharness.base.log import (
    DEBUG,
    ERROR,
    FATAL,
    INFO,
    WARNING,
    ConsoleLogger,
    LogMixin,
    MultiFileLogger,
    OutputParser,
    SimpleFileLogger,
)

try:
    import httplib
except ImportError:
    import http.client as httplib
try:
    import simplejson as json
except ImportError:
    import json
try:
    from urllib2 import Request, quote, urlopen
except ImportError:
    from urllib.request import Request, quote, urlopen
try:
    import urlparse
except ImportError:
    import urllib.parse as urlparse
if os.name == "nt":
    import locale

    try:
        import win32api
        import win32file

        PYWIN32 = True
    except ImportError:
        PYWIN32 = False

try:
    from urllib2 import HTTPError, URLError
except ImportError:
    from urllib.error import HTTPError, URLError


class ContentLengthMismatch(Exception):
    pass


def _validate_tar_member(member, path):
    def _is_within_directory(directory, target):
        real_directory = os.path.realpath(directory)
        real_target = os.path.realpath(target)
        prefix = os.path.commonprefix([real_directory, real_target])
        return prefix == real_directory

    member_path = os.path.join(path, member.name)
    if not _is_within_directory(path, member_path):
        raise Exception("Attempted path traversal in tar file: " + member.name)
    if member.issym():
        link_path = os.path.join(os.path.dirname(member_path), member.linkname)
        if not _is_within_directory(path, link_path):
            raise Exception("Attempted link path traversal in tar file: " + member.name)
    if member.mode & (stat.S_ISUID | stat.S_ISGID):
        raise Exception("Attempted setuid or setgid in tar file: " + member.name)


def _safe_extract(tar, path=".", *, numeric_owner=False):
    def _files(tar, path):
        for member in tar:
            _validate_tar_member(member, path)
            yield member

    tar.extractall(path, members=_files(tar, path), numeric_owner=numeric_owner)


def platform_name():
    pm = PlatformMixin()

    if pm._is_linux() and pm._is_64_bit():
        return "linux64"
    elif pm._is_linux() and not pm._is_64_bit():
        return "linux"
    elif pm._is_darwin():
        return "macosx"
    elif pm._is_windows() and pm._is_64_bit():
        return "win64"
    elif pm._is_windows() and not pm._is_64_bit():
        return "win32"
    else:
        return None


class PlatformMixin:
    def _is_windows(self):
        """check if the current operating system is Windows.

        Returns:
            bool: True if the current platform is Windows, False otherwise
        """
        system = platform.system()
        if system in ("Windows", "Microsoft"):
            return True
        if system.startswith("CYGWIN"):
            return True
        if os.name == "nt":
            return True

    def _is_darwin(self):
        """check if the current operating system is Darwin.

        Returns:
            bool: True if the current platform is Darwin, False otherwise
        """
        if platform.system() in ("Darwin"):
            return True
        if sys.platform.startswith("darwin"):
            return True

    def _is_linux(self):
        """check if the current operating system is a Linux distribution.

        Returns:
            bool: True if the current platform is a Linux distro, False otherwise
        """
        if platform.system() in ("Linux"):
            return True
        if sys.platform.startswith("linux"):
            return True

    def _is_debian(self):
        """check if the current operating system is explicitly Debian.
        This intentionally doesn't count Debian derivatives like Ubuntu.

        Returns:
            bool: True if the current platform is debian, False otherwise
        """
        if not self._is_linux():
            return False
        self.info(mozinfo.linux_distro)
        re_debian_distro = re.compile("debian")
        return re_debian_distro.match(mozinfo.linux_distro) is not None

    def _is_redhat_based(self):
        """check if the current operating system is a Redhat derived Linux distribution.

        Returns:
            bool: True if the current platform is a Redhat Linux distro, False otherwise
        """
        if not self._is_linux():
            return False
        re_redhat_distro = re.compile("Redhat|Fedora|CentOS|Oracle")
        return re_redhat_distro.match(mozinfo.linux_distro) is not None

    def _is_64_bit(self):
        if self._is_darwin():
            # osx is a special snowflake and to ensure the arch, it is better to use the following
            return (
                sys.maxsize > 2**32
            )  # context: https://docs.python.org/2/library/platform.html
        else:
            # Using machine() gives you the architecture of the host rather
            # than the build type of the Python binary
            return "64" in platform.machine()


# ScriptMixin {{{1
class ScriptMixin(PlatformMixin):
    """This mixin contains simple filesystem commands and the like.

    It also contains some very special but very complex methods that,
    together with logging and config, provide the base for all scripts
    in this harness.

    WARNING !!!
    This class depends entirely on `LogMixin` methods in such a way that it will
    only works if a class inherits from both `ScriptMixin` and `LogMixin`
    simultaneously.

    Depends on self.config of some sort.

    Attributes:
        env (dict): a mapping object representing the string environment.
        script_obj (ScriptMixin): reference to a ScriptMixin instance.
    """

    env = None
    script_obj = None
    ssl_context = None

    def query_filesize(self, file_path):
        self.info("Determining filesize for %s" % file_path)
        length = os.path.getsize(file_path)
        self.info(" %s" % str(length))
        return length

    # TODO this should be parallelized with the to-be-written BaseHelper!
    def query_sha512sum(self, file_path):
        self.info("Determining sha512sum for %s" % file_path)
        m = hashlib.sha512()
        contents = self.read_from_file(file_path, verbose=False, open_mode="rb")
        m.update(contents)
        sha512 = m.hexdigest()
        self.info(" %s" % sha512)
        return sha512

    def platform_name(self):
        """Return the platform name on which the script is running on.
        Returns:
            None: for failure to determine the platform.
            str: The name of the platform (e.g. linux64)
        """
        return platform_name()

    # Simple filesystem commands {{{2
    def mkdir_p(self, path, error_level=ERROR):
        """Create a directory if it doesn't exists.
        This method also logs the creation, error or current existence of the
        directory to be created.

        Args:
            path (str): path of the directory to be created.
            error_level (str): log level name to be used in case of error.

        Returns:
            None: for sucess.
            int: -1 on error
        """

        if not os.path.exists(path):
            self.info("mkdir: %s" % path)
            try:
                os.makedirs(path)
            except OSError:
                self.log("Can't create directory %s!" % path, level=error_level)
                return -1
        else:
            self.debug("mkdir_p: %s Already exists." % path)

    def rmtree(self, path, log_level=INFO, error_level=ERROR, exit_code=-1):
        """Delete an entire directory tree and log its result.
        This method also logs the platform rmtree function, its retries, errors,
        and current existence of the directory.

        Args:
            path (str): path to the directory tree root to remove.
            log_level (str, optional): log level name to for this operation. Defaults
                                       to `INFO`.
            error_level (str, optional): log level name to use in case of error.
                                         Defaults to `ERROR`.
            exit_code (int, optional): useless parameter, not use here.
                                       Defaults to -1

        Returns:
            None: for success
        """

        self.log("rmtree: %s" % path, level=log_level)
        error_message = "Unable to remove %s!" % path
        if self._is_windows():
            # Call _rmtree_windows() directly, since even checking
            # os.path.exists(path) will hang if path is longer than MAX_PATH.
            self.info("Using _rmtree_windows ...")
            return self.retry(
                self._rmtree_windows,
                error_level=error_level,
                error_message=error_message,
                args=(path,),
                log_level=log_level,
            )
        if os.path.exists(path):
            if os.path.isdir(path):
                return self.retry(
                    shutil.rmtree,
                    error_level=error_level,
                    error_message=error_message,
                    retry_exceptions=(OSError,),
                    args=(path,),
                    log_level=log_level,
                )
            else:
                return self.retry(
                    os.remove,
                    error_level=error_level,
                    error_message=error_message,
                    retry_exceptions=(OSError,),
                    args=(path,),
                    log_level=log_level,
                )
        else:
            self.debug("%s doesn't exist." % path)

    def query_msys_path(self, path):
        """replaces the Windows harddrive letter path style with a linux
        path style, e.g. C:// --> /C/
        Note: method, not used in any script.

        Args:
            path (str?): path to convert to the linux path style.
        Returns:
            str: in case `path` is a string. The result is the path with the new notation.
            type(path): `path` itself is returned in case `path` is not str type.
        """
        if not isinstance(path, str):
            return path
        path = path.replace("\\", "/")

        def repl(m):
            return "/%s/" % m.group(1)

        path = re.sub(r"""^([a-zA-Z]):/""", repl, path)
        return path

    def _rmtree_windows(self, path):
        """Windows-specific rmtree that handles path lengths longer than MAX_PATH.
            Ported from clobberer.py.

        Args:
            path (str): directory path to remove.

        Returns:
            None: if the path doesn't exists.
            int: the return number of calling `self.run_command`
            int: in case the path specified is not a directory but a file.
                 0 on success, non-zero on error. Note: The returned value
                 is the result of calling `win32file.DeleteFile`
        """

        assert self._is_windows()
        path = os.path.realpath(path)
        full_path = "\\\\?\\" + path
        if not os.path.exists(full_path):
            return
        if not PYWIN32:
            if not os.path.isdir(path):
                return self.run_command('del /F /Q "%s"' % path)
            else:
                return self.run_command('rmdir /S /Q "%s"' % path)
        # Make sure directory is writable
        win32file.SetFileAttributesW("\\\\?\\" + path, win32file.FILE_ATTRIBUTE_NORMAL)
        # Since we call rmtree() with a file, sometimes
        if not os.path.isdir("\\\\?\\" + path):
            return win32file.DeleteFile("\\\\?\\" + path)

        for ffrec in win32api.FindFiles("\\\\?\\" + path + "\\*.*"):
            file_attr = ffrec[0]
            name = ffrec[8]
            if name == "." or name == "..":
                continue
            full_name = os.path.join(path, name)

            if file_attr & win32file.FILE_ATTRIBUTE_DIRECTORY:
                self._rmtree_windows(full_name)
            else:
                try:
                    win32file.SetFileAttributesW(
                        "\\\\?\\" + full_name, win32file.FILE_ATTRIBUTE_NORMAL
                    )
                    win32file.DeleteFile("\\\\?\\" + full_name)
                except Exception:
                    # DeleteFile fails on long paths, del /f /q works just fine
                    self.run_command('del /F /Q "%s"' % full_name)

        win32file.RemoveDirectory("\\\\?\\" + path)

    def get_filename_from_url(self, url):
        """parse a filename base on an url.

        Args:
            url (str): url to parse for the filename

        Returns:
            str: filename parsed from the url, or `netloc` network location part
                 of the url.
        """

        parsed = urlparse.urlsplit(url.rstrip("/"))
        if parsed.path != "":
            return parsed.path.rsplit("/", 1)[-1]
        else:
            return parsed.netloc

    def _urlopen(self, url, **kwargs):
        """open the url `url` using `urllib2`.`
        This method can be overwritten to extend its complexity

        Args:
            url (str | urllib.request.Request): url to open
            kwargs: Arbitrary keyword arguments passed to the `urllib.request.urlopen` function.

        Returns:
            file-like: file-like object with additional methods as defined in
                       `urllib.request.urlopen`_.
            None: None may be returned if no handler handles the request.

        Raises:
            urllib2.URLError: on errors

        .. urillib.request.urlopen:
        https://docs.python.org/2/library/urllib2.html#urllib2.urlopen
        """
        # http://bugs.python.org/issue13359 - urllib2 does not automatically quote the URL
        url_quoted = quote(url, safe="%/:=&?~#+!$,;'@()*[]|")
        # windows certificates need to be refreshed (https://bugs.python.org/issue36011)
        if self.platform_name() in ("win64",) and platform.architecture()[0] in (
            "x64",
        ):
            if self.ssl_context is None:
                self.ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS)
                self.ssl_context.load_default_certs()
            return urlopen(url_quoted, context=self.ssl_context, **kwargs)
        else:
            return urlopen(url_quoted, **kwargs)

    def fetch_url_into_memory(self, url):
        """Downloads a file from a url into memory instead of disk.

        Args:
            url (str): URL path where the file to be downloaded is located.

        Raises:
            IOError: When the url points to a file on disk and cannot be found
            ContentLengthMismatch: When the length of the retrieved content does not match the
                                   Content-Length response header.
            ValueError: When the scheme of a url is not what is expected.

        Returns:
            BytesIO: contents of url
        """
        self.info(f"Fetch {url} into memory")
        parsed_url = urlparse.urlparse(url)

        if parsed_url.scheme in ("", "file"):
            path = parsed_url.path
            if not os.path.isfile(path):
                raise OSError(f"Could not find file to extract: {url}")

            content_length = os.stat(path).st_size

            # In case we're referrencing a file without file://
            if parsed_url.scheme == "":
                url = "file://%s" % os.path.abspath(url)
                parsed_url = urlparse.urlparse(url)

        request = Request(url)
        # When calling fetch_url_into_memory() you should retry when we raise
        # one of these exceptions:
        # * Bug 1300663 - HTTPError: HTTP Error 404: Not Found
        # * Bug 1300413 - HTTPError: HTTP Error 500: Internal Server Error
        # * Bug 1300943 - HTTPError: HTTP Error 503: Service Unavailable
        # * Bug 1300953 - URLError: <urlopen error [Errno -2] Name or service not known>
        # * Bug 1301594 - URLError: <urlopen error [Errno 10054] An existing connection was ...
        # * Bug 1301597 - URLError: <urlopen error [Errno 8] _ssl.c:504: EOF occurred in ...
        # * Bug 1301855 - URLError: <urlopen error [Errno 60] Operation timed out>
        # * Bug 1302237 - URLError: <urlopen error [Errno 104] Connection reset by peer>
        # * Bug 1301807 - BadStatusLine: ''
        #
        # Bug 1309912 - Adding timeout in hopes to solve blocking on response.read() (bug 1300413)
        response = urlopen(request, timeout=30)

        if parsed_url.scheme in ("http", "https"):
            content_length = int(response.headers.get("Content-Length"))

        response_body = response.read()
        response_body_size = len(response_body)

        self.info(f"Content-Length response header: {content_length}")
        self.info(f"Bytes received: {response_body_size}")

        if response_body_size != content_length:
            raise ContentLengthMismatch(
                "The retrieved Content-Length header declares a body length "
                f"of {content_length} bytes, while we actually retrieved {response_body_size} bytes"
            )

        if response.info().get("Content-Encoding") == "gzip":
            self.info('Content-Encoding is "gzip", so decompressing response body')
            # See http://www.zlib.net/manual.html#Advanced
            # section "ZEXTERN int ZEXPORT inflateInit2 OF....":
            #   Add 32 to windowBits to enable zlib and gzip decoding with automatic
            #   header detection, or add 16 to decode only the gzip format (the zlib
            #   format will return a Z_DATA_ERROR).
            # Adding 16 since we only wish to support gzip encoding.
            file_contents = zlib.decompress(response_body, zlib.MAX_WBITS | 16)
        else:
            file_contents = response_body

        # Use BytesIO instead of StringIO
        # http://stackoverflow.com/questions/34162017/unzip-buffer-with-python/34162395#34162395
        return BytesIO(file_contents)

    def _download_file(self, url, file_name):
        """Helper function for download_file()
        Additionaly this function logs all exceptions as warnings before
        re-raising them

        Args:
            url (str): string containing the URL with the file location
            file_name (str): name of the file where the downloaded file
                             is written.

        Returns:
            str: filename of the written file on disk

        Raises:
            urllib2.URLError: on incomplete download.
            urllib2.HTTPError: on Http error code
            socket.timeout: on connection timeout
            socket.error: on socket error
        """
        # If our URLs look like files, prefix them with file:// so they can
        # be loaded like URLs.
        if not (url.startswith("http") or url.startswith("file://")):
            if not os.path.isfile(url):
                self.fatal("The file %s does not exist" % url)
            url = "file://%s" % os.path.abspath(url)

        try:
            f_length = None
            f = self._urlopen(url, timeout=30)

            if f.info().get("content-length") is not None:
                f_length = int(f.info()["content-length"])
                got_length = 0
            if f.info().get("Content-Encoding") == "gzip":
                # Note, we'll download the full compressed content into its own
                # file, since that allows the gzip library to seek through it.
                # Once downloaded, we'll decompress it into the real target
                # file, and delete the compressed version.
                local_file = open(file_name + ".gz", "wb")
            else:
                local_file = open(file_name, "wb")
            while True:
                block = f.read(1024**2)
                if not block:
                    if f_length is not None and got_length != f_length:
                        raise URLError(
                            "Download incomplete; content-length was %d, "
                            "but only received %d" % (f_length, got_length)
                        )
                    break
                local_file.write(block)
                if f_length is not None:
                    got_length += len(block)
            local_file.close()
            if f.info().get("Content-Encoding") == "gzip":
                # Decompress file into target location, then remove compressed version
                with open(file_name, "wb") as f_out:
                    # On some execution paths, this could be called with python 2.6
                    # whereby gzip.open(...) cannot be used with a 'with' statement.
                    # So let's do this the python 2.6 way...
                    try:
                        f_in = gzip.open(file_name + ".gz", "rb")
                        shutil.copyfileobj(f_in, f_out)
                    finally:
                        f_in.close()
                os.remove(file_name + ".gz")
            return file_name
        except HTTPError as e:
            self.warning(
                "Server returned status %s %s for %s" % (str(e.code), str(e), url)
            )
            raise
        except URLError as e:
            self.warning("URL Error: %s" % url)

            # Failures due to missing local files won't benefit from retry.
            # Raise the original OSError.
            if isinstance(e.args[0], OSError) and e.args[0].errno == errno.ENOENT:
                raise e.args[0]

            raise
        except socket.timeout as e:
            self.warning("Timed out accessing %s: %s" % (url, str(e)))
            raise
        except OSError as e:
            self.warning("Socket error when accessing %s: %s" % (url, str(e)))
            raise

    def _retry_download(self, url, error_level, file_name=None, retry_config=None):
        """Helper method to retry download methods.

        This method calls `self.retry` on `self._download_file` using the passed
        parameters if a file_name is specified. If no file is specified, we will
        instead call `self._urlopen`, which grabs the contents of a url but does
        not create a file on disk.

        Args:
            url (str): URL path where the file is located.
            file_name (str): file_name where the file will be written to.
            error_level (str): log level to use in case an error occurs.
            retry_config (dict, optional): key-value pairs to be passed to
                                           `self.retry`. Defaults to `None`

        Returns:
            str: `self._download_file` return value is returned
            unknown: `self.retry` `failure_status` is returned on failure, which
                     defaults to -1
        """
        retry_args = dict(
            failure_status=None,
            retry_exceptions=(
                HTTPError,
                URLError,
                httplib.HTTPException,
                socket.timeout,
                socket.error,
            ),
            error_message="Can't download from %s to %s!" % (url, file_name),
            error_level=error_level,
        )

        if retry_config:
            retry_args.update(retry_config)

        download_func = self._urlopen
        kwargs = {"url": url}
        if file_name:
            download_func = self._download_file
            kwargs = {"url": url, "file_name": file_name}

        return self.retry(download_func, kwargs=kwargs, **retry_args)

    def _filter_entries(self, namelist, extract_dirs):
        """Filter entries of the archive based on the specified list of to extract dirs."""
        filter_partial = functools.partial(fnmatch.filter, namelist)
        entries = itertools.chain(*map(filter_partial, extract_dirs or ["*"]))

        for entry in entries:
            yield entry

    def unzip(self, compressed_file, extract_to, extract_dirs="*", verbose=False):
        """This method allows to extract a zip file without writing to disk first.

        Args:
            compressed_file (object): File-like object with the contents of a compressed zip file.
            extract_to (str): where to extract the compressed file.
            extract_dirs (list, optional): directories inside the archive file to extract.
                                           Defaults to '*'.
            verbose (bool, optional): whether or not extracted content should be displayed.
                                      Defaults to False.

        Raises:
            zipfile.BadZipfile: on contents of zipfile being invalid
        """
        with zipfile.ZipFile(compressed_file) as bundle:
            entries = self._filter_entries(bundle.namelist(), extract_dirs)

            for entry in entries:
                if verbose:
                    self.info(f" {entry}")

                # Exception to be retried:
                # Bug 1301645 - BadZipfile: Bad CRC-32 for file ...
                #    http://stackoverflow.com/questions/5624669/strange-badzipfile-bad-crc-32-problem/5626098#5626098
                # Bug 1301802 - error: Error -3 while decompressing: invalid stored block lengths
                bundle.extract(entry, path=extract_to)

                # ZipFile doesn't preserve permissions during extraction:
                # http://bugs.python.org/issue15795
                fname = os.path.realpath(os.path.join(extract_to, entry))
                try:
                    # getinfo() can raise KeyError
                    mode = bundle.getinfo(entry).external_attr >> 16 & 0x1FF
                    # Only set permissions if attributes are available. Otherwise all
                    # permissions will be removed eg. on Windows.
                    if mode:
                        os.chmod(fname, mode)

                except KeyError:
                    self.warning(f"{entry} was not found in the zip file")

    def deflate(self, compressed_file, mode, extract_to=".", *args, **kwargs):
        """This method allows to extract a compressed file from a tar{,bz2,gz,xz} files.

        Args:
            compressed_file (object): File-like object with the contents of a compressed file.
            mode (str): string of the form 'filemode[:compression]' (e.g. 'r:gz' or 'r:bz2')
            extract_to (str, optional): where to extract the compressed file.
        """
        with tarfile.open(fileobj=compressed_file, mode=mode) as t:
            _safe_extract(t, path=extract_to)

    def download_unpack(self, url, extract_to=".", extract_dirs="*", verbose=False):
        """Generic method to download and extract a compressed file without writing it
        to disk first.

        Args:
            url (str): URL where the file to be downloaded is located.
            extract_to (str, optional): directory where the downloaded file will
                                        be extracted to.
            extract_dirs (list, optional): directories inside the archive to extract.
                                           Defaults to `*`. It currently only applies to zip files.
            verbose (bool, optional): whether or not extracted content should be displayed.
                                      Defaults to False.

        """

        def _determine_extraction_method_and_kwargs(url):
            EXTENSION_TO_MIMETYPE = {
                "bz2": "application/x-bzip2",
                "xz": "application/x-xz",
                "gz": "application/x-gzip",
                "tar": "application/x-tar",
                "zip": "application/zip",
            }
            MIMETYPES = {
                "application/x-xz": {
                    "function": self.deflate,
                    "kwargs": {"mode": "r:xz"},
                },
                "application/x-bzip2": {
                    "function": self.deflate,
                    "kwargs": {"mode": "r:bz2"},
                },
                "application/x-gzip": {
                    "function": self.deflate,
                    "kwargs": {"mode": "r:gz"},
                },
                "application/x-tar": {
                    "function": self.deflate,
                    "kwargs": {"mode": "r"},
                },
                "application/zip": {
                    "function": self.unzip,
                },
                "application/x-zip-compressed": {
                    "function": self.unzip,
                },
            }

            filename = url.split("/")[-1]
            # XXX: bz2/gz/xz instead of tar.{bz2/gz/xz}
            extension = filename[filename.rfind(".") + 1 :]
            mimetype = EXTENSION_TO_MIMETYPE[extension]
            self.debug(f"Mimetype: {mimetype}")

            function = MIMETYPES[mimetype]["function"]
            kwargs = {
                "compressed_file": compressed_file,
                "extract_to": extract_to,
                "extract_dirs": extract_dirs,
                "verbose": verbose,
            }
            kwargs.update(MIMETYPES[mimetype].get("kwargs", {}))

            return function, kwargs

        # Many scripts overwrite this method and set extract_dirs to None
        extract_dirs = "*" if extract_dirs is None else extract_dirs
        self.info(
            "Downloading and extracting to {} these dirs {} from {}".format(
                extract_to,
                ", ".join(extract_dirs),
                url,
            )
        )

        # 1) Let's fetch the file
        retry_args = dict(
            retry_exceptions=(
                HTTPError,
                URLError,
                httplib.HTTPException,
                socket.timeout,
                socket.error,
                ContentLengthMismatch,
            ),
            sleeptime=30,
            attempts=5,
            error_message=f"Can't download from {url}",
            error_level=FATAL,
        )
        compressed_file = self.retry(
            self.fetch_url_into_memory, kwargs={"url": url}, **retry_args
        )

        # 2) We're guaranteed to have download the file with error_level=FATAL
        #    Let's unpack the file
        function, kwargs = _determine_extraction_method_and_kwargs(url)
        try:
            function(**kwargs)
        except zipfile.BadZipfile:
            # Dump the exception and exit
            self.exception(level=FATAL)

    def load_json_url(self, url, error_level=None, *args, **kwargs):
        """Returns a json object from a url (it retries)."""
        contents = self._retry_download(
            url=url, error_level=error_level, *args, **kwargs
        )
        return json.loads(contents.read())

    # http://www.techniqal.com/blog/2008/07/31/python-file-read-write-with-urllib2/
    # TODO thinking about creating a transfer object.
    def download_file(
        self,
        url,
        file_name=None,
        parent_dir=None,
        create_parent_dir=True,
        error_level=ERROR,
        exit_code=3,
        retry_config=None,
    ):
        """Python wget.
        Download the filename at `url` into `file_name` and put it on `parent_dir`.
        On error log with the specified `error_level`, on fatal exit with `exit_code`.
        Execute all the above based on `retry_config` parameter.

        Args:
            url (str): URL path where the file to be downloaded is located.
            file_name (str, optional): file_name where the file will be written to.
                                       Defaults to urls' filename.
            parent_dir (str, optional): directory where the downloaded file will
                                        be written to. Defaults to current working
                                        directory
            create_parent_dir (bool, optional): create the parent directory if it
                                                doesn't exist. Defaults to `True`
            error_level (str, optional): log level to use in case an error occurs.
                                         Defaults to `ERROR`
            retry_config (dict, optional): key-value pairs to be passed to
                                          `self.retry`. Defaults to `None`

        Returns:
            str: filename where the downloaded file was written to.
            unknown: on failure, `failure_status` is returned.
        """
        if not file_name:
            try:
                file_name = self.get_filename_from_url(url)
            except AttributeError:
                self.log(
                    "Unable to get filename from %s; bad url?" % url,
                    level=error_level,
                    exit_code=exit_code,
                )
                return
        if parent_dir:
            file_name = os.path.join(parent_dir, file_name)
            if create_parent_dir:
                self.mkdir_p(parent_dir, error_level=error_level)
        self.info("Downloading %s to %s" % (url, file_name))
        status = self._retry_download(
            url=url,
            error_level=error_level,
            file_name=file_name,
            retry_config=retry_config,
        )
        if status == file_name:
            self.info("Downloaded %d bytes." % os.path.getsize(file_name))
        return status

    def move(self, src, dest, log_level=INFO, error_level=ERROR, exit_code=-1):
        """recursively move a file or directory (src) to another location (dest).

        Args:
            src (str): file or directory path to move.
            dest (str): file or directory path where to move the content to.
            log_level (str): log level to use for normal operation. Defaults to
                                `INFO`
            error_level (str): log level to use on error. Defaults to `ERROR`

        Returns:
            int: 0 on success. -1 on error.
        """
        self.log("Moving %s to %s" % (src, dest), level=log_level)
        try:
            shutil.move(src, dest)
        # http://docs.python.org/tutorial/errors.html
        except OSError as e:
            self.log("IO error: %s" % str(e), level=error_level, exit_code=exit_code)
            return -1
        except shutil.Error as e:
            # ERROR level ends up reporting the failure to treeherder &
            # pollutes the failure summary list.
            self.log("shutil error: %s" % str(e), level=WARNING, exit_code=exit_code)
            return -1
        return 0

    def chmod(self, path, mode):
        """change `path` mode to `mode`.

        Args:
            path (str): path whose mode will be modified.
            mode (hex): one of the values defined at `stat`_

        .. _stat:
        https://docs.python.org/2/library/os.html#os.chmod
        """

        self.info("Chmoding %s to %s" % (path, str(oct(mode))))
        os.chmod(path, mode)

    def copyfile(
        self,
        src,
        dest,
        log_level=INFO,
        error_level=ERROR,
        copystat=False,
        compress=False,
    ):
        """copy or compress `src` into `dest`.

        Args:
            src (str): filepath to copy.
            dest (str): filepath where to move the content to.
            log_level (str, optional): log level to use for normal operation. Defaults to
                                      `INFO`
            error_level (str, optional): log level to use on error. Defaults to `ERROR`
            copystat (bool, optional): whether or not to copy the files metadata.
                                       Defaults to `False`.
            compress (bool, optional): whether or not to compress the destination file.
                                       Defaults to `False`.

        Returns:
            int: -1 on error
            None: on success
        """

        if compress:
            self.log("Compressing %s to %s" % (src, dest), level=log_level)
            try:
                infile = open(src, "rb")
                outfile = gzip.open(dest, "wb")
                outfile.writelines(infile)
                outfile.close()
                infile.close()
            except OSError as e:
                self.log(
                    "Can't compress %s to %s: %s!" % (src, dest, str(e)),
                    level=error_level,
                )
                return -1
        else:
            self.log("Copying %s to %s" % (src, dest), level=log_level)
            try:
                shutil.copyfile(src, dest)
            except (OSError, shutil.Error) as e:
                self.log(
                    "Can't copy %s to %s: %s!" % (src, dest, str(e)), level=error_level
                )
                return -1

        if copystat:
            try:
                shutil.copystat(src, dest)
            except (OSError, shutil.Error) as e:
                self.log(
                    "Can't copy attributes of %s to %s: %s!" % (src, dest, str(e)),
                    level=error_level,
                )
                return -1

    def copytree(
        self, src, dest, overwrite="no_overwrite", log_level=INFO, error_level=ERROR
    ):
        """An implementation of `shutil.copytree` that allows for `dest` to exist
        and implements different overwrite levels:
        - 'no_overwrite' will keep all(any) existing files in destination tree
        - 'overwrite_if_exists' will only overwrite destination paths that have
                                the same path names relative to the root of the
                                src and destination tree
        - 'clobber' will replace the whole destination tree(clobber) if it exists

        Args:
            src (str): directory path to move.
            dest (str): directory path where to move the content to.
            overwrite (str): string specifying the overwrite level.
            log_level (str, optional): log level to use for normal operation. Defaults to
                                      `INFO`
            error_level (str, optional): log level to use on error. Defaults to `ERROR`

        Returns:
            int: -1 on error
            None: on success
        """

        self.info("copying tree: %s to %s" % (src, dest))
        try:
            if overwrite == "clobber" or not os.path.exists(dest):
                self.rmtree(dest)
                shutil.copytree(src, dest)
            elif overwrite == "no_overwrite" or overwrite == "overwrite_if_exists":
                files = os.listdir(src)
                for f in files:
                    abs_src_f = os.path.join(src, f)
                    abs_dest_f = os.path.join(dest, f)
                    if not os.path.exists(abs_dest_f):
                        if os.path.isdir(abs_src_f):
                            self.mkdir_p(abs_dest_f)
                            self.copytree(abs_src_f, abs_dest_f, overwrite="clobber")
                        else:
                            shutil.copy2(abs_src_f, abs_dest_f)
                    elif overwrite == "no_overwrite":  # destination path exists
                        if os.path.isdir(abs_src_f) and os.path.isdir(abs_dest_f):
                            self.copytree(
                                abs_src_f, abs_dest_f, overwrite="no_overwrite"
                            )
                        else:
                            self.debug(
                                "ignoring path: %s as destination: \
                                    %s exists"
                                % (abs_src_f, abs_dest_f)
                            )
                    else:  # overwrite == 'overwrite_if_exists' and destination exists
                        self.debug("overwriting: %s with: %s" % (abs_dest_f, abs_src_f))
                        self.rmtree(abs_dest_f)

                        if os.path.isdir(abs_src_f):
                            self.mkdir_p(abs_dest_f)
                            self.copytree(
                                abs_src_f, abs_dest_f, overwrite="overwrite_if_exists"
                            )
                        else:
                            shutil.copy2(abs_src_f, abs_dest_f)
            else:
                self.fatal(
                    "%s is not a valid argument for param overwrite" % (overwrite)
                )
        except (OSError, shutil.Error):
            self.exception(
                "There was an error while copying %s to %s!" % (src, dest),
                level=error_level,
            )
            return -1

    def write_to_file(
        self,
        file_path,
        contents,
        verbose=True,
        open_mode="w",
        create_parent_dir=False,
        error_level=ERROR,
    ):
        """Write `contents` to `file_path`, according to `open_mode`.

        Args:
            file_path (str): filepath where the content will be written to.
            contents (str): content to write to the filepath.
            verbose (bool, optional): whether or not to log `contents` value.
                                      Defaults to `True`
            open_mode (str, optional): open mode to use for openning the file.
                                       Defaults to `w`
            create_parent_dir (bool, optional): whether or not to create the
                                                parent directory of `file_path`
            error_level (str, optional): log level to use on error. Defaults to `ERROR`

        Returns:
            str: `file_path` on success
            None: on error.
        """
        self.info("Writing to file %s" % file_path)
        if verbose:
            self.info("Contents:")
            for line in contents.splitlines():
                self.info(" %s" % line)
        if create_parent_dir:
            parent_dir = os.path.dirname(file_path)
            self.mkdir_p(parent_dir, error_level=error_level)
        try:
            fh = open(file_path, open_mode)
            try:
                fh.write(contents)
            except UnicodeEncodeError:
                fh.write(contents.encode("utf-8", "replace"))
            fh.close()
            return file_path
        except OSError:
            self.log("%s can't be opened for writing!" % file_path, level=error_level)

    @contextmanager
    def opened(self, file_path, verbose=True, open_mode="r", error_level=ERROR):
        """Create a context manager to use on a with statement.

        Args:
            file_path (str): filepath of the file to open.
            verbose (bool, optional): useless parameter, not used here.
                Defaults to True.
            open_mode (str, optional): open mode to use for openning the file.
                Defaults to `r`
            error_level (str, optional): log level name to use on error.
                Defaults to `ERROR`

        Yields:
            tuple: (file object, error) pair. In case of error `None` is yielded
                as file object, together with the corresponding error.
                If there is no error, `None` is returned as the error.
        """
        # See opened_w_error in http://www.python.org/dev/peps/pep-0343/
        self.info("Reading from file %s" % file_path)
        try:
            fh = open(file_path, open_mode)
        except OSError as err:
            self.log(
                "unable to open %s: %s" % (file_path, err.strerror), level=error_level
            )
            yield None, err
        else:
            try:
                yield fh, None
            finally:
                fh.close()

    def read_from_file(self, file_path, verbose=True, open_mode="r", error_level=ERROR):
        """Use `self.opened` context manager to open a file and read its
        content.

        Args:
            file_path (str): filepath of the file to read.
            verbose (bool, optional): whether or not to log the file content.
                Defaults to True.
            open_mode (str, optional): open mode to use for openning the file.
                Defaults to `r`
            error_level (str, optional): log level name to use on error.
                Defaults to `ERROR`

        Returns:
            None: on error.
            str: file content on success.
        """
        with self.opened(file_path, verbose, open_mode, error_level) as (fh, err):
            if err:
                return None
            contents = fh.read()
            if verbose:
                self.info("Contents:")
                for line in contents.splitlines():
                    self.info(" %s" % line)
            return contents

    def chdir(self, dir_name):
        self.log("Changing directory to %s." % dir_name)
        os.chdir(dir_name)

    def is_exe(self, fpath):
        """
        Determine if fpath is a file and if it is executable.
        """
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)

    def which(self, program):
        """OS independent implementation of Unix's which command

        Args:
            program (str): name or path to the program whose executable is
                being searched.

        Returns:
            None: if the executable was not found.
            str: filepath of the executable file.
        """
        if self._is_windows() and not program.endswith(".exe"):
            program += ".exe"
        fpath, fname = os.path.split(program)
        if fpath:
            if self.is_exe(program):
                return program
        else:
            # If the exe file is defined in the configs let's use that
            exe = self.query_exe(program)
            if self.is_exe(exe):
                return exe

            # If not defined, let's look for it in the $PATH
            env = self.query_env()
            for path in env["PATH"].split(os.pathsep):
                exe_file = os.path.join(path, program)
                if self.is_exe(exe_file):
                    return exe_file
        return None

    # More complex commands {{{2
    def retry(
        self,
        action,
        attempts=None,
        sleeptime=60,
        max_sleeptime=5 * 60,
        retry_exceptions=(Exception,),
        good_statuses=None,
        cleanup=None,
        error_level=ERROR,
        error_message="%(action)s failed after %(attempts)d tries!",
        failure_status=-1,
        log_level=INFO,
        args=(),
        kwargs={},
    ):
        """generic retry command. Ported from `util.retry`_

        Args:
            action (func): callable object to retry.
            attempts (int, optinal): maximum number of times to call actions.
                Defaults to `self.config.get('global_retries', 5)`
            sleeptime (int, optional): number of seconds to wait between
                attempts. Defaults to 60 and doubles each retry attempt, to
                a maximum of `max_sleeptime'
            max_sleeptime (int, optional): maximum value of sleeptime. Defaults
                to 5 minutes
            retry_exceptions (tuple, optional): Exceptions that should be caught.
                If exceptions other than those listed in `retry_exceptions' are
                raised from `action', they will be raised immediately. Defaults
                to (Exception)
            good_statuses (object, optional): return values which, if specified,
                will result in retrying if the return value isn't listed.
                Defaults to `None`.
            cleanup (func, optional): If `cleanup' is provided and callable
                it will be called immediately after an Exception is caught.
                No arguments will be passed to it. If your cleanup function
                requires arguments it is recommended that you wrap it in an
                argumentless function.
                Defaults to `None`.
            error_level (str, optional): log level name in case of error.
                Defaults to `ERROR`.
            error_message (str, optional): string format to use in case
                none of the attempts success. Defaults to
                '%(action)s failed after %(attempts)d tries!'
            failure_status (int, optional): flag to return in case the retries
                were not successfull. Defaults to -1.
            log_level (str, optional): log level name to use for normal activity.
                Defaults to `INFO`.
            args (tuple, optional): positional arguments to pass onto `action`.
            kwargs (dict, optional): key-value arguments to pass onto `action`.

        Returns:
            object: return value of `action`.
            int: failure status in case of failure retries.
        """
        if not callable(action):
            self.fatal("retry() called with an uncallable method %s!" % action)
        if cleanup and not callable(cleanup):
            self.fatal("retry() called with an uncallable cleanup method %s!" % cleanup)
        if not attempts:
            attempts = self.config.get("global_retries", 5)
        if max_sleeptime < sleeptime:
            self.debug(
                "max_sleeptime %d less than sleeptime %d" % (max_sleeptime, sleeptime)
            )
        n = 0
        while n <= attempts:
            retry = False
            n += 1
            try:
                self.log(
                    "retry: Calling %s with args: %s, kwargs: %s, attempt #%d"
                    % (action.__name__, str(args), str(kwargs), n),
                    level=log_level,
                )
                status = action(*args, **kwargs)
                if good_statuses and status not in good_statuses:
                    retry = True
            except retry_exceptions as e:
                retry = True
                error_message = "%s\nCaught exception: %s" % (error_message, str(e))
                self.log(
                    "retry: attempt #%d caught %s exception: %s"
                    % (n, type(e).__name__, str(e)),
                    level=INFO,
                )

            if not retry:
                return status
            else:
                if cleanup:
                    cleanup()
                if n == attempts:
                    self.log(
                        error_message % {"action": action, "attempts": n},
                        level=error_level,
                    )
                    return failure_status
                if sleeptime > 0:
                    self.log(
                        "retry: Failed, sleeping %d seconds before retrying"
                        % sleeptime,
                        level=log_level,
                    )
                    time.sleep(sleeptime)
                    sleeptime = sleeptime * 2
                    if sleeptime > max_sleeptime:
                        sleeptime = max_sleeptime

    def query_env(
        self,
        partial_env=None,
        replace_dict=None,
        purge_env=(),
        set_self_env=None,
        log_level=DEBUG,
        avoid_host_env=False,
    ):
        """Environment query/generation method.
        The default, self.query_env(), will look for self.config['env']
        and replace any special strings in there ( %(PATH)s ).
        It will then store it as self.env for speeding things up later.

        If you specify partial_env, partial_env will be used instead of
        self.config['env'], and we don't save self.env as it's a one-off.


        Args:
            partial_env (dict, optional): key-value pairs of the name and value
                of different environment variables. Defaults to an empty dictionary.
            replace_dict (dict, optional): key-value pairs to replace the old
                environment variables.
            purge_env (list): environment names to delete from the final
                environment dictionary.
            set_self_env (boolean, optional): whether or not the environment
                variables dictionary should be copied to `self`.
                Defaults to True.
            log_level (str, optional): log level name to use on normal operation.
                Defaults to `DEBUG`.
            avoid_host_env (boolean, optional): if set to True, we will not use
                any environment variables set on the host except PATH.
                Defaults to False.

        Returns:
            dict: environment variables names with their values.
        """
        if partial_env is None:
            if self.env is not None:
                return self.env
            partial_env = self.config.get("env", None)
            if partial_env is None:
                partial_env = {}
            if set_self_env is None:
                set_self_env = True

        env = {"PATH": os.environ["PATH"]} if avoid_host_env else os.environ.copy()

        default_replace_dict = self.query_abs_dirs()
        default_replace_dict["PATH"] = os.environ["PATH"]
        if not replace_dict:
            replace_dict = default_replace_dict
        else:
            for key in default_replace_dict:
                if key not in replace_dict:
                    replace_dict[key] = default_replace_dict[key]
        for key in partial_env.keys():
            env[key] = partial_env[key] % replace_dict
            self.log("ENV: %s is now %s" % (key, env[key]), level=log_level)
        for k in purge_env:
            if k in env:
                del env[k]
        if os.name == "nt":
            pref_encoding = locale.getpreferredencoding()
            for k, v in env.items():
                # When run locally on Windows machines, some environment
                # variables may be unicode.
                env[k] = six.ensure_str(v, pref_encoding)
        if set_self_env:
            self.env = env
        return env

    def query_exe(
        self,
        exe_name,
        exe_dict="exes",
        default=None,
        return_type=None,
        error_level=FATAL,
    ):
        """One way to work around PATH rewrites.

        By default, return exe_name, and we'll fall through to searching
        os.environ["PATH"].
        However, if self.config[exe_dict][exe_name] exists, return that.
        This lets us override exe paths via config file.

        If we need runtime setting, we can build in self.exes support later.

        Args:
            exe_name (str): name of the executable to search for.
            exe_dict(str, optional): name of the dictionary of executables
              present in `self.config`. Defaults to `exes`.
            default (str, optional): default name of the executable to search
              for. Defaults to `exe_name`.
            return_type (str, optional): type to which the original return
              value will be turn into. Only 'list', 'string' and `None` are
              supported. Defaults to `None`.
            error_level (str, optional): log level name to use on error.

        Returns:
            list: in case return_type is 'list'
            str: in case return_type is 'string'
            None: in case return_type is `None`
            Any: if the found executable is not of type list, tuple nor str.
        """
        if default is None:
            default = exe_name
        exe = self.config.get(exe_dict, {}).get(exe_name, default)
        repl_dict = {}
        if hasattr(self.script_obj, "query_abs_dirs"):
            # allow for 'make': '%(abs_work_dir)s/...' etc.
            dirs = self.script_obj.query_abs_dirs()
            repl_dict.update(dirs)
        if isinstance(exe, dict):
            found = False
            # allow for searchable paths of the exe
            for name, path in exe.items():
                if isinstance(path, list) or isinstance(path, tuple):
                    path = [x % repl_dict for x in path]
                    if all([os.path.exists(section) for section in path]):
                        found = True
                elif isinstance(path, str):
                    path = path % repl_dict
                    if os.path.exists(path):
                        found = True
                else:
                    self.log(
                        "a exes %s dict's value is not a string, list, or tuple. Got key "
                        "%s and value %s" % (exe_name, name, str(path)),
                        level=error_level,
                    )
                if found:
                    exe = path
                    break
            else:
                self.log(
                    "query_exe was a searchable dict but an existing "
                    "path could not be determined. Tried searching in "
                    "paths: %s" % (str(exe)),
                    level=error_level,
                )
                return None
        elif isinstance(exe, list) or isinstance(exe, tuple):
            exe = [x % repl_dict for x in exe]
        elif isinstance(exe, str):
            exe = exe % repl_dict
        else:
            self.log(
                "query_exe: %s is not a list, tuple, dict, or string: "
                "%s!" % (exe_name, str(exe)),
                level=error_level,
            )
            return exe
        if return_type == "list":
            if isinstance(exe, str):
                exe = [exe]
        elif return_type == "string":
            if isinstance(exe, list):
                exe = subprocess.list2cmdline(exe)
        elif return_type is not None:
            self.log(
                "Unknown return_type type %s requested in query_exe!" % return_type,
                level=error_level,
            )
        return exe

    def run_command(
        self,
        command,
        cwd=None,
        error_list=None,
        halt_on_failure=False,
        success_codes=None,
        env=None,
        partial_env=None,
        return_type="status",
        throw_exception=False,
        output_parser=None,
        output_timeout=None,
        fatal_exit_code=2,
        error_level=ERROR,
        **kwargs,
    ):
        """Run a command, with logging and error parsing.
        TODO: context_lines

        error_list example:
        [{'regex': re.compile('^Error: LOL J/K'), level=IGNORE},
         {'regex': re.compile('^Error:'), level=ERROR, contextLines='5:5'},
         {'substr': 'THE WORLD IS ENDING', level=FATAL, contextLines='20:'}
        ]
        (context_lines isn't written yet)

        Args:
            command (str | list | tuple): command or sequence of commands to
              execute and log.
            cwd (str, optional): directory path from where to execute the
              command. Defaults to `None`.
            error_list (list, optional): list of errors to pass to
              `mozharness.base.log.OutputParser`. Defaults to `None`.
            halt_on_failure (bool, optional): whether or not to redefine the
              log level as `FATAL` on errors. Defaults to False.
            success_codes (int, optional): numeric value to compare against
              the command return value.
            env (dict, optional): key-value of environment values to use to
              run the command. Defaults to None.
            partial_env (dict, optional): key-value of environment values to
              replace from the current environment values. Defaults to None.
            return_type (str, optional): if equal to 'num_errors' then the
              amount of errors matched by `error_list` is returned. Defaults
              to 'status'.
            throw_exception (bool, optional): whether or not to raise an
              exception if the return value of the command doesn't match
              any of the `success_codes`. Defaults to False.
            output_parser (OutputParser, optional): lets you provide an
              instance of your own OutputParser subclass. Defaults to `OutputParser`.
            output_timeout (int): amount of seconds to wait for output before
              the process is killed.
            fatal_exit_code (int, optional): call `self.fatal` if the return value
              of the command is not in `success_codes`. Defaults to 2.
            error_level (str, optional): log level name to use on error. Defaults
              to `ERROR`.
            **kwargs: Arbitrary keyword arguments.

        Returns:
            int: -1 on error.
            Any: `command` return value is returned otherwise.
        """
        if success_codes is None:
            success_codes = [0]
        if cwd is not None:
            if not os.path.isdir(cwd):
                level = error_level
                if halt_on_failure:
                    level = FATAL
                self.log(
                    "Can't run command %s in non-existent directory '%s'!"
                    % (command, cwd),
                    level=level,
                )
                return -1
            self.info("Running command: %s in %s" % (command, cwd))
        else:
            self.info("Running command: %s" % (command,))
        if isinstance(command, list) or isinstance(command, tuple):
            self.info("Copy/paste: %s" % subprocess.list2cmdline(command))
        shell = True
        if isinstance(command, list) or isinstance(command, tuple):
            shell = False
        if env is None:
            if partial_env:
                self.info("Using partial env: %s" % pprint.pformat(partial_env))
                env = self.query_env(partial_env=partial_env)
        else:
            if hasattr(self, "previous_env") and env == self.previous_env:
                self.info("Using env: (same as previous command)")
            else:
                self.info("Using env: %s" % pprint.pformat(env))
                self.previous_env = env

        if output_parser is None:
            parser = OutputParser(
                config=self.config, log_obj=self.log_obj, error_list=error_list
            )
        else:
            parser = output_parser

        try:
            p = subprocess.Popen(
                command,
                shell=shell,
                stdout=subprocess.PIPE,
                cwd=cwd,
                stderr=subprocess.STDOUT,
                env=env,
                bufsize=0,
            )
            if output_timeout:
                self.info(
                    "Calling %s with output_timeout %d" % (command, output_timeout)
                )

                def reader(fh, queue):
                    for line in iter(fh.readline, b""):
                        queue.put(line)
                    # Give a chance to the reading loop to exit without a timeout.
                    queue.put(b"")

                queue = Queue()
                threading.Thread(
                    target=reader, args=(p.stdout, queue), daemon=True
                ).start()

                try:
                    for line in iter(
                        functools.partial(queue.get, timeout=output_timeout), b""
                    ):
                        parser.add_lines(line)
                except Empty:
                    self.info(
                        "Automation Error: mozharness timed out after "
                        "%s seconds running %s" % (str(output_timeout), str(command))
                    )
                    p.kill()
            else:
                for line in iter(p.stdout.readline, b""):
                    parser.add_lines(line)
            p.wait()
            returncode = p.returncode
        except KeyboardInterrupt:
            level = error_level
            if halt_on_failure:
                level = FATAL
            self.log(
                "Process interrupted by the user, killing process with pid %s" % p.pid,
                level=level,
            )
            p.kill()
            return -1
        except OSError as e:
            level = error_level
            if halt_on_failure:
                level = FATAL
            self.log(
                "caught OS error %s: %s while running %s"
                % (e.errno, e.strerror, command),
                level=level,
            )
            return -1

        if returncode not in success_codes:
            if throw_exception:
                raise subprocess.CalledProcessError(returncode, command)
        # Force level to be INFO as message is not necessary in Treeherder
        self.log("Return code: %d" % returncode, level=INFO)

        if halt_on_failure:
            _fail = False
            if returncode not in success_codes:
                self.log(
                    "%s not in success codes: %s" % (returncode, success_codes),
                    level=error_level,
                )
                _fail = True
            if parser.num_errors:
                self.log("failures found while parsing output", level=error_level)
                _fail = True
            if _fail:
                self.return_code = fatal_exit_code
                self.fatal(
                    "Halting on failure while running %s" % (command,),
                    exit_code=fatal_exit_code,
                )
        if return_type == "num_errors":
            return parser.num_errors
        return returncode

    def get_output_from_command(
        self,
        command,
        cwd=None,
        halt_on_failure=False,
        env=None,
        silent=False,
        log_level=INFO,
        tmpfile_base_path="tmpfile",
        return_type="output",
        save_tmpfiles=False,
        throw_exception=False,
        fatal_exit_code=2,
        ignore_errors=False,
        success_codes=None,
        output_filter=None,
    ):
        """Similar to run_command, but where run_command is an
        os.system(command) analog, get_output_from_command is a `command`
        analog.

        Less error checking by design, though if we figure out how to
        do it without borking the output, great.

        TODO: binary mode? silent is kinda like that.
        TODO: since p.wait() can take a long time, optionally log something
        every N seconds?
        TODO: optionally only keep the first or last (N) line(s) of output?
        TODO: optionally only return the tmp_stdout_filename?

        ignore_errors=True is for the case where a command might produce standard
        error output, but you don't particularly care; setting to True will
        cause standard error to be logged at DEBUG rather than ERROR

        Args:
            command (str | list): command or list of commands to
              execute and log.
            cwd (str, optional): directory path from where to execute the
              command. Defaults to `None`.
            halt_on_failure (bool, optional): whether or not to redefine the
              log level as `FATAL` on error. Defaults to False.
            env (dict, optional): key-value of environment values to use to
              run the command. Defaults to None.
            silent (bool, optional): whether or not to output the stdout of
              executing the command. Defaults to False.
            log_level (str, optional): log level name to use on normal execution.
              Defaults to `INFO`.
            tmpfile_base_path (str, optional): base path of the file to which
              the output will be writen to. Defaults to 'tmpfile'.
            return_type (str, optional): if equal to 'output' then the complete
              output of the executed command is returned, otherwise the written
              filenames are returned. Defaults to 'output'.
            save_tmpfiles (bool, optional): whether or not to save the temporary
              files created from the command output. Defaults to False.
            throw_exception (bool, optional): whether or not to raise an
              exception if the return value of the command is not zero.
              Defaults to False.
            fatal_exit_code (int, optional): call self.fatal if the return value
              of the command match this value.
            ignore_errors (bool, optional): whether or not to change the log
              level to `ERROR` for the output of stderr. Defaults to False.
            success_codes (int, optional): numeric value to compare against
              the command return value.
            output_filter (func, optional): provide a function to filter output
              so that noise is reduced and lines are sanitized.  default: None

        Returns:
            None: if the cwd is not a directory.
            None: on IOError.
            tuple: stdout and stderr filenames.
            str: stdout output.
        """
        if cwd:
            if not os.path.isdir(cwd):
                level = ERROR
                if halt_on_failure:
                    level = FATAL
                self.log(
                    "Can't run command %s in non-existent directory %s!"
                    % (command, cwd),
                    level=level,
                )
                return None
            self.info("Getting output from command: %s in %s" % (command, cwd))
        else:
            self.info("Getting output from command: %s" % command)
        if isinstance(command, list):
            self.info("Copy/paste: %s" % subprocess.list2cmdline(command))
        # This could potentially return something?
        tmp_stdout = None
        tmp_stderr = None
        tmp_stdout_filename = "%s_stdout" % tmpfile_base_path
        tmp_stderr_filename = "%s_stderr" % tmpfile_base_path
        if success_codes is None:
            success_codes = [0]

        # TODO probably some more elegant solution than 2 similar passes
        try:
            tmp_stdout = open(tmp_stdout_filename, "w")
        except OSError:
            level = ERROR
            if halt_on_failure:
                level = FATAL
            self.log(
                "Can't open %s for writing!" % tmp_stdout_filename + self.exception(),
                level=level,
            )
            return None
        try:
            tmp_stderr = open(tmp_stderr_filename, "w")
        except OSError:
            level = ERROR
            if halt_on_failure:
                level = FATAL
            self.log(
                "Can't open %s for writing!" % tmp_stderr_filename + self.exception(),
                level=level,
            )
            return None
        shell = True
        if isinstance(command, list):
            shell = False

        p = subprocess.Popen(
            command,
            shell=shell,
            stdout=tmp_stdout,
            cwd=cwd,
            stderr=tmp_stderr,
            env=env,
            bufsize=0,
        )
        # XXX: changed from self.debug to self.log due to this error:
        #      TypeError: debug() takes exactly 1 argument (2 given)
        self.log(
            "Temporary files: %s and %s" % (tmp_stdout_filename, tmp_stderr_filename),
            level=DEBUG,
        )
        p.wait()
        tmp_stdout.close()
        tmp_stderr.close()
        return_level = DEBUG
        output = None
        if return_type == "output" or not silent:
            if os.path.exists(tmp_stdout_filename) and os.path.getsize(
                tmp_stdout_filename
            ):
                output = self.read_from_file(tmp_stdout_filename, verbose=False)
                if output_filter:
                    output = output_filter(output)
                if not silent:
                    self.log("Output received:", level=log_level)
                    output_lines = output.rstrip().splitlines()
                    for line in output_lines:
                        if not line or line.isspace():
                            continue
                        if isinstance(line, binary_type):
                            line = line.decode("utf-8")
                        self.log(" %s" % line, level=log_level)
                    output = "\n".join(output_lines)
        if os.path.exists(tmp_stderr_filename) and os.path.getsize(tmp_stderr_filename):
            errors = self.read_from_file(tmp_stderr_filename, verbose=False)
            if output_filter:
                errors = output_filter(errors)
            if errors:
                if not ignore_errors:
                    return_level = ERROR
                self.log("Errors received:", level=return_level)
                for line in errors.rstrip().splitlines():
                    if not line or line.isspace():
                        continue
                    if isinstance(line, binary_type):
                        line = line.decode("utf-8")
                    self.log(" %s" % line, level=return_level)
        elif p.returncode not in success_codes and not ignore_errors:
            return_level = ERROR
        # Clean up.
        if not save_tmpfiles:
            self.rmtree(tmp_stderr_filename, log_level=DEBUG)
            self.rmtree(tmp_stdout_filename, log_level=DEBUG)
        if p.returncode and throw_exception:
            raise subprocess.CalledProcessError(p.returncode, command)
        # Force level to be INFO as message is not necessary in Treeherder
        self.log("Return code: %d" % p.returncode, level=INFO)
        if halt_on_failure and return_level == ERROR:
            self.return_code = fatal_exit_code
            self.fatal(
                "Halting on failure while running %s" % command,
                exit_code=fatal_exit_code,
            )
        # Hm, options on how to return this? I bet often we'll want
        # output_lines[0] with no newline.
        if return_type != "output":
            return (tmp_stdout_filename, tmp_stderr_filename)
        else:
            return output

    def _touch_file(self, file_name, times=None, error_level=FATAL):
        """touch a file.

        Args:
            file_name (str): name of the file to touch.
            times (tuple, optional): 2-tuple as specified by `os.utime`_
              Defaults to None.
            error_level (str, optional): log level name in case of error.
              Defaults to `FATAL`.

        .. _`os.utime`:
           https://docs.python.org/3.4/library/os.html?highlight=os.utime#os.utime
        """
        self.info("Touching: %s" % file_name)
        try:
            os.utime(file_name, times)
        except OSError:
            try:
                open(file_name, "w").close()
            except OSError as e:
                msg = "I/O error(%s): %s" % (e.errno, e.strerror)
                self.log(msg, error_level=error_level)
        os.utime(file_name, times)

    def unpack(
        self,
        filename,
        extract_to,
        extract_dirs=None,
        error_level=ERROR,
        fatal_exit_code=2,
        verbose=False,
    ):
        """The method allows to extract a file regardless of its extension.

        Args:
            filename (str): filename of the compressed file.
            extract_to (str): where to extract the compressed file.
            extract_dirs (list, optional): directories inside the archive file to extract.
                                           Defaults to `None`.
            fatal_exit_code (int, optional): call `self.fatal` if the return value
              of the command is not in `success_codes`. Defaults to 2.
            verbose (bool, optional): whether or not extracted content should be displayed.
                                      Defaults to False.

        Raises:
            IOError: on `filename` file not found.

        """
        if not os.path.isfile(filename):
            raise OSError("Could not find file to extract: %s" % filename)

        if zipfile.is_zipfile(filename):
            try:
                self.info(f"Using ZipFile to extract {filename} to {extract_to}")
                with zipfile.ZipFile(filename) as bundle:
                    for entry in self._filter_entries(bundle.namelist(), extract_dirs):
                        if verbose:
                            self.info(" %s" % entry)
                        bundle.extract(entry, path=extract_to)

                        # ZipFile doesn't preserve permissions during extraction:
                        # http://bugs.python.org/issue15795
                        fname = os.path.realpath(os.path.join(extract_to, entry))
                        mode = bundle.getinfo(entry).external_attr >> 16 & 0x1FF
                        # Only set permissions if attributes are available. Otherwise all
                        # permissions will be removed eg. on Windows.
                        if mode:
                            os.chmod(fname, mode)
            except zipfile.BadZipfile as e:
                self.log(
                    "%s (%s)" % (str(e), filename),
                    level=error_level,
                    exit_code=fatal_exit_code,
                )

        # Bug 1211882 - is_tarfile cannot be trusted for dmg files
        elif tarfile.is_tarfile(filename) and not filename.lower().endswith(".dmg"):
            try:
                self.info(f"Using TarFile to extract {filename} to {extract_to}")
                with tarfile.open(filename) as bundle:
                    for entry in self._filter_entries(bundle.getnames(), extract_dirs):
                        _validate_tar_member(bundle.getmember(entry), extract_to)
                        if verbose:
                            self.info(" %s" % entry)
                        bundle.extract(entry, path=extract_to)
            except tarfile.TarError as e:
                self.log(
                    "%s (%s)" % (str(e), filename),
                    level=error_level,
                    exit_code=fatal_exit_code,
                )
        else:
            self.log(
                "No extraction method found for: %s" % filename,
                level=error_level,
                exit_code=fatal_exit_code,
            )

    def is_taskcluster(self):
        """Returns boolean indicating if we're running in TaskCluster."""
        # This may need expanding in the future to work on
        return "TASKCLUSTER_WORKER_TYPE" in os.environ


def PreScriptRun(func):
    """Decorator for methods that will be called before script execution.

    Each method on a BaseScript having this decorator will be called at the
    beginning of BaseScript.run().

    The return value is ignored. Exceptions will abort execution.
    """
    func._pre_run_listener = True
    return func


def PostScriptRun(func):
    """Decorator for methods that will be called after script execution.

    This is similar to PreScriptRun except it is called at the end of
    execution. The method will always be fired, even if execution fails.
    """
    func._post_run_listener = True
    return func


def PreScriptAction(action=None):
    """Decorator for methods that will be called at the beginning of each action.

    Each method on a BaseScript having this decorator will be called during
    BaseScript.run() before an individual action is executed. The method will
    receive the action's name as an argument.

    If no values are passed to the decorator, it will be applied to every
    action. If a string is passed, the decorated function will only be called
    for the action of that name.

    The return value of the method is ignored. Exceptions will abort execution.
    """

    def _wrapped(func):
        func._pre_action_listener = action
        return func

    def _wrapped_none(func):
        func._pre_action_listener = None
        return func

    if type(action) is type(_wrapped):
        return _wrapped_none(action)

    return _wrapped


def PostScriptAction(action=None):
    """Decorator for methods that will be called at the end of each action.

    This behaves similarly to PreScriptAction. It varies in that it is called
    after execution of the action.

    The decorated method will receive the action name as a positional argument.
    It will then receive the following named arguments:

        success - Bool indicating whether the action finished successfully.

    The decorated method will always be called, even if the action threw an
    exception.

    The return value is ignored.
    """

    def _wrapped(func):
        func._post_action_listener = action
        return func

    def _wrapped_none(func):
        func._post_action_listener = None
        return func

    if type(action) is type(_wrapped):
        return _wrapped_none(action)

    return _wrapped


# BaseScript {{{1
class BaseScript(ScriptMixin, LogMixin):
    def __init__(
        self,
        config_options=None,
        ConfigClass=BaseConfig,
        default_log_level="info",
        **kwargs,
    ):
        self._return_code = 0
        super(BaseScript, self).__init__()

        self.log_obj = None
        self.abs_dirs = None
        if config_options is None:
            config_options = []
        self.summary_list = []
        self.failures = []
        rw_config = ConfigClass(config_options=config_options, **kwargs)
        self.config = rw_config.get_read_only_config()
        self.actions = tuple(rw_config.actions)
        self.all_actions = tuple(rw_config.all_actions)
        self.env = None
        self.new_log_obj(default_log_level=default_log_level)
        self.script_obj = self

        # Indicate we're a source checkout if VCS directory is present at the
        # appropriate place. This code will break if this file is ever moved
        # to another directory.
        self.topsrcdir = None

        srcreldir = "testing/mozharness/mozharness/base"
        here = os.path.normpath(os.path.dirname(__file__))
        if here.replace("\\", "/").endswith(srcreldir):
            topsrcdir = os.path.normpath(os.path.join(here, "..", "..", "..", ".."))
            hg_dir = os.path.join(topsrcdir, ".hg")
            # .git might be a directory or a file for a git worktree
            git_path = os.path.join(topsrcdir, ".git")
            if os.path.isdir(hg_dir) or os.path.exists(git_path):
                self.topsrcdir = topsrcdir

        # Set self.config to read-only.
        #
        # We can create intermediate config info programmatically from
        # this in a repeatable way, with logs; this is how we straddle the
        # ideal-but-not-user-friendly static config and the
        # easy-to-write-hard-to-debug writable config.
        #
        # To allow for other, script-specific configurations
        # (e.g., props json parsing), before locking,
        # call self._pre_config_lock().  If needed, this method can
        # alter self.config.
        self._pre_config_lock(rw_config)
        self._config_lock()

        self.info("Run as %s" % rw_config.command_line)
        if self.config.get("dump_config_hierarchy"):
            # we only wish to dump and display what self.config is made up of,
            # against the current script + args, without actually running any
            # actions
            self._dump_config_hierarchy(rw_config.all_cfg_files_and_dicts)
        if self.config.get("dump_config"):
            self.dump_config(exit_on_finish=True)

        # Collect decorated methods. We simply iterate over the attributes of
        # the current class instance and look for signatures deposited by
        # the decorators.
        self._listeners = dict(
            pre_run=[],
            pre_action=[],
            post_action=[],
            post_run=[],
        )
        for k in dir(self):
            try:
                item = self._getattr(k)
            except Exception as e:
                item = None
                self.warning(
                    "BaseScript collecting decorated methods: "
                    f"failure to get attribute {k}: {str(e)}"
                )
            if not item:
                continue

            # We only decorate methods, so ignore other types.
            if not inspect.ismethod(item):
                continue

            if hasattr(item, "_pre_run_listener"):
                self._listeners["pre_run"].append(k)

            if hasattr(item, "_pre_action_listener"):
                self._listeners["pre_action"].append((k, item._pre_action_listener))

            if hasattr(item, "_post_action_listener"):
                self._listeners["post_action"].append((k, item._post_action_listener))

            if hasattr(item, "_post_run_listener"):
                self._listeners["post_run"].append(k)

    def _getattr(self, name):
        # `getattr(self, k)` will call the method `k` for any property
        # access. If the property depends upon a module which has not
        # been imported at the time the BaseScript initializer is
        # executed, this property access will result in an
        # Exception. Until Python 3's `inspect.getattr_static` is
        # available, the simplest approach is to ignore the specific
        # properties which are known to cause issues. Currently
        # adb_path and device are ignored since they require the
        # availablity of the mozdevice package which is not guaranteed
        # when BaseScript is called.
        property_list = set(["adb_path", "device"])
        if six.PY2:
            if name in property_list:
                item = None
            else:
                item = getattr(self, name)
        else:
            item = inspect.getattr_static(self, name)
            if type(item) is property:
                item = None
            else:
                item = getattr(self, name)
        return item

    def _dump_config_hierarchy(self, cfg_files):
        """interpret each config file used.

        This will show which keys/values are being added or overwritten by
        other config files depending on their hierarchy (when they were added).
        """
        # go through each config_file. We will start with the lowest and
        # print its keys/values that are being used in self.config. If any
        # keys/values are present in a config file with a higher precedence,
        # ignore those.
        dirs = self.query_abs_dirs()
        cfg_files_dump_config = {}  # we will dump this to file
        # keep track of keys that did not come from a config file
        keys_not_from_file = set(self.config.keys())
        if not cfg_files:
            cfg_files = []
        self.info("Total config files: %d" % (len(cfg_files)))
        if len(cfg_files):
            self.info("cfg files used from lowest precedence to highest:")
        for i, (target_file, target_dict) in enumerate(cfg_files):
            unique_keys = set(target_dict.keys())
            unique_dict = {}
            # iterate through the target_dicts remaining 'higher' cfg_files
            remaining_cfgs = cfg_files[slice(i + 1, len(cfg_files))]
            # where higher == more precedent
            for ii, (higher_file, higher_dict) in enumerate(remaining_cfgs):
                # now only keep keys/values that are not overwritten by a
                # higher config
                unique_keys = unique_keys.difference(set(higher_dict.keys()))
            # unique_dict we know now has only keys/values that are unique to
            # this config file.
            unique_dict = dict((key, target_dict.get(key)) for key in unique_keys)
            cfg_files_dump_config[target_file] = unique_dict
            self.action_message("Config File %d: %s" % (i + 1, target_file))
            self.info(pprint.pformat(unique_dict))
            # let's also find out which keys/values from self.config are not
            # from each target config file dict
            keys_not_from_file = keys_not_from_file.difference(set(target_dict.keys()))
        not_from_file_dict = dict(
            (key, self.config.get(key)) for key in keys_not_from_file
        )
        cfg_files_dump_config["not_from_cfg_file"] = not_from_file_dict
        self.action_message(
            "Not from any config file (default_config, " "cmd line options, etc)"
        )
        self.info(pprint.pformat(not_from_file_dict))

        # finally, let's dump this output as JSON and exit early
        self.dump_config(
            os.path.join(dirs["abs_log_dir"], "localconfigfiles.json"),
            cfg_files_dump_config,
            console_output=False,
            exit_on_finish=True,
        )

    def _pre_config_lock(self, rw_config):
        """This empty method can allow for config checking and manipulation
        before the config lock, when overridden in scripts.
        """
        pass

    def _config_lock(self):
        """After this point, the config is locked and should not be
        manipulated (based on mozharness.base.config.ReadOnlyDict)
        """
        self.config.lock()

    def _possibly_run_method(self, method_name, error_if_missing=False):
        """This is here for run()."""
        if hasattr(self, method_name) and callable(self._getattr(method_name)):
            return getattr(self, method_name)()
        elif error_if_missing:
            self.error("No such method %s!" % method_name)

    def run_action(self, action):
        if action not in self.actions:
            self.action_message("Skipping %s step." % action)
            return

        method_name = action.replace("-", "_")
        self.action_message("Running %s step." % action)

        # An exception during a pre action listener should abort execution.
        for fn, target in self._listeners["pre_action"]:
            if target is not None and target != action:
                continue

            try:
                self.info("Running pre-action listener: %s" % fn)
                method = getattr(self, fn)
                method(action)
            except Exception:
                self.error(
                    "Exception during pre-action for %s: %s"
                    % (action, traceback.format_exc())
                )

                for fn, target in self._listeners["post_action"]:
                    if target is not None and target != action:
                        continue

                    try:
                        self.info("Running post-action listener: %s" % fn)
                        method = getattr(self, fn)
                        method(action, success=False)
                    except Exception:
                        self.error(
                            "An additional exception occurred during "
                            "post-action for %s: %s" % (action, traceback.format_exc())
                        )

                self.fatal("Aborting due to exception in pre-action listener.")

        # We always run post action listeners, even if the main routine failed.
        success = False
        try:
            self.info("Running main action method: %s" % method_name)
            self._possibly_run_method("preflight_%s" % method_name)
            self._possibly_run_method(method_name, error_if_missing=True)
            self._possibly_run_method("postflight_%s" % method_name)
            success = True
        finally:
            post_success = True
            for fn, target in self._listeners["post_action"]:
                if target is not None and target != action:
                    continue

                try:
                    self.info("Running post-action listener: %s" % fn)
                    method = getattr(self, fn)
                    method(action, success=success and self.return_code == 0)
                except Exception:
                    post_success = False
                    self.error(
                        "Exception during post-action for %s: %s"
                        % (action, traceback.format_exc())
                    )

            step_result = "success" if success else "failed"
            self.action_message("Finished %s step (%s)" % (action, step_result))

            if not post_success:
                self.fatal("Aborting due to failure in post-action listener.")

    def run(self):
        """Default run method.
        This is the "do everything" method, based on actions and all_actions.

        First run self.dump_config() if it exists.
        Second, go through the list of all_actions.
        If they're in the list of self.actions, try to run
        self.preflight_ACTION(), self.ACTION(), and self.postflight_ACTION().

        Preflight is sanity checking before doing anything time consuming or
        destructive.

        Postflight is quick testing for success after an action.

        """
        for fn in self._listeners["pre_run"]:
            try:
                self.info("Running pre-run listener: %s" % fn)
                method = getattr(self, fn)
                method()
            except Exception:
                self.error(
                    "Exception during pre-run listener: %s" % traceback.format_exc()
                )

                for fn in self._listeners["post_run"]:
                    try:
                        method = getattr(self, fn)
                        method()
                    except Exception:
                        self.error(
                            "An additional exception occurred during a "
                            "post-run listener: %s" % traceback.format_exc()
                        )

                self.fatal("Aborting due to failure in pre-run listener.")

        self.dump_config()
        try:
            for action in self.all_actions:
                self.run_action(action)
        except Exception:
            self.fatal("Uncaught exception: %s" % traceback.format_exc())
        finally:
            post_success = True
            for fn in self._listeners["post_run"]:
                try:
                    self.info("Running post-run listener: %s" % fn)
                    method = getattr(self, fn)
                    method()
                except Exception:
                    post_success = False
                    self.error(
                        "Exception during post-run listener: %s"
                        % traceback.format_exc()
                    )

            if not post_success:
                self.fatal("Aborting due to failure in post-run listener.")

        return self.return_code

    def run_and_exit(self):
        """Runs the script and exits the current interpreter."""
        rc = self.run()
        if rc != 0:
            self.warning("returning nonzero exit status %d" % rc)
        sys.exit(rc)

    def clobber(self):
        """
        Delete the working directory
        """
        dirs = self.query_abs_dirs()
        self.rmtree(dirs["abs_work_dir"], error_level=FATAL)

    def query_abs_dirs(self):
        """We want to be able to determine where all the important things
        are.  Absolute paths lend themselves well to this, though I wouldn't
        be surprised if this causes some issues somewhere.

        This should be overridden in any script that has additional dirs
        to query.

        The query_* methods tend to set self.VAR variables as their
        runtime cache.
        """
        if self.abs_dirs:
            return self.abs_dirs
        c = self.config
        dirs = {}
        dirs["base_work_dir"] = c["base_work_dir"]
        dirs["abs_work_dir"] = os.path.join(c["base_work_dir"], c["work_dir"])
        dirs["abs_log_dir"] = os.path.join(c["base_work_dir"], c.get("log_dir", "logs"))
        if "GECKO_PATH" in os.environ:
            dirs["abs_src_dir"] = os.environ["GECKO_PATH"]
        self.abs_dirs = dirs
        return self.abs_dirs

    def dump_config(
        self, file_path=None, config=None, console_output=True, exit_on_finish=False
    ):
        """Dump self.config to localconfig.json"""
        config = config or self.config
        dirs = self.query_abs_dirs()
        if not file_path:
            file_path = os.path.join(dirs["abs_log_dir"], "localconfig.json")
        self.info("Dumping config to %s." % file_path)
        self.mkdir_p(os.path.dirname(file_path))
        json_config = json.dumps(config, sort_keys=True, indent=4)
        fh = codecs.open(file_path, encoding="utf-8", mode="w+")
        fh.write(json_config)
        fh.close()
        if console_output:
            self.info(pprint.pformat(config))
        if exit_on_finish:
            sys.exit()

    # logging {{{2
    def new_log_obj(self, default_log_level="info"):
        c = self.config
        log_dir = os.path.join(c["base_work_dir"], c.get("log_dir", "logs"))
        log_config = {
            "logger_name": "Simple",
            "log_name": "log",
            "log_dir": log_dir,
            "log_level": default_log_level,
            "log_format": "%(asctime)s %(levelname)8s - %(message)s",
            "log_to_console": True,
            "append_to_log": False,
        }
        log_type = self.config.get("log_type", "console")
        for key in log_config.keys():
            value = self.config.get(key, None)
            if value is not None:
                log_config[key] = value
        if log_type == "multi":
            self.log_obj = MultiFileLogger(**log_config)
        elif log_type == "simple":
            self.log_obj = SimpleFileLogger(**log_config)
        else:
            self.log_obj = ConsoleLogger(**log_config)

    def action_message(self, message):
        self.info(
            "[mozharness: %sZ] %s"
            % (datetime.datetime.utcnow().isoformat(" "), message)
        )

    def summary(self):
        """Print out all the summary lines added via add_summary()
        throughout the script.

        I'd like to revisit how to do this in a prettier fashion.
        """
        self.action_message("%s summary:" % self.__class__.__name__)
        if self.summary_list:
            for item in self.summary_list:
                try:
                    self.log(item["message"], level=item["level"])
                except ValueError:
                    """log is closed; print as a default. Ran into this
                    when calling from __del__()"""
                    print("### Log is closed! (%s)" % item["message"])

    def add_summary(self, message, level=INFO):
        self.summary_list.append({"message": message, "level": level})
        # TODO write to a summary-only log?
        # Summaries need a lot more love.
        self.log(message, level=level)

    def summarize_success_count(
        self, success_count, total_count, message="%d of %d successful.", level=None
    ):
        if level is None:
            level = INFO
            if success_count < total_count:
                level = ERROR
        self.add_summary(message % (success_count, total_count), level=level)

    def get_hash_for_file(self, file_path, hash_type="sha512"):
        bs = 65536
        hasher = hashlib.new(hash_type)
        with open(file_path, "rb") as fh:
            buf = fh.read(bs)
            while len(buf) > 0:
                hasher.update(buf)
                buf = fh.read(bs)
        return hasher.hexdigest()

    @property
    def return_code(self):
        return self._return_code

    @return_code.setter
    def return_code(self, code):
        old_return_code, self._return_code = self._return_code, code
        if old_return_code != code:
            self.warning("setting return code to %d" % code)
