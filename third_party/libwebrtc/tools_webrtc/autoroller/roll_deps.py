#!/usr/bin/env vpython3

# Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.
"""Script to automatically roll dependencies in the WebRTC DEPS file."""


import argparse
import base64
import collections
import logging
import os
import re
import subprocess
import sys
import urllib.request


def FindSrcDirPath():
    """Returns the abs path to the src/ dir of the project."""
    src_dir = os.path.dirname(os.path.abspath(__file__))
    while os.path.basename(src_dir) != 'src':
        src_dir = os.path.normpath(os.path.join(src_dir, os.pardir))
    return src_dir


# Skip these dependencies (list without solution name prefix).
DONT_AUTOROLL_THESE = [
    'src/examples/androidtests/third_party/gradle',
    # Disable the roll of 'android_ndk' as it won't appear in chromium DEPS.
    'src/third_party/android_ndk',
    'src/third_party/mockito/src',
    'src/third_party/protobuf-javascript',
]

# These dependencies are missing in chromium/src/DEPS, either unused or already
# in-tree. For instance, src/base is a part of the Chromium source git repo,
# but we pull it through a subtree mirror, so therefore it isn't listed in
# Chromium's deps but it is in ours.
WEBRTC_ONLY_DEPS = [
    'src/base',
    'src/build',
    'src/buildtools',
    'src/ios',
    'src/testing',
    'src/third_party',
    'src/third_party/clang_format/script',
    'src/third_party/gtest-parallel',
    'src/third_party/pipewire/linux-amd64',
    'src/tools',
]

WEBRTC_URL = 'https://webrtc.googlesource.com/src'
CHROMIUM_SRC_URL = 'https://chromium.googlesource.com/chromium/src'
CHROMIUM_COMMIT_TEMPLATE = CHROMIUM_SRC_URL + '/+/%s'
CHROMIUM_LOG_TEMPLATE = CHROMIUM_SRC_URL + '/+log/%s'
CHROMIUM_FILE_TEMPLATE = CHROMIUM_SRC_URL + '/+/%s/%s'

COMMIT_POSITION_RE = re.compile('^Cr-Commit-Position: .*#([0-9]+).*$')
CLANG_REVISION_RE = re.compile(r'^CLANG_REVISION = \'([-0-9a-z]+)\'$')
ROLL_BRANCH_NAME = 'roll_chromium_revision'

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CHECKOUT_SRC_DIR = FindSrcDirPath()
CHECKOUT_ROOT_DIR = os.path.realpath(os.path.join(CHECKOUT_SRC_DIR, os.pardir))

# Copied from tools/android/roll/android_deps/.../BuildConfigGenerator.groovy.
ANDROID_DEPS_START = r'=== ANDROID_DEPS Generated Code Start ==='
ANDROID_DEPS_END = r'=== ANDROID_DEPS Generated Code End ==='
# Location of automically gathered android deps.
ANDROID_DEPS_PATH = 'src/third_party/android_deps/'

NOTIFY_EMAIL = 'webrtc-trooper@grotations.appspotmail.com'

sys.path.append(os.path.join(CHECKOUT_SRC_DIR, 'build'))
import find_depot_tools

find_depot_tools.add_depot_tools_to_path()

CLANG_UPDATE_SCRIPT_URL_PATH = 'tools/clang/scripts/update.py'
CLANG_UPDATE_SCRIPT_LOCAL_PATH = os.path.join(CHECKOUT_SRC_DIR, 'tools',
                                              'clang', 'scripts', 'update.py')

DepsEntry = collections.namedtuple('DepsEntry', 'path url revision')
ChangedDep = collections.namedtuple('ChangedDep',
                                    'path url current_rev new_rev')
CipdDepsEntry = collections.namedtuple('CipdDepsEntry', 'path packages')
VersionEntry = collections.namedtuple('VersionEntry', 'version')
ChangedCipdPackage = collections.namedtuple(
    'ChangedCipdPackage', 'path package current_version new_version')
