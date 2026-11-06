# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
{
  'includes': [
    '../../coreconf/config.gypi'
  ],
  'targets': [
    {
      'target_name': 'mozpkix',
      'type': 'static_library',
      'standalone_static_library': 1,
      'sources': [
        'lib/pkixbuild.cpp',
        'lib/pkixc.cpp',
        'lib/pkixcert.cpp',
        'lib/pkixcheck.cpp',
        'lib/pkixder.cpp',
        'lib/pkixnames.cpp',
        'lib/pkixnss.cpp',
        'lib/pkixocsp.cpp',
        'lib/pkixresult.cpp',
        'lib/pkixtime.cpp',
        'lib/pkixverify.cpp',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_mozpkix_exports',
      ],
      'includes': [
        'mozpkix_cxx.gypi',
      ],
      'conditions': [
        [ 'mozpkix_only==0', {
          'dependencies': [
            '<(DEPTH)/exports.gyp:nss_exports'
          ],
        }],
      ],
    },
    {
      # The subset of mozpkix that libssl uses, so libssl3 links only these
      # objects and none of the C++ runtime the rest of mozpkix needs.
      'target_name': 'mozpkix_ssl',
      'type': 'static_library',
      'sources': [
        'lib/pkixcert.cpp',
        'lib/pkixder.cpp',
        'lib/pkixnss.cpp',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports',
        '<(DEPTH)/exports.gyp:nss_mozpkix_exports',
      ],
      'includes': [
        'mozpkix_cxx.gypi',
      ],
      # These are libraries rather than ldflags, and apply to every dependent
      # rather than only the final link, so that they also reach whatever the
      # Mozilla build system links libssl into. It ignores ldflags and
      # xcode_settings, and links libssl into libnss3 when it folds libraries.
      'all_dependent_settings': {
        'conditions': [
          [ 'OS=="linux"', {
            # The C++ link driver takes libgcc helpers such as __divdi3 from
            # libgcc_s.so. Link the static libgcc ahead of it, as the C driver
            # does.
            'libraries': [
              '-lgcc',
            ],
          }],
          [ 'OS!="win" and OS!="mac" and OS!="ios"', {
            # Drop the C++ runtime that the C++ link driver adds, which libssl
            # doesn't use.
            'libraries': [
              '-Wl,--as-needed',
            ],
          }],
          [ 'OS=="mac" or OS=="ios"', {
            # Drop the unused libc++ that the C++ link driver adds.
            'libraries': [
              '-Wl,-dead_strip_dylibs',
            ],
          }],
        ],
      },
      'variables': {
        # For the Mozilla build system: link this as an archive, so binaries
        # that also link mozpkix don't get these objects twice.
        'no_expand_libs': 1,
      },
    },
    {
      'target_name': 'mozpkix-testlib',
      'type': 'static_library',
      'standalone_static_library': 1,
      'sources': [
        'test-lib/pkixtestalg.cpp',
        'test-lib/pkixtestnss.cpp',
        'test-lib/pkixtestutil.cpp',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_mozpkix_exports',
      ],
      'conditions': [
        [ 'mozpkix_only==0', {
          'dependencies': [
            '<(DEPTH)/exports.gyp:nss_exports'
          ],
        }],
      ],
    },
  ],
  'variables': {
    'module': 'nss',
  }
}
