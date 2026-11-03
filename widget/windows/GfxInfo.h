/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WIDGET_WINDOWS_GFXINFO_H_
#define WIDGET_WINDOWS_GFXINFO_H_

#include "GfxInfoBase.h"

namespace mozilla::widget {

class GfxInfo : public GfxInfoBase {
 public:
  using GfxInfoBase::GetFeatureStatus;
  using GfxInfoBase::GetFeatureSuggestedDriverVersion;

  GfxInfo() = default;
  nsresult Init() override;

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

  uint32_t OperatingSystemVersion() override { return mWindowsVersion; }
  GfxVersionEx OperatingSystemVersionEx() override { return mWindowsVersionEx; }

#ifdef DEBUG
  NS_DECL_ISUPPORTS_INHERITED

  NS_IMETHOD SpoofVendorID(const nsAString& aVendorID) override;
  NS_IMETHOD SpoofDeviceID(const nsAString& aDeviceID) override;
  NS_IMETHOD SpoofDriverVersion(const nsAString& aDriverVersion) override;
  NS_IMETHOD SpoofOSVersion(uint32_t aVersion) override;
  NS_IMETHOD SpoofOSVersionEx(uint32_t aMajor, uint32_t aMinor, uint32_t aBuild,
                              uint32_t aRevision) override;
#endif

 private:
  ~GfxInfo() = default;

  // Disallow copy/move
  GfxInfo(const GfxInfo&) = delete;
  GfxInfo& operator=(const GfxInfo&) = delete;
  GfxInfo(GfxInfo&&) = delete;
  GfxInfo& operator=(GfxInfo&&) = delete;

  OperatingSystem GetOperatingSystem() override;

  nsresult GetFeatureStatusImpl(
      int32_t aFeature, int32_t* aStatus, nsAString& aSuggestedDriverVersion,
      const nsTArray<RefPtr<GfxDriverInfo>>& aDriverInfo,
      nsACString& aFailureId, OperatingSystem* aOS = nullptr) override;
  const nsTArray<RefPtr<GfxDriverInfo>>& GetGfxDriverInfo() override;

  void DescribeFeatures(JSContext* cx, JS::Handle<JSObject*> aOut) override;

  void AddCrashReportAnnotations();

  nsString mDeviceString[2];
  nsString mDeviceID[2];
  nsString mDriverVersion[2];
  nsString mDriverDate[2];
  nsString mDeviceKey[2];
  nsString mDeviceKeyDebug;
  nsString mAdapterVendorID[2];
  nsString mAdapterDeviceID[2];
  nsString mAdapterSubsysID[2];
  GfxVersionEx mWindowsVersionEx;
  uint32_t mWindowsVersion = 0;
  uint32_t mWindowsBuildNumber = 0;
  uint32_t mActiveGPUIndex = 0;  // This must be 0 or 1
  bool mHasDualGPU = false;
  bool mHasDriverVersionMismatch = false;
  bool mHasBattery = false;
};

}  // namespace mozilla::widget

#endif  // WIDGET_WINDOWS_GFXINFO_H_
