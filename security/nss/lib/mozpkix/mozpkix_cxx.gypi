# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Compiler settings for mozpkix and for the C++ code in NSS libraries that
# link it. Disable RTTI and, outside Windows, exceptions to reduce C++ runtime
# dependencies. Windows retains the /EHsc setting from coreconf/config.gypi.
{
  'conditions': [
    [ 'OS!="win"', {
      'cflags_cc': [
        '-fno-exceptions',
        '-fno-rtti',
      ],
      'xcode_settings': {
        'GCC_ENABLE_CPP_EXCEPTIONS': 'NO',
        'GCC_ENABLE_CPP_RTTI': 'NO',
      },
    }, {
      'msvs_settings': {
        'VCCLCompilerTool': {
          'RuntimeTypeInfo': 'false',
        },
      },
    }],
  ],
}
