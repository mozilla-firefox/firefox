#!/usr/bin/env vpython3

# Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.
"""Helps with manual Chromium rolls when the WebRTC autoroller is blocked.

Wraps tools_webrtc/autoroller/roll_deps.py. Run it from a WebRTC checkout:

  diagnose  Pins a Chromium revision and reports every DEPS entry that makes
            roll_deps.py fail, and the prebuilt deps that only the new //build
            uses, with Chromium's version of the entries.
  cat       Prints a chromium/src file at the pinned revision.
  apply     Rolls DEPS to the pinned revision with roll_deps.py's own logic,
            without creating a branch, committing or uploading, and writes
            the usual roll commit message.
"""

import argparse
import base64
import contextlib
import http.client
import io
import json
import os
import posixpath
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.error
import urllib.parse
import urllib.request

CHROMIUM_SRC_URL = 'https://chromium.googlesource.com/chromium/src'
INFO_FILE = 'roll_info.json'
CHROMIUM_DEPS_FILE = 'DEPS.chromium'
COMMIT_MESSAGE_FILE = 'commit_message.txt'
# urllib waits forever by default, so a stalled request would hang.
TIMEOUT_SECONDS = 60
REVISION_RE = re.compile(r'^[0-9a-f]{40}$')
# Closing line of a dict-valued entry of the `deps` dict of a DEPS file.
ENTRY_END_RE = re.compile(r'^  },?$', re.MULTILINE)
# Directories of //build whose GN files reference prebuilt deps.
BUILD_SUBDIRS = ('config', 'android')
# A //third_party path in a GN string, up to a target name or a variable.
THIRD_PARTY_REF_RE = re.compile(r'"//(third_party/[^":${}]+)')


def _find_checkout():
    """Returns the root of the WebRTC checkout to work on."""
    candidates = []
    try:
        candidates.append(
            subprocess.check_output(['git', 'rev-parse', '--show-toplevel'],
                                    stderr=subprocess.DEVNULL,
                                    text=True).strip())
    except (OSError, subprocess.CalledProcessError):
        pass
    # Fall back to the checkout that contains this script.
    candidates.append(
        os.path.join(os.path.dirname(os.path.realpath(__file__)), os.pardir,
                     os.pardir, os.pardir, os.pardir))
    for root in candidates:
        if os.path.isfile(
                os.path.join(root, 'tools_webrtc', 'autoroller',
                             'roll_deps.py')):
            return os.path.abspath(root)
    sys.exit('Run this script from a WebRTC checkout.')


SRC_ROOT = _find_checkout()
sys.path.insert(0, os.path.join(SRC_ROOT, 'tools_webrtc', 'autoroller'))
import roll_deps

# Inside the checkout rather than in a shared temporary directory, since
# `apply` executes the Chromium DEPS file saved there.
DEFAULT_STATE_DIR = os.path.join(SRC_ROOT, 'out', 'manual_roll')


def _fetch(url, retry_not_found=True, attempts=5, delay_seconds=10):
    """Returns the body of `url` as bytes, retrying on errors and timeouts."""
    error = None
    for attempt in range(attempts):
        if attempt:
            print('Retrying %s after: %s' % (url, error), file=sys.stderr)
            time.sleep(delay_seconds)
        try:
            with urllib.request.urlopen(url,
                                        timeout=TIMEOUT_SECONDS) as response:
                return response.read()
        except urllib.error.HTTPError as exc:
            if exc.code == 404 and not retry_not_found:
                raise RuntimeError('Not found: %s' % url) from exc
            error = exc
        except (urllib.error.URLError, http.client.HTTPException,
                OSError) as exc:
            error = exc
    raise RuntimeError('Failed to fetch %s: %s' % (url, error))


def _gitiles_text(path, retry_not_found=True):
    """Returns a file ('<revision>/<path>') or a commit ('<revision>')."""
    url = '%s/+/%s?format=TEXT' % (CHROMIUM_SRC_URL,
                                   urllib.parse.quote(path.lstrip('/')))
    return base64.b64decode(_fetch(url, retry_not_found)).decode('utf-8')


