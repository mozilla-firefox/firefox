/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GfxInfo.h"

#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <glib.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>

#include "MediaCodecsSupport.h"
#include "WidgetUtilsGtk.h"
#include "mozilla/GUniquePtr.h"
#include "mozilla/SSE.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/XREAppData.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/glean/GfxMetrics.h"
#include "nsAppRunner.h"
#include "nsCRTGlue.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsExceptionHandler.h"
#include "nsPrintfCString.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsUnicharUtils.h"
#include "nsWhitespaceTokenizer.h"
#include "prenv.h"

#ifdef MOZ_WAYLAND
#  include "nsWaylandDisplay.h"
#endif

#ifndef GBM_FORMAT_P010
#  define GBM_FORMAT_P010 __gbm_fourcc_code('P', '0', '1', '0')
#endif

// How long we wait for data from gfxtest probe processes in milliseconds.
#define GFX_TEST_TIMEOUT 4000
#define VAAPI_TEST_TIMEOUT 2000
#define V4L2_TEST_TIMEOUT 2000
#define VULKAN_TEST_TIMEOUT 2000

#define GFX_PROBE_BINARY u"gfxtest"_ns

namespace mozilla::widget {

#ifdef DEBUG
NS_IMPL_ISUPPORTS_INHERITED(GfxInfo, GfxInfoBase, nsIGfxInfoDebug)
#endif

int GfxInfo::sGLXTestPipe = -1;
pid_t GfxInfo::sGLXTestPID = 0;

// bits to use decoding codec information returned from glxtest
constexpr int CODEC_HW_DEC_H264 = 1 << 4;
constexpr int CODEC_HW_ENC_H264 = 1 << 5;
constexpr int CODEC_HW_DEC_VP8 = 1 << 6;
constexpr int CODEC_HW_ENC_VP8 = 1 << 7;
constexpr int CODEC_HW_DEC_VP9 = 1 << 8;
constexpr int CODEC_HW_ENC_VP9 = 1 << 9;
constexpr int CODEC_HW_DEC_AV1 = 1 << 10;
constexpr int CODEC_HW_ENC_AV1 = 1 << 11;
constexpr int CODEC_HW_DEC_HEVC = 1 << 12;
constexpr int CODEC_HW_ENC_HEVC = 1 << 13;

nsresult GfxInfo::Init() {
  mGLMajorVersion = 0;
  mGLMinorVersion = 0;
  mHasTextureFromPixmap = false;
  mIsMesa = false;
  mIsAccelerated = true;
  mIsWayland = false;
  mIsXWayland = false;
  mHasMultipleGPUs = false;
  mGlxTestError = false;
  return GfxInfoBase::Init();
}

const nsTArray<uint64_t>& GfxInfo::GetDMABufEGLModifiers(
    uint32_t aDrmFourcc) const {
  switch (aDrmFourcc) {
    case GBM_FORMAT_XRGB8888:
      return mDMABufEGLModifiersXRGB;
    case GBM_FORMAT_ARGB8888:
      return mDMABufEGLModifiersARGB;
    case GBM_FORMAT_NV12:
      return mDMABufEGLModifiersNV12;
    case GBM_FORMAT_P010:
      return mDMABufEGLModifiersP010;
    case GBM_FORMAT_YUV420:
      return mDMABufEGLModifiersYUV420;
    default: {
      NS_WARNING("GfxInfo::GetDMABufEGLModifiers(): unsupported format!");
      static const nsTArray<uint64_t> empty;
      return empty;
    }
  }
}

void GfxInfo::AddCrashReportAnnotations() {
  CrashReporter::RecordAnnotationNSCString(
      CrashReporter::Annotation::AdapterVendorID, mVendorId);
  CrashReporter::RecordAnnotationNSCString(
      CrashReporter::Annotation::AdapterDeviceID, mDeviceId);
  CrashReporter::RecordAnnotationNSCString(
      CrashReporter::Annotation::AdapterDriverVendor, mDriverVendor);
  CrashReporter::RecordAnnotationNSCString(
      CrashReporter::Annotation::AdapterDriverVersion, mDriverVersion);
  CrashReporter::RecordAnnotationBool(CrashReporter::Annotation::IsWayland,
                                      mIsWayland);
  CrashReporter::RecordAnnotationNSCString(
      CrashReporter::Annotation::DesktopEnvironment,
      GetDesktopEnvironmentIdentifier());

  if (mHasMultipleGPUs) {
    nsAutoCString note;
    note.AppendLiteral("Has dual GPUs.");
    if (!mSecondaryVendorId.IsEmpty()) {
      note.AppendLiteral(" GPU #2: AdapterVendorID2: ");
      note.Append(mSecondaryVendorId);
      note.AppendLiteral(", AdapterDeviceID2: ");
      note.Append(mSecondaryDeviceId);
    }
    CrashReporter::AppendAppNotesToCrashReport(note);
  }
}

static bool MakeFdNonBlocking(int fd) {
  return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) != -1;
}

static bool ManageChildProcess(const char* aProcessName, int* aPID, int* aPipe,
                               int aTimeout, char** aData) {
  // Don't try anything if we failed before
  if (*aPID < 0) {
    return false;
  }

  GIOChannel* channel = nullptr;
  *aData = nullptr;

  auto free = mozilla::MakeScopeExit([&] {
    if (channel) {
      g_io_channel_unref(channel);
    }
    if (*aPipe >= 0) {
      close(*aPipe);
      *aPipe = -1;
    }
  });

  const TimeStamp deadline =
      TimeStamp::Now() + TimeDuration::FromMilliseconds(aTimeout);

  struct pollfd pfd{};
  pfd.fd = *aPipe;
  pfd.events = POLLIN;

  while (poll(&pfd, 1, aTimeout) != 1) {
    if (errno != EAGAIN && errno != EINTR) {
      gfxCriticalNote << "ManageChildProcess(" << aProcessName
                      << "): poll failed: " << strerror(errno) << "\n";
      return false;
    }
    if (TimeStamp::Now() > deadline) {
      gfxCriticalNote << "ManageChildProcess(" << aProcessName
                      << "): process hangs\n";
      return false;
    }
  }

  channel = g_io_channel_unix_new(*aPipe);
  MakeFdNonBlocking(*aPipe);

  GUniquePtr<GError> error;
  gsize length = 0;
  int ret;
  do {
    error = nullptr;
    ret = g_io_channel_read_to_end(channel, aData, &length,
                                   getter_Transfers(error));
  } while (ret == G_IO_STATUS_AGAIN && TimeStamp::Now() < deadline);
  if (error || ret != G_IO_STATUS_NORMAL) {
    gfxCriticalNote << "ManageChildProcess(" << aProcessName
                    << "): failed to read data from child process: ";
    if (error) {
      gfxCriticalNote << error->message;
    } else {
      gfxCriticalNote << "timeout";
    }
    return false;
  }

  int status = 0;
  int pid = *aPID;
  *aPID = -1;

  while (true) {
    int ret = waitpid(pid, &status, WNOHANG);
    if (ret > 0) {
      break;
    }
    if (ret < 0) {
      if (errno == ECHILD) {
        // Bug 718629
        // ECHILD happens when the glxtest process got reaped got reaped after a
        // PR_CreateProcess as per bug 227246. This shouldn't matter, as we
        // still seem to get the data from the pipe, and if we didn't, the
        // outcome would be to blocklist anyway.
        return true;
      }
      if (errno != EAGAIN && errno != EINTR) {
        gfxCriticalNote << "ManageChildProcess(" << aProcessName
                        << "): waitpid failed: " << strerror(errno) << "\n";
        return false;
      }
    }
    if (TimeStamp::Now() > deadline) {
      gfxCriticalNote << "ManageChildProcess(" << aProcessName
                      << "): process hangs\n";
      return false;
    }
    // Wait 50ms to another waitpid() check.
    usleep(50000);
  }

  return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
}