ChangedVersionEntry = collections.namedtuple(
    'ChangedVersionEntry', 'path current_version new_version')

ChromiumRevisionUpdate = collections.namedtuple('ChromiumRevisionUpdate',
                                                ('current_chromium_rev '
                                                 'new_chromium_rev '))


class RollError(Exception):
    pass


def StrExpansion():
    return lambda str_value: str_value


def VarLookup(local_scope):
    return lambda var_name: local_scope['vars'][var_name]


def ParseDepsDict(deps_content):
    local_scope = {}
    global_scope = {
        'Str': StrExpansion(),
        'Var': VarLookup(local_scope),
        'deps_os': {},
    }
    exec(deps_content, global_scope, local_scope)
    return local_scope


def ParseLocalDepsFile(filename):
    with open(filename, 'rb') as f:
        deps_content = f.read().decode('utf-8')
    return ParseDepsDict(deps_content)


def ParseCommitPosition(commit_message):
    for line in reversed(commit_message.splitlines()):
        m = COMMIT_POSITION_RE.match(line.strip())
        if m:
            return int(m.group(1))
    logging.error('Failed to parse commit position id from:\n%s\n',
                  commit_message)
    sys.exit(-1)


def _RunCommand(command,
                working_dir=None,
                ignore_exit_code=False,
                extra_env=None,
                input_data=None):
    """Runs a command and returns the output from that command.

    If the command fails (exit code != 0), the function will exit the process.

    Returns:
      A tuple containing the stdout and stderr outputs as strings.
    """
    working_dir = working_dir or CHECKOUT_SRC_DIR
    logging.debug('CMD: %s CWD: %s', ' '.join(command), working_dir)
    env = os.environ.copy()
    if extra_env:
        assert all(isinstance(value, str) for value in extra_env.values())
        logging.debug('extra env: %s', extra_env)
        env.update(extra_env)
    p = subprocess.Popen(command,
                         stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE,
                         env=env,
                         cwd=working_dir,
                         universal_newlines=True)
    std_output, err_output = p.communicate(input_data)
    p.stdout.close()
    p.stderr.close()
    if not ignore_exit_code and p.returncode != 0:
        logging.error('Command failed: %s\n'
                      'stdout:\n%s\n'
                      'stderr:\n%s\n', ' '.join(command), std_output,
                      err_output)
        sys.exit(p.returncode)
    return std_output, err_output


def _GetBranches():
    """Returns a tuple of active,branches.

    The 'active' is the name of the currently active branch and 'branches' is a
    list of all branches.
    """
    lines = _RunCommand(['git', 'branch'])[0].split('\n')
    branches = []
    active = ''
    for line in lines:
        if '*' in line:
            # The assumption is that the first char will always be the '*'.
            active = line[1:].strip()
            branches.append(active)
        else:
            branch = line.strip()
            if branch:
                branches.append(branch)
    return active, branches


def _ReadGitilesContent(url):
    # Download and decode BASE64 content until
    # https://code.google.com/p/gitiles/issues/detail?id=7 is fixed.
    base64_content = ReadUrlContent(url + '?format=TEXT')
    return base64.b64decode(base64_content[0]).decode('utf-8')


def ReadRemoteCrFile(path_below_src, revision):
    """Reads a remote Chromium file of a specific revision.

    Args:
      path_below_src: A path to the target file relative to src dir.
      revision: Revision to read.
    Returns:
      A string with file content.
    """
    return _ReadGitilesContent(CHROMIUM_FILE_TEMPLATE %
                               (revision, path_below_src))


def ReadRemoteCrCommit(revision):
    """Reads a remote Chromium commit message. Returns a string."""
    return _ReadGitilesContent(CHROMIUM_COMMIT_TEMPLATE % revision)