def _gitiles_head():
    """Returns the hash of Chromium's HEAD commit, as served by gitiles."""
    # `git ls-remote` can return a commit that gitiles only serves minutes
    # later (e.g. through an sso:// URL rewrite), so ask gitiles directly.
    body = _fetch(CHROMIUM_SRC_URL + '/+/HEAD?format=JSON').decode('utf-8')
    # Gitiles prefixes JSON responses with a ")]}'" line.
    return json.loads(body.split('\n', 1)[1])['commit']


def _read(path):
    with open(path, encoding='utf-8') as handle:
        return handle.read()


def _write(path, content):
    with open(path, 'w', encoding='utf-8') as handle:
        handle.write(content)


def _load_state(state_dir):
    """Returns the roll info and Chromium DEPS saved by `diagnose`."""
    info_file = os.path.join(state_dir, INFO_FILE)
    if not os.path.isfile(info_file):
        sys.exit('No pinned revision in %s; run "diagnose" first.' % state_dir)
    info = json.loads(_read(info_file))
    return info, _read(os.path.join(state_dir, CHROMIUM_DEPS_FILE))


def object_stem(object_name):
    """Returns a GCS object name without its version.

    The name is cut at the first word that contains a digit, e.g.
    'Mac/clang-tidy-llvmorg-24-init-7747-g62397f8b-67.tar.xz' becomes
    'Mac/clang-tidy-llvmorg'.
    """
    directory, _, base = object_name.rpartition('/')
    words = []
    for word in base.split('-'):
        if any(c.isdigit() for c in word):
            break
        words.append(word)
    return '%s/%s' % (directory, '-'.join(words))


def raw_entry(deps_content, path):
    """Returns the text of a dict-valued `deps` entry of a DEPS file."""
    start = deps_content.find("'%s':" % path)
    if start < 0:
        return None
    end = ENTRY_END_RE.search(deps_content, start)
    return deps_content[start:end.end()] if end else None


def find_blockers(webrtc_deps, cr_deps):
    """Returns (path, reason) for each WebRTC DEPS entry blocking the roll.

    Runs the checks of roll_deps.CalculateChangedDeps and roll_deps.main on
    all entries instead of stopping at the first failure. Also reports GCS
    objects that are not in Chromium's order: `gclient setdep` updates them by
    index, which would silently swap their conditions.
    """
    # Reuse roll_deps.py's checks so that both stay in sync.
    # pylint: disable=protected-access
    blockers = []
    webrtc_entries = roll_deps.BuildDepsentryDict(webrtc_deps)
    cr_entries = roll_deps.BuildDepsentryDict(cr_deps)
    for path, entry in webrtc_entries.items():
        cr_entry = cr_entries.get(path)
        if cr_entry is None or path in roll_deps.DONT_AUTOROLL_THESE:
            continue
        if not isinstance(entry, type(cr_entry)):
            blockers.append((path, 'Dependency type changed from %s to %s.' %
                             (type(entry).__name__, type(cr_entry).__name__)))
        elif isinstance(entry, roll_deps.DepsEntry):
            if entry.url != cr_entry.url:
                blockers.append(
                    (path,
                     'URL changed from %s to %s.' % (entry.url, cr_entry.url)))
        elif isinstance(entry, roll_deps.CipdDepsEntry):
            try:
                list(
                    roll_deps._FindChangedCipdPackages(path, entry.packages,
                                                       cr_entry.packages))
            except AssertionError as exc:
                blockers.append((path, str(exc)))
        elif isinstance(entry, roll_deps.GcsDepsEntry):
            try:
                list(roll_deps._FindChangedGcsPackage(path, entry, cr_entry))
            except AssertionError as exc:
                blockers.append((path, str(exc)))
                continue
            stems = [object_stem(o['object_name']) for o in entry.objects]
            cr_stems = [
                object_stem(o['object_name']) for o in cr_entry.objects
            ]
            if stems != cr_stems:
                blockers.append(
                    (path, 'GCS objects are not in the same order as in '
                     'Chromium.\nWebRTC:   %s\nChromium: %s' %
                     (stems, cr_stems)))
    _, removed = roll_deps.FindRemovedDeps(webrtc_deps, cr_deps)
    for path in removed:
        blockers.append(
            (path, 'Not in Chromium\'s DEPS anymore (roll_deps.py raises a '
             'RollError). Delete it once the pinned //build no longer uses '
             'it.'))
    return blockers