// to understand this function, see bug 639842. We retrieve the OpenGL driver
// information in a separate process to protect against bad drivers.
void GfxInfo::GetData() {
  if (mInitialized) {
    return;
  }
  mInitialized = true;

  // In some cases (xpcshell test, Profile manager etc.)
  // FireGLXTestProcess() is not fired in advance
  // so we call it here.
  GfxInfo::FireGLXTestProcess();

  GfxInfoBase::GetData();

  char* glxData = nullptr;
  auto free = mozilla::MakeScopeExit([&] { g_free((void*)glxData); });

  bool error = !ManageChildProcess("glxtest", &sGLXTestPID, &sGLXTestPipe,
                                   GFX_TEST_TIMEOUT, &glxData);
  if (error) {
    gfxWarning()
        << "Failed to get GPU info from glxtest. Fallback to SW rendering! Run "
           "with MOZ_GFX_DEBUG=1 env variable to get further info.\n";
  }

  nsCString glVendor;
  nsCString glRenderer;
  nsCString glVersion;
  nsCString textureFromPixmap;
  nsCString testType;

  // Available if GLX_MESA_query_renderer is supported.
  nsCString mesaVendor;
  nsCString mesaDevice;
  nsCString mesaAccelerated;
  // Available if using a DRI-based libGL stack.
  nsCString driDriver;
  nsCString adapterRam;

  nsCString drmRenderDevice;

  nsCString ddxDriver;

  AutoTArray<nsCString, 2> pciVendors;
  AutoTArray<nsCString, 2> pciDevices;

  nsCString dmabufModifiersXRGB;
  nsCString dmabufModifiersARGB;
  nsCString dmabufModifiersNV12;
  nsCString dmabufModifiersP010;
  nsCString dmabufModifiersYUV420;

  nsCString* stringToFill = nullptr;
  bool logString = false;
  bool errorLog = false;

  char* bufptr = glxData;

  while (true) {
    char* line = NS_strtok("\n", &bufptr);
    if (!line) break;
    if (stringToFill) {
      stringToFill->Assign(line);
      stringToFill = nullptr;
    } else if (logString) {
      gfxWarning() << "glxtest: " << line;
      logString = false;
    } else if (!strcmp(line, "VENDOR")) {
      stringToFill = &glVendor;
    } else if (!strcmp(line, "RENDERER")) {
      stringToFill = &glRenderer;
    } else if (!strcmp(line, "VERSION")) {
      stringToFill = &glVersion;
    } else if (!strcmp(line, "TFP")) {
      stringToFill = &textureFromPixmap;
    } else if (!strcmp(line, "MESA_VENDOR_ID")) {
      stringToFill = &mesaVendor;
    } else if (!strcmp(line, "MESA_DEVICE_ID")) {
      stringToFill = &mesaDevice;
    } else if (!strcmp(line, "MESA_ACCELERATED")) {
      stringToFill = &mesaAccelerated;
    } else if (!strcmp(line, "MESA_VRAM")) {
      stringToFill = &adapterRam;
    } else if (!strcmp(line, "DDX_DRIVER")) {
      stringToFill = &ddxDriver;
    } else if (!strcmp(line, "DRI_DRIVER")) {
      stringToFill = &driDriver;
    } else if (!strcmp(line, "PCI_VENDOR_ID")) {
      stringToFill = pciVendors.AppendElement();
    } else if (!strcmp(line, "PCI_DEVICE_ID")) {
      stringToFill = pciDevices.AppendElement();
    } else if (!strcmp(line, "DRM_RENDERDEVICE")) {
      stringToFill = &drmRenderDevice;
    } else if (!strcmp(line, "TEST_TYPE")) {
      stringToFill = &testType;
    } else if (!strcmp(line, "DMABUF_MODIFIERS_XRGB")) {
      stringToFill = &dmabufModifiersXRGB;
    } else if (!strcmp(line, "DMABUF_MODIFIERS_ARGB")) {
      stringToFill = &dmabufModifiersARGB;
    } else if (!strcmp(line, "DMABUF_MODIFIERS_NV12")) {
      stringToFill = &dmabufModifiersNV12;
    } else if (!strcmp(line, "DMABUF_MODIFIERS_P010")) {
      stringToFill = &dmabufModifiersP010;
    } else if (!strcmp(line, "DMABUF_MODIFIERS_YUV420")) {
      stringToFill = &dmabufModifiersYUV420;
    } else if (!strcmp(line, "WARNING")) {
      logString = true;
    } else if (!strcmp(line, "ERROR")) {
      logString = true;
      errorLog = true;
    }
  }

  auto parseModifiers = [](const nsCString& aStr, nsTArray<uint64_t>& aOut) {
    if (aStr.IsEmpty()) {
      return;
    }
    nsCCharSeparatedTokenizer tokenizer(aStr, ',');
    while (tokenizer.hasMoreTokens()) {
      const auto& token = tokenizer.nextToken();
      nsresult rv;
      uint64_t val = token.ToUnsignedInteger64(&rv, 16);
      if (NS_SUCCEEDED(rv)) {
        aOut.AppendElement(val);
      }
    }
  };
  parseModifiers(dmabufModifiersXRGB, mDMABufEGLModifiersXRGB);
  parseModifiers(dmabufModifiersARGB, mDMABufEGLModifiersARGB);
  parseModifiers(dmabufModifiersNV12, mDMABufEGLModifiersNV12);
  parseModifiers(dmabufModifiersP010, mDMABufEGLModifiersP010);
  parseModifiers(dmabufModifiersYUV420, mDMABufEGLModifiersYUV420);

  MOZ_ASSERT(pciDevices.Length() == pciVendors.Length(),
             "Missing PCI vendors/devices");

  size_t pciLen = std::min(pciVendors.Length(), pciDevices.Length());
  mHasMultipleGPUs = pciLen > 1;

  if (!strcmp(textureFromPixmap.get(), "TRUE")) mHasTextureFromPixmap = true;

  // only useful for Linux kernel version check for FGLRX driver.
  // assumes X client == X server, which is sad.
  struct utsname unameobj{};
  if (uname(&unameobj) >= 0) {
    mOS.Assign(unameobj.sysname);
    mOSRelease.Assign(unameobj.release);
  }

  const char* spoofedVendor = PR_GetEnv("MOZ_GFX_SPOOF_GL_VENDOR");
  if (spoofedVendor) glVendor.Assign(spoofedVendor);
  const char* spoofedRenderer = PR_GetEnv("MOZ_GFX_SPOOF_GL_RENDERER");
  if (spoofedRenderer) glRenderer.Assign(spoofedRenderer);
  const char* spoofedVersion = PR_GetEnv("MOZ_GFX_SPOOF_GL_VERSION");
  if (spoofedVersion) glVersion.Assign(spoofedVersion);
  const char* spoofedOS = PR_GetEnv("MOZ_GFX_SPOOF_OS");
  if (spoofedOS) mOS.Assign(spoofedOS);
  const char* spoofedOSRelease = PR_GetEnv("MOZ_GFX_SPOOF_OS_RELEASE");
  if (spoofedOSRelease) mOSRelease.Assign(spoofedOSRelease);

  // Scan the GL_VERSION string for the GL and driver versions.
  nsCWhitespaceTokenizer tokenizer(glVersion);
  while (tokenizer.hasMoreTokens()) {
    nsCString token(tokenizer.nextToken());
    unsigned int major = 0, minor = 0, revision = 0, patch = 0;
    if (sscanf(token.get(), "%u.%u.%u.%u", &major, &minor, &revision, &patch) >=
        2) {
      // A survey of GL_VENDOR strings indicates that the first version is
      // always the GL version, the second is usually the driver version.
      if (mGLMajorVersion == 0) {
        mGLMajorVersion = major;
        mGLMinorVersion = minor;
      } else if (mDriverVersion.IsEmpty()) {  // Not already spoofed.
        mDriverVersion =
            nsPrintfCString("%u.%u.%u.%u", major, minor, revision, patch);
      }
    }
  }

  if (mGLMajorVersion == 0) {
    NS_WARNING("Failed to parse GL version!");
  }

  mDrmRenderDevice = std::move(drmRenderDevice);
  mTestType = std::move(testType);

  // Mesa always exposes itself in the GL_VERSION string, but not always the
  // GL_VENDOR string.
  mIsMesa = glVersion.Find("Mesa") != -1;

  // We need to use custom driver vendor IDs for mesa so we can treat them
  // differently than the proprietary drivers.
  if (mIsMesa) {
    mIsAccelerated = !mesaAccelerated.Equals("FALSE");
    // Process software rasterizers before the DRI driver string; we may be
    // forcing software rasterization on a DRI-accelerated X server by using
    // LIBGL_ALWAYS_SOFTWARE or a similar restriction.
    if (strcasestr(glRenderer.get(), "llvmpipe")) {
      CopyUTF16toUTF8(
          GfxDriverInfo::GetDriverVendor(DriverVendor::MesaLLVMPipe),
          mDriverVendor);
      mIsAccelerated = false;
    } else if (strcasestr(glRenderer.get(), "softpipe")) {
      CopyUTF16toUTF8(
          GfxDriverInfo::GetDriverVendor(DriverVendor::MesaSoftPipe),
          mDriverVendor);
      mIsAccelerated = false;
    } else if (strcasestr(glRenderer.get(), "software rasterizer")) {
      CopyUTF16toUTF8(GfxDriverInfo::GetDriverVendor(DriverVendor::MesaSWRast),
                      mDriverVendor);
      mIsAccelerated = false;
    } else if (strcasestr(driDriver.get(), "vmwgfx")) {
      CopyUTF16toUTF8(GfxDriverInfo::GetDriverVendor(DriverVendor::MesaVM),
                      mDriverVendor);
      mIsAccelerated = false;
    } else if (!mIsAccelerated) {
      CopyUTF16toUTF8(
          GfxDriverInfo::GetDriverVendor(DriverVendor::MesaSWUnknown),
          mDriverVendor);
    } else if (!driDriver.IsEmpty()) {
      mDriverVendor = nsPrintfCString("mesa/%s", driDriver.get());
    } else {
      // Some other mesa configuration where we couldn't get enough info.
      NS_WARNING("Failed to detect Mesa driver being used!");
      CopyUTF16toUTF8(GfxDriverInfo::GetDriverVendor(DriverVendor::MesaUnknown),
                      mDriverVendor);
    }

    if (!mesaVendor.IsEmpty()) {
      mVendorId = mesaVendor;
    }

    if (!mesaDevice.IsEmpty()) {
      mDeviceId = mesaDevice;
    }

    if (!mIsAccelerated && mVendorId.IsEmpty()) {
      mVendorId.Assign(glVendor.get());
    }

    if (!mIsAccelerated && mDeviceId.IsEmpty()) {
      mDeviceId.Assign(glRenderer.get());
    }
  } else if (glVendor.EqualsLiteral("NVIDIA Corporation")) {
    CopyUTF16toUTF8(GfxDriverInfo::GetDeviceVendor(DeviceVendor::NVIDIA),
                    mVendorId);
    mDriverVendor.AssignLiteral("nvidia/unknown");
    // TODO: Use NV-CONTROL X11 extension to query Device ID and VRAM.
  } else if (glVendor.EqualsLiteral("ATI Technologies Inc.")) {
    CopyUTF16toUTF8(GfxDriverInfo::GetDeviceVendor(DeviceVendor::ATI),
                    mVendorId);
    mDriverVendor.AssignLiteral("ati/unknown");
    // TODO: Look into ways to find the device ID on FGLRX.
  } else {
    NS_WARNING("Failed to detect GL vendor!");
  }

  if (!adapterRam.IsEmpty()) {
    mAdapterRAM = (uint32_t)atoi(adapterRam.get());
  }

  // If we have the DRI driver, we can derive the vendor ID from that if needed.
  if (mVendorId.IsEmpty() && !driDriver.IsEmpty()) {
    const char* nvidiaDrivers[] = {"nouveau", "tegra", nullptr};
    for (size_t i = 0; nvidiaDrivers[i]; ++i) {
      if (driDriver.Equals(nvidiaDrivers[i])) {
        CopyUTF16toUTF8(GfxDriverInfo::GetDeviceVendor(DeviceVendor::NVIDIA),
                        mVendorId);
        break;
      }
    }

    if (mVendorId.IsEmpty()) {
      const char* intelDrivers[] = {"iris", "crocus", "i915", "i965",
                                    "i810", "intel",  nullptr};
      for (size_t i = 0; intelDrivers[i]; ++i) {
        if (driDriver.Equals(intelDrivers[i])) {
          CopyUTF16toUTF8(GfxDriverInfo::GetDeviceVendor(DeviceVendor::Intel),
                          mVendorId);
          break;
        }
      }
    }

    if (mVendorId.IsEmpty()) {
      const char* amdDrivers[] = {"r600",   "r200",     "r100",
                                  "radeon", "radeonsi", nullptr};
      for (size_t i = 0; amdDrivers[i]; ++i) {
        if (driDriver.Equals(amdDrivers[i])) {
          CopyUTF16toUTF8(GfxDriverInfo::GetDeviceVendor(DeviceVendor::ATI),
                          mVendorId);
          break;
        }
      }
    }

    if (mVendorId.IsEmpty()) {
      if (driDriver.EqualsLiteral("freedreno")) {
        CopyUTF16toUTF8(GfxDriverInfo::GetDeviceVendor(DeviceVendor::Qualcomm),
                        mVendorId);
      }
    }
  }

  // If we still don't have a vendor ID, we can try the PCI vendor list.
  if (mVendorId.IsEmpty()) {
    if (pciVendors.IsEmpty()) {
      gfxWarning() << "No GPUs detected via PCI\n";
    } else {
      for (size_t i = 0; i < pciVendors.Length(); ++i) {
        if (mVendorId.IsEmpty()) {
          mVendorId = pciVendors[i];
        } else if (mVendorId != pciVendors[i]) {
          gfxWarning() << "More than 1 GPU vendor detected via PCI, cannot "
                          "deduce vendor\n";
          mVendorId.Truncate();
          break;
        }
      }
    }
  }

  // If we know the vendor ID, but didn't get a device ID, we can guess from the
  // PCI device list.
  if (mDeviceId.IsEmpty() && !mVendorId.IsEmpty()) {
    for (size_t i = 0; i < pciLen; ++i) {
      if (mVendorId.Equals(pciVendors[i])) {
        if (mDeviceId.IsEmpty()) {
          mDeviceId = pciDevices[i];
        } else if (mDeviceId != pciDevices[i]) {
          gfxWarning() << "More than 1 GPU from same vendor detected via "
                          "PCI, cannot deduce device\n";
          mDeviceId.Truncate();
          break;
        }
      }
    }
  }

  // Assuming we know the vendor, we should check for a secondary card.
  if (!mVendorId.IsEmpty()) {
    if (pciLen > 2) {
      gfxWarning()
          << "More than 2 GPUs detected via PCI, secondary GPU is arbitrary\n";
    }
    for (size_t i = 0; i < pciLen; ++i) {
      if (!mVendorId.Equals(pciVendors[i]) ||
          (!mDeviceId.IsEmpty() && !mDeviceId.Equals(pciDevices[i]))) {
        mSecondaryVendorId = pciVendors[i];
        mSecondaryDeviceId = pciDevices[i];
        break;
      }
    }
  }

  // If we couldn't choose, log them.
  if (mVendorId.IsEmpty()) {
    for (size_t i = 0; i < pciLen; ++i) {
      gfxWarning() << "PCI candidate " << pciVendors[i].get() << "/"
                   << pciDevices[i].get() << "\n";
    }
  }

  // Fallback to GL_VENDOR and GL_RENDERER.
  if (mVendorId.IsEmpty()) {
    mVendorId.Assign(glVendor.get());
  }
  if (mDeviceId.IsEmpty()) {
    mDeviceId.Assign(glRenderer.get());
  }

  mAdapterDescription.Assign(glRenderer);

  // Make a best effort guess at whether or not we are using the XWayland compat
  // layer. For all intents and purposes, we should otherwise believe we are
  // using X11.
  mIsWayland = GdkIsWaylandDisplay();
  mIsXWayland = IsXWaylandProtocol();

  if (!ddxDriver.IsEmpty()) {
    PRInt32 start = 0;
    PRInt32 loc = ddxDriver.Find(";", start);
    while (loc != kNotFound) {
      nsCString line(ddxDriver.get() + start, loc - start);
      mDdxDrivers.AppendElement(std::move(line));

      start = loc + 1;
      loc = ddxDriver.Find(";", start);
    }
  }

  if (error || errorLog || mTestType.IsEmpty()) {
    if (!mAdapterDescription.IsEmpty()) {
      mAdapterDescription.AppendLiteral(" (See failure log)");
    } else {
      mAdapterDescription.AppendLiteral("See failure log");
    }

    mGlxTestError = true;
  }

  AddCrashReportAnnotations();
}