def ReadUrlContent(url):
    """Connect to a remote host and read the contents.

    Args:
      url: URL to connect to.
    Returns:
      A list of lines.
    """
    conn = urllib.request.urlopen(url)
    try:
        return conn.readlines()
    except IOError as e:
        logging.exception('Error connecting to %s. Error: %s', url, e)
        raise
    finally:
        conn.close()


def GetMatchingDepsEntries(depsentry_dict, dir_path):
    """Gets all deps entries matching the provided path.

    This list may contain more than one DepsEntry object.
    Example: dir_path='src/testing' would give results containing both
    'src/testing/gtest' and 'src/testing/gmock' deps entries for Chromium's
    DEPS.
    Example 2: dir_path='src/build' should return 'src/build' but not
    'src/buildtools'.

    Returns:
      A list of DepsEntry objects.
    """
    result = []
    for path, depsentry in depsentry_dict.items():
        if path == dir_path:
            result.append(depsentry)
        else:
            parts = path.split('/')
            if all(part == parts[i]
                   for i, part in enumerate(dir_path.split('/'))):
                result.append(depsentry)
    return result


def BuildDepsentryDict(deps_dict):
    """Builds a dict of paths to DepsEntry objects from a raw deps dict."""
    result = {}

    def AddDepsEntries(deps_subdict):
        for path, dep in deps_subdict.items():
            if path in result:
                continue
            if not isinstance(dep, dict):
                dep = {'url': dep}
            if dep.get('dep_type') == 'cipd':
                result[path] = CipdDepsEntry(path, dep['packages'])
            else:
                if '@' not in dep['url']:
                    url, revision = dep['url'], 'HEAD'
                else:
                    url, revision = dep['url'].split('@')
                result[path] = DepsEntry(path, url, revision)

    def AddVersionEntry(vars_subdict):
        for key, value in vars_subdict.items():
            if key in result:
                continue
            if not key.endswith('_version'):
                continue
            key = re.sub('_version$', '', key)
            result[key] = VersionEntry(value)

    AddDepsEntries(deps_dict['deps'])
    for deps_os in ['win', 'mac', 'linux', 'android', 'ios', 'unix']:
        AddDepsEntries(deps_dict.get('deps_os', {}).get(deps_os, {}))
    AddVersionEntry(deps_dict.get('vars', {}))
    return result


def _FindChangedCipdPackages(path, old_pkgs, new_pkgs):
    old_pkgs_names = {p['package'] for p in old_pkgs}
    new_pkgs_names = {p['package'] for p in new_pkgs}
    pkgs_equal = (old_pkgs_names == new_pkgs_names)
    added_pkgs = [p for p in new_pkgs_names if p not in old_pkgs_names]
    removed_pkgs = [p for p in old_pkgs_names if p not in new_pkgs_names]

    assert pkgs_equal, ('Old: %s\n New: %s.\nYou need to do a manual roll '
                        'and remove/add entries in DEPS so the old and new '
                        'list match.\nMost likely, you should add \"%s\" and '
                        'remove \"%s\"' %
                        (old_pkgs, new_pkgs, added_pkgs, removed_pkgs))

    for old_pkg in old_pkgs:
        for new_pkg in new_pkgs:
            old_version = old_pkg['version']
            new_version = new_pkg['version']
            if (old_pkg['package'] == new_pkg['package']
                    and old_version != new_version):
                logging.debug('Roll dependency %s to %s', path, new_version)
                yield ChangedCipdPackage(path, old_pkg['package'], old_version,
                                         new_version)


def _FindChangedVars(name, old_version, new_version):
    if old_version != new_version:
        logging.debug('Roll dependency %s to %s', name, new_version)
        yield ChangedVersionEntry(name, old_version, new_version)


def _FindNewDeps(old, new):
    """ Gather dependencies only in `new` and return corresponding paths. """
    old_entries = set(BuildDepsentryDict(old))
    new_entries = set(BuildDepsentryDict(new))
    return [
        path for path in new_entries - old_entries
        if path not in DONT_AUTOROLL_THESE
    ]


