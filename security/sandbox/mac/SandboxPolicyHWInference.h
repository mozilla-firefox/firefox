/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SandboxPolicyHWInference_h
#define mozilla_SandboxPolicyHWInference_h

namespace mozilla {

static const char SandboxPolicyHWInference[] = R"SANDBOX_LITERAL(
  (version 1)

  (define shouldLog (param "SHOULD_LOG"))
  (define appPath (param "APP_PATH"))
  (define userCacheDir (param "DARWIN_USER_CACHE_DIR"))
  (define bundleIDCacheDir (param "BUNDLE_ID_CACHE_DIR"))
  (define homePath (param "HOME_PATH"))
  (define crashPort (param "CRASH_PORT"))
  (define macosVersion (string->number (param "MAC_OS_VERSION")))
  (define isRosettaTranslated (param "IS_ROSETTA_TRANSLATED"))

  (define (moz-deny feature)
    (if (string=? shouldLog "TRUE")
      (deny feature)
      (deny feature (with no-log))))

  (moz-deny default)
  (moz-deny process-info*)
  (moz-deny nvram*)
  (moz-deny iokit-get-properties)
  (moz-deny file-map-executable)

  (allow process-info-pidinfo process-info-setcontrol (target self))
  (allow user-preference-read)
  (allow file-read-metadata (subpath "/"))
  (allow file-map-executable file-read*
    (subpath "/System")
    (subpath "/usr/lib")
    (subpath "/Library/GPUBundles")
    (subpath appPath))

  (allow signal (target self))
  (allow file-read*
    (literal "/dev/random")
    (literal "/dev/urandom")
    (subpath "/usr/share/icu"))

  (if (string? crashPort)
    (allow mach-lookup (global-name crashPort)))