int GfxInfo::FireTestProcess(const nsAString& aBinaryFile, int* aOutPipe,
                             const char** aStringArgs) {
  nsCOMPtr<nsIFile> appFile;
  nsresult rv = XRE_GetBinaryPath(getter_AddRefs(appFile));
  if (NS_FAILED(rv)) {
    gfxCriticalNote << "Couldn't find application file.\n";
    return false;
  }
  nsCOMPtr<nsIFile> exePath;
  rv = appFile->GetParent(getter_AddRefs(exePath));
  if (NS_FAILED(rv)) {
    gfxCriticalNote << "Couldn't get application directory.\n";
    return false;
  }
  exePath->Append(aBinaryFile);

#define MAX_ARGS 8
  char* argv[MAX_ARGS + 2];

  argv[0] = strdup(exePath->NativePath().get());
  for (int i = 0; i < MAX_ARGS; i++) {
    if (aStringArgs[i]) {
      argv[i + 1] = strdup(aStringArgs[i]);
    } else {
      argv[i + 1] = nullptr;
      break;
    }
  }

  // Use G_SPAWN_LEAVE_DESCRIPTORS_OPEN | G_SPAWN_DO_NOT_REAP_CHILD flags
  // to g_spawn_async_with_pipes() run posix_spawn() directly.
  int pid;
  GUniquePtr<GError> err;
  g_spawn_async_with_pipes(
      nullptr, argv, nullptr,
      GSpawnFlags(G_SPAWN_LEAVE_DESCRIPTORS_OPEN | G_SPAWN_DO_NOT_REAP_CHILD),
      nullptr, nullptr, &pid, nullptr, aOutPipe, nullptr,
      getter_Transfers(err));
  if (err) {
    gfxCriticalNote << "FireTestProcess failed: " << err->message << "\n";
    pid = 0;
  }
  for (auto& arg : argv) {
    if (!arg) {
      break;
    }
    free(arg);
  }
  return pid;
}

bool GfxInfo::FireGLXTestProcess() {
  if (sGLXTestPID != 0) {
    return true;
  }

  int pfd[2];
  if (pipe(pfd) == -1) {
    gfxCriticalNote << "FireGLXTestProcess failed to create pipe\n";
    return false;
  }
  sGLXTestPipe = pfd[0];

  auto pipeID = std::to_string(pfd[1]);
  const char* args[] = {"glx", "-f", pipeID.c_str(),
                        IsWaylandEnabled() ? "-w" : nullptr, nullptr};
  sGLXTestPID = FireTestProcess(GFX_PROBE_BINARY, nullptr, args);
  // Set pid to -1 to avoid further test launch.
  if (!sGLXTestPID) {
    sGLXTestPID = -1;
  }
  close(pfd[1]);
  return true;
}

