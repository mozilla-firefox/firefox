# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
{
  'includes': [
    '../../coreconf/config.gypi',
    '../../cmd/platlibs.gypi'
  ],
  'targets': [
    {
      'target_name': 'selfserv',
      'type': 'executable',
      'sources': [
        'selfserv.c'
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:dbm_exports',
        '<(DEPTH)/exports.gyp:nss_exports',
        '<(DEPTH)/lib/zlib/zlib.gyp:nss_zlib'
      ]
    }
  ],
  'target_defaults': {
    'defines': [
      'NSPR20'
    ]
  },
  'variables': {
    'module': 'nss'
  }
}
