# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
{
  'sources': [
    'aeskeywrap.c',
    'cmac.c',
    'alghmac.c',
    'arcfive.c',
    'arcfour.c',
    'blake2b.c',
    'camellia.c',
    'chacha20poly1305.c',
    'crypto_primitives.c',
    'ctr.c',
    'cts.c',
    'des.c',
    'desblapi.c',
    'dh.c',
    'drbg.c',
    'dsa.c',
    'ec.c',
    'ecdecode.c',
    'ecl/ecp_25519.c',
    'ecl/ecp_secp256r1.c',
    'ecl/ecp_secp384r1.c',
    'ecl/ecp_secp521r1.c',
    'fipsfreebl.c',
    'blinit.c',
    'freeblver.c',
    'hmacct.c',
    'jpake.c',
    'kyber.c',
    'ldvector.c',
    'md2.c',
    'md5.c',
    'ml_dsa.c',
    'dilithium-pqcrystals-ref.c',
    'mpi/mp_gf2m.c',
    'mpi/mpcpucache.c',
    'mpi/mpi.c',
    'mpi/mplogic.c',
    'mpi/mpmontg.c',
    'mpi/mpprime.c',
    'pqg.c',
    'rawhash.c',
    'rijndael.c',
    'rsa.c',
    'rsapkcs.c',
    'sha_fast.c',
    'shvfy.c',
    'sysrand.c',
    'tlsprfalg.c',
    'secmpi.c',
    'verified/Hacl_Hash_SHA3.c',
    'verified/libcrux_sha3_portable.c',
    'verified/libcrux_mlkem768_portable.c',
    'verified/libcrux_mlkem1024_portable.c',
    'verified/libcrux_mlkem_portable.c',
    'verified/libcrux_core.c',
    'verified/Hacl_P256.c',
    'verified/Hacl_P384.c',
    'verified/Hacl_P521.c',
    'sha3.c',
    'shake.c',
    'verified/Hacl_Curve25519_51.c',
    'verified/Hacl_Ed25519.c',
  ],
  'defines': [
    # For kyber-pqcrystals-ref.c. If we ever decide to support Kyber512 or
    # Kyber1024, we'll need to build separate static libraries with different
    # values of KYBER_K.
    'KYBER_K=3',
  ],
  'conditions': [
    [ 'OS=="linux" or OS=="android"', {
      'conditions': [
        [ 'target_arch=="x64"', {
          'sources': [
            'arcfour-amd64-gas.s',
            'mpi/mpi_amd64.c',
            'mpi/mpi_amd64_common.S',
            'mpi/mp_comba.c',
          ],
        }],
        [ 'target_arch=="ia32"', {
          'sources': [
            'mpi/mpi_x86.s',
          ],
        }],
        [ 'target_arch=="arm"', {
          'sources': [
            'mpi/mpi_arm.c',
          ],
        }],
        [ 'target_arch=="ppc64le"', {
          'sources': [
            'chacha20poly1305-ppc.c',
            'chacha20-ppc64le.S',
          ],
        }]
      ],
    }],
    [ 'OS=="win"', {
      'libraries': [
        '-ladvapi32',
      ],
      'conditions': [
        [ 'cc_use_gnu_ld!=1 and target_arch=="x64"', {
          'sources': [
            'arcfour-amd64-masm.asm',
            'mpi/mpi_amd64.c',
            'mpi/mpi_amd64_masm.asm',
            'mpi/mp_comba_amd64_masm.asm',
          ],
        }],
        [ 'cc_use_gnu_ld!=1 and target_arch=="ia32"', {
          'sources': [
            'mpi/mpi_x86_asm.c',
          ],
        }],
      ],
    }],
    ['have_int128_support==1', {
      'sources': [
        # All intel x64 and 64-bit ARM architectures get the 64 bit version.
        'ecl/curve25519_64.c',
        'verified/Hacl_Curve25519_51.c',
      ],
    }, {
      'sources': [
        # All other architectures get the generic 32 bit implementation.
        'ecl/curve25519_32.c',
      ],
    }],
    ['(target_arch!="ppc64" and target_arch!="ppc64le") or disable_altivec==1', {
      'sources': [
        # Gyp does not support per-file cflags, so working around like this.
        # ppc performance greatly benefits from specific flags.
        'sha512.c',
      ],
    }],
    [ 'disable_chachapoly==0', {
      # The ChaCha20 code is linked in through the static ssse3-crypto lib on
      # all platforms that support SSSE3. There are runtime checks in place to
      # choose the correct ChaCha implementation at runtime.
      'sources': [
        'verified/Hacl_Chacha20.c',
        'verified/Hacl_Chacha20Poly1305_32.c',
        'verified/Hacl_Poly1305_32.c',
      ],
    }],
    [ 'disable_deprecated_seed==0', {
      'sources': [
        'deprecated/seed.c',
      ],
    }],
    [ 'disable_deprecated_rc2==0', {
      'sources': [
        'deprecated/alg2268.c',
      ],
    }],
    [ 'disable_kyber==0', {
      'sources': [
        'kyber-pqcrystals-ref.c',
      ],
    }],
    [ 'fuzz==1', {
      'sources!': [ 'drbg.c' ],
      'sources': [ 'det_rng.c' ],
    }],
    [ 'fuzz_tls==1', {
      'defines': [
        'UNSAFE_FUZZER_MODE',
      ],
    }],
    [ 'ct_verif==1', {
      'defines': [
        'CT_VERIF',
      ],
    }],
    [ 'only_dev_random==1', {
      'defines': [
        'SEED_ONLY_DEV_URANDOM',
      ]
    }],
    [ 'OS=="mac"', {
      'conditions': [
        [ 'target_arch=="ia32"', {
          'sources': [
            'mpi/mpi_sse2.s',
          ],
          'defines': [
            'MP_USE_UINT_DIGIT',
            'MP_ASSEMBLY_MULTIPLY',
            'MP_ASSEMBLY_SQUARE',
            'MP_ASSEMBLY_DIV_2DX1D',
          ],
        }, 'target_arch=="x64"', {
          'sources': [
            'mpi/mpi_amd64.c',
            'mpi/mpi_amd64_common.S',
            'mpi/mp_comba.c',
          ],
          'defines': [
            'MPI_AMD64',
            'MP_ASSEMBLY_MULTIPLY',
            'NSS_USE_COMBA',
          ],
        }],
      ],
    }],
  ],
 'ldflags': [
   '-Wl,-Bsymbolic'
 ],
}
