/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _mozilla_widget_GfxInfo_h_
#define _mozilla_widget_GfxInfo_h_

#include "GfxInfoBase.h"
#include "nsString.h"

namespace mozilla {
namespace widget {

class GfxInfo : public GfxInfoBase {
 public:
  GfxInfo();
  // We only declare the subset of nsIGfxInfo that we actually implement. The
  // rest is brought forward from GfxInfoBase.
  NS_IMETHOD GetDWriteEnabled(bool* aDWriteEnabled) override;
  NS_IMETHOD GetDWriteVersion(nsAString& aDwriteVersion) override;
  NS_IMETHOD GetHasBattery(bool* aHasBattery) override;
  NS_IMETHOD GetWindowProtocol(nsAString& aWindowProtocol) override;
  NS_IMETHOD GetTestType(nsAString& aTestType) override;
  NS_IMETHOD GetCleartypeParameters(nsAString& aCleartypeParams) override;
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

#ifdef DEBUG
  NS_DECL_ISUPPORTS_INHERITED

  NS_IMETHOD SpoofVendorID(const nsAString& aVendorID) override;
  NS_IMETHOD SpoofDeviceID(const nsAString& aDeviceID) override;
  NS_IMETHOD SpoofDriverVersion(const nsAString& aDriverVersion) override;
  NS_IMETHOD SpoofOSVersion(uint32_t aVersion) override;
  NS_IMETHOD SpoofOSVersionEx(uint32_t aMajor, uint32_t aMinor, uint32_t aBuild,
                              uint32_t aRevision) override;
#endif

  virtual uint32_t OperatingSystemVersion() override { return mMacOSVersion; }

 protected:
  virtual ~GfxInfo() {}

  OperatingSystem GetOperatingSystem() override;
  GfxVersionEx OperatingSystemVersionEx() override;
  virtual nsresult GetFeatureStatusImpl(
      int32_t aFeature, int32_t* aStatus, nsAString& aSuggestedDriverVersion,
      const nsTArray<RefPtr<GfxDriverInfo>>& aDriverInfo,
      nsACString& aFailureId, OperatingSystem* aOS = nullptr) override;
  virtual const nsTArray<RefPtr<GfxDriverInfo>>& GetGfxDriverInfo() override;

 private:
  static constexpr uint32_t kMaxGPUs = 2;

  void GetDeviceInfo();
  void GetSelectedCityInfo();
  void AddCrashReportAnnotations();

  uint32_t mNumGPUsDetected;

  uint32_t mAdapterRAM[kMaxGPUs];
  nsString mDeviceID[kMaxGPUs];
  nsString mDriverVersion[kMaxGPUs];
  nsString mDriverDate[kMaxGPUs];
  nsString mDeviceKey[kMaxGPUs];

  nsString mAdapterVendorID[kMaxGPUs];
  nsString mAdapterDeviceID[kMaxGPUs];

  GfxVersionEx mMacOSVersionEx;
  uint32_t mMacOSVersion;
};

}  // namespace widget
}  // namespace mozilla

#endif /* _mozilla_widget_GfxInfo_h_ */
