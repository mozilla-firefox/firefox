/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

#include <cinttypes>

#include "gtest/gtest.h"
#include "json/json.h"
#include "mozilla/Preferences.h"
#include "mozilla/Printf.h"
#include "mozilla/XREAppData.h"
#include "nsAppRunner.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIFile.h"
#include "nsIProperties.h"
#include "nsIPropertyBag2.h"
#include "nsString.h"
#include "prtime.h"

#include <cstdio>

#ifdef XP_MACOSX
#  include "nsILocalFileMac.h"
#endif

using mozilla::Preferences;
using mozilla::PrefValueKind;

class DowngradePingTest : public ::testing::Test {
 protected:
  const mozilla::XREAppData* mOriginalAppData = nullptr;
  mozilla::XREAppData mFakeAppData{};
  mozilla::PathString mCreatedPingPath;
  nsCOMPtr<nsIFile> mTmpAppDataDir;

  void SetUp() override {
    mFakeAppData.name = "TestApp";
    mFakeAppData.version = "130.0";
    mFakeAppData.buildID = "20250101000000";
    mFakeAppData.vendor = "TestVendor";
    mOriginalAppData = gAppData;
    gAppData = &mFakeAppData;

    nsCOMPtr<nsIFile> tmpDir;
    ASSERT_EQ(NS_OK,
              NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(tmpDir)));
    ASSERT_EQ(NS_OK, tmpDir->Append(u"downgrade-ping-test"_ns));
    ASSERT_EQ(NS_OK, tmpDir->CreateUnique(nsIFile::DIRECTORY_TYPE, 0755));
    mTmpAppDataDir = tmpDir;

    nsCOMPtr<nsIProperties> dirSvc =
        do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID);
    ASSERT_TRUE(dirSvc);
    ASSERT_EQ(NS_OK, dirSvc->Set(XRE_USER_APP_DATA_DIR, mTmpAppDataDir));

    ASSERT_EQ(NS_OK,
              Preferences::SetBool("datareporting.healthreport.uploadEnabled",
                                   true, PrefValueKind::User));
    ASSERT_EQ(NS_OK,
              Preferences::SetCString("toolkit.telemetry.server",
                                      "https://incoming.telemetry.test"_ns,
                                      PrefValueKind::User));
    ASSERT_EQ(NS_OK, Preferences::SetCString("toolkit.telemetry.cachedClientID",
                                             "test-client-id-1234"_ns,
                                             PrefValueKind::User));
    ASSERT_EQ(NS_OK, Preferences::SetCString(
                         "toolkit.telemetry.cachedProfileGroupID",
                         "test-profile-group-id-5678"_ns, PrefValueKind::User));
  }

  void TearDown() override {
    if (mOriginalAppData) {
      gAppData = mOriginalAppData;
      mOriginalAppData = nullptr;
    }
    Preferences::ClearUser("datareporting.healthreport.uploadEnabled");
    Preferences::ClearUser("toolkit.telemetry.server");
    Preferences::ClearUser("toolkit.telemetry.cachedClientID");
    Preferences::ClearUser("toolkit.telemetry.cachedProfileGroupID");
    Preferences::ClearUser("toolkit.profiles.machineID");
    RemoveUpdateTelemetryJson();
#ifdef XP_WIN
    RemoveInstallTelemetryJson();
#endif
    if (mTmpAppDataDir) {
      nsCOMPtr<nsIProperties> dirSvc =
          do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID);
      if (dirSvc) {
        dirSvc->Undefine(XRE_USER_APP_DATA_DIR);
      }
      mTmpAppDataDir->Remove(true);
      mTmpAppDataDir = nullptr;
    }
  }

  bool ReadPingFile(const mozilla::PathString& aPath, nsCString& aOutContents) {
    nsCOMPtr<nsIFile> file;
    nsresult rv = NS_NewPathStringLocalFile(aPath, getter_AddRefs(file));
    if (NS_FAILED(rv) || !file) return false;

    int64_t fileSize;
    rv = file->GetFileSize(&fileSize);
    if (NS_FAILED(rv)) return false;

    FILE* f = nullptr;
    rv = file->OpenANSIFileDesc("rb", &f);
    if (NS_FAILED(rv) || !f) return false;

    aOutContents.SetLength(fileSize);
    size_t bytesRead = fread(aOutContents.BeginWriting(), 1, fileSize, f);
    fclose(f);
    return bytesRead == static_cast<size_t>(fileSize);
  }