def FindAddedDeps(webrtc_deps, new_cr_deps):
    """
    Calculate new deps entries of interest.

    Ideally, that would mean: only appearing in chromium DEPS
    but transitively used in WebRTC.

    Since it's hard to compute, we restrict ourselves to a well defined subset:
    deps sitting in `ANDROID_DEPS_PATH`.
    Otherwise, assumes that's a Chromium-only dependency.

    Args:
      webrtc_deps: dict of deps as defined in the WebRTC DEPS file.
      new_cr_deps: dict of deps as defined in the chromium DEPS file.

    Caveat: Doesn't detect a new package in existing dep.

    Returns:
      A tuple consisting of:
        A list of paths added dependencies sitting in `ANDROID_DEPS_PATH`.
        A list of paths for other added dependencies.
    """
    all_added_deps = _FindNewDeps(webrtc_deps, new_cr_deps)
    generated_android_deps = [
        path for path in all_added_deps if path.startswith(ANDROID_DEPS_PATH)
    ]
    other_deps = [
        path for path in all_added_deps if path not in generated_android_deps
    ]
    return generated_android_deps, other_deps


def FindRemovedDeps(webrtc_deps, new_cr_deps):
    """
    Calculate obsolete deps entries.

    Ideally, that would mean: no more appearing in chromium DEPS
    and not used in WebRTC.

    Since it's hard to compute:
     1/ We restrict ourselves to a well defined subset:
        deps sitting in `ANDROID_DEPS_PATH`.
     2/ We rely on existing behavior of CalculateChangeDeps.
        I.e. Assumes non-CIPD dependencies are WebRTC-only, don't remove them.

    Args:
      webrtc_deps: dict of deps as defined in the WebRTC DEPS file.
      new_cr_deps: dict of deps as defined in the chromium DEPS file.

    Caveat: Doesn't detect a deleted package in existing dep.

    Returns:
      A tuple consisting of:
        A list of paths of dependencies removed from `ANDROID_DEPS_PATH`.
        A list of paths of unexpected disappearing dependencies.
    """
    all_removed_deps = _FindNewDeps(new_cr_deps, webrtc_deps)
    generated_android_deps = sorted([
        path for path in all_removed_deps if path.startswith(ANDROID_DEPS_PATH)
    ])
    # Webrtc-only dependencies are handled in CalculateChangedDeps.
    other_deps = sorted([
        path for path in all_removed_deps
        if path not in generated_android_deps and path not in WEBRTC_ONLY_DEPS
    ])
    return generated_android_deps, other_deps


def CalculateChangedDeps(webrtc_deps, new_cr_deps):
    """
    Calculate changed deps entries based on entries defined in the WebRTC DEPS
    file:
     - If a shared dependency with the Chromium DEPS file: roll it to the same
       revision as Chromium (i.e. entry in the new_cr_deps dict)
     - If it's a Chromium sub-directory, roll it to the HEAD revision (notice
       this means it may be ahead of the chromium_revision, but generally these
       should be close).
     - If it's another DEPS entry (not shared with Chromium), roll it to HEAD
       unless it's configured to be skipped.

    Returns:
      A list of ChangedDep objects representing the changed deps.
    """
    result = []
    webrtc_entries = BuildDepsentryDict(webrtc_deps)
    new_cr_entries = BuildDepsentryDict(new_cr_deps)
    for path, webrtc_deps_entry in webrtc_entries.items():
        if path in DONT_AUTOROLL_THESE:
            continue
        cr_deps_entry = new_cr_entries.get(path)
        if cr_deps_entry:
            assert type(cr_deps_entry) is type(webrtc_deps_entry)

            if isinstance(cr_deps_entry, CipdDepsEntry):
                result.extend(
                    _FindChangedCipdPackages(path, webrtc_deps_entry.packages,
                                             cr_deps_entry.packages))
                continue

            if isinstance(cr_deps_entry, VersionEntry):
                result.extend(
                    _FindChangedVars(path, webrtc_deps_entry.version,
                                     cr_deps_entry.version))
                continue

            # Use the revision from Chromium's DEPS file.
            new_rev = cr_deps_entry.revision
            assert webrtc_deps_entry.url == cr_deps_entry.url, (
                'WebRTC DEPS entry %s has a different URL %s than Chromium %s.'
                % (path, webrtc_deps_entry.url, cr_deps_entry.url))
        else:
            if isinstance(webrtc_deps_entry, DepsEntry):
                # Use the HEAD of the deps repo.
                stdout, _ = _RunCommand(
                    ['git', 'ls-remote', webrtc_deps_entry.url, 'HEAD'])
                new_rev = stdout.strip().split('\t')[0]
            else:
                # The dependency has been removed from chromium.
                # This is handled by FindRemovedDeps.
                continue

        # Check if an update is necessary.
        if webrtc_deps_entry.revision != new_rev:
            logging.debug('Roll dependency %s to %s', path, new_rev)
            result.append(
                ChangedDep(path, webrtc_deps_entry.url,
                           webrtc_deps_entry.revision, new_rev))
    return sorted(result)


