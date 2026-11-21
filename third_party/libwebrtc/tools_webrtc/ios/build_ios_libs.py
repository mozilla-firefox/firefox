#!/usr/bin/env vpython3

# Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.
"""WebRTC iOS XCFramework build script.
Each architecture is compiled separately before being merged together.
By default, the library is created in out_ios_libs/. (Change with -o.)
"""

import argparse
import logging
import os
import shutil
import subprocess
import sys

os.environ['PATH'] = '/usr/libexec' + os.pathsep + os.environ['PATH']

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..'))
sys.path.append(os.path.join(SRC_DIR, 'build'))
import find_depot_tools

SDK_OUTPUT_DIR = os.path.join(SRC_DIR, 'out_ios_libs')
SDK_FRAMEWORK_NAME = 'WebRTC.framework'
SDK_DSYM_NAME = 'WebRTC.dSYM'
SDK_XCFRAMEWORK_NAME = 'WebRTC.xcframework'

ENABLED_ARCHS = [
    'device:arm64', 'simulator:arm64', 'simulator:x64',
    'catalyst:arm64', 'catalyst:x64',
    'arm64', 'x64'
]
DEFAULT_ARCHS = [
    'device:arm64', 'simulator:arm64', 'simulator:x64'
]
IOS_MINIMUM_DEPLOYMENT_TARGET = {
    'device': '12.0',
    'simulator': '12.0',
    'catalyst': '14.0'
}
LIBVPX_BUILD_VP9 = False

sys.path.append(os.path.join(SCRIPT_DIR, '..', 'libs'))
from generate_licenses import LicenseBuilder


def _ParseArgs():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build_config',
                        default='release',
                        choices=['debug', 'release'],
                        help='The build config. Can be "debug" or "release". '
                        'Defaults to "release".')
    parser.add_argument(
        '--arch',
        nargs='+',
        default=DEFAULT_ARCHS,
        choices=ENABLED_ARCHS,
        help='Architectures to build. Defaults to %(default)s.')
    parser.add_argument(
        '-c',
        '--clean',
        action='store_true',
        default=False,
        help='Removes the previously generated build output, if any.')
    parser.add_argument(
        '-p',
        '--purify',
        action='store_true',
        default=False,
        help='Purifies the previously generated build output by '
        'removing the temporary results used when (re)building.')
    parser.add_argument(
        '-o',
        '--output-dir',
        type=os.path.abspath,
        default=SDK_OUTPUT_DIR,
        help='Specifies a directory to output the build artifacts to. '
        'If specified together with -c, deletes the dir.')
    parser.add_argument(
        '-r',
        '--revision',
        type=int,
        default=0,
        help='Specifies a revision number to embed if building the framework.')
    parser.add_argument('--verbose',
                        action='store_true',
                        default=False,
                        help='Debug logging.')
    parser.add_argument('--use-goma',
                        action='store_true',
                        default=False,
                        help='Use goma to build.')
    parser.add_argument('--use-remoteexec',
                        action='store_true',
                        default=False,
                        help='Use RBE to build.')
    parser.add_argument(
        '--deployment-target',
        default=IOS_MINIMUM_DEPLOYMENT_TARGET['device'],
        help='Raise the minimum deployment target to build for. '
        'Cannot be lowered below 12.0 for iOS/iPadOS '
        'and 14.0 for Catalyst.')
    parser.add_argument(
        '--extra-gn-args',
        default=[],
        nargs='*',
        help='Additional GN args to be used during Ninja generation.')

    return parser.parse_args()


def _RunCommand(cmd):
    logging.debug('Running: %r', cmd)
    subprocess.check_call(cmd, cwd=SRC_DIR)


def _CleanArtifacts(output_dir):
    if os.path.isdir(output_dir):
        logging.info('Deleting %s', output_dir)
        shutil.rmtree(output_dir)


def _CleanTemporary(output_dir, architectures):
    if os.path.isdir(output_dir):
        logging.info('Removing temporary build files.')
        for arch in architectures:
            arch_lib_path = os.path.join(output_dir, arch)
            if os.path.isdir(arch_lib_path):
                shutil.rmtree(arch_lib_path)