def find_stale_android_deps(webrtc_deps, cr_deps, webrtc_content):
    """Returns Android deps dropped by Chromium that roll_deps.py keeps.

    roll_deps.py only replaces the ANDROID_DEPS section of DEPS, so dropped
    entries under src/third_party/android_deps/ stay if they are outside it.
    """
    removed, _ = roll_deps.FindRemovedDeps(webrtc_deps, cr_deps)
    section_start = webrtc_content.find(roll_deps.ANDROID_DEPS_START)
    section_end = webrtc_content.find(roll_deps.ANDROID_DEPS_END)
    return [
        path for path in removed
        if not section_start < webrtc_content.find("'%s':" %
                                                   path) < section_end
    ]


def third_party_refs(archive, directory):
    """Returns {//third_party path: GN files} for a .tar.gz of `directory`."""
    refs = {}
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        for member in tar.getmembers():
            if not member.isfile() or not member.name.endswith(
                ('.gn', '.gni')):
                continue
            text = tar.extractfile(member).read().decode('utf-8', 'replace')
            name = posixpath.join(directory, posixpath.normpath(member.name))
            for match in THIRD_PARTY_REF_RE.finditer(text):
                refs.setdefault(match.group(1).rstrip('/'), set()).add(name)
    return refs


def find_missing_build_deps(old_refs, new_refs, webrtc_deps, cr_deps):
    """Returns {path: GN files} for prebuilts only the new //build uses.

    Looks at the CIPD and GCS deps of Chromium's DEPS that the new //build
    references but the pinned //build doesn't, and returns the ones missing
    from WebRTC's DEPS: roll_deps.py only adds deps to the ANDROID_DEPS
    section.
    """

    def users(refs, path):
        directory = path[len('src/'):]
        return {
            name
            for ref, names in refs.items()
            if ref == directory or ref.startswith(directory + '/')
            for name in names
        }

    missing = {}
    for path, dep in cr_deps['deps'].items():
        if (not isinstance(dep, dict)
                or dep.get('dep_type') not in ('cipd', 'gcs')
                or path in webrtc_deps['deps']):
            continue
        new_users = users(new_refs, path)
        if new_users and not users(old_refs, path):
            missing[path] = new_users
    return missing


def _build_refs(archive_url):
    """Returns third_party_refs() for //build/config and //build/android."""
    refs = {}
    for subdir in BUILD_SUBDIRS:
        archive = _fetch('%s/%s.tar.gz' % (archive_url, subdir))
        for ref, names in third_party_refs(archive, 'build/' + subdir).items():
            refs.setdefault(ref, set()).update(names)
    return refs


def _find_missing_build_deps(webrtc_deps, cr_deps, new_rev):
    """Compares the pinned //build with Chromium's //build at `new_rev`."""
    build = roll_deps.BuildDepsentryDict(webrtc_deps)['src/build']
    old_refs = _build_refs('%s/+archive/%s' % (build.url, build.revision))
    new_refs = _build_refs('%s/+archive/%s/build' %
                           (CHROMIUM_SRC_URL, new_rev))
    return find_missing_build_deps(old_refs, new_refs, webrtc_deps, cr_deps)


