# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
{
  'variables': {
    'module%': '',
    'variables': {
      'variables': {
        'variables': {
          'python%': 'python3',
        },
        # chromium uses pymod_do_main, but gyp doesn't set a sensible
        # Python sys.path (gyp_chromium does).
        'python%': '<(python)',
        'host_arch%': '<!("<(python)" <(DEPTH)/coreconf/detect_host_arch.py)',
      },
      'python%': '<(python)',
      'host_arch%': '<(host_arch)',
      'conditions': [
        ['OS=="android"', {
          'target_arch%': 'arm',
        }, 'OS=="ios"', {
          'target_arch%': 'arm64',
        }, {
          # Default architecture we're building for is the architecture we're
          # building on.
          'target_arch%': '<(host_arch)',
        }],
        ['OS=="linux"', {
          # FIPS-140 LOWHASH
          'freebl_name': 'freeblpriv3',
        }, {
          'freebl_name': 'freebl3',
        }],
        ['OS=="mac"', {
          'use_system_sqlite%': 1,
        },{
          'use_system_sqlite%': 0,
        }],
        ['OS=="mac" or OS=="ios" or OS=="solaris" or OS=="win"', {
          'cc_use_gnu_ld%': 0,
        }, {
          'cc_use_gnu_ld%': 1,
        }],
        ['OS=="win"', {
          'use_system_zlib%': 0,
          'nspr_libs%': ['nspr4.lib', 'plc4.lib', 'plds4.lib'],
          'zlib_libs%': [],
          #TODO
          'moz_debug_flags%': '',
          'dll_prefix': '',
          'dll_suffix': 'dll',
        }, {
          'use_system_zlib%': 1,
          'nspr_libs%': ['-lplds4', '-lplc4', '-lnspr4'],
          'zlib_libs%': ['-lz'],
          'dll_prefix': 'lib',
          'conditions': [
            ['OS=="mac" or OS=="ios"', {
              'moz_debug_flags%': '-gdwarf-2 -gfull',
              'dll_suffix': 'dylib',
            }, {
              'moz_debug_flags%': '-gdwarf-2',
              'dll_suffix': 'so',
            }],
          ],
        }],
        ['"<(GENERATOR)"=="ninja"', {
          'cc_is_clang%': '<!("<(python)" <(DEPTH)/coreconf/check_cc.py clang)',
        }, {
          'cc_is_clang%': '0',
        }],
        ['"<(GENERATOR)"=="ninja"', {
          'cc_is_gcc%': '<!("<(python)" <(DEPTH)/coreconf/check_cc.py gcc)',
        }, {
          'cc_is_gcc%': '0',
        }],
        ['"<(GENERATOR)"=="ninja"', {
          'cc_is_cc%': '<!("<(python)" <(DEPTH)/coreconf/check_cc.py cc)',
        }, {
          'cc_is_cc%': '0',
        }],
      ],
    },
    # Copy conditionally-set variables out one scope.
    'python%': '<(python)',
    'host_arch%': '<(host_arch)',
    'target_arch%': '<(target_arch)',
    'use_system_zlib%': '<(use_system_zlib)',
    'zlib_libs%': ['<@(zlib_libs)'],
    'moz_debug_flags%': '<(moz_debug_flags)',
    'nspr_libs%': ['<@(nspr_libs)'],
    'nspr_lib_dir%': '<(nspr_lib_dir)',
    'nspr_include_dir%': '<(nspr_include_dir)',
    'use_system_sqlite%': '<(use_system_sqlite)',
    'sqlite_libs%': ['-lsqlite3'],
    'dll_prefix': '<(dll_prefix)',
    'dll_suffix': '<(dll_suffix)',
    'freebl_name': '<(freebl_name)',
    'cc_is_clang%': '<(cc_is_clang)',
    'cc_is_gcc%': '<(cc_is_gcc)',
    'cc_use_gnu_ld%': '<(cc_use_gnu_ld)',
    # Windows only: build with clang-cl, lld-link and llvm-lib from llvm_bindir
    # (an absolute path) instead of MSVC. build.sh --clang-cl sets these.
    'clang_cl%': 0,
    'llvm_bindir%': '',
    # Some defaults
    'disable_arm_hw_aes%': 0,
    'disable_arm_hw_sha1%': 0,
    'disable_arm_hw_sha2%': 0,
    'disable_intel_hw_sha%': 0,
    'disable_tests%': 0,
    'disable_chachapoly%': 0,
    'disable_dsa%': 0,
    'disable_deprecated_seed%': 0,
    'disable_deprecated_rc2%': 0,
    'disable_dbm%': 1,
    'disable_libpkix%': 1,
    'disable_werror%': 0,
    'disable_altivec%': 0,
    'disable_crypto_vsx%': 0,
    'disable_arm32_neon%': 0,
    'mozilla_client%': 0,
    'comm_client%': 0,
    'moz_fold_libs%': 0,
    'moz_folded_library_name%': '',
    'sanitizer_flags%': 0,
    # Windows only: the architecture suffix of clang-cl's ASan runtime
    # libraries. build.sh sets this and puts the runtime's directory in
    # the LIB environment variable.
    'clang_rt_asan_arch%': '',
    'static_libs%': 0,
    'no_zdefs%': 0,
    'fuzz%': 0,
    'fuzz_tls%': 0,
    'fuzz_oss%': 0,
    'sign_libs%': 1,
    'use_pprof%': 0,
    'ct_verif%': 0,
    'emit_llvm%': 0,
    'nss_public_dist_dir%': '<(nss_dist_dir)/public',
    'nss_private_dist_dir%': '<(nss_dist_dir)/private',
    # This is only needed when building with --mozpkix-only and might not work
    # on all machines.
    'nss_include_dir%': '/usr/include/nss',
    'only_dev_random%': 1,
    'disable_fips%': 1,
    'fips_module_id%': '',
    'mozpkix_only%': 0,
    'mozilla_central%': 0,
    'coverage%': 0,
    'softfp_cflags%': '',
    'enable_draft_hpke%': 0,
    'disable_ckbi%': 0,
    'ppc_abi%': 0,
    'use_pkcs5_pbkd2_params2_only%': 0,
    'nss_use_pkcs5_pbkd2_params2_only%': 0,
    'softoken_use_pkcs5_pbkd2_params2_only%': 0,
  },
  'target_defaults': {
    # Settings specific to targets should go here.
    # This is mostly for linking to libraries.
    'variables': {
      'mapfile%': '',
      'static_libs%': 0,
      'debug_optimization_level%': '0',
      'release_optimization_level%': '2',
    },
    'standalone_static_library': 0,
    'include_dirs': [
      '<(nspr_include_dir)',
      '<(nss_dist_dir)/private/<(module)',
    ],
    'conditions': [
      [ 'mozpkix_only==1 and OS=="linux"', {
        'include_dirs': [
          '<(nss_include_dir)',
        ],
      }],
      [ 'disable_fips==0', {
        'defines': [
          'NSS_ENABLE_FIPS_INDICATORS',
        ],
      }],
      [ 'disable_fips==1', {
        'defines': [
          'NSS_FIPS_DISABLED',
          'NSS_NO_INIT_SUPPORT',
        ],
      }],
      [ 'fips_module_id!=""', {
        'defines': [
          'NSS_FIPS_MODULE_ID="<(fips_module_id)"'
        ],
      }],
      [ 'OS!="android" and OS!="mac" and OS!="ios" and OS!="win"', {
        'libraries': [
          '-lpthread',
        ],
      }],
      [ 'OS=="linux"', {
        'libraries': [
          '-ldl',
          '-lc',
        ],
      }],
      [ 'OS=="android"', {
        'libraries': [
          '-llog',
        ],
      }],
      # Without this, NSPR's Windows headers mark PR_* dllimport, breaking a static link.
      [ 'OS=="win"', {
        'defines': [
          '_NSPR_BUILD_',
        ],
      }],
      [ 'fuzz==1', {
        'variables': {
          'debug_optimization_level%': '3',
        },
      }],
      [ 'target_arch=="ia32" or target_arch=="x64"', {
        'defines': [
          'NSS_X86_OR_X64',
        ],
        # For Windows.
        'msvs_settings': {
          'VCCLCompilerTool': {
            'PreprocessorDefinitions': [
              'NSS_X86_OR_X64',
            ],
          },
        },
      }],
      [ 'target_arch=="ia32"', {
        'defines': [
          'NSS_X86',
        ],
        # For Windows.
        'msvs_settings': {
          'VCCLCompilerTool': {
            'PreprocessorDefinitions': [
              'NSS_X86',
            ],
          },
        },
      }],
      [ 'target_arch=="arm64" or target_arch=="aarch64" or target_arch=="sparc64" or target_arch=="ppc64" or target_arch=="ppc64le" or target_arch=="s390x" or target_arch=="mips64" or target_arch=="e2k" or target_arch=="riscv64" or target_arch=="loongarch64"', {
        'defines': [
          'NSS_USE_64',
        ],
      }],
      [ 'target_arch=="x64"', {
        'defines': [
          'NSS_X64',
          'NSS_USE_64',
        ],
        # For Windows.
        'msvs_settings': {
          'VCCLCompilerTool': {
            'PreprocessorDefinitions': [
              'NSS_X64',
              'NSS_USE_64',
            ],
          },
        },
      }],
    ],
    'target_conditions': [
      # If we want to properly export a static library, and copy it to lib,
      # we need to mark it as a 'standalone_static_library'. Otherwise,
      # the relative paths in the thin archive will break linking.
      [ '_type=="shared_library"', {
        'product_dir': '<(nss_dist_obj_dir)/lib'
      }, '_type=="executable"', {
        'product_dir': '<(nss_dist_obj_dir)/bin'
      }, 'static_libs==1 or _standalone_static_library==1', {
        'product_dir': '<(nss_dist_obj_dir)/lib'
      }],
      # mapfile handling
      [ 'mapfile!=""', {
        # Work around a gyp bug. Fixed upstream but not in Ubuntu packages:
        # https://chromium.googlesource.com/external/gyp/+/b85ad3e578da830377dbc1843aa4fbc5af17a192%5E%21/
        'sources': [
          '<(DEPTH)/coreconf/empty.c',
        ],
        'xcode_settings': {
          'OTHER_LDFLAGS': [
            '-exported_symbols_list',
            '<(INTERMEDIATE_DIR)/out.>(mapfile)',
          ],
        },
        'conditions': [
          [ 'cc_use_gnu_ld==1', {
            'ldflags': [
              '-Wl,--version-script,<(INTERMEDIATE_DIR)/out.>(mapfile)',
            ],
          }],
          [ 'cc_use_gnu_ld!=1 and OS=="win"', {
            # On Windows, .def files are used directly as sources.
            'sources': [
              '>(mapfile)',
            ],
          }, {
            # On other platforms, .def files need processing.
            'sources': [
              '<(INTERMEDIATE_DIR)/out.>(mapfile)',
            ],
            'actions': [{
              'action_name': 'generate_mapfile',
              'inputs': [
                '>(mapfile)',
              ],
              'outputs': [
                '<(INTERMEDIATE_DIR)/out.>(mapfile)',
              ],
              'action': ['<@(process_map_file)'],
            }],
          }]
        ],
      }, 'static_libs==1 and _type=="shared_library"', {
        # When linking a shared lib against a static one, XCode doesn't
        # export the latter's symbols by default. -all_load fixes that.
        'xcode_settings': {
          'OTHER_LDFLAGS': [
            '-all_load',
          ],
        },
      }],
      [ '_type=="shared_library" or _type=="executable"', {
        'libraries': [
          '<@(nspr_libs)',
        ],
        'library_dirs': [
          '<(nspr_lib_dir)',
        ],
      }],
      # Shared library specific settings.
      [ '_type=="shared_library"', {
        'conditions': [
          [ 'cc_use_gnu_ld==1', {
            'ldflags': [
              '-Wl,--gc-sections',
            ],
            'conditions': [
              ['no_zdefs==0', {
                'ldflags': [
                  '-Wl,-z,defs',
                ],
              }],
            ],
          }],
        ],
        'xcode_settings': {
          'DYLIB_INSTALL_NAME_BASE': '@executable_path',
          'DYLIB_COMPATIBILITY_VERSION': '1',
          'DYLIB_CURRENT_VERSION': '1',
          'OTHER_LDFLAGS': [
            '-headerpad_max_install_names',
          ],
        },
        'msvs_settings': {
          'VCLinkerTool': {
            'SubSystem': '2',
          },
        },
      }],
      [ '_type=="static_library" and static_libs==1', {
        'standalone_static_library': 1,
      }],
    ],
    'default_configuration': 'Debug',
    'configurations': {
      # Common settings for Debug+Release should go here.
      'Common': {
        'abstract': 1,
        'defines': [
          'USE_UTIL_DIRECTLY',
          'NO_NSPR_10_SUPPORT',
          'SSL_DISABLE_DEPRECATED_CIPHER_SUITE_NAMES',
        ],
        'msvs_configuration_attributes': {
          'OutputDirectory': '$(SolutionDir)$(ConfigurationName)',
          'IntermediateDirectory': '$(OutDir)\\obj\\$(ProjectName)',
        },
        'msvs_settings': {
          'VCCLCompilerTool': {
            'AdditionalIncludeDirectories': ['<(nspr_include_dir)'],
          },
        },
        'xcode_settings': {
          'CLANG_CXX_LANGUAGE_STANDARD': 'c++0x',
          'OTHER_CFLAGS': [
            '-fPIC',
            '-fno-common',
            '-pipe',
          ],
        },
        'conditions': [
          [ 'OS=="linux" or OS=="android"', {
            'defines': [
              'LINUX2_1',
              'LINUX',
              'linux',
              '_DEFAULT_SOURCE', # for <endian.h> functions, strdup, realpath, and getentropy
              '_BSD_SOURCE', # for the above in glibc <= 2.19
              '_POSIX_SOURCE', # for <signal.h>
              'SDB_MEASURE_USE_TEMP_DIR', # use tmpdir for the access calls
            ],
          }],
          [ 'OS=="dragonfly" or OS=="freebsd"', {
            'defines': [
              'FREEBSD',
            ],
          }],
          [ 'OS=="netbsd"', {
            'defines': [
              'NETBSD',
            ],
          }],
          [ 'OS=="openbsd"', {
            'defines': [
              'OPENBSD',
            ],
          }],
          ['OS=="mac" or OS=="ios" or OS=="dragonfly" or OS=="freebsd" or OS=="netbsd" or OS=="openbsd"', {
            'defines': [
              'HAVE_BSD_FLOCK',
            ],
          }],
          [ 'OS!="win"', {
            'defines': [
              'HAVE_STRERROR',
              'XP_UNIX',
              '_REENTRANT',
            ],
          }],
          [ 'OS!="mac" and OS!="ios" and OS!="solaris" and OS!="win"', {
            'ldflags': [
              '-z', 'noexecstack',
            ],
          }],
          [ 'OS!="mac" and OS!="ios" and OS!="win"', {
            'cflags': [
              '-fPIC',
              '-pipe',
              '-ffunction-sections',
              '-fdata-sections',
            ],
            'cflags_c': [
              '-std=c99',
            ],
            'cflags_cc': [
              '-std=c++11',
            ],
            'conditions': [
              [ 'target_arch=="ia32"', {
                'cflags': ['-m32'],
                'ldflags': ['-m32'],
              }],
              [ 'target_arch=="x64"', {
                'cflags': ['-m64'],
                'ldflags': ['-m64'],
              }],
            ],
          }],
          [ 'use_pprof==1 and OS!="android" and OS!="win"', {
            'conditions': [
              [ 'OS=="mac" or OS=="ios"', {
                'xcode_settings': {
                  'OTHER_LDFLAGS': [ '-lprofiler' ],
                },
              }, {
                'ldflags': [ '-lprofiler' ],
              }],
              [ 'OS!="linux"', {
                'library_dirs': [
                  '/usr/local/lib/',
                ],
              }],
            ],
          }],
          [ 'disable_werror==0 and OS!="android" and OS!="win"', {
            'cflags': [
              '<!@("<(python)" <(DEPTH)/coreconf/werror.py)',
            ],
            'xcode_settings': {
              'OTHER_CFLAGS': [
                '<!@("<(python)" <(DEPTH)/coreconf/werror.py)',
              ],
            },
          }],
          [ 'fuzz_tls==1', {
            'cflags': [
              '-Wno-unused-function',
              '-Wno-unused-variable',
            ],
            'xcode_settings': {
              'OTHER_CFLAGS': [
                '-Wno-unused-function',
                '-Wno-unused-variable',
              ],
            },
          }],
          [ 'sanitizer_flags!=0', {
            'cflags': ['<@(sanitizer_flags)'],
            'ldflags': ['<@(sanitizer_flags)'],
            'xcode_settings': {
              'OTHER_CFLAGS': ['<@(sanitizer_flags)'],
              # We want to pass -fsanitize=... to our final link call,
              # but not to libtool. OTHER_LDFLAGS is passed to both.
              # To trick GYP into doing what we want, we'll piggyback on
              # LIBRARY_SEARCH_PATHS, producing "-L/usr/lib -fsanitize=...".
              # The -L/usr/lib is redundant but innocuous: it's a default path.
              'LIBRARY_SEARCH_PATHS': ['/usr/lib <(sanitizer_flags)'],
            },
            # gyp's Windows generator ignores cflags and ldflags. See
            # clang_cl_sanitizers in coreconf/sanitizers.sh.
            'msvs_settings': {
              'VCCLCompilerTool': {
                'AdditionalOptions': [
                  '<@(sanitizer_flags)',
                  '/clang:-fno-omit-frame-pointer',
                  '/clang:-fno-optimize-sibling-calls',
                ],
              },
            },
            'conditions': [
              # Link the ASan runtime as clang-cl would: the DLL's import
              # library, the whole static thunk, and the SEH interceptor.
              [ 'clang_rt_asan_arch!=""', {
                'msvs_settings': {
                  'VCLinkerTool': {
                    'AdditionalOptions': [
                      'clang_rt.asan_dynamic-<(clang_rt_asan_arch).lib',
                      '-wholearchive:clang_rt.asan_dynamic_runtime_thunk-<(clang_rt_asan_arch).lib',
                    ],
                  },
                },
                'conditions': [
                  [ 'target_arch=="ia32"', {
                    'msvs_settings': {
                      'VCLinkerTool': {
                        'AdditionalOptions': [ '-include:___asan_seh_interceptor' ],
                      },
                    },
                  }, {
                    'msvs_settings': {
                      'VCLinkerTool': {
                        'AdditionalOptions': [ '-include:__asan_seh_interceptor' ],
                      },
                    },
                  }],
                ],
              }],
            ],
          }],
          [ 'emit_llvm==1', {
            'cflags': ['-flto'],
            'ldflags': ['-flto', '-fuse-ld=gold', '-Wl,-plugin-opt=save-temps'],
          }],
          [ 'OS=="android" and mozilla_client==0', {
            'defines': [
              'NO_SYSINFO',
              'NO_FORK_CHECK',
              'ANDROID',
            ],
          }],
          [ 'OS=="mac" or OS=="ios"', {
            'defines': [
              'DARWIN',
            ],
            'conditions': [
              [ 'target_arch=="ia32"', {
                'xcode_settings': {
                  'ARCHS': ['i386'],
                },
              }],
              [ 'target_arch=="x64"', {
                'xcode_settings': {
                  'ARCHS': ['x86_64'],
                },
              }],
              [ 'target_arch=="arm64"', {
                'xcode_settings': {
                  'ARCHS': ['arm64'],
                },
              }],
            ],
          }],
          [ 'OS=="ios"', {
            'xcode_settings': {
              'IPHONEOS_DEPLOYMENT_TARGET': '<(iphone_deployment_target)',
            },
          }],
          [ 'OS=="win"', {
            'defines': [
              '_WINDOWS',
              '_CRT_SECURE_NO_WARNINGS',
              '_CRT_NONSTDC_NO_WARNINGS',
            ],
            'cflags': [
              '-W3',
              '-w44267', # Disable C4267: conversion from 'size_t' to 'type', possible loss of data
              '-w44244', # Disable C4244: conversion from 'type1' to 'type2', possible loss of data
              '-w44018', # Disable C4018: 'expression' : signed/unsigned mismatch
              '-w44312', # Disable C4312: 'type cast': conversion from 'type1' to 'type2' of greater size
            ],
            'conditions': [
              [ 'disable_werror==0', {
                'cflags': ['-WX']
              }],
              [ 'target_arch=="ia32"', {
                'msvs_configuration_platform': 'Win32',
                'msvs_settings': {
                  'VCLinkerTool': {
                    'MinimumRequiredVersion': '5.01',  # XP.
                    'TargetMachine': '1',
                    'ImageHasSafeExceptionHandlers': 'false',
                  },
                  'VCCLCompilerTool': {
                    'PreprocessorDefinitions': [
                      'WIN32',
                    ],
                    'AdditionalOptions': [ '/EHsc' ],
                  },
                },
              }],
              [ 'target_arch=="x64"', {
                'msvs_configuration_platform': 'x64',
                'msvs_settings': {
                  'VCLinkerTool': {
                    'TargetMachine': '17', # x86-64
                  },
                  'VCCLCompilerTool': {
                    'PreprocessorDefinitions': [
                      'WIN64',
                      '_AMD64_',
                    ],
                    'AdditionalOptions': [ '/EHsc' ],
                  },
                },
              }],
              [ 'target_arch=="arm64"', {
                'msvs_configuration_platform': 'ARM64',
                'msvs_settings': {
                  'VCCLCompilerTool': {
                    'PreprocessorDefinitions': [
                      'WIN64',
                      '_ARM64_',
                      '__ARM_FEATURE_CRYPTO',
                    ],
                    'AdditionalOptions': [ '/EHsc' ],
                  },
                },
                'conditions': [
                  # gyp runs clang-cl with -m64, which alone means x86-64.
                  [ 'clang_cl==1', {
                    'msvs_settings': {
                      'VCCLCompilerTool': {
                        'AdditionalOptions': [ '--target=aarch64-pc-windows-msvc' ],
                      },
                    },
                  }],
                ],
              }],
            ],
          }],
          [ 'disable_dbm==1', {
            'defines': [
              'NSS_DISABLE_DBM',
            ],
          }],
          [ 'disable_libpkix==1', {
            'defines': [
              'NSS_DISABLE_LIBPKIX',
            ],
          }],
          [ 'disable_dsa==1', {
            'defines': [
              'NSS_DISABLE_DSA',
            ],
          }],
          [ 'disable_deprecated_seed==1', {
            'defines': [
              'NSS_DISABLE_DEPRECATED_SEED',
            ],
          }],
          [ 'disable_deprecated_rc2==1', {
            'defines': [
              'NSS_DISABLE_DEPRECATED_RC2',
            ],
          }],

          [ 'use_pkcs5_pbkd2_params2_only==1', {
            'defines': [
              'NSS_USE_PKCS5_PBKD2_PARAMS2_ONLY',
              'SOFTOKEN_USE_PKCS5_PBKD2_PARAMS2_ONLY',
            ],
          }],
          [ 'nss_use_pkcs5_pbkd2_params2_only==1', {
            'defines': [
              'NSS_USE_PKCS5_PBKD2_PARAMS2_ONLY',
            ],
          }],
          [ 'softoken_use_pkcs5_pbkd2_params2_only==1', {
            'defines': [
              'SOFTOKEN_USE_PKCS5_PBKD2_PARAMS2_ONLY',
            ],
          }],
        ],
      },
      # Common settings for debug should go here.
      'Debug': {
        'inherit_from': ['Common'],
        'conditions': [
          [ 'OS!="mac" and OS!="ios" and OS!="win"', {
            'cflags': [
              '-g',
              '<(moz_debug_flags)',
            ],
          }]
        ],
        #TODO: DEBUG_$USER
        'defines': ['DEBUG'],
        'cflags': [ '-O<(debug_optimization_level)' ],
        'xcode_settings': {
          'COPY_PHASE_STRIP': 'NO',
          'GCC_OPTIMIZATION_LEVEL': '<(debug_optimization_level)',
          'GCC_GENERATE_DEBUGGING_SYMBOLS': 'YES',
        },
        'msvs_settings': {
          'VCCLCompilerTool': {
            'Optimization': '<(debug_optimization_level)',
            'BasicRuntimeChecks': '3',
            'RuntimeLibrary': '2', # /MD
            'DebugInformationFormat': '3',
          },
          'VCLinkerTool': {
            'LinkIncremental': '1',
            'GenerateDebugInformation' : 'true',
          },
          'VCResourceCompilerTool': {
            'PreprocessorDefinitions': ['DEBUG'],
          },
        },
      },
      # Common settings for release should go here.
      'Release': {
        'inherit_from': ['Common'],
        'defines': ['NDEBUG'],
        'cflags': [ '-O<(release_optimization_level)' ],
        'xcode_settings': {
          'DEAD_CODE_STRIPPING': 'YES',  # -Wl,-dead_strip
          'GCC_OPTIMIZATION_LEVEL': '<(release_optimization_level)',
        },
        'msvs_settings': {
          'VCCLCompilerTool': {
            'Optimization': '<(release_optimization_level)',
            'RuntimeLibrary': '2', # /MD
          },
          'VCLinkerTool': {
            'LinkIncremental': '1',
          },
        },
      },
      'conditions': [
        [ 'OS=="win"', {
          # The gyp ninja backend requires these.
          # TODO: either we should support building both 32/64-bit as
          # configurations from the same gyp build, or we should fix
          # upstream gyp to not require these.
          'Debug_x64': {
            'inherit_from': ['Debug'],
          },
          'Release_x64': {
            'inherit_from': ['Release'],
          },
        }],
      ],
    },
  },
  'conditions': [
    # gyp's ninja generator only switches to clang-cl when CC names it here; it
    # ignores CC in the environment on Windows. Every build file includes this
    # one, which keeps the settings identical across them as gyp requires.
    [ 'clang_cl==1', {
      'make_global_settings': [
        ['CC', '<(llvm_bindir)/clang-cl'],
        ['LD', '<(llvm_bindir)/lld-link'],
        ['AR', '<(llvm_bindir)/llvm-lib'],
      ],
    }],
    [ 'cc_use_gnu_ld==1 or OS=="solaris"', {
      'variables': {
        'process_map_file': ['/bin/sh', '-c', '/usr/bin/env grep -v ";-" >(mapfile) | sed -e "s,;+,," -e "s; DATA ;;" -e "s,;;,," -e "s,;.*,;," > >@(_outputs)'],
      },
    }],
    [ 'OS=="mac" or OS=="ios"', {
      'variables': {
        'process_map_file': ['/bin/sh', '-c', '/usr/bin/grep -v ";+" >(mapfile) | grep -v ";-" | sed -e "s; DATA ;;" -e "s,;;,," -e "s,;.*,," -e "s,^,_," > >@(_outputs)'],
      },
    }],
  ],
}
