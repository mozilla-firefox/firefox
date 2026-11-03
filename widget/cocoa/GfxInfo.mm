/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <OpenGL/CGLRenderers.h>
#include <OpenGL/OpenGL.h>

#include "GfxInfo.h"
#include "js/PropertyAndElement.h"  // JS_SetElement, JS_SetProperty
#include "mozilla/Preferences.h"
#include "nsCocoaFeatures.h"
#include "nsCocoaUtils.h"
#include "nsExceptionHandler.h"
#include "nsUnicharUtils.h"

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>

#include "jsapi.h"

using namespace mozilla;
using namespace mozilla::widget;

#ifdef DEBUG
NS_IMPL_ISUPPORTS_INHERITED(GfxInfo, GfxInfoBase, nsIGfxInfoDebug)
#endif

GfxInfo::GfxInfo() : mNumGPUsDetected(0), mMacOSVersion{0} {
  mAdapterRAM[0] = mAdapterRAM[1] = 0;
}

static OperatingSystem MacOSVersionToOperatingSystem(uint32_t aMacOSVersion) {
  switch (nsCocoaFeatures::ExtractMajorVersion(aMacOSVersion)) {
    case 10:
      switch (nsCocoaFeatures::ExtractMinorVersion(aMacOSVersion)) {
        case 6:
          return OperatingSystem::MacOS10_6;
        case 7:
          return OperatingSystem::MacOS10_7;
        case 8:
          return OperatingSystem::MacOS10_8;
        case 9:
          return OperatingSystem::MacOS10_9;
        case 10:
          return OperatingSystem::MacOS10_10;
        case 11:
          return OperatingSystem::MacOS10_11;
        case 12:
          return OperatingSystem::MacOS10_12;
        case 13:
          return OperatingSystem::MacOS10_13;
        case 14:
          return OperatingSystem::MacOS10_14;
        case 15:
          return OperatingSystem::MacOS10_15;
        case 16:
          // Depending on the SDK version, we either get 10.16 or 11.0.
          // Normalize this to 11.0.
          return OperatingSystem::MacOS11_0;
        default:
          break;
      }
      break;
    case 11:
      switch (nsCocoaFeatures::ExtractMinorVersion(aMacOSVersion)) {
        case 0:
          return OperatingSystem::MacOS11_0;
        default:
          break;
      }
      break;
  }

  return OperatingSystem::Unknown;
}
// The following three functions are derived from Chromium code
static CFTypeRef SearchPortForProperty(io_registry_entry_t dspPort,
                                       CFStringRef propertyName) {
  return IORegistryEntrySearchCFProperty(
      dspPort, kIOServicePlane, propertyName, kCFAllocatorDefault,
      kIORegistryIterateRecursively | kIORegistryIterateParents);
}

static uint32_t IntValueOfCFData(CFDataRef d) {
  uint32_t value = 0;

  if (d) {
    const uint32_t* vp = reinterpret_cast<const uint32_t*>(CFDataGetBytePtr(d));
    if (vp != nullptr) value = *vp;
  }

  return value;
}