def diagnose(args):
    webrtc_content = _read(args.deps_file)
    webrtc_deps = roll_deps.ParseDepsDict(webrtc_content)
    current_rev = webrtc_deps['vars']['chromium_revision']
    new_rev = args.revision or _gitiles_head()
    print('Rolling Chromium %s..%s' % (current_rev, new_rev))
    cr_content = _gitiles_text(new_rev + '/DEPS')
    info = {
        'current_rev': current_rev,
        'new_rev': new_rev,
        'current_pos':
        roll_deps.ParseCommitPosition(_gitiles_text(current_rev)),
        'new_pos': roll_deps.ParseCommitPosition(_gitiles_text(new_rev)),
    }
    os.makedirs(args.state_dir, exist_ok=True)
    _write(os.path.join(args.state_dir, INFO_FILE), json.dumps(info, indent=2))
    _write(os.path.join(args.state_dir, CHROMIUM_DEPS_FILE), cr_content)
    print('Commit positions %(current_pos)d..%(new_pos)d' % info)

    cr_deps = roll_deps.ParseDepsDict(cr_content)
    blockers = find_blockers(webrtc_deps, cr_deps)
    for name, content in (('WebRTC', webrtc_content), ('Chromium',
                                                       cr_content)):
        if (roll_deps.ANDROID_DEPS_START not in content
                or roll_deps.ANDROID_DEPS_END not in content):
            blockers.append(
                ('DEPS', '%s\'s DEPS lacks the ANDROID_DEPS markers that '
                 'roll_deps.UpdateDepsFile needs.' % name))
    for path, reason in blockers:
        print('\nBLOCKER: %s\n%s' % (path, reason))
        entry = raw_entry(cr_content, path)
        if entry:
            print('Chromium entry:\n%s' % entry)

    missing = _find_missing_build_deps(webrtc_deps, cr_deps, new_rev)
    for path, files in sorted(missing.items()):
        print('\nMISSING: %s\nUsed by the new //build (%s) but not by the '
              'pinned one, and not in WebRTC\'s DEPS. roll_deps.py won\'t add '
              'it.' % (path, ', '.join(sorted(files))))
        entry = raw_entry(cr_content, path)
        if entry:
            print('Chromium entry:\n%s' % entry)

    stale = find_stale_android_deps(webrtc_deps, cr_deps, webrtc_content)
    if stale:
        print('\nDropped by Chromium, delete in a manual roll: %s' %
              ', '.join(stale))
    print('\n%d blocker(s), %d missing dep(s). Pinned state saved in %s.' %
          (len(blockers), len(missing), args.state_dir))
    if blockers or missing:
        print('After fixing DEPS, rerun with --revision %s.' % new_rev)
    return 1 if blockers or missing else 0


def cat(args):
    info, _ = _load_state(args.state_dir)
    revision = info['current_rev'] if args.old else info['new_rev']
    sys.stdout.write(
        _gitiles_text('%s/%s' % (revision, args.path), retry_not_found=False))
    return 0


@contextlib.contextmanager
def _setdep_on(deps_file):
    """Makes roll_deps.UpdateDepsFile work on `deps_file` on any host.

    UpdateDepsFile requires every changed dep to be checked out, which isn't
    the case e.g. for Android deps on a Mac. `gclient setdep` only edits the
    DEPS file, so that check is skipped. `gclient setdep` is also pointed at
    `deps_file`, which may be a copy of the checkout's DEPS.
    """
    # pylint: disable=protected-access
    run_command = roll_deps._RunCommand
    is_existing_dir = roll_deps._IsExistingDir

    def run(command, *args, **kwargs):
        if command[:2] == ['gclient', 'setdep']:
            command = command + ['--deps-file', deps_file]
        return run_command(command, *args, **kwargs)

    roll_deps._RunCommand = run
    roll_deps._IsExistingDir = lambda path: True
    try:
        yield
    finally:
        roll_deps._RunCommand = run_command
        roll_deps._IsExistingDir = is_existing_dir


