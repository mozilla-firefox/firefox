/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WIDGET_GTK_GFXINFO_h_
#define WIDGET_GTK_GFXINFO_h_

#include "GfxInfoBase.h"
#include "nsString.h"

namespace mozilla {
namespace widget {

class GfxInfo final : public GfxInfoBase {
 public:
  // We only declare the subset of nsIGfxInfo that we actually implement. The
  // rest is brought forward from GfxInfoBase.
  NS_IMETHOD GetDWriteEnabled(bool* aDWriteEnabled) override;
  NS_IMETHOD GetDWriteVersion(nsAString& aDwriteVersion) override;
  NS_IMETHOD GetHasBattery(bool* aHasBattery) override;
  NS_IMETHOD GetCleartypeParameters(nsAString& aCleartypeParams) override;
  NS_IMETHOD GetWindowProtocol(nsAString& aWindowProtocol) override;
  NS_IMETHOD GetTestType(nsAString& aTestType) override;
  NS_IMETHOD GetAdapterDescription(nsAString& aAdapterDescription) override;
  NS_IMETHOD GetAdapterDriver(nsAString& aAdapterDriver) override;
  NS_IMETHOD GetAdapterVendorID(nsAString& aAdapterVendorID) override;
  NS_IMETHOD GetAdapterDeviceID(nsAString& aAdapterDeviceID) override;
  NS_IMETHOD GetAdapterSubsysID(nsAString& aAdapterSubsysID) override;
  NS_IMETHOD GetAdapterRAM(uint32_t* aAdapterRAM) override;
  NS_IMETHOD GetAdapterDriverVendor(nsAString& aAdapterDriverVendor) override;
  NS_IMETHOD GetAdapterDriverVersion(nsAString& aAdapterDriverVersion) override;
  NS_IMETHOD GetAdapterDriverDate(nsAString& aAdapterDriverDate) override;
  NS_IMETHOD GetAdapterDescription2(nsAString& aAdapterDescription) override;
  NS_IMETHOD GetAdapterDriver2(nsAString& aAdapterDriver) override;
  NS_IMETHOD GetAdapterVendorID2(nsAString& aAdapterVendorID) override;
  NS_IMETHOD GetAdapterDeviceID2(nsAString& aAdapterDeviceID) override;
  NS_IMETHOD GetAdapterSubsysID2(nsAString& aAdapterSubsysID) override;
  NS_IMETHOD GetAdapterRAM2(uint32_t* aAdapterRAM) override;
  NS_IMETHOD GetAdapterDriverVendor2(nsAString& aAdapterDriverVendor) override;
  NS_IMETHOD GetAdapterDriverVersion2(
      nsAString& aAdapterDriverVersion) override;
  NS_IMETHOD GetAdapterDriverDate2(nsAString& aAdapterDriverDate) override;
  NS_IMETHOD GetIsGPU2Active(bool* aIsGPU2Active) override;
  NS_IMETHOD GetDrmRenderDevice(nsACString& aDrmRenderDevice) override;
  using GfxInfoBase::GetFeatureStatus;
  using GfxInfoBase::GetFeatureSuggestedDriverVersion;

  virtual nsresult Init() override;

  NS_IMETHOD_(void) GetData() override;

  const nsTArray<uint64_t>& GetDMABufEGLModifiers(uint32_t aDrmFourcc) const;

  static bool FireGLXTestProcess();

#ifdef DEBUG
  NS_DECL_ISUPPORTS_INHERITED

  NS_IMETHOD SpoofVendorID(const nsAString& aVendorID) override;
  NS_IMETHOD SpoofDeviceID(const nsAString& aDeviceID) override;
  NS_IMETHOD SpoofDriverVersion(const nsAString& aDriverVersion) override;
  NS_IMETHOD SpoofOSVersion(uint32_t aVersion) override;
  NS_IMETHOD SpoofOSVersionEx(uint32_t aMajor, uint32_t aMinor, uint32_t aBuild,
                              uint32_t aRevision) override;

  GfxVersionEx OperatingSystemVersionEx() override { return mOSVersionEx; }
#endif

 protected:
  ~GfxInfo() = default;

  OperatingSystem GetOperatingSystem() override {
    return OperatingSystem::Linux;
  }
  virtual nsresult GetFeatureStatusImpl(
      int32_t aFeature, int32_t* aStatus, nsAString& aSuggestedDriverVersion,
      const nsTArray<RefPtr<GfxDriverInfo>>& aDriverInfo,
      nsACString& aFailureId, OperatingSystem* aOS = nullptr) override;
  virtual const nsTArray<RefPtr<GfxDriverInfo>>& GetGfxDriverInfo() override;

  virtual bool DoesWindowProtocolMatch(
      const nsAString& aBlocklistWindowProtocol,
      const nsAString& aWindowProtocol) override;

  virtual bool DoesDriverVendorMatch(const nsAString& aBlocklistVendor,
                                     const nsAString& aDriverVendor) override;
  static int FireTestProcess(const nsAString& aBinaryFile, int* aOutPipe,
                             const char** aStringArgs);

 private:
  bool mInitialized = false;
  nsCString mVendorId;
  nsCString mDeviceId;
  nsCString mDriverVendor;
  nsCString mDriverVersion;
  nsCString mAdapterDescription;
  uint32_t mAdapterRAM;
  nsCString mOS;
  nsCString mOSRelease;
  nsCString mTestType;

  nsCString mSecondaryVendorId;
  nsCString mSecondaryDeviceId;

  nsCString mDrmRenderDevice;

  nsTArray<nsCString> mDdxDrivers;

  struct ScreenInfo {
    uint32_t mWidth;
    uint32_t mHeight;
    bool mIsDefault;
  };

  nsTArray<ScreenInfo> mScreenInfo;
  bool mHasTextureFromPixmap;
  unsigned int mGLMajorVersion, mGLMinorVersion;
  bool mIsMesa;
  bool mIsAccelerated;
  bool mIsWayland;
  bool mIsXWayland;
  bool mHasMultipleGPUs;
  bool mGlxTestError;
  mozilla::Maybe<bool> mIsVAAPISupported;
  int mVAAPISupportedCodecs = 0;
  mozilla::Maybe<bool> mIsV4L2Supported;
  int mV4L2SupportedCodecs = 0;
  mozilla::Maybe<bool> mIsVulkanSupported;
  int mVulkanSupportedCodecs = 0;

  nsTArray<uint64_t> mDMABufEGLModifiersXRGB;
  nsTArray<uint64_t> mDMABufEGLModifiersARGB;
  nsTArray<uint64_t> mDMABufEGLModifiersNV12;
  nsTArray<uint64_t> mDMABufEGLModifiersP010;
  nsTArray<uint64_t> mDMABufEGLModifiersYUV420;

  static int sGLXTestPipe;
  static pid_t sGLXTestPID;

#ifdef DEBUG
  GfxVersionEx mOSVersionEx;
#endif

  void GetDataVAAPI();
  void GetDataV4L2();
  void GetDataVulkan();
  void V4L2ProbeDevice(nsCString& dev);
  void AddCrashReportAnnotations();
};

}  // namespace widget
}  // namespace mozilla

#endif /* WIDGET_GTK_GFXINFO_h_ */
