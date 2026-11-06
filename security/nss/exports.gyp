# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
{
  'includes': [
    'coreconf/config.gypi'
  ],
  'conditions': [
    [ 'mozpkix_only==0', {
      'targets': [
        {
          'target_name': 'nss_exports',
          'type': 'none',
          'direct_dependent_settings': {
            'include_dirs': [
              '<(nss_public_dist_dir)/nss',
            ]
          },
          'dependencies': [
            'cmd/lib/exports.gyp:cmd_lib_exports',
            'lib/base/exports.gyp:lib_base_exports',
            'lib/certdb/exports.gyp:lib_certdb_exports',
            'lib/certhigh/exports.gyp:lib_certhigh_exports',
            'lib/ckfw/builtins/exports.gyp:lib_ckfw_builtins_exports',
            'lib/ckfw/exports.gyp:lib_ckfw_exports',
            'lib/cryptohi/exports.gyp:lib_cryptohi_exports',
            'lib/dev/exports.gyp:lib_dev_exports',
            'lib/freebl/exports.gyp:lib_freebl_exports',
            'lib/nss/exports.gyp:lib_nss_exports',
            'lib/pk11wrap/exports.gyp:lib_pk11wrap_exports',
            'lib/pkcs12/exports.gyp:lib_pkcs12_exports',
            'lib/pkcs7/exports.gyp:lib_pkcs7_exports',
            'lib/pki/exports.gyp:lib_pki_exports',
            'lib/smime/exports.gyp:lib_smime_exports',
            'lib/softoken/exports.gyp:lib_softoken_exports',
            'lib/sqlite/exports.gyp:lib_sqlite_exports',
            'lib/ssl/exports.gyp:lib_ssl_exports',
            'lib/util/exports.gyp:lib_util_exports',
            'lib/zlib/exports.gyp:lib_zlib_exports',
          ],
          'conditions': [
            [ 'disable_libpkix==0', {
              'dependencies': [
                'lib/libpkix/include/exports.gyp:lib_libpkix_include_exports',
                'lib/libpkix/pkix/certsel/exports.gyp:lib_libpkix_pkix_certsel_exports',
                'lib/libpkix/pkix/checker/exports.gyp:lib_libpkix_pkix_checker_exports',
                'lib/libpkix/pkix/crlsel/exports.gyp:lib_libpkix_pkix_crlsel_exports',
                'lib/libpkix/pkix/params/exports.gyp:lib_libpkix_pkix_params_exports',
                'lib/libpkix/pkix/results/exports.gyp:lib_libpkix_pkix_results_exports',
                'lib/libpkix/pkix/store/exports.gyp:lib_libpkix_pkix_store_exports',
                'lib/libpkix/pkix/top/exports.gyp:lib_libpkix_pkix_top_exports',
                'lib/libpkix/pkix/util/exports.gyp:lib_libpkix_pkix_util_exports',
                'lib/libpkix/pkix_pl_nss/module/exports.gyp:lib_libpkix_pkix_pl_nss_module_exports',
                'lib/libpkix/pkix_pl_nss/pki/exports.gyp:lib_libpkix_pkix_pl_nss_pki_exports',
                'lib/libpkix/pkix_pl_nss/system/exports.gyp:lib_libpkix_pkix_pl_nss_system_exports',
              ],
            }],
          ],
        },
        {
          'target_name': 'dbm_exports',
          'type': 'none',
          'conditions': [
            ['disable_dbm==0', {
              'direct_dependent_settings': {
                'include_dirs': [
                  '<(nss_public_dist_dir)/dbm'
                ]
              },
              'dependencies': [
                'lib/dbm/include/exports.gyp:lib_dbm_include_exports'
              ],
            }],
          ],
        }
      ],
    }],
  ],
  'targets': [
    {
      'target_name': 'nss_mozpkix_exports',
      'type': 'none',
      'direct_dependent_settings': {
        'include_dirs': [
          '<(nss_public_dist_dir)/nss'
        ]
      },
      'dependencies': [
        'lib/mozpkix/exports.gyp:lib_mozpkix_exports',
        'lib/mozpkix/exports.gyp:lib_mozpkix_test_exports',
      ],
    },
  ],
}