def CalculateChangedClang(new_cr_rev):

    def GetClangRev(lines):
        for line in lines:
            match = CLANG_REVISION_RE.match(line)
            if match:
                return match.group(1)
        raise RollError('Could not parse Clang revision!')

    with open(CLANG_UPDATE_SCRIPT_LOCAL_PATH, 'r') as f:
        current_lines = f.readlines()
    current_rev = GetClangRev(current_lines)

    new_clang_update_py = ReadRemoteCrFile(CLANG_UPDATE_SCRIPT_URL_PATH,
                                           new_cr_rev).splitlines()
    new_rev = GetClangRev(new_clang_update_py)
    return ChangedDep(CLANG_UPDATE_SCRIPT_LOCAL_PATH, None, current_rev,
                      new_rev)


def GenerateCommitMessage(
        rev_update,
        current_commit_pos,
        new_commit_pos,
        changed_deps_list,
        added_deps_paths=None,
        removed_deps_paths=None,
        clang_change=None,
):
    current_cr_rev = rev_update.current_chromium_rev[0:10]
    new_cr_rev = rev_update.new_chromium_rev[0:10]
    rev_interval = '%s..%s' % (current_cr_rev, new_cr_rev)
    git_number_interval = '%s:%s' % (current_commit_pos, new_commit_pos)

    commit_msg = [
        'Roll chromium_revision %s (%s)\n' %
        (rev_interval, git_number_interval),
        'Change log: %s' % (CHROMIUM_LOG_TEMPLATE % rev_interval),
        'Full diff: %s\n' % (CHROMIUM_COMMIT_TEMPLATE % rev_interval)
    ]

    def Section(adjective, deps):
        noun = 'dependency' if len(deps) == 1 else 'dependencies'
        commit_msg.append('%s %s' % (adjective, noun))

    if changed_deps_list:
        Section('Changed', changed_deps_list)

        for c in changed_deps_list:
            if isinstance(c, ChangedCipdPackage):
                commit_msg.append('* %s: %s..%s' %
                                  (c.path, c.current_version, c.new_version))
            elif isinstance(c, ChangedVersionEntry):
                commit_msg.append('* %s_version: %s..%s' %
                                  (c.path, c.current_version, c.new_version))
            else:
                commit_msg.append(
                    '* %s: %s/+log/%s..%s' %
                    (c.path, c.url, c.current_rev[0:10], c.new_rev[0:10]))

    if added_deps_paths:
        Section('Added', added_deps_paths)
        commit_msg.extend('* %s' % p for p in added_deps_paths)

    if removed_deps_paths:
        Section('Removed', removed_deps_paths)
        commit_msg.extend('* %s' % p for p in removed_deps_paths)

    if any([changed_deps_list, added_deps_paths, removed_deps_paths]):
        change_url = CHROMIUM_FILE_TEMPLATE % (rev_interval, 'DEPS')
        commit_msg.append('DEPS diff: %s\n' % change_url)
    else:
        commit_msg.append('No dependencies changed.')

    if clang_change and clang_change.current_rev != clang_change.new_rev:
        commit_msg.append('Clang version changed %s:%s' %
                          (clang_change.current_rev, clang_change.new_rev))
        change_url = CHROMIUM_FILE_TEMPLATE % (rev_interval,
                                               CLANG_UPDATE_SCRIPT_URL_PATH)
        commit_msg.append('Details: %s\n' % change_url)
    else:
        commit_msg.append('No update to Clang.\n')

    commit_msg.append('BUG=None')
    return '\n'.join(commit_msg)