void GfxInfo::GetDataVAAPI() {
  if (mIsVAAPISupported.isSome()) {
    return;
  }
  mIsVAAPISupported = Some(false);

#ifdef MOZ_ENABLE_VAAPI
  char* vaapiData = nullptr;
  auto free = mozilla::MakeScopeExit([&] { g_free((void*)vaapiData); });

  int vaapiPipe = -1;
  int vaapiPID = 0;
  const char* args[] = {"vaapi", "-d", mDrmRenderDevice.get(), nullptr};
  vaapiPID = FireTestProcess(GFX_PROBE_BINARY, &vaapiPipe, args);
  if (!vaapiPID) {
    return;
  }

  if (!ManageChildProcess("vaapitest", &vaapiPID, &vaapiPipe,
                          VAAPI_TEST_TIMEOUT, &vaapiData)) {
    gfxCriticalNote << "vaapitest: ManageChildProcess failed\n";
    return;
  }

  char* bufptr = vaapiData;
  char* line;
  while ((line = NS_strtok("\n", &bufptr))) {
    if (!strcmp(line, "VAAPI_SUPPORTED")) {
      line = NS_strtok("\n", &bufptr);
      if (!line) {
        gfxCriticalNote << "vaapitest: Failed to get VAAPI support\n";
        return;
      }
      mIsVAAPISupported = Some(!strcmp(line, "TRUE"));
    } else if (!strcmp(line, "VAAPI_HWCODECS")) {
      line = NS_strtok("\n", &bufptr);
      if (!line) {
        gfxCriticalNote << "vaapitest: Failed to get VAAPI codecs\n";
        return;
      }

      std::istringstream(line) >> mVAAPISupportedCodecs;

#  define VAAPI_CODEC_CHECK(name)                           \
    if (mVAAPISupportedCodecs & CODEC_HW_DEC_##name) {      \
      media::MCSInfo::AddSupport(                           \
          media::MediaCodecsSupport::name##HardwareDecode); \
    }                                                       \
    if (mVAAPISupportedCodecs & CODEC_HW_ENC_##name) {      \
      media::MCSInfo::AddSupport(                           \
          media::MediaCodecsSupport::name##HardwareEncode); \
    }
      VAAPI_CODEC_CHECK(H264)
      VAAPI_CODEC_CHECK(VP8)
      VAAPI_CODEC_CHECK(VP9)
      VAAPI_CODEC_CHECK(AV1)
      VAAPI_CODEC_CHECK(HEVC)
#  undef VAAPI_CODEC_CHECK
    } else if (!strcmp(line, "WARNING") || !strcmp(line, "ERROR")) {
      gfxCriticalNote << "vaapitest: " << line;
      line = NS_strtok("\n", &bufptr);
      if (line) {
        gfxCriticalNote << "vaapitest: " << line << "\n";
      }
      return;
    }
  }
#endif
}

// Probe all V4L2 devices and check their capabilities
void GfxInfo::GetDataV4L2() {
  if (mIsV4L2Supported.isSome()) {
    // We have already probed v4l2 support, no need to do it again.
    return;
  }
  mIsV4L2Supported = Some(false);

#ifdef MOZ_ENABLE_V4L2
  DIR* dir = opendir("/dev");
  if (!dir) {
    gfxCriticalNote << "Could not list /dev\n";
    return;
  }
  struct dirent* dir_entry;
  while ((dir_entry = readdir(dir))) {
    if (!strncmp(dir_entry->d_name, "video", 5)) {
      nsCString path = "/dev/"_ns;
      path += nsDependentCString(dir_entry->d_name);
      V4L2ProbeDevice(path);
    }
  }
  closedir(dir);
#endif  // MOZ_ENABLE_V4L2
}

void GfxInfo::GetDataVulkan() {
  if (mIsVulkanSupported.isSome()) {
    return;
  }
  mIsVulkanSupported = Some(false);
  mVulkanSupportedCodecs = 0;

#if defined(MOZ_ENABLE_VULKAN_VIDEO)
  char* vulkanData = nullptr;
  auto freeVulkan = mozilla::MakeScopeExit([&] { g_free((void*)vulkanData); });

  int vulkanPipe = -1;
  int vulkanPID = 0;
  const char* args[4];
  args[0] = "vulkan";
  if (mDrmRenderDevice.IsEmpty()) {
    args[1] = "-p";
    args[2] = nullptr;
  } else {
    args[1] = "-d";
    args[2] = mDrmRenderDevice.get();
    args[3] = nullptr;
  }
  vulkanPID = FireTestProcess(GFX_PROBE_BINARY, &vulkanPipe, args);
  if (!vulkanPID) {
    gfxCriticalNote << "Failed to start vulkantest process\n";
    return;
  }

  if (!ManageChildProcess("vulkantest", &vulkanPID, &vulkanPipe,
                          VULKAN_TEST_TIMEOUT, &vulkanData)) {
    gfxCriticalNote << "vulkantest: ManageChildProcess failed\n";
    return;
  }

  char* bufptr = vulkanData;
  char* line;
  while ((line = NS_strtok("\n", &bufptr))) {
    if (!strcmp(line, "VULKAN_SUPPORTED")) {
      line = NS_strtok("\n", &bufptr);
      if (!line) {
        gfxCriticalNote << "vulkantest: Failed to get Vulkan support\n";
        return;
      }
      mIsVulkanSupported = Some(!strcmp(line, "TRUE"));
    } else if (!strcmp(line, "VULKAN_HWCODECS")) {
      line = NS_strtok("\n", &bufptr);
      if (!line) {
        gfxCriticalNote << "vulkantest: Failed to get Vulkan codecs\n";
        return;
      }
      std::istringstream(line) >> mVulkanSupportedCodecs;

#  define VULKAN_CODEC_CHECK(name)                          \
    if (mVulkanSupportedCodecs & CODEC_HW_DEC_##name) {     \
      media::MCSInfo::AddSupport(                           \
          media::MediaCodecsSupport::name##HardwareDecode); \
    }
      VULKAN_CODEC_CHECK(H264)
      VULKAN_CODEC_CHECK(VP8)
      VULKAN_CODEC_CHECK(VP9)
      VULKAN_CODEC_CHECK(AV1)
      VULKAN_CODEC_CHECK(HEVC)
#  undef VULKAN_CODEC_CHECK
    } else if (!strcmp(line, "WARNING") || !strcmp(line, "ERROR")) {
      gfxCriticalNote << "vulkantest: " << line;
      line = NS_strtok("\n", &bufptr);
      if (line) {
        gfxCriticalNote << "vulkantest: " << line << "\n";
      }
      return;
    }
  }
#endif
}

// Check the capabilities of a single V4L2 device.  If the device doesn't work
// or doesn't support any codecs we recognise, then we just ignore it.  If it
// does support recognised codecs then add these codecs to the supported list
// and mark V4L2 as supported: We only need a single working device to enable
// V4L2, when we come to decode FFmpeg will probe all the devices and choose
// the appropriate one.
void GfxInfo::V4L2ProbeDevice(nsCString& dev) {
  char* v4l2Data = nullptr;
  auto free = mozilla::MakeScopeExit([&] { g_free((void*)v4l2Data); });

  int v4l2Pipe = -1;
  int v4l2PID = 0;
  const char* args[] = {"v4l2", "-d", dev.get(), nullptr};
  v4l2PID = FireTestProcess(GFX_PROBE_BINARY, &v4l2Pipe, args);
  if (!v4l2PID) {
    gfxCriticalNote << "Failed to start v4l2test process\n";
    return;
  }

  if (!ManageChildProcess("v4l2test", &v4l2PID, &v4l2Pipe, V4L2_TEST_TIMEOUT,
                          &v4l2Data)) {
    gfxCriticalNote << "v4l2test: ManageChildProcess failed\n";
    return;
  }

  char* bufptr = v4l2Data;
  char* line;
  nsTArray<nsCString> capFormats;
  nsTArray<nsCString> outFormats;
  bool supported = false;
  // Use gfxWarning rather than gfxCriticalNote from here on because the
  // errors/warnings output by v4l2test are generally just caused by devices
  // which aren't M2M decoders. Set gfx.logging.level=5 to see these messages.

  while ((line = NS_strtok("\n", &bufptr))) {
    if (!strcmp(line, "V4L2_SUPPORTED")) {
      line = NS_strtok("\n", &bufptr);
      if (!line) {
        gfxWarning() << "v4l2test: Failed to get V4L2 support\n";
        return;
      }
      supported = !strcmp(line, "TRUE");
    } else if (!strcmp(line, "V4L2_CAPTURE_FMTS")) {
      line = NS_strtok("\n", &bufptr);
      if (!line) {
        gfxWarning() << "v4l2test: Failed to get V4L2 CAPTURE formats\n";
        return;
      }
      char* capture_fmt;
      while ((capture_fmt = NS_strtok(" ", &line))) {
        capFormats.AppendElement(capture_fmt);
      }
    } else if (!strcmp(line, "V4L2_OUTPUT_FMTS")) {
      line = NS_strtok("\n", &bufptr);
      if (!line) {
        gfxWarning() << "v4l2test: Failed to get V4L2 OUTPUT formats\n";
        return;
      }
      char* output_fmt;
      while ((output_fmt = NS_strtok(" ", &line))) {
        outFormats.AppendElement(output_fmt);
      }
    } else if (!strcmp(line, "WARNING") || !strcmp(line, "ERROR")) {
      line = NS_strtok("\n", &bufptr);
      if (line) {
        gfxWarning() << "v4l2test: " << line << "\n";
      }
      return;
    }
  }

  // If overall SUPPORTED flag is not TRUE then stop now
  if (!supported) {
    return;
  }

  // Currently the V4L2 decode platform only supports YUV420 and NV12
  if (!capFormats.Contains("YV12") && !capFormats.Contains("NV12")) {
    return;
  }

  // Supported codecs
  if (outFormats.Contains("H264")) {
    mIsV4L2Supported = Some(true);
    media::MCSInfo::AddSupport(media::MediaCodecsSupport::H264HardwareDecode);
    mV4L2SupportedCodecs |= CODEC_HW_DEC_H264;
  }
  if (outFormats.Contains("VP80")) {
    mIsV4L2Supported = Some(true);
    media::MCSInfo::AddSupport(media::MediaCodecsSupport::VP8HardwareDecode);
    mV4L2SupportedCodecs |= CODEC_HW_DEC_VP8;
  }
  if (outFormats.Contains("VP90")) {
    mIsV4L2Supported = Some(true);
    media::MCSInfo::AddSupport(media::MediaCodecsSupport::VP9HardwareDecode);
    mV4L2SupportedCodecs |= CODEC_HW_DEC_VP9;
  }
  if (outFormats.Contains("HEVC")) {
    mIsV4L2Supported = Some(true);
    media::MCSInfo::AddSupport(media::MediaCodecsSupport::HEVCHardwareDecode);
    mV4L2SupportedCodecs |= CODEC_HW_DEC_HEVC;
  }
  if (outFormats.Contains("AV01")) {
    mIsV4L2Supported = Some(true);
    media::MCSInfo::AddSupport(media::MediaCodecsSupport::AV1HardwareDecode);
    mV4L2SupportedCodecs |= CODEC_HW_DEC_AV1;
  }
}

const nsTArray<RefPtr<GfxDriverInfo>>& GfxInfo::GetGfxDriverInfo() {
  if (!sDriverInfo->Length()) {
    // Mesa 10.0 provides the GLX_MESA_query_renderer extension, which allows us
    // to query device IDs backing a GL context for blocklisting.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaAll, DeviceFamily::All,
        GfxDriverInfo::optionalFeatures,
        nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION, DRIVER_LESS_THAN,
        V(10, 0, 0, 0), "FEATURE_FAILURE_OLD_MESA", "Mesa 10.0");

    // NVIDIA Mesa baseline (see bug 1714391).
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaNouveau, DeviceFamily::All,
        GfxDriverInfo::optionalFeatures,
        nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION, DRIVER_LESS_THAN,
        V(11, 0, 0, 0), "FEATURE_FAILURE_OLD_NV_MESA", "Mesa 11.0");

    // NVIDIA baseline (ported from old blocklist)
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::NvidiaAll,
        GfxDriverInfo::optionalFeatures,
        nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION, DRIVER_LESS_THAN,
        V(257, 21, 0, 0), "FEATURE_FAILURE_OLD_NVIDIA", "NVIDIA 257.21");

    // fglrx baseline (chosen arbitrarily as 2013-07-22 release).
    APPEND_TO_DRIVER_BLOCKLIST(
        OperatingSystem::Linux, DeviceFamily::AtiAll,
        GfxDriverInfo::optionalFeatures,
        nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION, DRIVER_LESS_THAN,
        V(13, 15, 100, 1), "FEATURE_FAILURE_OLD_FGLRX", "fglrx 13.15.100.1");

    ////////////////////////////////////
    // FEATURE_WEBRENDER

    // All Mesa software drivers, they should get Software WebRender instead.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::SoftwareMesaAll, DeviceFamily::All,
        nsIGfxInfo::FEATURE_WEBRENDER, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0), "FEATURE_FAILURE_SOFTWARE_GL",
        "");

    // Older generation Intel devices do not perform well with WebRender.
    APPEND_TO_DRIVER_BLOCKLIST(
        OperatingSystem::Linux, DeviceFamily::IntelWebRenderBlocked,
        nsIGfxInfo::FEATURE_WEBRENDER, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0), "INTEL_DEVICE_GEN5_OR_OLDER",
        "");

    // Nvidia Mesa baseline, see bug 1563859.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaAll, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_WEBRENDER,
        nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION, DRIVER_LESS_THAN,
        V(18, 2, 0, 0), "FEATURE_FAILURE_WEBRENDER_OLD_MESA", "Mesa 18.2.0.0");

    // Disable on all older Nvidia drivers due to stability issues.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_WEBRENDER, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_LESS_THAN, V(470, 82, 0, 0),
        "FEATURE_FAILURE_WEBRENDER_OLD_NVIDIA", "470.82.0");

    // Older generation NVIDIA devices do not perform well with WebRender.
    APPEND_TO_DRIVER_BLOCKLIST(
        OperatingSystem::Linux, DeviceFamily::NvidiaWebRenderBlocked,
        nsIGfxInfo::FEATURE_WEBRENDER, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0),
        "NVIDIA_EARLY_TESLA_AND_C67_C68", "");

    // Mesa baseline, chosen arbitrarily. Linux users are generally good about
    // updating their Mesa libraries so we don't want to arbitarily support
    // WebRender on old drivers with outstanding bugs to work around.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaAll, DeviceFamily::All,
        nsIGfxInfo::FEATURE_WEBRENDER,
        nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION, DRIVER_LESS_THAN,
        V(17, 0, 0, 0), "FEATURE_FAILURE_WEBRENDER_OLD_MESA", "Mesa 17.0.0.0");

    // Mesa baseline for non-Intel/NVIDIA/ATI devices. These other devices will
    // often have less mature drivers so let's block older Mesa versions.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaNonIntelNvidiaAtiAll,
        DeviceFamily::All, nsIGfxInfo::FEATURE_WEBRENDER,
        nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION, DRIVER_LESS_THAN,
        V(22, 2, 0, 0), "FEATURE_FAILURE_WEBRENDER_OLD_MESA_OTHER",
        "Mesa 22.2.0.0");

    // Bug 1690568 / Bug 1393793 - Require Mesa 17.3.0+ for devices using the
    // AMD r600 driver to avoid shader compilation issues.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaR600, DeviceFamily::All,
        nsIGfxInfo::FEATURE_WEBRENDER,
        nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION, DRIVER_LESS_THAN,
        V(17, 3, 0, 0), "FEATURE_FAILURE_WEBRENDER_OLD_MESA_R600",
        "Mesa 17.3.0.0");

    // Disable on all ATI devices not using Mesa for now.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::AtiAll,
        nsIGfxInfo::FEATURE_WEBRENDER, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0),
        "FEATURE_FAILURE_WEBRENDER_NO_LINUX_ATI", "");

    // Disable R600 GPUs with Mesa drivers.
    // Bug 1673939 - Garbled text on RS880 GPUs with Mesa drivers.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaAll, DeviceFamily::AmdR600,
        nsIGfxInfo::FEATURE_WEBRENDER, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0),
        "FEATURE_FAILURE_WEBRENDER_BUG_1673939",
        "https://gitlab.freedesktop.org/mesa/mesa/-/issues/3720");

    // Bug 1635186 - Poor performance with video playing in a background window
    // on XWayland. Keep in sync with FEATURE_X11_EGL below to only enable them
    // together by default. Only Mesa and Nvidia binary drivers are expected
    // on Wayland right now.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::XWayland, DriverVendor::MesaAll, DeviceFamily::All,
        nsIGfxInfo::FEATURE_WEBRENDER,
        nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION, DRIVER_LESS_THAN,
        V(17, 0, 0, 0), "FEATURE_FAILURE_WEBRENDER_BUG_1635186",
        "Mesa 17.0.0.0");

    // Bug 1815481 - Disable mesa drivers in virtual machines.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaVM, DeviceFamily::All,
        nsIGfxInfo::FEATURE_WEBRENDER, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0),
        "FEATURE_FAILURE_WEBRENDER_MESA_VM", "");

    ////////////////////////////////////
    // FEATURE_MESA_THREADING

    // Bug 1852794 - Disable old nouveau drivers (<= 23.2.1.0).
    // Bug 2055560 - Disable on all nouveau drivers (== 25.2.8.0).
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaNouveau, DeviceFamily::All,
        nsIGfxInfo::FEATURE_MESA_THREADING, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0),
        "FEATURE_FAILURE_MESA_THREADING_BUG_2055560", "");

    ////////////////////////////////////
    // FEATURE_WEBGL_USE_HARDWARE

    // Disable hardware mesa drivers in virtual machines due to instability.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaVM, DeviceFamily::All,
        nsIGfxInfo::FEATURE_WEBGL_USE_HARDWARE,
        nsIGfxInfo::FEATURE_BLOCKED_DEVICE, DRIVER_COMPARISON_IGNORED,
        V(0, 0, 0, 0), "FEATURE_FAILURE_WEBGL_MESA_VM", "");

    // Disable nvidia 390.157 due to startup crashes (Bug 2028081)
    APPEND_TO_DRIVER_BLOCKLIST_RANGE_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_WEBGL_USE_HARDWARE,
        nsIGfxInfo::FEATURE_BLOCKED_DEVICE, DRIVER_BETWEEN_INCLUSIVE_START,
        V(390, 157, 0, 0), V(391, 0, 0, 0), "FEATURE_FAILURE_WEBGL_OLD_NVIDIA",
        "391.0.0");

    ////////////////////////////////////
    // FEATURE_X11_EGL
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaAll, DeviceFamily::All,
        nsIGfxInfo::FEATURE_X11_EGL, nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION,
        DRIVER_LESS_THAN, V(17, 0, 0, 0), "FEATURE_X11_EGL_OLD_MESA",
        "Mesa 17.0.0.0");

    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaAll, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_X11_EGL, nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION,
        DRIVER_LESS_THAN, V(18, 2, 0, 0), "FEATURE_X11_EGL_OLD_MESA_NOUVEAU",
        "Mesa 18.2.0.0");

    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_X11_EGL, nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION,
        DRIVER_LESS_THAN, V(470, 82, 0, 0),
        "FEATURE_ROLLOUT_X11_EGL_NVIDIA_BINARY", "470.82.0");

    // Disable on all AMD devices not using Mesa.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::AtiAll,
        nsIGfxInfo::FEATURE_X11_EGL, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0),
        "FEATURE_FAILURE_X11_EGL_NO_LINUX_ATI", "");

    ////////////////////////////////////
    // FEATURE_DMABUF
    // Disabled due to high volume crash tracked in bug 1788573, fixed in the
    // 545 driver.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_DMABUF, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_LESS_THAN, V(545, 23, 6, 0), "FEATURE_FAILURE_BUG_1788573", "");

    // Disabled due to high volume crash tracked in bug 1978911, fixed in the
    // 575 driver.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_DMABUF, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_LESS_THAN, V(575, 64, 5, 0), "FEATURE_FAILURE_BUG_1978911", "");

    // Disabled due to high volume crash tracked in bug 1913778. It appears that
    // only this version of the driver is affected.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaRadeonsi, DeviceFamily::AtiAll,
        nsIGfxInfo::FEATURE_DMABUF, nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION,
        DRIVER_EQUAL, V(24, 1, 3, 0), "FEATURE_FAILURE_BUG_1913778", "");

    ////////////////////////////////////
    // FEATURE_DMABUF_SURFACE_EXPORT
    // Disabled on all Mesa drivers due to various issue, among them:
    // https://gitlab.freedesktop.org/mesa/mesa/-/issues/6666
    // https://gitlab.freedesktop.org/mesa/mesa/-/issues/6796
    // https://gitlab.freedesktop.org/mesa/mesa/-/issues/6688
    // https://gitlab.freedesktop.org/mesa/mesa/-/issues/6988
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaAll, DeviceFamily::All,
        nsIGfxInfo::FEATURE_DMABUF_SURFACE_EXPORT,
        nsIGfxInfo::FEATURE_BLOCKED_DEVICE, DRIVER_COMPARISON_IGNORED,
        V(0, 0, 0, 0), "FEATURE_FAILURE_BROKEN_DRIVER", "");

    ////////////////////////////////////
    // FEATURE_DMABUF_WEBGL
    // Disabled due to DMABuf rendering/correctness with WebGL on Nvidia driver,
    // tracked in bug 1924578.