def _ParseArchitecture(architectures):
    result = dict()
    for arch in architectures:
        if ":" in arch:
            target_environment, target_cpu = arch.split(":")
        else:
            logging.warning('The environment for build is not specified.')
            logging.warning('It is assumed based on cpu type.')
            logging.warning('See crbug.com/1138425 for more details.')
            if arch == "x64":
                target_environment = "simulator"
            else:
                target_environment = "device"
            target_cpu = arch
        archs = result.get(target_environment)
        if archs is None:
            result[target_environment] = {target_cpu}
        else:
            archs.add(target_cpu)

    return result


def _VersionMax(*versions):
    return max(*versions,
               key=lambda version:
               [int(component) for component in version.split('.')])


def BuildWebRTC(output_dir, target_environment, target_arch, flavor,
                gn_target_name, ios_deployment_target, libvpx_build_vp9,
                use_goma, use_remoteexec, extra_gn_args):
    gn_args = [
        'target_os="ios"',
        'ios_enable_code_signing=false',
        'is_component_build=false',
        'rtc_include_tests=false',
    ]

    # Add flavor option.
    if flavor == 'debug':
        gn_args.append('is_debug=true')
    elif flavor == 'release':
        gn_args.append('is_debug=false')
    else:
        raise ValueError('Unexpected flavor type: %s' % flavor)

    gn_args.append('target_environment="%s"' % target_environment)

    gn_args.append('target_cpu="%s"' % target_arch)

    gn_args.append('ios_deployment_target="%s"' % ios_deployment_target)

    gn_args.append('rtc_libvpx_build_vp9=' +
                   ('true' if libvpx_build_vp9 else 'false'))

    gn_args.append('use_lld=true')
    gn_args.append('use_goma=' + ('true' if use_goma else 'false'))
    gn_args.append('use_remoteexec=' + ('true' if use_remoteexec else 'false'))
    gn_args.append('rtc_enable_objc_symbol_export=true')

    args_string = ' '.join(gn_args + extra_gn_args)
    logging.info('Building WebRTC with args: %s', args_string)

    cmd = [
        sys.executable,
        os.path.join(find_depot_tools.DEPOT_TOOLS_PATH, 'gn.py'),
        'gen',
        output_dir,
        '--args=' + args_string,
    ]
    _RunCommand(cmd)
    logging.info('Building target: %s', gn_target_name)

    cmd = [
        os.path.join(SRC_DIR, 'third_party', 'ninja', 'ninja'),
        '-C',
        output_dir,
        gn_target_name,
    ]
    if use_goma or use_remoteexec:
        cmd.extend(['-j', '200'])
    _RunCommand(cmd)


