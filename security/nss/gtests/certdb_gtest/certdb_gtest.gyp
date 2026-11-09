# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
{
  'includes': [
    '../../coreconf/config.gypi',
    '../common/gtest.gypi',
  ],
  'targets': [
    {
      'target_name': 'certdb_gtest',
      'type': 'executable',
      'sources': [
        'alg1485_unittest.cc',
        'cert_unittest.cc',
        'crl_unittest.cc',
        'decode_certs_unittest.cc',
        'name_constraints_unittest.cc',
        'smime_profile_unittest.cc',
        '<(DEPTH)/gtests/common/gtests.cc'
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports',
        '<(DEPTH)/gtests/google_test/google_test.gyp:gtest',
        '<(DEPTH)/lib/util/util.gyp:nssutil3',
        '<(DEPTH)/lib/ssl/ssl.gyp:ssl3',
        '<(DEPTH)/lib/nss/nss.gyp:nss3',
        '<(DEPTH)/lib/smime/smime.gyp:smime3',
      ]
    }
  ],
  'variables': {
    'module': 'nss'
  }
}