#ifdef NIGHTLY_BUILD
    // Block all but the most recent drivers for NVIDIA legacy devices.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_DMABUF_WEBGL, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_LESS_THAN, V(470, 256, 2, 0), "FEATURE_FAILURE_BUG_1981326",
        "NVIDIA 470.256.02 / 580.76.05");

    // Block all but the most recent drivers for NVIDIA supported devices.
    APPEND_TO_DRIVER_BLOCKLIST_RANGE_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_DMABUF_WEBGL,
        nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION,
        DRIVER_BETWEEN_INCLUSIVE_START, V(471, 0, 0, 0), V(580, 76, 5, 0),
        "FEATURE_FAILURE_BUG_1981326", "NVIDIA 470.256.02 / 580.76.05");
#else
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_DMABUF_WEBGL, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0), "FEATURE_FAILURE_BUG_1924578",
        "");
#endif

    ////////////////////////////////////
    // FEATURE_HARDWARE_VIDEO_DECODING
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaAll, DeviceFamily::All,
        nsIGfxInfo::FEATURE_HARDWARE_VIDEO_DECODING,
        nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION, DRIVER_LESS_THAN,
        V(21, 0, 0, 0), "FEATURE_HARDWARE_VIDEO_DECODING_MESA",
        "Mesa 21.0.0.0");

    // Disable on all NVIDIA hardware
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::All, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_HARDWARE_VIDEO_DECODING,
        nsIGfxInfo::FEATURE_BLOCKED_DEVICE, DRIVER_COMPARISON_IGNORED,
        V(0, 0, 0, 0), "FEATURE_HARDWARE_VIDEO_DECODING_NO_LINUX_NVIDIA", "");

    // Disable on all AMD devices not using Mesa.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::AtiAll,
        nsIGfxInfo::FEATURE_HARDWARE_VIDEO_DECODING,
        nsIGfxInfo::FEATURE_BLOCKED_DEVICE, DRIVER_COMPARISON_IGNORED,
        V(0, 0, 0, 0), "FEATURE_HARDWARE_VIDEO_DECODING_NO_LINUX_AMD", "");

    // Disable on r600 driver due to decoding artifacts (Bug 1824307)
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaR600, DeviceFamily::All,
        nsIGfxInfo::FEATURE_HARDWARE_VIDEO_DECODING,
        nsIGfxInfo::FEATURE_BLOCKED_DEVICE, DRIVER_COMPARISON_IGNORED,
        V(0, 0, 0, 0), "FEATURE_HARDWARE_VIDEO_DECODING_NO_R600", "");

    // Disable on AMD devices using broken Mesa (Bug 1837140).
    // https://gitlab.freedesktop.org/mesa/mesa/-/commit/0c024bbe641b092bbb
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaAll, DeviceFamily::AtiAll,
        nsIGfxInfo::FEATURE_HARDWARE_VIDEO_DECODING,
        nsIGfxInfo::FEATURE_BLOCKED_DEVICE, DRIVER_LESS_THAN, V(24, 2, 0, 0),
        "FEATURE_HARDWARE_VIDEO_DECODING_AMD_DISABLE", "Mesa 24.2.0");

    ////////////////////////////////////
    // FEATURE_HW_DECODED_VIDEO_ZERO_COPY - ALLOWLIST
    APPEND_TO_DRIVER_BLOCKLIST2(OperatingSystem::Linux, DeviceFamily::All,
                                nsIGfxInfo::FEATURE_HW_DECODED_VIDEO_ZERO_COPY,
                                nsIGfxInfo::FEATURE_ALLOW_ALWAYS,
                                DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0),
                                "FEATURE_ROLLOUT_ALL");

    // Disable on AMD devices using broken Mesa (Bug 1837138).
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaAll, DeviceFamily::AtiAll,
        nsIGfxInfo::FEATURE_HW_DECODED_VIDEO_ZERO_COPY,
        nsIGfxInfo::FEATURE_BLOCKED_DEVICE, DRIVER_LESS_THAN, V(24, 2, 0, 0),
        "FEATURE_HARDWARE_VIDEO_ZERO_COPY_LINUX_AMD_DISABLE", "Mesa 24.2.0.0");

    /////////////////////////////////////////
    // FEATURE_HARDWARE_VIDEO_DECODING_VULKAN