void GfxInfo::GetDeviceInfo() {
  mNumGPUsDetected = 0;

  CFMutableDictionaryRef pci_dev_dict = IOServiceMatching("IOPCIDevice");
  io_iterator_t io_iter;
  if (IOServiceGetMatchingServices(kIOMasterPortDefault, pci_dev_dict,
                                   &io_iter) != kIOReturnSuccess) {
    MOZ_DIAGNOSTIC_ASSERT(
        false,
        "Failed to detect any GPUs (couldn't enumerate IOPCIDevice services)");
    return;
  }

  io_registry_entry_t entry = IO_OBJECT_NULL;
  while ((entry = IOIteratorNext(io_iter)) != IO_OBJECT_NULL) {
    constexpr uint32_t kClassCodeDisplayVGA = 0x30000;
    CFTypeRef class_code_ref =
        SearchPortForProperty(entry, CFSTR("class-code"));
    if (class_code_ref) {
      const uint32_t class_code = IntValueOfCFData((CFDataRef)class_code_ref);
      CFRelease(class_code_ref);

      if (class_code == kClassCodeDisplayVGA) {
        CFTypeRef vendor_id_ref =
            SearchPortForProperty(entry, CFSTR("vendor-id"));
        if (vendor_id_ref) {
          mAdapterVendorID[mNumGPUsDetected].AppendPrintf(
              "0x%04x", IntValueOfCFData((CFDataRef)vendor_id_ref));
          CFRelease(vendor_id_ref);
        }
        CFTypeRef device_id_ref =
            SearchPortForProperty(entry, CFSTR("device-id"));
        if (device_id_ref) {
          mAdapterDeviceID[mNumGPUsDetected].AppendPrintf(
              "0x%04x", IntValueOfCFData((CFDataRef)device_id_ref));
          CFRelease(device_id_ref);
        }
        ++mNumGPUsDetected;
      }
    }
    IOObjectRelease(entry);
    if (mNumGPUsDetected == kMaxGPUs) {
      break;
    }
  }
  IOObjectRelease(io_iter);

  // If we found IOPCI VGA devices, don't look for other devices
  if (mNumGPUsDetected > 0) {
    return;
  }

#if defined(__aarch64__)
  CFMutableDictionaryRef agx_dev_dict = IOServiceMatching("AGXAccelerator");
  if (IOServiceGetMatchingServices(kIOMasterPortDefault, agx_dev_dict,
                                   &io_iter) == kIOReturnSuccess) {
    io_registry_entry_t entry = IO_OBJECT_NULL;
    while ((entry = IOIteratorNext(io_iter)) != IO_OBJECT_NULL) {
      CFTypeRef vendor_id_ref =
          SearchPortForProperty(entry, CFSTR("vendor-id"));
      if (vendor_id_ref) {
        mAdapterVendorID[mNumGPUsDetected].AppendPrintf(
            "0x%04x", IntValueOfCFData((CFDataRef)vendor_id_ref));
        CFRelease(vendor_id_ref);
        ++mNumGPUsDetected;
      }
      IOObjectRelease(entry);
      if (mNumGPUsDetected == kMaxGPUs) {
        break;
      }
    }

    IOObjectRelease(io_iter);
  }

  // If we found an AGXAccelerator, don't look for an AppleParavirtGPU
  if (mNumGPUsDetected > 0) {
    return;
  }
#endif

  // "AppleParavirtGPU" is the class name in VMs that use the Apple
  // Virtualization framework. But it's "AppleParavirtGPUControl" in VMware
  // VMs (on Intel) that use VMware's "paravirtualized driver". Parallels
  // Intel VMs have a "AppleParavirtGPUControl" service. But, like VMware
  // VMs that don't use the "paravirtualized driver", they also have
  // kClassCodeDisplayVGA devices. So these cases will have been dealt with
  // above. On Apple Silicon, Parallels uses Apple's Virtualization
  // framework. VMware doesn't support macOS guest VMs on Apple Silicon.
  for (const char* className :
       {"AppleParavirtGPU", "AppleParavirtGPUControl"}) {
    CFMutableDictionaryRef apv_dev_dict = IOServiceMatching(className);
    if (IOServiceGetMatchingServices(kIOMasterPortDefault, apv_dev_dict,
                                     &io_iter) == kIOReturnSuccess) {
      io_registry_entry_t entry = IOIteratorNext(io_iter);
      if (entry != IO_OBJECT_NULL) {
        IOObjectRelease(entry);
        IOIteratorReset(io_iter);
        break;
      }
      IOObjectRelease(io_iter);
    }
    io_iter = IO_OBJECT_NULL;
  }
  if (io_iter) {
    io_registry_entry_t entry = IO_OBJECT_NULL;
    while ((entry = IOIteratorNext(io_iter)) != IO_OBJECT_NULL) {
      CFTypeRef vendor_id_ref =
          SearchPortForProperty(entry, CFSTR("vendor-id"));
      if (vendor_id_ref) {
        mAdapterVendorID[mNumGPUsDetected].AppendPrintf(
            "0x%04x", IntValueOfCFData((CFDataRef)vendor_id_ref));
        CFRelease(vendor_id_ref);
      }

      CFTypeRef device_id_ref =
          SearchPortForProperty(entry, CFSTR("device-id"));
      if (device_id_ref) {
        mAdapterDeviceID[mNumGPUsDetected].AppendPrintf(
            "0x%04x", IntValueOfCFData((CFDataRef)device_id_ref));
        CFRelease(device_id_ref);
      }
      ++mNumGPUsDetected;
      IOObjectRelease(entry);
      if (mNumGPUsDetected == kMaxGPUs) {
        break;
      }
    }

    IOObjectRelease(io_iter);
  }

  MOZ_DIAGNOSTIC_ASSERT(mNumGPUsDetected > 0, "Failed to detect any GPUs");
}