def apply_roll(args):
    info, cr_content = _load_state(args.state_dir)
    cr_deps = roll_deps.ParseDepsDict(cr_content)
    deps_file = os.path.abspath(args.deps_file)
    webrtc_deps = roll_deps.ParseLocalDepsFile(deps_file)
    if webrtc_deps['vars']['chromium_revision'] != info['current_rev']:
        sys.exit('%s is not at chromium_revision %s; run "diagnose" again.' %
                 (deps_file, info['current_rev']))
    blockers = find_blockers(webrtc_deps, cr_deps)
    if blockers:
        sys.exit('Fix these DEPS entries first: %s' %
                 ', '.join(path for path, _ in blockers))
    if not shutil.which('gclient'):
        sys.exit('gclient not found: add depot_tools to PATH.')

    rev_update = roll_deps.ChromiumRevisionUpdate(info['current_rev'],
                                                  info['new_rev'])
    changed_deps = roll_deps.CalculateChangedDeps(webrtc_deps, cr_deps)
    added, _ = roll_deps.FindAddedDeps(webrtc_deps, cr_deps)
    removed, _ = roll_deps.FindRemovedDeps(webrtc_deps, cr_deps)
    message = roll_deps.GenerateCommitMessage(rev_update,
                                              info['current_pos'],
                                              info['new_pos'],
                                              changed_deps,
                                              added_deps_paths=added,
                                              removed_deps_paths=removed)
    with _setdep_on(deps_file):
        roll_deps.UpdateDepsFile(deps_file, rev_update, changed_deps,
                                 cr_content)
    message_file = os.path.join(args.state_dir, COMMIT_MESSAGE_FILE)
    _write(message_file, message + '\n')
    print('Rolled %d dependencies in %s.' % (len(changed_deps), deps_file))

    # Everything that is in Chromium's DEPS must now match it. WebRTC-only
    # git deps roll to their HEAD, which may have moved in the meantime.
    cr_entries = roll_deps.BuildDepsentryDict(cr_deps)
    leftovers = [
        dep.path for dep in roll_deps.CalculateChangedDeps(
            roll_deps.ParseLocalDepsFile(deps_file), cr_deps)
        if dep.path in cr_entries
    ]
    if leftovers:
        print('Still different from Chromium: %s' % ', '.join(leftovers))
        return 1
    print('DEPS matches Chromium@%s. Commit message: %s' %
          (info['new_rev'][:10], message_file))
    return 0


def _revision(value):
    if not REVISION_RE.match(value):
        raise argparse.ArgumentTypeError('expected a full commit hash: %r' %
                                         value)
    return value


def main():
    sys.stdout.reconfigure(line_buffering=True)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument(
        '--state-dir',
        default=DEFAULT_STATE_DIR,
        help=('Where the pinned revision, Chromium\'s DEPS and the commit '
              'message are stored (default: %(default)s).'))
    deps_file = argparse.ArgumentParser(add_help=False)
    deps_file.add_argument('--deps-file',
                           default=os.path.join(SRC_ROOT, 'DEPS'),
                           help='WebRTC DEPS file (default: %(default)s).')

    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser(
        'diagnose',
        parents=[common, deps_file],
        help='Pin a Chromium revision and report what blocks the roll.')
    command.add_argument('--revision',
                         type=_revision,
                         help='Chromium commit to roll to (default: HEAD).')
    command.set_defaults(func=diagnose)
    command = commands.add_parser(
        'cat',
        parents=[common],
        help='Print a chromium/src file at the pinned revision.')
    command.add_argument('path', help='e.g. build/config/android/config.gni')
    command.add_argument(
        '--old',
        action='store_true',
        help='Read it at WebRTC\'s current chromium_revision instead.')
    command.set_defaults(func=cat)
    command = commands.add_parser('apply',
                                  parents=[common, deps_file],
                                  help='Roll DEPS to the pinned revision.')
    command.set_defaults(func=apply_roll)

    args = parser.parse_args()
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