#ifdef NIGHTLY_BUILD
    // Enable on NVIDIA and recent drivers.
    APPEND_TO_DRIVER_BLOCKLIST(
        OperatingSystem::Linux, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_HARDWARE_VIDEO_DECODING_VULKAN,
        nsIGfxInfo::FEATURE_STATUS_OK, DRIVER_GREATER_THAN_OR_EQUAL,
        V(580, 76, 5, 0), "FEATURE_VIDEO_DECODING_VULKAN_NIGHTLY_NVIDIA", "");
#endif
    APPEND_TO_DRIVER_BLOCKLIST(
        OperatingSystem::Linux, DeviceFamily::All,
        nsIGfxInfo::FEATURE_HARDWARE_VIDEO_DECODING_VULKAN,
        nsIGfxInfo::FEATURE_BLOCKED_DEVICE, DRIVER_COMPARISON_IGNORED,
        V(0, 0, 0, 0), "FEATURE_VIDEO_DECODING_VULKAN_DISABLED", "");

    ////////////////////////////////////
    // FEATURE_VIDEO_HDR & FEATURE_WEBRENDER_COMPOSITOR

    // Disable when Wayland compositor support is missing or it's disabled by
    // pref, mirror gfxPlatform::UseHDR() logic.
    // We do that because:
    // 1) we need to enable HDR early to use WEBRENDER_COMPOSITOR
    // 2) advertise HDR/WEBRENDER_COMPOSITOR at about:support
    bool hdrEnabled = false;
