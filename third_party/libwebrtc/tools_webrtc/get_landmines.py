#!/usr/bin/env vpython3

# Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.
"""
This file emits the list of reasons why a particular build needs to
be clobbered (or a list of 'landmines').
"""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
CHECKOUT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, os.pardir))
sys.path.insert(0, os.path.join(CHECKOUT_ROOT, 'build'))
import landmine_utils

host_os = landmine_utils.host_os  # pylint: disable=invalid-name


# pylint: disable=line-too-long
def print_landmines():  # pylint: disable=invalid-name
    """
  ALL LANDMINES ARE EMITTED FROM HERE.
  """
    # DO NOT add landmines as part of a regular CL. Landmines are a last-effort
    # bandaid fix if a CL that got landed has a build dependency bug and all
    # bots need to be cleaned up. If you're writing a new CL that causes build
    # dependency problems, fix the dependency problems instead of adding a
    # landmine.
    # See the Chromium version in src/build/get_landmines.py for usage examples.
    print('Clobber to remove out/{Debug,Release}/args.gn (webrtc:5070)')
    print('Clobber due to the rtc_test_suite migration (crbug.com/498394143)')
    if host_os() == 'win':
        print(
            'Clobber to resolve some issues with corrupt .pdb files on bots.')
        print('Clobber due to corrupt .pdb files (after #14623)')
        print(
            'Clobber due to Win 64-bit Debug linking error (crbug.com/668961)')
        print('Clobber due to Win Clang Debug linking errors in '
              'https://codereview.webrtc.org/2786603002')
        print('Clobber due to Win Debug linking errors in '
              'https://codereview.webrtc.org/2832063003/')
        print('Clobber win x86 bots (issues with isolated files).')
        print('Clobber because of libc++ issue')
        print('Clobber because of libc++ issue - take 2')
        print('Clobber because of libc++ issue - take 3')
        print('Clobber because of libc++ issue - take 4 (crbug.com/1337238)')
        print('Clobber because of libc++ issue - take 5 (crbug.com/1337238)')
        print('Clobber because of libc++ issue - take 6 (crbug.com/1337238)')
        print('Clobber because b/367066321')
    if host_os() == 'mac':  # includes iOS
        print('Clobber due changing compilation type #3 (webrtc:498394143)')
        print('Clobber due changing compilation type again (webrtc:498394143)')
        print(
            'Clobber due changing compilation type of call_tests (webrtc:498394143)'
        )
        print('Clobber due to iOS compile errors (crbug.com/694721)')
        print('Clobber to unblock https://codereview.webrtc.org/2709573003')
        print('Clobber to fix https://codereview.webrtc.org/2709573003 after '
              'landing')
        print('Clobber to fix https://codereview.webrtc.org/2767383005 before'
              'landing (changing rtc_executable -> rtc_test on iOS)')
        print('Clobber to fix https://codereview.webrtc.org/2767383005 before'
              'landing (changing rtc_executable -> rtc_test on iOS)')
        print('Another landmine for low_bandwidth_audio_test (webrtc:7430)')
        print('Clobber to change neteq_rtpplay type to executable')
        print('Clobber to remove .xctest files.')
        print('Clobber to remove .xctest files (take 2).')
        print('Switching rtc_executable to rtc_test')
        print('Lets clobber iOS due to signing issue b/396118151')
        print('Lets clobber iOS due to signing issue b/396118151 (2nd try)')
        print('Lets clobber iOS due to rtc_executable -> rtc_test')
        print('Lets clobber iOS due to rtc_executable -> rtc_test')
        print('Lets clobber iOS due to rtc_library -> rtc_test')
        print('Lets clobber iOS due to moving rtc_test_suite')
        print('Lets clobber iOS due to rtc_test -> rtc_cc_test')
        print('Lets clobber iOS due to rtc_library -> rtc_test')


def main():
    print_landmines()
    return 0


if __name__ == '__main__':
    sys.exit(main())