nsresult GfxInfo::Init() {
  nsresult rv = GfxInfoBase::Init();

  // Calling CGLQueryRendererInfo causes us to switch to the discrete GPU
  // even when we don't want to. We'll avoid doing so for now and just
  // use the device ids.

  GetDeviceInfo();

  AddCrashReportAnnotations();

  mMacOSVersion = nsCocoaFeatures::macOSVersion();
  mMacOSVersionEx =
      GfxVersionEx(nsCocoaFeatures::ExtractMajorVersion(mMacOSVersion),
                   nsCocoaFeatures::ExtractMinorVersion(mMacOSVersion),
                   nsCocoaFeatures::ExtractBugFixVersion(mMacOSVersion));

  return rv;
}

NS_IMETHODIMP
GfxInfo::GetDWriteEnabled(bool* aEnabled) { return NS_ERROR_FAILURE; }

/* readonly attribute bool HasBattery; */
NS_IMETHODIMP GfxInfo::GetHasBattery(bool* aHasBattery) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* readonly attribute DOMString DWriteVersion; */
NS_IMETHODIMP
GfxInfo::GetDWriteVersion(nsAString& aDwriteVersion) {
  return NS_ERROR_FAILURE;
}

/* readonly attribute DOMString cleartypeParameters; */
NS_IMETHODIMP
GfxInfo::GetCleartypeParameters(nsAString& aCleartypeParams) {
  return NS_ERROR_FAILURE;
}

/* readonly attribute DOMString windowProtocol; */
NS_IMETHODIMP
GfxInfo::GetWindowProtocol(nsAString& aWindowProtocol) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* readonly attribute DOMString testType; */
NS_IMETHODIMP
GfxInfo::GetTestType(nsAString& aTestType) { return NS_ERROR_NOT_IMPLEMENTED; }

/* readonly attribute DOMString adapterDescription; */
NS_IMETHODIMP
GfxInfo::GetAdapterDescription(nsAString& aAdapterDescription) {
  aAdapterDescription.AssignLiteral("");
  return NS_OK;
}

/* readonly attribute DOMString adapterDescription2; */
NS_IMETHODIMP
GfxInfo::GetAdapterDescription2(nsAString& aAdapterDescription) {
  if (mNumGPUsDetected < kMaxGPUs) {
    return NS_ERROR_FAILURE;
  }
  aAdapterDescription.AssignLiteral("");
  return NS_OK;
}

/* readonly attribute DOMString adapterRAM; */
NS_IMETHODIMP
GfxInfo::GetAdapterRAM(uint32_t* aAdapterRAM) {
  *aAdapterRAM = mAdapterRAM[0];
  return NS_OK;
}

/* readonly attribute DOMString adapterRAM2; */
NS_IMETHODIMP
GfxInfo::GetAdapterRAM2(uint32_t* aAdapterRAM) {
  if (mNumGPUsDetected < kMaxGPUs) {
    return NS_ERROR_FAILURE;
  }
  *aAdapterRAM = mAdapterRAM[1];
  return NS_OK;
}