#ifdef MOZ_WAYLAND
    hdrEnabled = GdkIsWaylandDisplay() &&
                 ((WaylandDisplayGet()->IsHDREnabled() &&
                   WaylandDisplayGet()->GetFractionalScaleManager() &&
                   StaticPrefs::gfx_color_management_hdr()) ||
                  StaticPrefs::gfx_color_management_hdr_force_enabled());
#endif
    if (!hdrEnabled) {
      APPEND_TO_DRIVER_BLOCKLIST(OperatingSystem::Linux, DeviceFamily::All,
                                 nsIGfxInfo::FEATURE_VIDEO_HDR,
                                 nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
                                 DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0),
                                 "FEATURE_VIDEO_HDR_DISABLED", "");
      APPEND_TO_DRIVER_BLOCKLIST(
          OperatingSystem::Linux, DeviceFamily::All,
          nsIGfxInfo::FEATURE_WEBRENDER_COMPOSITOR,
          nsIGfxInfo::FEATURE_BLOCKED_DEVICE, DRIVER_COMPARISON_IGNORED,
          V(0, 0, 0, 0), "FEATURE_FAILURE_WEBRENDER_COMPOSITOR_DISABLED", "");
    }

    ////////////////////////////////////
    // FEATURE_WEBRENDER_PARTIAL_PRESENT
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::X11, DriverVendor::NonMesaAll, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_WEBRENDER_PARTIAL_PRESENT,
        nsIGfxInfo::FEATURE_BLOCKED_DEVICE, DRIVER_COMPARISON_IGNORED,
        V(0, 0, 0, 0), "FEATURE_ROLLOUT_WR_PARTIAL_PRESENT_NVIDIA_BINARY", "");

    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::XWayland, DriverVendor::MesaAll, DeviceFamily::All,
        nsIGfxInfo::FEATURE_WEBRENDER_PARTIAL_PRESENT,
        nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION, DRIVER_LESS_THAN,
        V(21, 0, 0, 0), "FEATURE_FAILURE_WEBRENDER_PARTIAL_PRESENT_BUG_1677892",
        "Mesa 21.0.0.0");

    ////////////////////////////////////
    // FEATURE_WEBGPU
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaAll, DeviceFamily::All,
        nsIGfxInfo::FEATURE_WEBGPU, nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION,
        DRIVER_LESS_THAN, V(25, 0, 4, 0),
        "FEATURE_FAILURE_WEBGPU_MESA_BUG_1979007", "Mesa 25.0.4");

    ////////////////////////////////////

    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::MesaNouveau, DeviceFamily::All,
        nsIGfxInfo::FEATURE_THREADSAFE_GL, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0),
        "FEATURE_FAILURE_THREADSAFE_GL_NOUVEAU", "");

    // Disabled due to high volume crash tracked in bug 1788573, fixed in the
    // 545 driver.
    APPEND_TO_DRIVER_BLOCKLIST_EXT(
        OperatingSystem::Linux, ScreenSizeStatus::All, BatteryStatus::All,
        WindowProtocol::All, DriverVendor::NonMesaAll, DeviceFamily::NvidiaAll,
        nsIGfxInfo::FEATURE_THREADSAFE_GL, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_LESS_THAN, V(545, 23, 6, 0), "FEATURE_FAILURE_BUG_1788573", "");

    // AMD R600 family does not perform well with WebRender.
    APPEND_TO_DRIVER_BLOCKLIST(
        OperatingSystem::Linux, DeviceFamily::AmdR600,
        nsIGfxInfo::FEATURE_WEBRENDER, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0), "AMD_R600_FAMILY", "");
  }
  return *sDriverInfo;
}

bool GfxInfo::DoesWindowProtocolMatch(const nsAString& aBlocklistWindowProtocol,
                                      const nsAString& aWindowProtocol) {
  if (mIsWayland &&
      aBlocklistWindowProtocol.Equals(
          GfxDriverInfo::GetWindowProtocol(WindowProtocol::WaylandAll),
          nsCaseInsensitiveStringComparator)) {
    return true;
  }
  if (!mIsWayland &&
      aBlocklistWindowProtocol.Equals(
          GfxDriverInfo::GetWindowProtocol(WindowProtocol::X11All),
          nsCaseInsensitiveStringComparator)) {
    return true;
  }
  return GfxInfoBase::DoesWindowProtocolMatch(aBlocklistWindowProtocol,
                                              aWindowProtocol);
}

bool GfxInfo::DoesDriverVendorMatch(const nsAString& aBlocklistVendor,
                                    const nsAString& aDriverVendor) {
  if (mIsMesa) {
    if (aBlocklistVendor.Equals(
            GfxDriverInfo::GetDriverVendor(DriverVendor::MesaAll),
            nsCaseInsensitiveStringComparator)) {
      return true;
    }
    if (mIsAccelerated &&
        aBlocklistVendor.Equals(
            GfxDriverInfo::GetDriverVendor(DriverVendor::HardwareMesaAll),
            nsCaseInsensitiveStringComparator)) {
      return true;
    }
    if (!mIsAccelerated &&
        aBlocklistVendor.Equals(
            GfxDriverInfo::GetDriverVendor(DriverVendor::SoftwareMesaAll),
            nsCaseInsensitiveStringComparator)) {
      return true;
    }
    if (aBlocklistVendor.Equals(GfxDriverInfo::GetDriverVendor(
                                    DriverVendor::MesaNonIntelNvidiaAtiAll),
                                nsCaseInsensitiveStringComparator)) {
      return !mVendorId.Equals("0x8086") && !mVendorId.Equals("0x10de") &&
             !mVendorId.Equals("0x1002");
    }
  }
  if (!mIsMesa && aBlocklistVendor.Equals(
                      GfxDriverInfo::GetDriverVendor(DriverVendor::NonMesaAll),
                      nsCaseInsensitiveStringComparator)) {
    return true;
  }
  return GfxInfoBase::DoesDriverVendorMatch(aBlocklistVendor, aDriverVendor);
}

nsresult GfxInfo::GetFeatureStatusImpl(
    int32_t aFeature, int32_t* aStatus, nsAString& aSuggestedDriverVersion,
    const nsTArray<RefPtr<GfxDriverInfo>>& aDriverInfo, nsACString& aFailureId,
    OperatingSystem* aOS /* = nullptr */)