  (allow sysctl-read
    (sysctl-name-regex #"^sysctl\.")
    (sysctl-name "kern.ostype")
    (sysctl-name "kern.osversion")
    (sysctl-name "kern.osrelease")
    (sysctl-name "kern.osproductversion")
    (sysctl-name "kern.version")
    (sysctl-name "kern.hostname")
    (sysctl-name "hw.machine")
    (sysctl-name "hw.memsize")
    (sysctl-name "hw.model")
    (sysctl-name "hw.ncpu")
    (sysctl-name "hw.activecpu")
    (sysctl-name "hw.byteorder")
    (sysctl-name "hw.pagesize_compat")
    (sysctl-name "hw.logicalcpu_max")
    (sysctl-name "hw.physicalcpu_max")
    (sysctl-name "hw.busfrequency_compat")
    (sysctl-name "hw.busfrequency_max")
    (sysctl-name "hw.cpufrequency")
    (sysctl-name "hw.cpufrequency_compat")
    (sysctl-name "hw.cpufrequency_max")
    (sysctl-name "hw.l2cachesize")
    (sysctl-name "hw.l3cachesize")
    (sysctl-name "hw.cachelinesize")
    (sysctl-name "hw.cachelinesize_compat")
    (sysctl-name "hw.tbfrequency_compat")
    (sysctl-name "hw.vectorunit")
    (sysctl-name "hw.optional.sse2")
    (sysctl-name "hw.optional.sse3")
    (sysctl-name "hw.optional.sse4_1")
    (sysctl-name "hw.optional.sse4_2")
    (sysctl-name "hw.optional.avx1_0")
    (sysctl-name "hw.optional.avx2_0")
    (sysctl-name "hw.optional.avx512f")
    (sysctl-name "hw.optional.avx512bw")
    (sysctl-name "machdep.cpu.vendor")
    (sysctl-name "machdep.cpu.family")
    (sysctl-name "machdep.cpu.model")
    (sysctl-name "machdep.cpu.stepping")
    (sysctl-name "machdep.ptrauth_enabled")
    (sysctl-name "debug.intel.gstLevelGST")
    (sysctl-name "debug.intel.gstLoaderControl")
    (sysctl-name "hw.perflevel0.logicalcpu")
    (sysctl-name "hw.perflevel0.physicalcpu")
    (sysctl-name "hw.perflevel0.physicalcpu_max")
    (sysctl-name "hw.perflevel0.logicalcpu")
    (sysctl-name "hw.perflevel0.logicalcpu_max")
    (sysctl-name "hw.perflevel0.l1icachesize")
    (sysctl-name "hw.perflevel0.l1dcachesize")
    (sysctl-name "hw.perflevel0.l2cachesize")
    (sysctl-name "hw.perflevel0.cpusperl2")
    (sysctl-name "hw.perflevel0.name")
    (sysctl-name "hw.perflevel1.logicalcpu")
    (sysctl-name "hw.perflevel1.physicalcpu")
    (sysctl-name "hw.perflevel1.physicalcpu_max")
    (sysctl-name "hw.perflevel1.logicalcpu")
    (sysctl-name "hw.perflevel1.logicalcpu_max")
    (sysctl-name "hw.perflevel1.l1icachesize")
    (sysctl-name "hw.perflevel1.l1dcachesize")
    (sysctl-name "hw.perflevel1.l2cachesize")
    (sysctl-name "hw.perflevel1.cpusperl2")
    (sysctl-name "hw.perflevel1.name"))

  (allow mach-lookup
    (global-name "com.apple.system.opendirectoryd.libinfo")
    (global-name "com.apple.system.opendirectoryd.membership")
    (global-name "com.apple.CoreServices.coreservicesd")
    (global-name "com.apple.lsd.mapdb")
    ; Metal compilation service (required for ML inference)
    (global-name "com.apple.MTLCompilerService")
    (global-name "com.apple.cvmsServ"))

  (define (home-subpath home-relative-subpath)
    (subpath (string-append homePath home-relative-subpath)))

  (allow file-read*
    (literal "/")
    (literal "/private/tmp")
    (literal "/private/var/tmp"))

  ; Allow creation of the bundle ID cache directory and files within.
  (allow file-read* file-write*
    (require-all
      (require-not (vnode-type SYMLINK))
      (subpath bundleIDCacheDir)))

  ; Allow issuing sandbox extensions for the MTLCompilerService process
  ; to be able to read and write files in the bundle ID cache dir in the
  ; "com.apple.{metalfe,gpuarchiver}" subdirectories. Only observed
  ; to be needed on macOS 14 and earlier versions.
  (if (<= macosVersion 1500)
    (allow file-issue-extension
      (require-all
        (extension-class "com.apple.app-sandbox.read-write")
        (require-not (vnode-type SYMLINK))
        (require-any
          (subpath (string-append bundleIDCacheDir "/com.apple.metalfe"))
          (subpath (string-append bundleIDCacheDir "/com.apple.gpuarchiver"))))))

  ; Minimal IOKit properties needed for Metal device identification
  (allow iokit-get-properties
    (iokit-property "vendor-id")
    (iokit-property "device-id")
    (iokit-property "IOVARendererID")
    (iokit-property "MetalPluginName")
    (iokit-property "MetalPluginClassName")
    (iokit-property "gpu-core-count"))

  ; Minimal IOKit connections needed for Metal compute
  (allow iokit-open
    (iokit-connection "IOAccelerator")
    (iokit-user-client-class "IOAccelerationUserClient")
    (iokit-user-client-class "AGPMClient")
    (iokit-user-client-class "AppleGraphicsControlClient")
    (iokit-user-client-class "AppleMGPUPowerControlClient")
    (iokit-user-client-class "AppleGraphicsPolicyClient"))

  (if (string=? isRosettaTranslated "TRUE")
    (allow file-map-executable (subpath "/private/var/db/oah")))
)SANDBOX_LITERAL";

}  // namespace mozilla

#endif  // mozilla_SandboxPolicyHWInference_h