/* readonly attribute DOMString adapterDriver; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriver(nsAString& aAdapterDriver) {
  aAdapterDriver.AssignLiteral("");
  return NS_OK;
}

/* readonly attribute DOMString adapterDriver2; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriver2(nsAString& aAdapterDriver) {
  if (mNumGPUsDetected < kMaxGPUs) {
    return NS_ERROR_FAILURE;
  }
  aAdapterDriver.AssignLiteral("");
  return NS_OK;
}

/* readonly attribute DOMString adapterDriverVendor; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriverVendor(nsAString& aAdapterDriverVendor) {
  aAdapterDriverVendor.AssignLiteral("");
  return NS_OK;
}

/* readonly attribute DOMString adapterDriverVendor2; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriverVendor2(nsAString& aAdapterDriverVendor) {
  if (mNumGPUsDetected < kMaxGPUs) {
    return NS_ERROR_FAILURE;
  }
  aAdapterDriverVendor.AssignLiteral("");
  return NS_OK;
}

/* readonly attribute DOMString adapterDriverVersion; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriverVersion(nsAString& aAdapterDriverVersion) {
  aAdapterDriverVersion.AssignLiteral("");
  return NS_OK;
}

/* readonly attribute DOMString adapterDriverVersion2; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriverVersion2(nsAString& aAdapterDriverVersion) {
  if (mNumGPUsDetected < kMaxGPUs) {
    return NS_ERROR_FAILURE;
  }
  aAdapterDriverVersion.AssignLiteral("");
  return NS_OK;
}

/* readonly attribute DOMString adapterDriverDate; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriverDate(nsAString& aAdapterDriverDate) {
  aAdapterDriverDate.AssignLiteral("");
  return NS_OK;
}

/* readonly attribute DOMString adapterDriverDate2; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriverDate2(nsAString& aAdapterDriverDate) {
  if (mNumGPUsDetected < kMaxGPUs) {
    return NS_ERROR_FAILURE;
  }
  aAdapterDriverDate.AssignLiteral("");
  return NS_OK;
}

/* readonly attribute DOMString adapterVendorID; */
NS_IMETHODIMP
GfxInfo::GetAdapterVendorID(nsAString& aAdapterVendorID) {
  aAdapterVendorID = mAdapterVendorID[0];
  return NS_OK;
}

/* readonly attribute DOMString adapterVendorID2; */
NS_IMETHODIMP
GfxInfo::GetAdapterVendorID2(nsAString& aAdapterVendorID) {
  if (mNumGPUsDetected < kMaxGPUs) {
    return NS_ERROR_FAILURE;
  }
  aAdapterVendorID = mAdapterVendorID[1];
  return NS_OK;
}

/* readonly attribute DOMString adapterDeviceID; */
NS_IMETHODIMP
GfxInfo::GetAdapterDeviceID(nsAString& aAdapterDeviceID) {
  aAdapterDeviceID = mAdapterDeviceID[0];
  return NS_OK;
}

/* readonly attribute DOMString adapterDeviceID2; */
NS_IMETHODIMP
GfxInfo::GetAdapterDeviceID2(nsAString& aAdapterDeviceID) {
  if (mNumGPUsDetected < kMaxGPUs) {
    return NS_ERROR_FAILURE;
  }
  aAdapterDeviceID = mAdapterDeviceID[1];
  return NS_OK;
}

/* readonly attribute DOMString adapterSubsysID; */
NS_IMETHODIMP
GfxInfo::GetAdapterSubsysID(nsAString& aAdapterSubsysID) {
  return NS_ERROR_FAILURE;
}

/* readonly attribute DOMString adapterSubsysID2; */
NS_IMETHODIMP
GfxInfo::GetAdapterSubsysID2(nsAString& aAdapterSubsysID) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetDrmRenderDevice(nsACString& aDrmRenderDevice) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* readonly attribute boolean isGPU2Active; */
NS_IMETHODIMP
GfxInfo::GetIsGPU2Active(bool* aIsGPU2Active) { return NS_ERROR_FAILURE; }

