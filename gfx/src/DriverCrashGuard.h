/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef gfx_src_DriverCrashGuard_h_
#define gfx_src_DriverCrashGuard_h_

#include <functional>
#include <string>

#include "nsCOMPtr.h"
#include "nsIFile.h"
#include "nsIGfxInfo.h"
#include "nsString.h"

namespace mozilla {

namespace dom {
class ContentParent;
}  // namespace dom

namespace gfx {

enum class DriverInitStatus {
  // Drivers have not been initialized yet.
  Unknown,

  // We're attempting to initialize drivers.
  Attempting,

  // Drivers were successfully initialized last run.
  Okay,

  // We crashed during driver initialization, and have restarted.
  Crashed
};

enum class CrashGuardType : uint32_t {
  D3D11Layers,
  GLContext,
  WMFVPXVideo,
  // Add new entries above this line, update the name array in
  // DriverCrashGuard.cpp, make sure to add an entry in ContentParent.cpp,
  // and add a fluent identifier in aboutSupport.ftl.

  NUM_TYPES
};

// DriverCrashGuard is used to detect crashes at graphics driver callsites.
//
// If the graphics environment is unrecognized or has changed since the last
// session, the crash guard will activate and will detect any crashes within
// the scope of the guard object.
//
// If a callsite has a previously encountered crash, and the environment has
// not changed since the last session, then the guard will set a status flag
// indicating that the driver should not be used.
class DriverCrashGuard {
 public:
  DriverCrashGuard(CrashGuardType aType, dom::ContentParent* aContentParent);
  virtual ~DriverCrashGuard();

  bool Crashed();
  void NotifyCrashed();

  // These are the values reported to Telemetry
  // (gfx.graphics_driver_startup_test).
  // Values should not change; add new values to the end.
  enum class TelemetryState {
    Okay = 0,
    EnvironmentChanged = 1,
    RecoveredFromCrash = 2,
    FeatureDisabled = 3
  };

  enum class Mode {
    // Normal operation.
    Normal,

    // Acting as a proxy between the parent and child process.
    Proxy
  };

  typedef std::function<void(const char* aName, const char* aPrefName)>
      CrashGuardCallback;
  static void ForEachActiveCrashGuard(const CrashGuardCallback& aCallback);

 protected:
  virtual void Initialize();
  // UpdateEnvironment needs to return true should we need to attempt the
  // operation once again.
  // It should return true once only so that in case of a crash, we won't
  // needlessly attempt the operation over and over again leading to continual
  // crashes. several times
  virtual bool UpdateEnvironment() {
    // We don't care about any extra preferences here.
    return false;
  }
  virtual void LogCrashRecovery() = 0;
  virtual void LogFeatureDisabled() = 0;

  // Helper functions.
  bool FeatureEnabled(int aFeature, bool aDefault = true);
  bool CheckAndUpdatePref(const char* aPrefName,
                          const nsAString& aCurrentValue);
  bool CheckAndUpdateBoolPref(const char* aPrefName, bool aCurrentValue);
  std::string GetFullPrefName(const char* aPref);

 private:
  // Either process.
  void InitializeIfNeeded();
  bool CheckOrRefreshEnvironment();
  bool UpdateBaseEnvironment();
  DriverInitStatus GetStatus() const;

  // Parent process only.
  nsCOMPtr<nsIFile> GetGuardFile();
  bool RecoverFromCrash();
  void ActivateGuard();
  void FlushPreferences();
  void SetStatus(DriverInitStatus aStatus);

 private:
  CrashGuardType mType;
  Mode mMode;
  bool mInitialized;
  bool mGuardActivated;
  bool mCrashDetected;
  nsCOMPtr<nsIFile> mGuardFile;

 protected:
  nsCString mStatusPref;
  nsCOMPtr<nsIGfxInfo> mGfxInfo;
};

class D3D11LayersCrashGuard final : public DriverCrashGuard {
 public:
  explicit D3D11LayersCrashGuard(dom::ContentParent* aContentParent = nullptr);

 protected:
  void Initialize() override;
  bool UpdateEnvironment() override;
  void LogCrashRecovery() override;
  void LogFeatureDisabled() override;

 private:
  void RecordTelemetry(TelemetryState aState);
};

class GLContextCrashGuard final : public DriverCrashGuard {
 public:
  explicit GLContextCrashGuard(dom::ContentParent* aContentParent = nullptr);
  void Initialize() override;

 protected:
  bool UpdateEnvironment() override;
  void LogCrashRecovery() override;
  void LogFeatureDisabled() override;
};

class WMFVPXVideoCrashGuard final : public DriverCrashGuard {
 public:
  explicit WMFVPXVideoCrashGuard(dom::ContentParent* aContentParent = nullptr);

 protected:
  void LogCrashRecovery() override;
  void LogFeatureDisabled() override;
};

}  // namespace gfx
}  // namespace mozilla

#endif  // gfx_src_DriverCrashGuard_h_
