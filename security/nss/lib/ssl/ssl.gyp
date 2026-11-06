# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
{
  'includes': [
    '../../coreconf/config.gypi'
  ],
  'targets': [
    {
      'target_name': 'ssl',
      'type': 'static_library',
      'sources': [
        'authcert.c',
        'cmpcert.c',
        'dtlscon.c',
        'dtls13con.c',
        'mozpkixwrappers.cpp',
        'prelib.c',
        'selfencrypt.c',
        'ssl3con.c',
        'ssl3ecc.c',
        'ssl3ext.c',
        'ssl3exthandle.c',
        'ssl3gthr.c',
        'sslauth.c',
        'sslbloom.c',
        'sslcert.c',
        'sslcon.c',
        'ssldef.c',
        'sslencode.c',
        'sslenum.c',
        'sslerr.c',
        'sslerrstrs.c',
        'sslgrp.c',
        'sslinfo.c',
        'sslinit.c',
        'sslmutex.c',
        'sslnonce.c',
        'sslprimitive.c',
        'sslreveal.c',
        'sslsecur.c',
        'sslsnce.c',
        'sslsock.c',
        'sslspec.c',
        'ssltrace.c',
        'sslver.c',
        'tls13con.c',
        'tls13ech.c',
        'tls13echv.c',
        'tls13exthandle.c',
        'tls13hashstate.c',
        'tls13hkdf.c',
        'tls13psk.c',
        'tls13replay.c',
        'tls13signature.c',
        'tls13subcerts.c',
      ],
      'conditions': [
        [ 'OS=="win"', {
          'sources': [
            'win32err.c',
          ],
          'defines': [
            'IN_LIBSSL',
          ],
        }, {
          # Not Windows.
          'sources': [
            'unix_err.c'
          ],
        }],
        [ 'fuzz_tls==1', {
          'defines': [
            'UNSAFE_FUZZER_MODE',
          ],
        }],
        [ 'enable_sslkeylogfile==1', {
          'defines': [
            'NSS_ALLOW_SSLKEYLOGFILE',
          ],
        }],
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports',
        '<(DEPTH)/exports.gyp:nss_mozpkix_exports',
        '<(DEPTH)/lib/mozpkix/mozpkix.gyp:mozpkix_ssl',
      ],
      'includes': [
        '../mozpkix/mozpkix_cxx.gypi',
      ],
    },
    {
      'target_name': 'ssl3',
      'type': 'shared_library',
      'dependencies': [
        'ssl',
        '<(DEPTH)/lib/nss/nss.gyp:nss3',
        '<(DEPTH)/lib/util/util.gyp:nssutil3',
        '<(DEPTH)/lib/freebl/freebl.gyp:freebl',
      ],
      'variables': {
        'mapfile': 'ssl.def'
      }
    }
  ],
  'variables': {
    'module': 'nss'
  }
}