void GfxInfo::AddCrashReportAnnotations() {
  nsString deviceID, vendorID, driverVersion;

  GetAdapterDeviceID(deviceID);
  GetAdapterVendorID(vendorID);
  GetAdapterDriverVersion(driverVersion);

  CrashReporter::RecordAnnotationNSString(
      CrashReporter::Annotation::AdapterVendorID, vendorID);
  CrashReporter::RecordAnnotationNSString(
      CrashReporter::Annotation::AdapterDeviceID, deviceID);
  CrashReporter::RecordAnnotationNSString(
      CrashReporter::Annotation::AdapterDriverVersion, driverVersion);
}

// We don't support checking driver versions on Mac.
#define IMPLEMENT_MAC_DRIVER_BLOCKLIST(os, device, features, blockOn, ruleId)  \
  APPEND_TO_DRIVER_BLOCKLIST(os, device, features, blockOn,                    \
                             DRIVER_COMPARISON_IGNORED, V(0, 0, 0, 0), ruleId, \
                             "")

const nsTArray<RefPtr<GfxDriverInfo>>& GfxInfo::GetGfxDriverInfo() {
  if (!sDriverInfo->Length()) {
    IMPLEMENT_MAC_DRIVER_BLOCKLIST(
        OperatingSystem::MacOS, DeviceFamily::RadeonX1000,
        nsIGfxInfo::FEATURE_OPENGL_LAYERS, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        "FEATURE_FAILURE_MAC_RADEONX1000_NO_TEXTURE2D");
    IMPLEMENT_MAC_DRIVER_BLOCKLIST(
        OperatingSystem::MacOS, DeviceFamily::Geforce7300GT,
        nsIGfxInfo::FEATURE_WEBGL_OPENGL, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        "FEATURE_FAILURE_MAC_7300_NO_WEBGL");
    IMPLEMENT_MAC_DRIVER_BLOCKLIST(
        OperatingSystem::MacOS, DeviceFamily::IntelHDGraphicsToIvyBridge,
        nsIGfxInfo::FEATURE_GL_SWIZZLE, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        "FEATURE_FAILURE_MAC_INTELHD4000_NO_SWIZZLE");
    // We block texture swizzling everwhere on mac because it's broken in some
    // configurations and we want to support GPU switching.
    IMPLEMENT_MAC_DRIVER_BLOCKLIST(
        OperatingSystem::MacOS, DeviceFamily::All,
        nsIGfxInfo::FEATURE_GL_SWIZZLE, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        "FEATURE_FAILURE_MAC_GPU_SWITCHING_NO_SWIZZLE");

    // Older generation Intel devices do not perform well with WebRender.
    IMPLEMENT_MAC_DRIVER_BLOCKLIST(
        OperatingSystem::MacOS, DeviceFamily::IntelWebRenderBlocked,
        nsIGfxInfo::FEATURE_WEBRENDER, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
        "FEATURE_FAILURE_INTEL_GEN5_OR_OLDER");
  }
  return *sDriverInfo;
}

OperatingSystem GfxInfo::GetOperatingSystem() {
  return MacOSVersionToOperatingSystem(mMacOSVersion);
}

GfxVersionEx GfxInfo::OperatingSystemVersionEx() { return mMacOSVersionEx; }