def UpdateDepsFile(deps_filename, rev_update, changed_deps, new_cr_content):
    """Update the DEPS file with the new revision."""

    with open(deps_filename, 'rb') as deps_file:
        deps_content = deps_file.read().decode('utf-8')

    # Update the chromium_revision variable.
    deps_content = deps_content.replace(rev_update.current_chromium_rev,
                                        rev_update.new_chromium_rev)

    # Add and remove dependencies. For now: only generated android deps.
    # Since gclient cannot add or remove deps, we on the fact that
    # these android deps are located in one place we can copy/paste.
    deps_re = re.compile(ANDROID_DEPS_START + '.*' + ANDROID_DEPS_END,
                         re.DOTALL)
    new_deps = deps_re.search(new_cr_content)
    old_deps = deps_re.search(deps_content)
    if not new_deps or not old_deps:
        faulty = 'Chromium' if not new_deps else 'WebRTC'
        raise RollError('Was expecting to find "%s" and "%s"\n'
                        'in %s DEPS' %
                        (ANDROID_DEPS_START, ANDROID_DEPS_END, faulty))
    deps_content = deps_re.sub(new_deps.group(0), deps_content)

    for dep in changed_deps:
        if isinstance(dep, ChangedVersionEntry):
            deps_content = deps_content.replace(dep.current_version,
                                                dep.new_version)

    with open(deps_filename, 'wb') as deps_file:
        deps_file.write(deps_content.encode('utf-8'))

    # Update each individual DEPS entry.
    for dep in changed_deps:
        # ChangedVersionEntry types are already been processed.
        if isinstance(dep, ChangedVersionEntry):
            continue
        local_dep_dir = os.path.join(CHECKOUT_ROOT_DIR, dep.path)
        if not os.path.isdir(local_dep_dir):
            raise RollError(
                'Cannot find local directory %s. Either run\n'
                'gclient sync --deps=all\n'
                'or make sure the .gclient file for your solution contains all '
                'platforms in the target_os list, i.e.\n'
                'target_os = ["android", "unix", "mac", "ios", "win"];\n'
                'Then run "gclient sync" again.' % local_dep_dir)
        if isinstance(dep, ChangedCipdPackage):
            package = dep.package.format()  # Eliminate double curly brackets
            update = '%s:%s@%s' % (dep.path, package, dep.new_version)
        else:
            update = '%s@%s' % (dep.path, dep.new_rev)
        _RunCommand(['gclient', 'setdep', '--revision', update],
                    working_dir=CHECKOUT_SRC_DIR)


def _IsTreeClean():
    stdout, _ = _RunCommand(['git', 'status', '--porcelain'])
    if len(stdout) == 0:
        return True

    logging.error('Dirty/unversioned files:\n%s', stdout)
    return False


def _EnsureUpdatedMainBranch(dry_run):
    current_branch = _RunCommand(['git', 'rev-parse', '--abbrev-ref',
                                  'HEAD'])[0].splitlines()[0]
    if current_branch != 'main':
        logging.error(
            'Please checkout the main branch and re-run this script.')
        if not dry_run:
            sys.exit(-1)

    logging.info('Updating main branch...')
    _RunCommand(['git', 'pull'])