#ifdef XP_MACOSX
  already_AddRefed<nsILocalFileMac> GetAppBundle() {
    nsCOMPtr<nsIFile> greDir;
    if (NS_FAILED(NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(greDir)))) {
      return nullptr;
    }
    nsCOMPtr<nsIFile> contentsDir;
    if (NS_FAILED(greDir->GetParent(getter_AddRefs(contentsDir))) ||
        !contentsDir) {
      return nullptr;
    }
    nsCOMPtr<nsIFile> appBundle;
    if (NS_FAILED(contentsDir->GetParent(getter_AddRefs(appBundle))) ||
        !appBundle) {
      return nullptr;
    }
    nsCOMPtr<nsILocalFileMac> macFile = do_QueryInterface(appBundle);
    return macFile.forget();
  }
#endif

  bool WriteUpdateTelemetryJson(uint64_t aTimestampMs) {
#ifdef XP_MACOSX
    nsCOMPtr<nsILocalFileMac> macFile = GetAppBundle();
    if (!macFile) {
      return false;
    }
    char buf[128];
    SprintfLiteral(buf, "{\"install_timestamp\":\"%" PRIu64 "\"}",
                   aTimestampMs);
    nsTArray<uint8_t> value;
    value.AppendElements(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
    return NS_SUCCEEDED(
        macFile->SetXAttr("org.mozilla.update-telemetry"_ns, value));
#else
    nsCOMPtr<nsIFile> greDir;
    if (NS_FAILED(NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(greDir)))) {
      return false;
    }
    nsCOMPtr<nsIFile> file;
    if (NS_FAILED(greDir->Clone(getter_AddRefs(file)))) {
      return false;
    }
    if (NS_FAILED(file->Append(u"update_telemetry.json"_ns))) {
      return false;
    }

    FILE* f = nullptr;
    if (NS_FAILED(file->OpenANSIFileDesc("w", &f)) || !f) {
      return false;
    }
    fprintf(f, "{\"install_timestamp\":\"%" PRIu64 "\"}", aTimestampMs);
    fclose(f);
    return true;
#endif
  }

  void RemoveUpdateTelemetryJson() {
#ifdef XP_MACOSX
    nsCOMPtr<nsILocalFileMac> macFile = GetAppBundle();
    if (!macFile) {
      return;
    }
    macFile->DelXAttr("org.mozilla.update-telemetry"_ns);
#else
    nsCOMPtr<nsIFile> greDir;
    if (NS_FAILED(NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(greDir)))) {
      return;
    }
    nsCOMPtr<nsIFile> file;
    if (NS_FAILED(greDir->Clone(getter_AddRefs(file)))) {
      return;
    }
    if (NS_FAILED(file->Append(u"update_telemetry.json"_ns))) {
      return;
    }
    file->Remove(false);
#endif
  }

#ifdef XP_WIN
  bool WriteInstallTelemetryJson(uint64_t aFileTime) {
    nsCOMPtr<nsIFile> greDir;
    if (NS_FAILED(NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(greDir))))
      return false;
    nsCOMPtr<nsIFile> file;
    if (NS_FAILED(greDir->Clone(getter_AddRefs(file)))) return false;
    if (NS_FAILED(file->Append(u"installation_telemetry.json"_ns)))
      return false;

    char json[128];
    SprintfLiteral(json, "{\"install_timestamp\":\"%" PRIu64 "\"}", aFileTime);

    nsAutoString utf16;
    utf16.AppendASCII(json);

    FILE* f = nullptr;
    if (NS_FAILED(file->OpenANSIFileDesc("wb", &f)) || !f) return false;
    fwrite(utf16.get(), sizeof(char16_t), utf16.Length(), f);
    fclose(f);
    return true;
  }

  void RemoveInstallTelemetryJson() {
    nsCOMPtr<nsIFile> greDir;
    if (NS_FAILED(NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(greDir))))
      return;
    nsCOMPtr<nsIFile> file;
    if (NS_FAILED(greDir->Clone(getter_AddRefs(file)))) return;
    if (NS_FAILED(file->Append(u"installation_telemetry.json"_ns))) return;
    file->Remove(false);
  }
#endif
};

TEST_F(DowngradePingTest, MissingClientId) {
  Preferences::ClearUser("toolkit.telemetry.cachedClientID");

  EXPECT_FALSE(GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Nothing(), false));
}

TEST_F(DowngradePingTest, InvalidVersionFormat) {
  EXPECT_FALSE(GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0-no-underscore"_ns, false, 0, "test-channel"_ns,
      "default"_ns, mozilla::Nothing(), false));
}

