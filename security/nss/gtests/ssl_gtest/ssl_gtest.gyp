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
      'target_name': 'ssl_gtest',
      'type': 'executable',
      'sources': [
        'bloomfilter_unittest.cc',
        'libssl_internals.c',
        '<(DEPTH)/gtests/common/pk11_import.cc',
        'selfencrypt_unittest.cc',
        'ssl_0rtt_unittest.cc',
        'ssl_aead_unittest.cc',
        'ssl_agent_unittest.cc',
        'ssl_auth_unittest.cc',
        'ssl_cert_ext_unittest.cc',
        'ssl_certificate_compression_unittest.cc',
        'ssl_cipherorder_unittest.cc',
        'ssl_ciphersuite_unittest.cc',
        'ssl_custext_unittest.cc',
        'ssl_damage_unittest.cc',
        'ssl_debug_env_unittest.cc',
        'ssl_dhe_unittest.cc',
        'ssl_drop_unittest.cc',
        'ssl_ecdh_unittest.cc',
        'ssl_ems_unittest.cc',
        'ssl_exporter_unittest.cc',
        'ssl_extension_unittest.cc',
        'ssl_fragment_unittest.cc',
        'ssl_fuzz_unittest.cc',
        'ssl_gather_unittest.cc',
        'ssl_gtest.cc',
        'ssl_hrr_unittest.cc',
        'ssl_keyupdate_unittest.cc',
        'ssl_loopback_unittest.cc',
        'ssl_masking_unittest.cc',
        'ssl_misc_unittest.cc',
        'ssl_reconfig_unittest.cc',
        'ssl_option_unittest.cc',
        'ssl_peer_cert_unittest.cc',
        'ssl_record_unittest.cc',
        'ssl_recordsep_unittest.cc',
        'ssl_recordsize_unittest.cc',
        'ssl_renegotiation_unittest.cc',
        'ssl_resumption_unittest.cc',
        'ssl_skip_unittest.cc',
        'ssl_staticrsa_unittest.cc',
        'ssl_timers_unittest.cc',
        'ssl_tls13compat_unittest.cc',
        'ssl_v2_client_hello_unittest.cc',
        'ssl_version_unittest.cc',
        'ssl_versionpolicy_unittest.cc',
        'test_io.cc',
        'tls_agent.cc',
        'tls_connect.cc',
        'tls_ech_unittest.cc',
        'tls_filter.cc',
        'tls_grease_unittest.cc',
        'tls_hkdf_unittest.cc',
        'tls_mldsa_unittest.cc',
        'tls_mlkem_unittest.cc',
        'tls_protect.cc',
        'tls_psk_unittest.cc',
        'tls_subcerts_unittest.cc',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports',
        '<(DEPTH)/lib/util/util.gyp:nssutil3',
        '<(DEPTH)/gtests/google_test/google_test.gyp:gtest',
        '<(DEPTH)/lib/smime/smime.gyp:smime',
        '<(DEPTH)/lib/ssl/ssl.gyp:ssl',
        '<(DEPTH)/lib/nss/nss.gyp:nss_static',
        '<(DEPTH)/lib/pkcs12/pkcs12.gyp:pkcs12',
        '<(DEPTH)/lib/pkcs7/pkcs7.gyp:pkcs7',
        '<(DEPTH)/lib/certhigh/certhigh.gyp:certhi',
        '<(DEPTH)/lib/cryptohi/cryptohi.gyp:cryptohi',
        '<(DEPTH)/lib/certdb/certdb.gyp:certdb',
        '<(DEPTH)/lib/pki/pki.gyp:nsspki',
        '<(DEPTH)/lib/dev/dev.gyp:nssdev',
        '<(DEPTH)/lib/base/base.gyp:nssb',
        '<(DEPTH)/cpputil/cpputil.gyp:cpputil',
        '<(DEPTH)/lib/libpkix/libpkix.gyp:libpkix',
      ],
      'conditions': [
        [ 'static_libs==1', {
          'dependencies': [
            '<(DEPTH)/lib/pk11wrap/pk11wrap.gyp:pk11wrap_static',
          ],
        }, {
          'dependencies': [
            '<(DEPTH)/lib/sqlite/sqlite.gyp:sqlite3',
            '<(DEPTH)/lib/pk11wrap/pk11wrap.gyp:pk11wrap',
            '<(DEPTH)/lib/softoken/softoken.gyp:softokn',
            '<(DEPTH)/lib/freebl/freebl.gyp:freebl',
          ],
        }],
        [ 'disable_dbm==0', {
          'dependencies': [
            '<(DEPTH)/lib/dbm/src/src.gyp:dbm',
          ],
        }],
        [ 'enable_sslkeylogfile==1 and sanitizer_flags==0', {
          'sources': [
            'ssl_keylog_unittest.cc',
          ],
          'defines': [
            'NSS_ALLOW_SSLKEYLOGFILE',
          ],
        }],
        # ssl_gtest fuzz defines should only be determined by the 'fuzz_tls'
        # flag (so as to match lib/ssl). If gtest.gypi added the define due
        # to '--fuzz' only, remove it.
        ['fuzz_tls==1', {
          'defines': [
            'UNSAFE_FUZZER_MODE',
          ],
        }, {
          'defines!': [
            'UNSAFE_FUZZER_MODE',
          ],
        }],
      ],
    }
  ],
  'target_defaults': {
    'include_dirs': [
      '../../lib/ssl'
    ],
    'defines': [
      'NSS_USE_STATIC_LIBS'
    ],
  },
  'variables': {
    'module': 'nss',
  }
}