{
  NS_ENSURE_ARG_POINTER(aStatus);
  *aStatus = nsIGfxInfo::FEATURE_STATUS_UNKNOWN;
  aSuggestedDriverVersion.SetIsVoid(true);
  OperatingSystem os = OperatingSystem::Linux;
  if (aOS) *aOS = os;

  if (sShutdownOccurred) {
    return NS_OK;
  }

  GetData();

  if (mGlxTestError) {
    // If glxtest failed, block most features by default.
    if (OnlyAllowFeatureOnKnownConfig(aFeature)) {
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
      aFailureId = "FEATURE_FAILURE_GLXTEST_FAILED";
    } else {
      *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
    }
    return NS_OK;
  }

  if (mGLMajorVersion == 1) {
    // We're on OpenGL 1. In most cases that indicates really old hardware.
    // We better block them, rather than rely on them to fail gracefully,
    // because they don't! see bug 696636
    if (OnlyAllowFeatureOnKnownConfig(aFeature)) {
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
      aFailureId = "FEATURE_FAILURE_OPENGL_1";
    } else {
      *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
    }
    return NS_OK;
  }

  // Blocklist software GL implementations from using layers acceleration.
  // On the test infrastructure, we'll force-enable layers acceleration.
  if (aFeature == nsIGfxInfo::FEATURE_OPENGL_LAYERS && !mIsAccelerated &&
      !PR_GetEnv("MOZ_LAYERS_ALLOW_SOFTWARE_GL")) {
    *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
    aFailureId = "FEATURE_FAILURE_SOFTWARE_GL";
    return NS_OK;
  }

  if (aFeature == nsIGfxInfo::FEATURE_WEBRENDER) {
    // Don't try Webrender on devices where we are guaranteed to fail.
    if (mGLMajorVersion < 3) {
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
      aFailureId = "FEATURE_FAILURE_OPENGL_LESS_THAN_3";
      return NS_OK;
    }

    // Bug 1710400: Disable Webrender on the deprecated Intel DDX driver
    for (const nsCString& driver : mDdxDrivers) {
      if (strcasestr(driver.get(), "Intel")) {
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_DDX_INTEL";
        return NS_OK;
      }
    }
  }

  const struct {
    int32_t mFeature;
    int32_t mCodec;
    nsLiteralCString mFailureId;
  } kFeatureToCodecs[] = {
      {nsIGfxInfo::FEATURE_H264_HW_DECODE, CODEC_HW_DEC_H264,
       "FEATURE_FAILURE_VIDEO_DECODING_MISSING"_ns},
      {nsIGfxInfo::FEATURE_H264_HW_ENCODE, CODEC_HW_ENC_H264,
       "FEATURE_FAILURE_VIDEO_ENCODING_MISSING"_ns},
      {nsIGfxInfo::FEATURE_VP8_HW_DECODE, CODEC_HW_DEC_VP8,
       "FEATURE_FAILURE_VIDEO_DECODING_MISSING"_ns},
      {nsIGfxInfo::FEATURE_VP8_HW_ENCODE, CODEC_HW_ENC_VP8,
       "FEATURE_FAILURE_VIDEO_ENCODING_MISSING"_ns},
      {nsIGfxInfo::FEATURE_VP9_HW_DECODE, CODEC_HW_DEC_VP9,
       "FEATURE_FAILURE_VIDEO_DECODING_MISSING"_ns},
      {nsIGfxInfo::FEATURE_VP9_HW_ENCODE, CODEC_HW_ENC_VP9,
       "FEATURE_FAILURE_VIDEO_ENCODING_MISSING"_ns},
      {nsIGfxInfo::FEATURE_AV1_HW_DECODE, CODEC_HW_DEC_AV1,
       "FEATURE_FAILURE_VIDEO_DECODING_MISSING"_ns},
      {nsIGfxInfo::FEATURE_AV1_HW_ENCODE, CODEC_HW_ENC_AV1,
       "FEATURE_FAILURE_VIDEO_ENCODING_MISSING"_ns},
      {nsIGfxInfo::FEATURE_HEVC_HW_DECODE, CODEC_HW_DEC_HEVC,
       "FEATURE_FAILURE_VIDEO_DECODING_MISSING"_ns},
      {nsIGfxInfo::FEATURE_HEVC_HW_ENCODE, CODEC_HW_ENC_HEVC,
       "FEATURE_FAILURE_VIDEO_ENCODING_MISSING"_ns}};

  for (const auto& pair : kFeatureToCodecs) {
    if (aFeature != pair.mFeature) {
      continue;
    }
    if ((mVAAPISupportedCodecs & pair.mCodec) ||
        (mV4L2SupportedCodecs & pair.mCodec) ||
        (mVulkanSupportedCodecs & pair.mCodec)) {
      *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
    } else {
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_PLATFORM_TEST;
      aFailureId = pair.mFailureId;
    }
    return NS_OK;
  }

  auto ret = GfxInfoBase::GetFeatureStatusImpl(
      aFeature, aStatus, aSuggestedDriverVersion, aDriverInfo, aFailureId, &os);

  // Probe Vulkan on supported devices only
  if (aFeature == nsIGfxInfo::FEATURE_HARDWARE_VIDEO_DECODING_VULKAN) {
    if (!StaticPrefs::
            media_hardware_video_decoding_vulkan_enabled_AtStartup()) {
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_PLATFORM_TEST;
      aFailureId = "FEATURE_HARDWARE_VIDEO_DECODING_VULKAN_PREF_DISABLED"_ns;
      return NS_OK;
    }
    bool probeHWDecode =
        mIsAccelerated &&
        (*aStatus == nsIGfxInfo::FEATURE_STATUS_OK ||
         StaticPrefs::
             media_hardware_video_decoding_vulkan_force_enabled_AtStartup());
    if (probeHWDecode) {
      GetDataVulkan();
    } else {
      mIsVulkanSupported = Some(false);
    }
    if (!mIsVulkanSupported.value()) {
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_PLATFORM_TEST;
      aFailureId = "FEATURE_FAILURE_VIDEO_DECODING_VULKAN_TEST_FAILED";
    }
  }

  // Probe VA-API/V4L2 on supported devices only
  if (aFeature == nsIGfxInfo::FEATURE_HARDWARE_VIDEO_DECODING) {
    if (!StaticPrefs::media_hardware_video_decoding_enabled_AtStartup()) {
      return ret;
    }
    bool probeHWDecode =
        mIsAccelerated &&
        (*aStatus == nsIGfxInfo::FEATURE_STATUS_OK ||
         StaticPrefs::media_hardware_video_decoding_force_enabled_AtStartup());
    if (probeHWDecode) {
      GetDataVAAPI();
      GetDataV4L2();
    } else {
      mIsVAAPISupported = Some(false);
      mIsV4L2Supported = Some(false);
    }
    if (!mIsVAAPISupported.value() && !mIsV4L2Supported.value()) {
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_PLATFORM_TEST;
      aFailureId = "FEATURE_FAILURE_VIDEO_DECODING_TEST_FAILED";
    }
  }

  return ret;
}

NS_IMETHODIMP
GfxInfo::GetDWriteEnabled(bool* aEnabled) { return NS_ERROR_FAILURE; }

NS_IMETHODIMP
GfxInfo::GetDWriteVersion(nsAString& aDwriteVersion) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP GfxInfo::GetHasBattery(bool* aHasBattery) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
GfxInfo::GetCleartypeParameters(nsAString& aCleartypeParams) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetWindowProtocol(nsAString& aWindowProtocol) {
  GetData();
  if (mIsWayland) {
    aWindowProtocol = GfxDriverInfo::GetWindowProtocol(WindowProtocol::Wayland);
  } else if (mIsXWayland) {
    aWindowProtocol =
        GfxDriverInfo::GetWindowProtocol(WindowProtocol::XWayland);
  } else {
    aWindowProtocol = GfxDriverInfo::GetWindowProtocol(WindowProtocol::X11);
  }
  glean::gfx::linux_window_protocol.Set(NS_ConvertUTF16toUTF8(aWindowProtocol));
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetTestType(nsAString& aTestType) {
  GetData();
  AppendASCIItoUTF16(mTestType, aTestType);
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDescription(nsAString& aAdapterDescription) {
  GetData();
  AppendASCIItoUTF16(mAdapterDescription, aAdapterDescription);
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDescription2(nsAString& aAdapterDescription) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterRAM(uint32_t* aAdapterRAM) {
  GetData();
  *aAdapterRAM = mAdapterRAM;
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterRAM2(uint32_t* aAdapterRAM) { return NS_ERROR_FAILURE; }

NS_IMETHODIMP
GfxInfo::GetAdapterDriver(nsAString& aAdapterDriver) {
  aAdapterDriver.Truncate();
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriver2(nsAString& aAdapterDriver) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriverVendor(nsAString& aAdapterDriverVendor) {
  GetData();
  CopyASCIItoUTF16(mDriverVendor, aAdapterDriverVendor);
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriverVendor2(nsAString& aAdapterDriverVendor) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriverVersion(nsAString& aAdapterDriverVersion) {
  GetData();
  CopyASCIItoUTF16(mDriverVersion, aAdapterDriverVersion);
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriverVersion2(nsAString& aAdapterDriverVersion) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriverDate(nsAString& aAdapterDriverDate) {
  aAdapterDriverDate.Truncate();
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriverDate2(nsAString& aAdapterDriverDate) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterVendorID(nsAString& aAdapterVendorID) {
  GetData();
  CopyUTF8toUTF16(mVendorId, aAdapterVendorID);
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterVendorID2(nsAString& aAdapterVendorID) {
  GetData();
  CopyUTF8toUTF16(mSecondaryVendorId, aAdapterVendorID);
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDeviceID(nsAString& aAdapterDeviceID) {
  GetData();
  CopyUTF8toUTF16(mDeviceId, aAdapterDeviceID);
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDeviceID2(nsAString& aAdapterDeviceID) {
  GetData();
  CopyUTF8toUTF16(mSecondaryDeviceId, aAdapterDeviceID);
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterSubsysID(nsAString& aAdapterSubsysID) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterSubsysID2(nsAString& aAdapterSubsysID) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetIsGPU2Active(bool* aIsGPU2Active) {
  // This is never the case, as the active GPU should be the primary GPU.
  *aIsGPU2Active = false;
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetDrmRenderDevice(nsACString& aDrmRenderDevice) {
  GetData();
  aDrmRenderDevice.Assign(mDrmRenderDevice);
  return NS_OK;
}

#ifdef DEBUG

// Implement nsIGfxInfoDebug
// We don't support spoofing anything on Linux

NS_IMETHODIMP GfxInfo::SpoofVendorID(const nsAString& aVendorID) {
  GetData();
  CopyUTF16toUTF8(aVendorID, mVendorId);
  mIsAccelerated = true;
  return NS_OK;
}

NS_IMETHODIMP GfxInfo::SpoofDeviceID(const nsAString& aDeviceID) {
  GetData();
  CopyUTF16toUTF8(aDeviceID, mDeviceId);
  return NS_OK;
}

NS_IMETHODIMP GfxInfo::SpoofDriverVersion(const nsAString& aDriverVersion) {
  GetData();
  CopyUTF16toUTF8(aDriverVersion, mDriverVersion);
  return NS_OK;
}

NS_IMETHODIMP GfxInfo::SpoofOSVersion(uint32_t aVersion) {
  // We don't support OS versioning on Linux. There's just "Linux".
  return NS_OK;
}

NS_IMETHODIMP GfxInfo::SpoofOSVersionEx(uint32_t aMajor, uint32_t aMinor,
                                        uint32_t aBuild, uint32_t aRevision) {
  // We don't support OS versioning on Linux. There's just "Linux", so this is
  // just for testing purposes.
#  ifdef DEBUG
  mOSVersionEx = GfxVersionEx(aMajor, aMinor, aBuild, aRevision);
#  endif
  return NS_OK;
}

#endif

}  // namespace mozilla::widget
