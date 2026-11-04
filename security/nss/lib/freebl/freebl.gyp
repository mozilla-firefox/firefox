# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
{
  'includes': [
    '../../coreconf/config.gypi'
  ],
  'targets': [
    {
      'target_name': 'hw-acc-crypto-avx',
      'type': 'static_library',
      # 'sources': [
      #   All AVX hardware accelerated crypto currently requires x64
      # ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports'
      ],
      'conditions': [
        [ 'target_arch=="x64"', {
          'cflags': [
            '-mssse3',
            '-msse4.1',
            '-msse4.2'
          ],
          'cflags_mozilla': [
            '-mssse3',
            '-msse4.1',
            '-msse4.2',
            '-mpclmul',
            '-maes',
            '-mavx',
          ],
          # GCC doesn't define this.
          'defines': [
            '__SSSE3__',
          ],
        }],
        [ '(OS=="linux" or OS=="android" or OS=="dragonfly" or OS=="freebsd" or \
            OS=="netbsd" or OS=="openbsd") and target_arch=="x64"', {
          'cflags': [
            '-mpclmul',
            '-maes',
            '-mavx',
          ],
        }],
        # macOS build doesn't use cflags.
        [ '(OS=="mac" or OS=="ios") and target_arch=="x64"', {
          'xcode_settings': {
            'OTHER_CFLAGS': [
              '-mssse3',
              '-msse4.1',
              '-msse4.2',
              '-mpclmul',
              '-maes',
              '-mavx',
            ],
          },
        }],
        # MSVC needs no flags for these intrinsics; clang-cl does.
        [ 'OS=="win" and clang_cl==1 and target_arch=="x64"', {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'AdditionalOptions': [
                '-mssse3',
                '-msse4.1',
                '-msse4.2',
                '-mpclmul',
                '-maes',
                '-mavx',
              ],
            },
          },
        }],
        [ 'target_arch=="arm"', {
          # Gecko doesn't support non-NEON platform on Android, but tier-3
          # platform such as Linux/arm will need it
          'cflags_mozilla': [
            '-mfpu=neon'
          ],
        }],
        [ 'target_arch=="x64"', {
          'sources': [
            'verified/Hacl_Poly1305_128.c',
            'verified/Hacl_Chacha20_Vec128.c',
            'verified/Hacl_Chacha20Poly1305_128.c',
          ],
        }],
        # HACL's Vec128 uses baseline NEON only, which is mandatory on aarch64,
        # so these need no -march of their own.
        [ 'target_arch=="arm64" or target_arch=="aarch64"', {
          'sources': [
            'verified/Hacl_Poly1305_128.c',
            'verified/Hacl_Chacha20_Vec128.c',
            'verified/Hacl_Chacha20Poly1305_128.c',
          ],
        }],
      ],
    },
    {
      'target_name': 'hw-acc-crypto-avx2',
      'type': 'static_library',
      # 'sources': [
      #   All AVX2 hardware accelerated crypto currently requires x64
      # ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports'
      ],
      'conditions': [
        [ 'target_arch=="x64"', {
          'cflags': [
            '-mssse3',
            '-msse4.1',
            '-msse4.2'
          ],
          'cflags_mozilla': [
            '-mssse3',
            '-msse4.1',
            '-msse4.2',
            '-mpclmul',
            '-maes',
            '-mavx',
            '-mavx2',
          ],
          # GCC doesn't define this.
          'defines': [
            '__SSSE3__',
          ],
        }],
        [ '(OS=="linux" or OS=="android" or OS=="dragonfly" or OS=="freebsd" or \
            OS=="netbsd" or OS=="openbsd") and target_arch=="x64"', {
          'cflags': [
            '-mpclmul',
            '-maes',
            '-mavx',
            '-mavx2',
          ],
        }],
        # macOS build doesn't use cflags.
        [ 'OS=="mac" or OS=="ios"', {
          'xcode_settings': {
            'OTHER_CFLAGS': [
              '-mssse3',
              '-msse4.1',
              '-msse4.2',
              '-mpclmul',
              '-maes',
              '-mavx',
              '-mavx2',
            ],
          },
        }],
        [ 'OS=="win" and clang_cl==1 and target_arch=="x64"', {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'AdditionalOptions': [
                '-mssse3',
                '-msse4.1',
                '-msse4.2',
                '-mpclmul',
                '-maes',
                '-mavx',
                '-mavx2',
              ],
            },
          },
        }],
        [ 'target_arch=="arm"', {
          # Gecko doesn't support non-NEON platform on Android, but tier-3
          # platform such as Linux/arm will need it
          'cflags_mozilla': [
            '-mfpu=neon'
          ],
        }],
        [ 'target_arch=="x64"', {
          'sources': [
            'verified/Hacl_Poly1305_256.c',
            'verified/Hacl_Chacha20_Vec256.c',
            'verified/Hacl_Chacha20Poly1305_256.c',
          ],
        }],
      ],
    },
    {
      'target_name': 'sha-x86_c_lib',
      'type': 'static_library',
      'sources': [
        'sha256-x86.c'
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports'
      ],
      'cflags': [
        '-msha',
        '-mssse3',
        '-msse4.1'
      ],
      'cflags_mozilla': [
        '-msha',
        '-mssse3',
        '-msse4.1'
      ],
      'conditions': [
        # macOS build doesn't use cflags.
        [ 'OS=="mac" or OS=="ios"', {
          'xcode_settings': {
            'OTHER_CFLAGS': [
              '-msha',
              '-mssse3',
              '-msse4.1'
            ],
          },
        }],
        [ 'OS=="win" and clang_cl==1', {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'AdditionalOptions': [
                '-msha',
                '-mssse3',
                '-msse4.1'
              ],
            },
          },
        }]
      ]
    },
    {
      'target_name': 'sha512-ppc-nodepend_c_lib',
      'type': 'static_library',
      'sources': [
        'sha512.c',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports'
      ],
      'conditions': [
        [ 'ppc_abi==2', {
          'sources': [
            'sha512-p8.s',
          ],
        }],
        [ 'disable_crypto_vsx==0', {
          'cflags': [
            '-mcrypto',
            '-maltivec',
            '-mvsx',
            '-funroll-loops',
            '-fpeel-loops'
           ],
           'cflags_mozilla': [
            '-mcrypto',
            '-maltivec',
            '-mvsx',
            '-funroll-loops',
            '-fpeel-loops'
           ],
        }, 'disable_crypto_vsx==1', {
          'cflags': [
            '-maltivec',
            '-funroll-loops',
            '-fpeel-loops'
          ],
          'cflags_mozilla': [
            '-maltivec',
            '-funroll-loops',
            '-fpeel-loops'
          ],
        }]
      ]
    },
    {
      'target_name': 'sha512-ppc_c_lib',
      'type': 'static_library',
      'sources': [
        'sha512.c',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports'
      ],
      'conditions': [
        [ 'ppc_abi==2', {
          'sources': [
            'sha512-p8.s',
          ],
        }],
        [ 'disable_crypto_vsx==0', {
          'cflags': [
            '-mcrypto',
            '-maltivec',
            '-mvsx',
            '-funroll-loops',
            '-fpeel-loops'
           ],
           'cflags_mozilla': [
            '-mcrypto',
            '-maltivec',
            '-mvsx',
            '-funroll-loops',
            '-fpeel-loops'
           ],
        }, 'disable_crypto_vsx==1', {
          'cflags': [
            '-maltivec',
            '-funroll-loops',
            '-fpeel-loops'
          ],
          'cflags_mozilla': [
            '-maltivec',
            '-funroll-loops',
            '-fpeel-loops'
          ],
        }]
      ],
      'defines!': [
        'FREEBL_NO_DEPEND',
      ],
    },
    {
      'target_name': 'chacha20-ppc_lib',
      'type': 'static_library',
      'sources': [
        'chacha20poly1305-ppc.c',
        'chacha20-ppc64le.S',
      ]
    },
    {
      'target_name': 'armv8_c_lib',
      'type': 'static_library',
      'sources': [
        'aes-armv8.c',
        'sha1-armv8.c',
        'sha256-armv8.c',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports'
      ],
      'conditions': [
        [ 'target_arch=="arm"', {
          'cflags': [
            '-march=armv8-a',
            '-mfpu=crypto-neon-fp-armv8',
            '<@(softfp_cflags)',
          ],
          'cflags_mozilla': [
            '-march=armv8-a',
            '-mfpu=crypto-neon-fp-armv8',
            '<@(softfp_cflags)',
          ],
        }, 'target_arch=="arm64" or target_arch=="aarch64"', {
          'cflags': [
            '-march=armv8-a+crypto'
          ],
          'cflags_mozilla': [
            '-march=armv8-a+crypto'
          ],
          'conditions': [
            [ 'OS=="win" and clang_cl==1', {
              'msvs_settings': {
                'VCCLCompilerTool': {
                  'AdditionalOptions': [ '-march=armv8-a+crypto' ],
                },
              },
            }],
          ],
        }]
      ]
    },
    {
      'target_name': 'freebl',
      'type': 'static_library',
      'sources': [
        'loader.c'
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports'
      ]
    },
    # The vendored libcrux ML-KEM + ML-DSA combined extraction (lib/freebl/libcrux/)
    # is built with an isolated include scope: the verified/ dirs are removed so
    # libcrux's krml/eurydice headers can't collide with the HACL* extraction that
    # still lives in verified/. The libcrux .c are self-contained (no PORT_/RNG_),
    # so a single mode-agnostic library serves every freebl variant. The freebl
    # wrappers that DO call PORT_/RNG_ (kyber.c, ml_dsa.c) live in the 'pqcwrap'
    # targets, which mirror the FREEBL_NO_DEPEND split of the freebl libs that
    # link them. The target is named 'crux' rather than 'libcrux': Firefox's
    # build system treats a "lib" prefix on a LIBRARY_NAME specially, so a
    # 'libcrux' target can't be named in USE_LIBS there.
    {
      'target_name': 'crux',
      'type': 'static_library',
      'sources': [
        'libcrux/combined_core.c',
        'libcrux/libcrux_ct_ops.c',
        'libcrux/libcrux_sha3_portable.c',
        'libcrux/libcrux_mlkem_core.c',
        'libcrux/libcrux_mlkem_portable.c',
        'libcrux/libcrux_mlkem512_portable.c',
        'libcrux/libcrux_mlkem768_portable.c',
        'libcrux/libcrux_mlkem1024_portable.c',
        'libcrux/libcrux_mldsa_core.c',
        'libcrux/libcrux_mldsa_portable.c',
        'libcrux/libcrux_mldsa44_portable.c',
        'libcrux/libcrux_mldsa65_portable.c',
        'libcrux/libcrux_mldsa87_portable.c',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports',
      ],
      'include_dirs!': [
        'verified',
        'verified/internal',
        'verified/karamel/include',
        'verified/karamel/krmllib/dist/minimal',
      ],
      'include_dirs': [
        'libcrux',
        'libcrux/internal',
        'libcrux/karamel',
      ],
      'conditions': [
        [ 'OS=="win"', {
          # C4146: unary minus applied to an unsigned type. eurydice_glue.h
          # does this deliberately in its wrapping_neg helpers. The extraction
          # is kept byte-identical to upstream (CI diffs it), so silence the
          # warning rather than patching the header.
          'msvs_settings': {
            'VCCLCompilerTool': {
              'AdditionalOptions': [ '/wd4146' ],
            },
          },
        }],
      ],
    },
    {
      'target_name': 'pqcwrap',
      'type': 'static_library',
      'sources': [
        'kyber.c',
        'ml_dsa.c',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports',
        'crux',
      ],
      'include_dirs!': [
        'verified',
        'verified/internal',
        'verified/karamel/include',
        'verified/karamel/krmllib/dist/minimal',
      ],
      'include_dirs': [
        'libcrux',
        'libcrux/internal',
        'libcrux/karamel',
      ],
      'conditions': [
        [ 'OS=="win"', {
          # See the crux target: eurydice_glue.h trips C4146.
          'msvs_settings': {
            'VCCLCompilerTool': {
              'AdditionalOptions': [ '/wd4146' ],
            },
          },
        }],
      ],
    },
    {
      'target_name': 'pqcwrap_static',
      'type': 'static_library',
      'sources': [
        'kyber.c',
        'ml_dsa.c',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports',
        'crux',
      ],
      'include_dirs!': [
        'verified',
        'verified/internal',
        'verified/karamel/include',
        'verified/karamel/krmllib/dist/minimal',
      ],
      'include_dirs': [
        'libcrux',
        'libcrux/internal',
        'libcrux/karamel',
      ],
      'conditions': [
        [ 'OS=="win"', {
          # See the crux target: eurydice_glue.h trips C4146.
          'msvs_settings': {
            'VCCLCompilerTool': {
              'AdditionalOptions': [ '/wd4146' ],
            },
          },
        }],
        [ 'OS=="linux"', {
          'defines!': [
            'FREEBL_NO_DEPEND',
            'FREEBL_LOWHASH',
          ],
        }],
      ],
    },
    # Build a static freebl library so we can statically link it into
    # the binary. This way we don't have to dlopen() the shared lib
    # but can directly call freebl functions.
    {
      'target_name': 'freebl_static',
      'type': 'static_library',
      'includes': [
        'freebl_base.gypi',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports',
        'hw-acc-crypto-avx',
        'hw-acc-crypto-avx2',
        'crux',
        'pqcwrap_static',
        'gcm.gyp:gcm'
      ],
      'conditions': [
        [ '(disable_arm_hw_aes==0 or disable_arm_hw_sha1==0 or disable_arm_hw_sha2==0) and (target_arch=="arm" or target_arch=="arm64" or target_arch=="aarch64")', {
          'dependencies': [
            'armv8_c_lib'
          ],
        }],
        [ '(target_arch=="ia32" or target_arch=="x64") and disable_intel_hw_sha==0', {
          'dependencies': [
            'sha-x86_c_lib',
          ],
        }],
        [ 'disable_arm32_neon==1 and target_arch=="arm"', {
          'defines!': [
            'NSS_DISABLE_ARM32_NEON',
          ],
        }],
        [ 'disable_altivec==0 and target_arch=="ppc64"', {
          'dependencies': [
            'sha512-ppc_c_lib',
          ],
        }],
        [ 'disable_altivec==0 and target_arch=="ppc64le"', {
          'dependencies': [
            'sha512-ppc_c_lib',
            'chacha20-ppc_lib',
          ],
        }],
        [ 'disable_altivec==1 and (target_arch=="ppc64" or target_arch=="ppc64le")', {
          'defines': [
            'NSS_DISABLE_ALTIVEC',
          ],
        }],
        [ 'disable_crypto_vsx==1 and (target_arch=="ppc" or target_arch=="ppc64" or target_arch=="ppc64le")', {
          'defines': [
            'NSS_DISABLE_CRYPTO_VSX',
          ],
        }],
        [ 'OS=="linux"', {
          'defines!': [
            'FREEBL_NO_DEPEND',
            'FREEBL_LOWHASH',
          ],
        }],
      ],
    },
    {
      'target_name': '<(freebl_name)',
      'type': 'shared_library',
      'includes': [
        'freebl_base.gypi',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports',
        'hw-acc-crypto-avx',
        'hw-acc-crypto-avx2',
        'crux',
        'pqcwrap',
        'gcm.gyp:gcm-nodepend',
      ],
      'conditions': [
        [ 'target_arch=="arm" or target_arch=="arm64" or target_arch=="aarch64"', {
          'dependencies': [
            'armv8_c_lib',
          ],
        }],
        [ '(target_arch=="ia32" or target_arch=="x64") and disable_intel_hw_sha==0', {
          'dependencies': [
            'sha-x86_c_lib',
          ],
        }],
        [ 'disable_arm32_neon==1 and target_arch=="arm"', {
          'defines!': [
            'NSS_DISABLE_ARM32_NEON',
          ],
        }],
        [ 'disable_altivec==0', {
          'conditions': [
            [ 'target_arch=="ppc64"', {
              'dependencies': [
                'sha512-ppc-nodepend_c_lib',
              ],
            }, 'target_arch=="ppc64le"', {
               'dependencies': [
                 'sha512-ppc-nodepend_c_lib',
               ],
            }],
          ],
        }],
        [ 'disable_altivec==1 and (target_arch=="ppc64" or target_arch=="ppc64le")', {
          'defines': [
            'NSS_DISABLE_ALTIVEC',
          ],
        }],
        [ 'disable_crypto_vsx==1 and (target_arch=="ppc" or target_arch=="ppc64" or target_arch=="ppc64le")', {
          'defines': [
            'NSS_DISABLE_CRYPTO_VSX',
          ],
        }],
        [ 'OS!="linux"', {
          'conditions': [
            [ 'moz_fold_libs==0', {
              'dependencies': [
                '<(DEPTH)/lib/util/util.gyp:nssutil3',
              ],
            }, {
              'libraries': [
                '<(moz_folded_library_name)',
              ],
            }],
          ],
        }],
        [ 'OS=="linux"', {
          'sources': [
            'nsslowhash.c',
            'stubs.c',
          ],
        }],
      ],
      'variables': {
       'conditions': [
         [ 'OS=="linux"', {
           'mapfile': 'freebl_hash_vector.def',
         }, {
           'mapfile': 'freebl.def',
         }],
       ]
      },
    },
    {
      'target_name': 'freebl_64int_3',
      'includes': [
        'freebl_base.gypi',
      ],
      'type': 'shared_library',
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports',
        'hw-acc-crypto-avx',
        'hw-acc-crypto-avx2',
        'crux',
        'pqcwrap',
      ],
    },
    {
      'target_name': 'freebl_64fpu_3',
      'includes': [
        'freebl_base.gypi',
      ],
      'type': 'shared_library',
      'sources': [
        'mpi/mpi_sparc.c',
        'mpi/mpv_sparcv9.s',
        'mpi/montmulfv9.s',
      ],
      'dependencies': [
        '<(DEPTH)/exports.gyp:nss_exports',
        'hw-acc-crypto-avx',
        'hw-acc-crypto-avx2',
        'crux',
        'pqcwrap',
        'gcm.gyp:gcm',
      ],
      'asflags_mozilla': [
        '-mcpu=v9', '-Wa,-xarch=v9a'
      ],
      'defines': [
        'MP_NO_MP_WORD',
        'MP_USE_UINT_DIGIT',
        'MP_ASSEMBLY_MULTIPLY',
        'MP_USING_MONT_MULF',
        'MP_MONT_USE_MP_MUL',
      ],
    },
  ],
  'conditions': [
    [ 'OS=="linux"', {
      # stub build
      'targets': [
        {
          'target_name': 'freebl3',
          'type': 'shared_library',
          'defines': [
            'FREEBL_NO_DEPEND',
          ],
          'sources': [
            'lowhash_vector.c'
          ],
          'dependencies': [
            '<(DEPTH)/exports.gyp:nss_exports'
          ],
          'variables': {
            'mapfile': 'freebl_hash.def'
          }
        },
      ],
    }],
  ],
  'target_defaults': {
    'include_dirs': [
      'mpi',
      'ecl',
      'verified',
      'verified/internal',
      'verified/karamel/include',
      'verified/karamel/krmllib/dist/minimal',
      'deprecated',
    ],
    'defines': [
      'SHLIB_SUFFIX=\"<(dll_suffix)\"',
      'SHLIB_PREFIX=\"<(dll_prefix)\"',
      'SHLIB_VERSION=\"3\"',
      'SOFTOKEN_SHLIB_VERSION=\"3\"',
      'RIJNDAEL_INCLUDE_TABLES',
      'MP_API_COMPATIBLE',
      # Bug 1918767 / Bug 1918711 - by setting KRML_MUSTINLINE=inline here, we
      # avoid it being defined to `inline __forceinline` (for msvc) or `inline
      # __attribute((always_inline))` (for gcc/clang) in
      # verified/karamel/include/krml/internal/target.h. These other
      # configurations can cause excessive stack usage.
      'KRML_MUSTINLINE=inline'
    ],
    'conditions': [
      [ 'OS=="win" and target_arch=="ia32"', {
        'msvs_settings': {
          'VCCLCompilerTool': {
            #TODO: -Ox optimize flags
            'PreprocessorDefinitions': [
              'MP_ASSEMBLY_MULTIPLY',
              'MP_ASSEMBLY_SQUARE',
              'MP_ASSEMBLY_DIV_2DX1D',
              'MP_USE_UINT_DIGIT',
              'MP_NO_MP_WORD',
              'USE_HW_AES',
            ],
          },
        },
      }],
      [ 'OS=="win" and target_arch=="x64"', {
        'msvs_settings': {
          'VCCLCompilerTool': {
            #TODO: -Ox optimize flags
            'PreprocessorDefinitions': [
              # Should be copied to mingw defines below
              'NSS_BEVAND_ARCFOUR',
              'MPI_AMD64',
              'MP_ASSEMBLY_MULTIPLY',
              'NSS_USE_COMBA',
              'USE_HW_AES',
            ],
          },
        },
      }],
      [ '(OS=="win" or OS=="mac" or OS=="ios") and (target_arch=="ia32" or target_arch=="x64") and disable_intel_hw_sha==0', {
        'defines': [
          'USE_HW_SHA2',
        ],
      }],
      [ '(OS=="win" or OS=="mac" or OS=="ios") and (target_arch=="arm64" or target_arch=="aarch64") and disable_arm_hw_aes==0', {
        'defines': [
          'USE_HW_AES',
        ],
      }],
      [ '(OS=="win" or OS=="mac" or OS=="ios") and (target_arch=="arm64" or target_arch=="aarch64") and disable_arm_hw_sha1==0', {
        'defines': [
          'USE_HW_SHA1',
        ],
      }],
      [ '(OS=="win" or OS=="mac" or OS=="ios") and (target_arch=="arm64" or target_arch=="aarch64") and disable_arm_hw_sha2==0', {
        'defines': [
          'USE_HW_SHA2',
        ],
      }],
      # Poly1305_256 requires the flag to run
      ['target_arch=="x64"', {
        'defines':[
          'HACL_CAN_COMPILE_VEC128',
          'HACL_CAN_COMPILE_VEC256',
          'HACL_CAN_COMPILE_INTRINSICS',
        ],
      }],
      # On arm64, enable HACL Vec128 (NEON). HACL_CAN_COMPILE_INTRINSICS gates
      # x86 _addcarry_u64/_subborrow_u64 in lib_intrinsics.h; do not define it
      # here or P-curve sources will pull in <x86intrin.h>.
      ['target_arch=="arm64" or target_arch=="aarch64"', {
        'defines':[
          'HACL_CAN_COMPILE_VEC128',
        ],
      }],
      # MSVC has no __int128 type. Use emulated int128 and leave
      # have_int128_support as-is for Curve25519 impl. selection.
      [ 'have_int128_support==1 and (OS!="win" or cc_is_clang==1 or cc_is_gcc==1)', {
        'defines': [
          # The Makefile does version-tests on GCC, but we're not doing that here.
          'HAVE_INT128_SUPPORT',
          'HACL_CAN_COMPILE_UINT128'
        ],
      }, {
        'defines': [
          'KRML_VERIFIED_UINT128',
        ],
      }],
      [ 'OS=="linux"', {
        'defines': [
          'FREEBL_LOWHASH',
          'FREEBL_NO_DEPEND',
        ],
      }],
      [ 'OS=="linux" or OS=="android"', {
        'conditions': [
          [ 'target_arch=="x64"', {
            'defines': [
              'NSS_BEVAND_ARCFOUR',
              'MPI_AMD64',
              'MP_ASSEMBLY_MULTIPLY',
              'NSS_USE_COMBA',
            ],
          }],
          [ 'target_arch=="x64"', {
            'defines': [
              'USE_HW_AES',
            ],
          }],
          [ 'target_arch=="ia32"', {
            'defines': [
              'MP_ASSEMBLY_MULTIPLY',
              'MP_ASSEMBLY_SQUARE',
              'MP_ASSEMBLY_DIV_2DX1D',
              'MP_USE_UINT_DIGIT',
            ],
          }],
          [ 'target_arch=="arm"', {
            'defines': [
              'MP_ASSEMBLY_MULTIPLY',
              'MP_ASSEMBLY_SQUARE',
              'MP_USE_UINT_DIGIT',
              'SHA_NO_LONG_LONG',
              'ARMHF',
            ],
          }],
          [ 'disable_intel_hw_sha==0 and (target_arch=="ia32" or target_arch=="x64")', {
            'defines': [
              'USE_HW_SHA2',
            ],
          }],
          [ 'disable_arm_hw_aes==0 and (target_arch=="arm" or target_arch=="arm64" or target_arch=="aarch64")', {
            'defines': [
              'USE_HW_AES',
            ],
          }],
          [ 'disable_arm_hw_sha1==0 and (target_arch=="arm" or target_arch=="arm64" or target_arch=="aarch64")', {
            'defines': [
              'USE_HW_SHA1',
            ],
          }],
          [ 'disable_arm_hw_sha2==0 and (target_arch=="arm" or target_arch=="arm64" or target_arch=="aarch64")', {
            'defines': [
              'USE_HW_SHA2',
            ],
          }],
        ],
      }],
    ],
  },
  'variables': {
    'module': 'nss',
    'conditions': [
      [ 'target_arch=="x64" or target_arch=="arm64" or target_arch=="aarch64"', {
        'have_int128_support%': 1,
      }, {
        'have_int128_support%': 0,
      }],
      [ 'target_arch=="arm"', {
        # When the compiler uses the softfloat ABI, we want to use the compatible softfp ABI when enabling NEON for these objects.
        # Confusingly, __SOFTFP__ is the name of the define for the softfloat ABI, not for the softfp ABI.
        'softfp_cflags': '<!(sh -c "${CC:-cc} -o - -E -dM - ${CFLAGS} < /dev/null | grep __SOFTFP__ > /dev/null && echo -mfloat-abi=softfp || true")',
      }],
      [ 'target_arch=="ppc64" or target_arch=="ppc64le"', {
       'ppc_abi': '<!(sh -c "${CC:-cc} -dM -E - < /dev/null | awk \'\\$2 == \\"_CALL_ELF\\" {print \\$3}\'")',
      }],
    ],
  }
}