def main():
    args = _ParseArgs()

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO)

    if args.clean:
        _CleanArtifacts(args.output_dir)
        return 0

    # architectures is typed as Dict[str, Set[str]],
    # where key is for the environment (device or simulator)
    # and value is for the cpu type.
    architectures = _ParseArchitecture(args.arch)
    gn_args = args.extra_gn_args

    if args.purify:
        _CleanTemporary(args.output_dir, list(architectures.keys()))
        return 0

    gn_target_name = 'framework_objc'
    gn_args.append('enable_dsyms=true')
    gn_args.append('enable_stripping=true')

    # Build all architectures.
    framework_paths = []
    all_lib_paths = []
    for (environment, archs) in list(architectures.items()):
        ios_deployment_target = _VersionMax(
            args.deployment_target, IOS_MINIMUM_DEPLOYMENT_TARGET[environment])
        framework_path = os.path.join(args.output_dir, environment)
        framework_paths.append(framework_path)
        lib_paths = []
        for arch in archs:
            lib_path = os.path.join(framework_path, arch + '_libs')
            lib_paths.append(lib_path)
            BuildWebRTC(lib_path, environment, arch, args.build_config,
                        gn_target_name, ios_deployment_target,
                        LIBVPX_BUILD_VP9, args.use_goma, args.use_remoteexec,
                        gn_args)
        all_lib_paths.extend(lib_paths)

        # Combine the slices.
        dylib_path = os.path.join(SDK_FRAMEWORK_NAME, 'WebRTC')
        # Dylibs will be combined, all other files are the same across archs.
        shutil.rmtree(os.path.join(framework_path, SDK_FRAMEWORK_NAME),
                      ignore_errors=True)
        shutil.copytree(os.path.join(lib_paths[0], SDK_FRAMEWORK_NAME),
                        os.path.join(framework_path, SDK_FRAMEWORK_NAME),
                        symlinks=True)
        logging.info('Merging framework slices for %s.', environment)
        dylib_paths = [os.path.join(path, dylib_path) for path in lib_paths]
        out_dylib_path = os.path.join(framework_path, dylib_path)
        if os.path.islink(out_dylib_path):
            out_dylib_path = os.path.join(os.path.dirname(out_dylib_path),
                                          os.readlink(out_dylib_path))
        try:
            os.remove(out_dylib_path)
        except OSError:
            pass
        cmd = ['lipo'] + dylib_paths + ['-create', '-output', out_dylib_path]
        _RunCommand(cmd)

        # Merge the dSYM slices.
        lib_dsym_dir_path = os.path.join(lib_paths[0], SDK_DSYM_NAME)
        if os.path.isdir(lib_dsym_dir_path):
            shutil.rmtree(os.path.join(framework_path, SDK_DSYM_NAME),
                          ignore_errors=True)
            shutil.copytree(lib_dsym_dir_path,
                            os.path.join(framework_path, SDK_DSYM_NAME))
            logging.info('Merging dSYM slices.')
            dsym_path = os.path.join(SDK_DSYM_NAME, 'Contents', 'Resources',
                                     'DWARF', 'WebRTC')
            lib_dsym_paths = [
                os.path.join(path, dsym_path) for path in lib_paths
            ]
            out_dsym_path = os.path.join(framework_path, dsym_path)
            try:
                os.remove(out_dsym_path)
            except OSError:
                pass
            cmd = ['lipo'
                   ] + lib_dsym_paths + ['-create', '-output', out_dsym_path]
            _RunCommand(cmd)

            # Check for Mac-style WebRTC.framework/Resources/ (for Catalyst)...
            resources_dir = os.path.join(framework_path, SDK_FRAMEWORK_NAME,
                                         'Resources')
            if not os.path.exists(resources_dir):
                # ...then fall back to iOS-style WebRTC.framework/
                resources_dir = os.path.dirname(resources_dir)

            # Modify the version number.
            # Format should be <Branch cut MXX>.<Hotfix #>.<Rev #>.
            # e.g. 55.0.14986 means
            # branch cut 55, no hotfixes, and revision 14986.
            infoplist_path = os.path.join(resources_dir, 'Info.plist')
            cmd = [
                'PlistBuddy', '-c', 'Print :CFBundleShortVersionString',
                infoplist_path
            ]
            major_minor = subprocess.check_output(cmd).decode('utf-8').strip()
            version_number = '%s.%s' % (major_minor, args.revision)
            logging.info('Substituting revision number: %s', version_number)
            cmd = [
                'PlistBuddy', '-c', 'Set :CFBundleVersion ' + version_number,
                infoplist_path
            ]
            _RunCommand(cmd)
            _RunCommand(['plutil', '-convert', 'binary1', infoplist_path])

    xcframework_dir = os.path.join(args.output_dir, SDK_XCFRAMEWORK_NAME)
    if os.path.isdir(xcframework_dir):
        shutil.rmtree(xcframework_dir)

    logging.info('Creating xcframework.')
    cmd = ['xcodebuild', '-create-xcframework', '-output', xcframework_dir]

    # Apparently, xcodebuild needs absolute paths for input arguments
    for framework_path in framework_paths:
        cmd += [
            '-framework',
            os.path.abspath(os.path.join(framework_path, SDK_FRAMEWORK_NAME)),
        ]
        dsym_full_path = os.path.join(framework_path, SDK_DSYM_NAME)
        if os.path.exists(dsym_full_path):
            cmd += ['-debug-symbols', os.path.abspath(dsym_full_path)]

    _RunCommand(cmd)

    # Generate the license file.
    logging.info('Generate license file.')
    gn_target_full_name = '//sdk:' + gn_target_name
    builder = LicenseBuilder(all_lib_paths, [gn_target_full_name])
    builder.generate_license_text(
        os.path.join(args.output_dir, SDK_XCFRAMEWORK_NAME))

    logging.info('Done.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