nsresult GfxInfo::GetFeatureStatusImpl(
    int32_t aFeature, int32_t* aStatus, nsAString& aSuggestedDriverVersion,
    const nsTArray<RefPtr<GfxDriverInfo>>& aDriverInfo, nsACString& aFailureId,
    OperatingSystem* aOS /* = nullptr */) {
  NS_ENSURE_ARG_POINTER(aStatus);
  aSuggestedDriverVersion.SetIsVoid(true);
  *aStatus = nsIGfxInfo::FEATURE_STATUS_UNKNOWN;
  OperatingSystem os = MacOSVersionToOperatingSystem(mMacOSVersion);
  if (aOS) *aOS = os;

  if (sShutdownOccurred) {
    return NS_OK;
  }

  // Don't evaluate special cases when we're evaluating the downloaded
  // blocklist.
  if (!aDriverInfo.Length()) {
    if (aFeature == nsIGfxInfo::FEATURE_CANVAS2D_ACCELERATION) {
      // See bug 1249659
      switch (os) {
        case OperatingSystem::MacOS10_5:
        case OperatingSystem::MacOS10_6:
        case OperatingSystem::MacOS10_7:
          *aStatus = nsIGfxInfo::FEATURE_BLOCKED_OS_VERSION;
          aFailureId = "FEATURE_FAILURE_CANVAS_MACOS_VERSION";
          break;
        default:
          *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
          break;
      }
      return NS_OK;
    } else if (aFeature == nsIGfxInfo::FEATURE_WEBRENDER &&
               nsCocoaFeatures::ProcessIsRosettaTranslated()) {
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
      aFailureId = "FEATURE_UNQUALIFIED_WEBRENDER_MAC_ROSETTA";
      return NS_OK;
    } else if (aFeature == nsIGfxInfo::FEATURE_WEBGL_ANGLE_METAL) {
      if (mMacOSVersionEx.Compare(GfxVersionEx(12, 0, 0)) < 0) {
        // ANGLE only supports macOS 12 onwards. Blocked until we restore
        // support for earlier OS versions. See bug 2053051.
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_OS_VERSION;
        aFailureId = "FEATURE_FAILURE_METAL_ANGLE_MACOS_VERSION";
      } else {
        *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
      }
      return NS_OK;
    } else if (aFeature == nsIGfxInfo::FEATURE_WEBRENDER_ANGLE_METAL) {
      if (mMacOSVersionEx.Compare(GfxVersionEx(12, 0, 0)) < 0) {
        // ANGLE only supports macOS 12 onwards. Blocked until we restore
        // support for earlier OS versions. See bug 2053051.
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_OS_VERSION;
        aFailureId = "FEATURE_FAILURE_METAL_ANGLE_MACOS_VERSION";
      } else if (mNumGPUsDetected > 1) {
        // Blocked on devices with multiple GPUs until we implement support for
        // GPU switching. See bug 1600178.
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
        aFailureId = "FEATURE_FAILURE_METAL_ANGLE_MULTIPLE_GPUS";
      } else {
        *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
      }
      return NS_OK;
    }
  }

  return GfxInfoBase::GetFeatureStatusImpl(
      aFeature, aStatus, aSuggestedDriverVersion, aDriverInfo, aFailureId, &os);
}

#ifdef DEBUG

// Implement nsIGfxInfoDebug

/* void spoofVendorID (in DOMString aVendorID); */
NS_IMETHODIMP GfxInfo::SpoofVendorID(const nsAString& aVendorID) {
  mAdapterVendorID[0] = aVendorID;
  return NS_OK;
}

/* void spoofDeviceID (in unsigned long aDeviceID); */
NS_IMETHODIMP GfxInfo::SpoofDeviceID(const nsAString& aDeviceID) {
  mAdapterDeviceID[0] = aDeviceID;
  return NS_OK;
}

/* void spoofDriverVersion (in DOMString aDriverVersion); */
NS_IMETHODIMP GfxInfo::SpoofDriverVersion(const nsAString& aDriverVersion) {
  mDriverVersion[0] = aDriverVersion;
  return NS_OK;
}

/* void spoofOSVersion (in unsigned long aVersion); */
NS_IMETHODIMP GfxInfo::SpoofOSVersion(uint32_t aVersion) {
  mMacOSVersion = aVersion;
  return NS_OK;
}

NS_IMETHODIMP GfxInfo::SpoofOSVersionEx(uint32_t aMajor, uint32_t aMinor,
                                        uint32_t aBuild, uint32_t aRevision) {
  mMacOSVersionEx = GfxVersionEx(aMajor, aMinor, aBuild, aRevision);
  return NS_OK;
}

#endif
