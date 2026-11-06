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
          'target_name': 'nss_libs',
          'type': 'none',
          'dependencies': [
            'lib/softoken/softoken.gyp:softokn3',
          ],
          'conditions': [
            [ 'OS=="solaris" and target_arch=="sparc64"', {
              'dependencies': [
                'lib/freebl/freebl.gyp:freebl_64int_3',
                'lib/freebl/freebl.gyp:freebl_64fpu_3',
              ],
            }, {
              'dependencies': [
                'lib/freebl/freebl.gyp:freebl3',
              ],
            }],
            [ 'moz_fold_libs==0', {
              'dependencies': [
                'lib/nss/nss.gyp:nss3',
                'lib/smime/smime.gyp:smime3',
                'lib/sqlite/sqlite.gyp:sqlite3',
                'lib/ssl/ssl.gyp:ssl3',
                'lib/util/util.gyp:nssutil3',
              ],
            }],
            [ 'OS=="linux"', {
              'dependencies': [
                'lib/freebl/freebl.gyp:freeblpriv3',
              ],
            }],
            [ 'OS=="linux" and mozilla_client==0', {
              'dependencies': [
                'lib/sysinit/sysinit.gyp:nsssysinit',
              ],
            }],
            [ 'disable_dbm==0', {
              'dependencies': [
                'lib/softoken/legacydb/legacydb.gyp:nssdbm3',
              ],
            }],
            [ 'disable_ckbi==0', {
              'dependencies': [
                'lib/ckfw/builtins/builtins.gyp:nssckbi',
              ],
            }],
          ],
        },
        {
          'target_name': 'nss_static_libs',
          'type': 'none',
          'dependencies': [
            'cmd/lib/lib.gyp:sectool',
            'lib/base/base.gyp:nssb',
            'lib/certdb/certdb.gyp:certdb',
            'lib/certhigh/certhigh.gyp:certhi',
            'lib/ckfw/ckfw.gyp:nssckfw',
            'lib/cryptohi/cryptohi.gyp:cryptohi',
            'lib/dev/dev.gyp:nssdev',
            'lib/freebl/freebl.gyp:freebl',
            'lib/libpkix/libpkix.gyp:libpkix',
            # mozpkix and mozpkix-testlib are static C++ libs
            'lib/mozpkix/mozpkix.gyp:mozpkix',
            'lib/mozpkix/mozpkix.gyp:mozpkix-testlib',
            'lib/nss/nss.gyp:nss_static',
            'lib/pk11wrap/pk11wrap.gyp:pk11wrap',
            'lib/pkcs12/pkcs12.gyp:pkcs12',
            'lib/pkcs7/pkcs7.gyp:pkcs7',
            'lib/pki/pki.gyp:nsspki',
            'lib/smime/smime.gyp:smime',
            'lib/softoken/softoken.gyp:softokn',
            'lib/ssl/ssl.gyp:ssl',
            'lib/util/util.gyp:nssutil',
          ],
          'conditions': [
            [ 'OS=="linux" and mozilla_client==0', {
              'dependencies': [
                'lib/sysinit/sysinit.gyp:nsssysinit_static',
              ],
            }],
            [ 'disable_dbm==0', {
              'dependencies': [
                'lib/dbm/src/src.gyp:dbm',
                'lib/softoken/legacydb/legacydb.gyp:nssdbm',
              ],
            }],
            [ 'use_system_sqlite==0', {
              'dependencies': [
                'lib/sqlite/sqlite.gyp:sqlite',
              ],
            }],
            [ 'moz_fold_libs==1', {
              'dependencies': [
                'lib/nss/nss.gyp:nss3_static',
                'lib/smime/smime.gyp:smime3_static',
              ],
            }],
          ],
        },
        {
          'target_name': 'nss_cmds',
          'type': 'none',
          'dependencies': [
            'cmd/certutil/certutil.gyp:certutil',
            'cmd/pk12util/pk12util.gyp:pk12util',
          ],
          'conditions': [
            [ 'comm_client==1', {
              'dependencies': [
                'cmd/smimetools/smimetools.gyp:cmsutil',
                'cmd/atob/atob.gyp:atob',
                'cmd/btoa/btoa.gyp:btoa',
              ],
            }],
            [ 'mozilla_client==0', {
              'dependencies': [
                'cmd/crlutil/crlutil.gyp:crlutil',
                'cmd/dbtool/dbtool.gyp:dbtool',
                'cmd/modutil/modutil.gyp:modutil',
                'cmd/nssdefaults/nssdefaults.gyp:nssdefaults',
                'cmd/pwdecrypt/pwdecrypt.gyp:pwdecrypt',
                'cmd/shlibsign/shlibsign.gyp:shlibsign',
                'cmd/signver/signver.gyp:signver',
                'cmd/smimetools/smimetools.gyp:cmsutil',
                'cmd/ssltap/ssltap.gyp:ssltap',
                'cmd/symkeyutil/symkeyutil.gyp:symkeyutil',
                'cmd/validation/validation.gyp:validation',
                'nss-tool/nss_tool.gyp:nss',
                'nss-tool/nss_tool.gyp:hw-support',
              ],
            }],
          ],
        },
      ],
    }, { # else, i.e. mozpkix_only==1
      # Build only mozpkix.
      'targets': [
        {
          'target_name': 'nss_mozpkix_libs',
          'type': 'none',
          'dependencies': [
            # mozpkix and mozpkix-testlib are static C++ libs
            'lib/mozpkix/mozpkix.gyp:mozpkix',
            'lib/mozpkix/mozpkix.gyp:mozpkix-testlib',
          ],
        },
      ],
    }],
    [ 'disable_tests==0', {
      'targets': [
        {
          'target_name': 'nss_tests',
          'type': 'none',
          'dependencies': [
            'cmd/addbuiltin/addbuiltin.gyp:addbuiltin',
            'cmd/atob/atob.gyp:atob',
            'cmd/bltest/bltest.gyp:bltest',
            'cmd/btoa/btoa.gyp:btoa',
            'cmd/chktest/chktest.gyp:chktest',
            'cmd/dbtest/dbtest.gyp:dbtest',
            'cmd/derdump/derdump.gyp:derdump',
            'cmd/digest/digest.gyp:digest',
            'cmd/ecperf/ecperf.gyp:ecperf',
            'cmd/fbectest/fbectest.gyp:fbectest',
            'cmd/httpserv/httpserv.gyp:httpserv',
            'cmd/listsuites/listsuites.gyp:listsuites',
            'cmd/makepqg/makepqg.gyp:makepqg',
            'cmd/multinit/multinit.gyp:multinit',
            'cmd/nss-policy-check/nss-policy-check.gyp:nss-policy-check',
            'cmd/ocspclnt/ocspclnt.gyp:ocspclnt',
            'cmd/ocspresp/ocspresp.gyp:ocspresp',
            'cmd/oidcalc/oidcalc.gyp:oidcalc',
            'cmd/p7content/p7content.gyp:p7content',
            'cmd/p7env/p7env.gyp:p7env',
            'cmd/p7sign/p7sign.gyp:p7sign',
            'cmd/p7verify/p7verify.gyp:p7verify',
            'cmd/pk11ectest/pk11ectest.gyp:pk11ectest',
            'cmd/pk11gcmtest/pk11gcmtest.gyp:pk11gcmtest',
            'cmd/pk11mode/pk11mode.gyp:pk11mode',
            'cmd/pk11importtest/pk11importtest.gyp:pk11importtest',
            'cmd/pk1sign/pk1sign.gyp:pk1sign',
            'cmd/pp/pp.gyp:pp',
            'cmd/rsaperf/rsaperf.gyp:rsaperf',
            'cmd/rsapoptst/rsapoptst.gyp:rsapoptst',
            'cmd/sdbthreadtst/sdbthreadtst.gyp:sdbthreadtst',
            'cmd/sdrtest/sdrtest.gyp:sdrtest',
            'cmd/selfserv/selfserv.gyp:selfserv',
            'cmd/shlibsign/mangle/mangle.gyp:mangle',
            'cmd/strsclnt/strsclnt.gyp:strsclnt',
            'cmd/tests/tests.gyp:baddbdir',
            'cmd/tests/tests.gyp:dertimetest',
            'cmd/tests/tests.gyp:encodeinttest',
            'cmd/tests/tests.gyp:nonspr10',
            'cmd/tests/tests.gyp:remtest',
            'cmd/tests/tests.gyp:secmodtest',
            'cmd/tstclnt/tstclnt.gyp:tstclnt',
            'cmd/vfychain/vfychain.gyp:vfychain',
            'cmd/vfyserv/vfyserv.gyp:vfyserv',
            'cmd/mpitests/mpitests.gyp:mpi_tests',
            'gtests/base_gtest/base_gtest.gyp:base_gtest',
            'gtests/certhigh_gtest/certhigh_gtest.gyp:certhigh_gtest',
            'gtests/cryptohi_gtest/cryptohi_gtest.gyp:cryptohi_gtest',
            'gtests/der_gtest/der_gtest.gyp:der_gtest',
            'gtests/certdb_gtest/certdb_gtest.gyp:certdb_gtest',
            'gtests/freebl_gtest/freebl_gtest.gyp:freebl_gtest',
            'gtests/mozpkix_gtest/mozpkix_gtest.gyp:mozpkix_gtest',
            'gtests/nss_bogo_shim/nss_bogo_shim.gyp:nss_bogo_shim',
            'gtests/pkcs11testmodule/pkcs11testmodule.gyp:pkcs11testmodule',
            'gtests/pk11_gtest/pk11_gtest.gyp:pk11_gtest',
            'gtests/smime_gtest/smime_gtest.gyp:smime_gtest',
            'gtests/softoken_gtest/softoken_gtest.gyp:softoken_gtest',
            'gtests/softoken_static_gtest/softoken_static_gtest.gyp:softoken_static_gtest',
            'gtests/ssl_gtest/ssl_gtest.gyp:ssl_gtest',
            'gtests/util_gtest/util_gtest.gyp:util_gtest',
            'lib/ckfw/builtins/testlib/builtins-testlib.gyp:nssckbi-testlib',
          ],
          'conditions': [
            [ 'OS=="linux"', {
              'dependencies': [
                'cmd/lowhashtest/lowhashtest.gyp:lowhashtest',
              ],
            }],
            [ 'OS=="linux" and mozilla_client==0', {
              'dependencies': [
                'gtests/sysinit_gtest/sysinit_gtest.gyp:sysinit_gtest',
              ],
            }],
            [ 'disable_libpkix==0', {
              'dependencies': [
                'cmd/pkix-errcodes/pkix-errcodes.gyp:pkix-errcodes',
              ],
            }],
            [ 'disable_fips==0', {
              'dependencies': [
                'cmd/fipstest/fipstest.gyp:fipstest',
              ],
            }],
          ],
        },
      ],
    }],
    [ 'sign_libs==1', {
      'targets': [
        {
        'target_name': 'nss_sign_shared_libs',
          'type': 'none',
          'dependencies': [
            'cmd/shlibsign/shlibsign.gyp:shlibsign',
          ],
          'actions': [
            {
          'action_name': 'shlibsign',
              'msvs_cygwin_shell': 0,
              'inputs': [
                '<(nss_dist_obj_dir)/lib/<(dll_prefix)freebl3.<(dll_suffix)',
                '<(nss_dist_obj_dir)/lib/<(dll_prefix)freeblpriv3.<(dll_suffix)',
                '<(nss_dist_obj_dir)/lib/<(dll_prefix)nssdbm3.<(dll_suffix)',
                '<(nss_dist_obj_dir)/lib/<(dll_prefix)softokn3.<(dll_suffix)',
              ],
              'outputs': [
                '<(nss_dist_obj_dir)/lib/<(dll_prefix)freebl3.chk',
                '<(nss_dist_obj_dir)/lib/<(dll_prefix)freeblpriv3.chk',
                '<(nss_dist_obj_dir)/lib/<(dll_prefix)nssdbm3.chk',
                '<(nss_dist_obj_dir)/lib/<(dll_prefix)softokn3.chk'
              ],
              'conditions': [
                ['OS!="linux"', {
                  'inputs/': [['exclude', 'freeblpriv']],
                  'outputs/': [['exclude', 'freeblpriv']]
                }],
                ['disable_dbm==1', {
                  'inputs/': [['exclude', 'nssdbm3']],
                  'outputs/': [['exclude', 'nssdbm3']]
                }],
              ],
              'action': ['<(python)', '<(DEPTH)/coreconf/shlibsign.py', '<@(_inputs)']
            }
          ],
        },
      ],
    }],
    [ 'fuzz_tls==1', {
      'targets': [
        {
          'target_name': 'fuzz_warning',
          'type': 'none',
          'actions': [
            {
              'action_name': 'fuzz_warning',
              'action': ['cat', 'fuzz/warning.txt'],
              'inputs': ['fuzz/warning.txt'],
              'ninja_use_console': 1,
              'outputs': ['dummy'],
            }
          ],
        },
      ],
    }],
    [ 'fuzz==1', {
      'targets': [
        {
          'target_name': 'fuzz',
          'type': 'none',
          'dependencies': [
            'fuzz/fuzz.gyp:fuzz',
          ],
        },
      ],
    }],
    [ 'mozilla_central==1', {
      'targets': [
        {
          'target_name': 'test_nssckbi',
          'type': 'none',
          'dependencies': [
            'lib/ckfw/builtins/testlib/builtins-testlib.gyp:nssckbi-testlib',
          ],
        },
      ],
    }],
  ],
}