TEST_F(DowngradePingTest, FullPingStructure) {
  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, true, 1,
      "test-channel"_ns, "default"_ns, mozilla::Nothing(), true);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString url;
  ASSERT_TRUE(BuildDowngradePingUrl("test-ping-id"_ns, "test-channel"_ns, url));
  EXPECT_TRUE(StringBeginsWith(url, "https://incoming.telemetry.test"_ns));
  EXPECT_NE(url.Find("/downgrade/"), kNotFound);

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  EXPECT_STREQ(root["type"].asCString(), "downgrade");
  EXPECT_STREQ(root["id"].asCString(), "test-ping-id");
  {
    std::string dateStr = root["creationDate"].asString();
    EXPECT_FALSE(dateStr.empty());
    int year, month, day, hour, minute, second;
    EXPECT_EQ(6, sscanf(dateStr.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month,
                        &day, &hour, &minute, &second));
    PRExplodedTime pingExploded = {};
    pingExploded.tm_year = year;
    pingExploded.tm_month = month - 1;
    pingExploded.tm_mday = day;
    pingExploded.tm_hour = hour;
    pingExploded.tm_min = minute;
    pingExploded.tm_sec = second;
    pingExploded.tm_params = PR_GMTParameters(&pingExploded);
    PRTime pingTime = PR_ImplodeTime(&pingExploded);
    double diff = double(PR_Now() - pingTime) / PR_USEC_PER_SEC;
    EXPECT_GE(diff, 0);
    EXPECT_LT(diff, 600);
  }
  EXPECT_EQ(root["version"].asInt(), 4);
  EXPECT_STREQ(root["clientId"].asCString(), "test-client-id-1234");
  EXPECT_STREQ(root["profileGroupId"].asCString(),
               "test-profile-group-id-5678");

  Json::Value app = root["application"];
  EXPECT_TRUE(app.isObject());
  {
    nsCOMPtr<nsIPropertyBag2> sysInfo =
        do_GetService("@mozilla.org/system-info;1");
    ASSERT_TRUE(sysInfo);
    nsAutoCString expectedArch;
    sysInfo->GetPropertyAsACString(u"arch"_ns, expectedArch);
    EXPECT_STREQ(app["architecture"].asCString(), expectedArch.get());
  }
  EXPECT_STREQ(app["buildId"].asCString(), "20250101000000");
  EXPECT_STREQ(app["name"].asCString(), "TestApp");
  EXPECT_STREQ(app["version"].asCString(), "130.0");
  EXPECT_STREQ(app["vendor"].asCString(), "TestVendor");
  EXPECT_STREQ(app["channel"].asCString(), "test-channel");

  Json::Value payload = root["payload"];
  EXPECT_TRUE(payload.isObject());
  EXPECT_STREQ(payload["lastVersion"].asCString(), "131.0");
  EXPECT_STREQ(payload["lastBuildId"].asCString(), "20250201000000");
  EXPECT_TRUE(payload["hasSync"].asBool());
  EXPECT_EQ(payload["button"].asInt(), 1);
  EXPECT_TRUE(payload["isDifferentInstall"].asBool());
  EXPECT_STREQ(payload["profileSelectionReason"].asCString(), "default");
}

TEST_F(DowngradePingTest, PayloadBooleansFalse) {
  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "restart"_ns, mozilla::Nothing(), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  EXPECT_FALSE(payload["hasSync"].asBool());
  EXPECT_EQ(payload["button"].asInt(), 0);
  EXPECT_FALSE(payload["isDifferentInstall"].asBool());
  EXPECT_STREQ(payload["profileSelectionReason"].asCString(), "restart");
}

TEST_F(DowngradePingTest, IsNewUpdateTrue) {
  // Update happened after the lock time.
  PRTime lockTime = PRTime(1700000000) * PR_MSEC_PER_SEC;
  uint64_t updateTimestampMs = 1701388800000ULL;

  ASSERT_TRUE(WriteUpdateTelemetryJson(updateTimestampMs));

  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Some(lockTime), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  EXPECT_TRUE(payload.isMember("isNewUpdate"));
  EXPECT_TRUE(payload["isNewUpdate"].asBool());
}

TEST_F(DowngradePingTest, IsNewUpdateFalse) {
  // Update happened before the lock time.
  uint64_t updateTimestampMs = 1700000000000ULL;
  PRTime lockTime = PRTime(1701388800) * PR_MSEC_PER_SEC;

  ASSERT_TRUE(WriteUpdateTelemetryJson(updateTimestampMs));

  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Some(lockTime), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  EXPECT_TRUE(payload.isMember("isNewUpdate"));
  EXPECT_FALSE(payload["isNewUpdate"].asBool());
}

TEST_F(DowngradePingTest, IsNewUpdateAbsentWithoutLockTime) {
  uint64_t updateTimestampMs = 1701388800000ULL;

  ASSERT_TRUE(WriteUpdateTelemetryJson(updateTimestampMs));

  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Nothing(), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  EXPECT_FALSE(payload.isMember("isNewUpdate"));
}