def _CreateRollBranch(dry_run):
    logging.info('Creating roll branch: %s', ROLL_BRANCH_NAME)
    if not dry_run:
        _RunCommand(['git', 'checkout', '-b', ROLL_BRANCH_NAME])


def _RemovePreviousRollBranch(dry_run):
    active_branch, branches = _GetBranches()
    if active_branch == ROLL_BRANCH_NAME:
        active_branch = 'main'
    if ROLL_BRANCH_NAME in branches:
        logging.info('Removing previous roll branch (%s)', ROLL_BRANCH_NAME)
        if not dry_run:
            _RunCommand(['git', 'checkout', active_branch])
            _RunCommand(['git', 'branch', '-D', ROLL_BRANCH_NAME])


def _LocalCommit(commit_msg, dry_run):
    logging.info('Committing changes locally.')
    if not dry_run:
        _RunCommand(['git', 'add', '--update', '.'])
        _RunCommand(['git', 'commit', '-m', commit_msg])


def ChooseCQMode(skip_cq, cq_over, current_commit_pos, new_commit_pos):
    if skip_cq:
        return 0
    if (new_commit_pos - current_commit_pos) < cq_over:
        return 1
    return 2


def _GetCcRecipients(changed_deps_list):
    """Returns a list of emails to notify based on the changed deps list.
    """
    cc_recipients = []
    for c in changed_deps_list:
        if 'libvpx' in c.path or 'libaom' in c.path:
            cc_recipients.append('marpan@webrtc.org')
            cc_recipients.append('jianj@chromium.org')
    return cc_recipients


def _UploadCL(commit_queue_mode, add_cc=None):
    """Upload the committed changes as a changelist to Gerrit.

    commit_queue_mode:
     - 2: Submit to commit queue.
     - 1: Run trybots but do not submit to CQ.
     - 0: Skip CQ, upload only.

    add_cc: A list of email addresses to add as CC recipients.
    """
    cc_recipients = [NOTIFY_EMAIL]
    if add_cc:
        cc_recipients.extend(add_cc)
    cmd = ['git', 'cl', 'upload', '--force', '--bypass-hooks']
    if commit_queue_mode >= 2:
        logging.info('Sending the CL to the CQ...')
        cmd.extend(['-o', 'label=Bot-Commit+1'])
        cmd.extend(['-o', 'label=Commit-Queue+2'])
        cmd.extend(['--send-mail', '--cc', ','.join(cc_recipients)])
    elif commit_queue_mode >= 1:
        logging.info('Starting CQ dry run...')
        cmd.extend(['-o', 'label=Commit-Queue+1'])
    extra_env = {
        'EDITOR': 'true',
        'SKIP_GCE_AUTH_FOR_GIT': '1',
    }
    stdout, stderr = _RunCommand(cmd, extra_env=extra_env)
    logging.debug('Output from "git cl upload":\nstdout:\n%s\n\nstderr:\n%s',
                  stdout, stderr)


