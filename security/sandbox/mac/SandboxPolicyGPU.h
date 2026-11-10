/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SandboxPolicyGPU_h
#define mozilla_SandboxPolicyGPU_h

namespace mozilla {

static const char SandboxPolicyGPU[] = R"SANDBOX_LITERAL(
  (version 1)

  (define shouldLog (param "SHOULD_LOG"))
  (define appPath (param "APP_PATH"))
  (define userCacheDir (param "DARWIN_USER_CACHE_DIR"))
  (define bundleIDCacheDir (param "BUNDLE_ID_CACHE_DIR"))
  (define homePath (param "HOME_PATH"))
  (define crashPort (param "CRASH_PORT"))
  (define macosVersion (string->number (param "MAC_OS_VERSION")))
  (define isRosettaTranslated (param "IS_ROSETTA_TRANSLATED"))
  (define allowRemoteAppleImageIO (param "ALLOW_REMOTE_APPLE_IMAGEIO"))

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
    (sysctl-name "kern.hv_vmm_present")
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
    ; Graphics
    (global-name "com.apple.CARenderServer")
    (global-name "com.apple.MTLCompilerService")
    (global-name "com.apple.CoreDisplay.master")
    (global-name "com.apple.CoreDisplay.Notification")
    (global-name "com.apple.cvmsServ"))

  (if (string=? allowRemoteAppleImageIO "TRUE")
    (allow mach-lookup
      (global-name "com.apple.ImageIOXPCService")))

  ; Allow access to defaults services
  (allow mach-lookup
    (global-name "com.apple.cfprefsd.agent")
    (global-name "com.apple.cfprefsd.daemon"))
  (allow ipc-posix-shm-read-data
    (ipc-posix-name-regex #"^apple\.cfprefs\..*"))

  (define (home-subpath home-relative-subpath)
    (subpath (string-append homePath home-relative-subpath)))

  (allow file-read*
    (subpath "/Library/ColorSync/Profiles")
    (literal "/")
    (literal "/private/tmp")
    (literal "/private/var/tmp")
    (home-subpath "/Library/Colors")
    (home-subpath "/Library/ColorSync/Profiles"))

  (allow file-read* (subpath "/private/var/db/CVMS"))

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

  (allow iokit-get-properties
    (iokit-property "board-id")
    (iokit-property "product-id")
    (iokit-property "class-code")
    (iokit-property "vendor-id")
    (iokit-property "device-id")
    (iokit-property "IODVDBundleName")
    (iokit-property "IOGLBundleName")
    (iokit-property "IOGVACodec")
    (iokit-property "IOGVAHEVCDecode")
    (iokit-property "IOAVDHEVCDecodeCapabilities")
    (iokit-property "IOGVAHEVCEncode")
    (iokit-property "IOGVAXDecode")
    (iokit-property "IOAVDAV1DecodeCapabilities")
    (iokit-property "IOPCITunnelled")
    (iokit-property "IOVARendererID")
    (iokit-property "MetalPluginName")
    (iokit-property "MetalPluginClassName")
    (iokit-property "gpu-core-count"))

  ; VM compatibility
  (with-filter (iokit-registry-entry-class "IOPlatformDevice")
    (allow iokit-get-properties
      (iokit-property "product-id")
      (iokit-property "soc-generation")
      (iokit-property "IORegistryEntryPropertyKeys")
      (iokit-property "ean-storage-present")))
  (with-filter (iokit-registry-entry-class "IOService")
    (allow iokit-get-properties
      (iokit-property "housing-color")
      (iokit-property "syscfg-v2-data")))

  (allow iokit-set-properties
    (require-all
      (iokit-connection "IODisplay")
        (require-any
          (iokit-property "brightness"
                          "linear-brightness"
                          "commit"
                          "rgcs"
                          "ggcs"
                          "bgcs"))))

  (allow iokit-open
    (iokit-connection "IOAccelerator")
    (iokit-user-client-class "AppleIntelMEUserClient")
    (iokit-user-client-class "AppleSNBFBUserClient")
    (iokit-user-client-class "IOAccelerationUserClient")
    (iokit-user-client-class "IOSurfaceRootUserClient")
    (iokit-user-client-class "IOSurfaceSendRight")
    (iokit-user-client-class "IOFramebufferSharedUserClient")
    (iokit-user-client-class "AGPMClient")
    (iokit-user-client-class "IOHIDParamUserClient")
    (iokit-user-client-class "RootDomainUserClient")
    (iokit-user-client-class "AppleMGPUPowerControlClient")
    (iokit-user-client-class "AppleGraphicsControlClient")
    (iokit-user-client-class "AppleGraphicsPolicyClient"))

  ; Fonts
  (allow file-read*
    (subpath "/Library/Fonts")
    (subpath "/Library/Application Support/Apple/Fonts")
    (home-subpath "/Library/Fonts")
    ; Allow read access to paths allowed via sandbox extensions.
    ; This is needed for fonts in non-standard locations normally
    ; due to third party font managers. The extensions are
    ; automatically issued by the font server in response to font
    ; API calls.
    (extension "com.apple.app-sandbox.read"))
  ; Fonts may continue to work without explicitly allowing these
  ; services because, at present, connections are made to the services
  ; before the sandbox is enabled as a side-effect of some API calls.
  (allow mach-lookup
    (global-name "com.apple.fonts")
    (global-name "com.apple.FontObjectsServer"))

  (if (string=? isRosettaTranslated "TRUE")
    (allow file-map-executable (subpath "/private/var/db/oah")))
)SANDBOX_LITERAL";

// Added to the GPU process when the parent sets hasWindowServer.
static const char SandboxPolicyGPUWindowServerAddend[] = R"SANDBOX_LITERAL(

  (allow mach-lookup
    (global-name "com.apple.windowserver.active"))

)SANDBOX_LITERAL";

}  // namespace mozilla

#endif  // mozilla_SandboxPolicyGPU_h