TEST_F(DowngradePingTest, DaysSinceLock) {
  PRTime lockTime = PRTime(1700000000) * PR_MSEC_PER_SEC;

  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Some(lockTime), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  EXPECT_TRUE(payload.isMember("daysSinceLock"));
  int64_t expectedDays = (int64_t(time(nullptr)) - 1700000000) / 86400;
  int64_t actual = payload["daysSinceLock"].asInt64();
  EXPECT_GE(actual, expectedDays - 1);
  EXPECT_LE(actual, expectedDays + 1);
}

TEST_F(DowngradePingTest, SameMachine) {
  nsCString machineId;
  if (NS_FAILED(GetMachineId(machineId))) {
    GTEST_SKIP() << "No machine ID available";
  }
  ASSERT_EQ(NS_OK, Preferences::SetCString("toolkit.profiles.machineID",
                                           machineId, PrefValueKind::User));

  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Nothing(), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  EXPECT_TRUE(payload.isMember("isDifferentMachine"));
  EXPECT_FALSE(payload["isDifferentMachine"].asBool());
}

TEST_F(DowngradePingTest, DifferentMachine) {
  nsCString machineId;
  if (NS_FAILED(GetMachineId(machineId))) {
    GTEST_SKIP() << "No machine ID available";
  }
  ASSERT_EQ(NS_OK, Preferences::SetCString("toolkit.profiles.machineID",
                                           "some-other-machine"_ns,
                                           PrefValueKind::User));

  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Nothing(), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  EXPECT_TRUE(payload.isMember("isDifferentMachine"));
  EXPECT_TRUE(payload["isDifferentMachine"].asBool());
}

TEST_F(DowngradePingTest, DifferentMachineAbsentWithoutPref) {
  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Nothing(), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  EXPECT_FALSE(payload.isMember("isDifferentMachine"));
}

#ifdef XP_WIN
TEST_F(DowngradePingTest, ProfileType) {
  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Nothing(), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  ASSERT_TRUE(payload.isMember("profileType"));
  std::string profileType = payload["profileType"].asString();
  EXPECT_TRUE(profileType == "normal" || profileType == "roaming" ||
              profileType == "mandatory" || profileType == "temporary");
}

TEST_F(DowngradePingTest, IsNewInstallTrue) {
  // Install happened after the lock time.
  PRTime lockTime = PRTime(1700000000) * PR_MSEC_PER_SEC;
  constexpr uint64_t kEpochOffset = 116444736000000000ULL;
  int64_t installUnixSec = 1701388800;
  uint64_t installFileTime =
      uint64_t(installUnixSec) * PR_USEC_PER_SEC * 10 + kEpochOffset;

  if (!WriteInstallTelemetryJson(installFileTime)) return;

  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Some(lockTime), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  EXPECT_TRUE(payload.isMember("isNewInstall"));
  EXPECT_TRUE(payload["isNewInstall"].asBool());
}

TEST_F(DowngradePingTest, IsNewInstallFalse) {
  // Install happened before the lock time.
  constexpr uint64_t kEpochOffset = 116444736000000000ULL;
  int64_t installUnixSec = 1700000000;
  uint64_t installFileTime =
      uint64_t(installUnixSec) * PR_USEC_PER_SEC * 10 + kEpochOffset;
  PRTime lockTime = PRTime(1701388800) * PR_MSEC_PER_SEC;

  if (!WriteInstallTelemetryJson(installFileTime)) return;

  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Some(lockTime), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  EXPECT_TRUE(payload.isMember("isNewInstall"));
  EXPECT_FALSE(payload["isNewInstall"].asBool());
}

TEST_F(DowngradePingTest, IsNewInstallAbsentWithoutLockTime) {
  constexpr uint64_t kEpochOffset = 116444736000000000ULL;
  int64_t installUnixSec = 1701388800;
  uint64_t installFileTime =
      uint64_t(installUnixSec) * PR_USEC_PER_SEC * 10 + kEpochOffset;

  if (!WriteInstallTelemetryJson(installFileTime)) return;

  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Nothing(), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  EXPECT_FALSE(payload.isMember("isNewInstall"));
}
#else
TEST_F(DowngradePingTest, IsNewInstallNotPresentOnNonWindows) {
  PRTime lockTime = PRTime(1700000000) * PR_MSEC_PER_SEC;

  auto result = GenerateDowngradeTelemetry(
      "test-ping-id"_ns, "131.0_20250201000000/20250201000000"_ns, false, 0,
      "test-channel"_ns, "default"_ns, mozilla::Some(lockTime), false);
  ASSERT_TRUE(result);
  mCreatedPingPath = *result;

  nsCString contents;
  ASSERT_TRUE(ReadPingFile(*result, contents));

  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(
      reader.parse(contents.BeginReading(), contents.EndReading(), root));

  Json::Value payload = root["payload"];
  EXPECT_FALSE(payload.isMember("isNewInstall"));
  EXPECT_FALSE(payload.isMember("profileType"));
}
#endif