def GetRollRevisionRanges(opts, webrtc_deps):
    current_cr_rev = webrtc_deps['vars']['chromium_revision']
    new_cr_rev = opts.revision
    if not new_cr_rev:
        stdout, _ = _RunCommand(['git', 'ls-remote', CHROMIUM_SRC_URL, 'HEAD'])
        head_rev = stdout.strip().split('\t')[0]
        logging.info('No revision specified. Using HEAD: %s', head_rev)
        new_cr_rev = head_rev

    return ChromiumRevisionUpdate(current_cr_rev, new_cr_rev)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--clean',
                   action='store_true',
                   default=False,
                   help='Removes any previous local roll branch.')
    p.add_argument('-r',
                   '--revision',
                   help=('Chromium Git revision to roll to. Defaults to the '
                         'Chromium HEAD revision if omitted.'))
    p.add_argument(
        '--dry-run',
        action='store_true',
        default=False,
        help=('Calculate changes and modify DEPS, but don\'t create '
              'any local branch, commit, upload CL or send any '
              'tryjobs.'))
    p.add_argument(
        '-i',
        '--ignore-unclean-workdir',
        action='store_true',
        default=False,
        help=('Ignore if the current branch is not main or if there '
              'are uncommitted changes (default: %(default)s).'))
    grp = p.add_mutually_exclusive_group()
    grp.add_argument(
        '--skip-cq',
        action='store_true',
        default=False,
        help='Skip sending the CL to the CQ (default: %(default)s)')
    grp.add_argument('--cq-over',
                     type=int,
                     default=1,
                     help=('Commit queue dry run if the revision difference '
                           'is below this number (default: %(default)s)'))
    p.add_argument('-v',
                   '--verbose',
                   action='store_true',
                   default=False,
                   help='Be extra verbose in printing of log messages.')
    opts = p.parse_args()

    if opts.verbose:
        logging.basicConfig(level=logging.DEBUG)
    else:
        logging.basicConfig(level=logging.INFO)

    if not opts.ignore_unclean_workdir and not _IsTreeClean():
        logging.error('Please clean your local checkout first.')
        return 1

    if opts.clean:
        _RemovePreviousRollBranch(opts.dry_run)

    if not opts.ignore_unclean_workdir:
        _EnsureUpdatedMainBranch(opts.dry_run)

    deps_filename = os.path.join(CHECKOUT_SRC_DIR, 'DEPS')
    webrtc_deps = ParseLocalDepsFile(deps_filename)

    rev_update = GetRollRevisionRanges(opts, webrtc_deps)

    current_commit_pos = ParseCommitPosition(
        ReadRemoteCrCommit(rev_update.current_chromium_rev))
    new_commit_pos = ParseCommitPosition(
        ReadRemoteCrCommit(rev_update.new_chromium_rev))

    new_cr_content = ReadRemoteCrFile('DEPS', rev_update.new_chromium_rev)
    new_cr_deps = ParseDepsDict(new_cr_content)
    changed_deps = CalculateChangedDeps(webrtc_deps, new_cr_deps)
    # Discard other deps, assumed to be chromium-only dependencies.
    new_generated_android_deps, _ = FindAddedDeps(webrtc_deps, new_cr_deps)
    removed_generated_android_deps, other_deps = FindRemovedDeps(
        webrtc_deps, new_cr_deps)
    if other_deps:
        raise RollError('WebRTC DEPS entries are missing from Chromium: %s.\n'
                        'Remove them or add them to either '
                        'WEBRTC_ONLY_DEPS or DONT_AUTOROLL_THESE.' %
                        other_deps)
    clang_change = CalculateChangedClang(rev_update.new_chromium_rev)
    commit_msg = GenerateCommitMessage(
        rev_update,
        current_commit_pos,
        new_commit_pos,
        changed_deps,
        added_deps_paths=new_generated_android_deps,
        removed_deps_paths=removed_generated_android_deps,
        clang_change=clang_change)
    logging.debug('Commit message:\n%s', commit_msg)

    _CreateRollBranch(opts.dry_run)
    if not opts.dry_run:
        UpdateDepsFile(deps_filename, rev_update, changed_deps, new_cr_content)
    if _IsTreeClean():
        logging.info("No DEPS changes detected, skipping CL creation.")
    else:
        _LocalCommit(commit_msg, opts.dry_run)
        commit_queue_mode = ChooseCQMode(opts.skip_cq, opts.cq_over,
                                         current_commit_pos, new_commit_pos)
        logging.info('Uploading CL...')
        if not opts.dry_run:
            _UploadCL(commit_queue_mode, _GetCcRecipients(changed_deps))
    return 0


if __name__ == '__main__':
    sys.exit(main())
