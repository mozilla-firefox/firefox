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
      'target_name': 'mozpkix_gtest',
      'type': 'executable',
      'sources': [
        '<(DEPTH)/gtests/common/gtests.cc',
        'pkixbuild_tests.cpp',
        'pkixc_tests.cpp',
        'pkixcert_extension_tests.cpp',
        'pkixcert_signature_algorithm_tests.cpp',
        'pkixcheck_CheckExtendedKeyUsage_tests.cpp',
        'pkixcheck_CheckIssuer_tests.cpp',
        'pkixcheck_CheckKeyUsage_tests.cpp',
        'pkixcheck_CheckSignatureAlgorithm_tests.cpp',
        'pkixcheck_CheckValidity_tests.cpp',
        'pkixcheck_ParseValidity_tests.cpp',
        'pkixcheck_TLSFeaturesSatisfiedInternal_tests.cpp',
        'pkixder_input_tests.cpp',
        'pkixder_pki_types_tests.cpp',
        'pkixder_universal_types_tests.cpp',
        'pkixgtest.cpp',
        'pkixnames_tests.cpp',
        'pkixnss_tests.cpp',
        'pkixocsp_CreateEncodedOCSPRequest_tests.cpp',
        'pkixocsp_VerifyEncodedOCSPResponse.cpp',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports',
        '<(DEPTH)/gtests/google_test/google_test.gyp:gtest',
        '<(DEPTH)/lib/util/util.gyp:nssutil',
        '<(DEPTH)/lib/nss/nss.gyp:nss_static',
        '<(DEPTH)/lib/pk11wrap/pk11wrap.gyp:pk11wrap_static',
        '<(DEPTH)/lib/cryptohi/cryptohi.gyp:cryptohi',
        '<(DEPTH)/lib/certhigh/certhigh.gyp:certhi',
        '<(DEPTH)/lib/certdb/certdb.gyp:certdb',
        '<(DEPTH)/lib/base/base.gyp:nssb',
        '<(DEPTH)/lib/dev/dev.gyp:nssdev',
        '<(DEPTH)/lib/pki/pki.gyp:nsspki',
        '<(DEPTH)/lib/libpkix/libpkix.gyp:libpkix',
        '<(DEPTH)/lib/mozpkix/mozpkix.gyp:mozpkix',
        '<(DEPTH)/lib/mozpkix/mozpkix.gyp:mozpkix-testlib',
      ],
      'include_dirs': [
        '<(DEPTH)/lib/mozpkix/',
        '<(DEPTH)/lib/mozpkix/lib',
        '<(DEPTH)/lib/mozpkix/include/',
        '<(DEPTH)/lib/mozpkix/include/pkix-test/',
      ],
      'conditions': [
        [ 'OS=="win"', {
          'libraries': [
            'advapi32.lib',
          ],
        }, {
          # mozpkix is built without RTTI, so UBSan's vptr check can't find
          # typeinfo for its classes, such as TrustDomain::IssuerChecker.
          'cflags_cc': [
            '-fno-sanitize=vptr',
          ],
        }],
      ],
      'defines': [
        'NSS_USE_STATIC_LIBS'
      ],
    }
  ],
  'variables': {
    'module': 'nss',
    'use_static_libs': 1,
  }
}
