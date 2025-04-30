/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*****************************************************************************/
/* This is an automatically generated file. If you're not                    */
/* PublicKeyPinningService.cpp, you shouldn't be #including it.              */
/*****************************************************************************/
#include <stdint.h>
/* AffirmTrust Commercial */
static const char kAffirmTrust_CommercialFingerprint[] =
  "bEZLmlsjOl6HTadlwm8EUBDS3c/0V5TwtMfkqvpQFJU=";

/* AffirmTrust Networking */
static const char kAffirmTrust_NetworkingFingerprint[] =
  "lAcq0/WPcPkwmOWl9sBMlscQvYSdgxhJGa6Q64kK5AA=";

/* AffirmTrust Premium */
static const char kAffirmTrust_PremiumFingerprint[] =
  "x/Q7TPW3FWgpT4IrU3YmBfbd0Vyt7Oc56eLDy6YenWc=";

/* AffirmTrust Premium ECC */
static const char kAffirmTrust_Premium_ECCFingerprint[] =
  "MhmwkRT/SVo+tusAwu/qs0ACrl8KVsdnnqCHo/oDfk8=";

/* Baltimore CyberTrust Root */
static const char kBaltimore_CyberTrust_RootFingerprint[] =
  "Y9mvm0exBk1JoQ57f9Vm28jKo5lFm/woKcVxrYxu80o=";

/* COMODO Certification Authority */
static const char kCOMODO_Certification_AuthorityFingerprint[] =
  "AG1751Vd2CAmRCxPGieoDomhmJy4ezREjtIZTBgZbV4=";

/* COMODO ECC Certification Authority */
static const char kCOMODO_ECC_Certification_AuthorityFingerprint[] =
  "58qRu/uxh4gFezqAcERupSkRYBlBAvfcw7mEjGPLnNU=";

/* COMODO RSA Certification Authority */
static const char kCOMODO_RSA_Certification_AuthorityFingerprint[] =
  "grX4Ta9HpZx6tSHkmCrvpApTQGo67CYDnvprLg5yRME=";

/* Comodo AAA Services root */
static const char kComodo_AAA_Services_rootFingerprint[] =
  "vRU+17BDT2iGsXvOi76E7TQMcTLXAqj0+jGPdW7L1vM=";

/* DigiCert Assured ID Root CA */
static const char kDigiCert_Assured_ID_Root_CAFingerprint[] =
  "I/Lt/z7ekCWanjD0Cvj5EqXls2lOaThEA0H2Bg4BT/o=";

/* DigiCert Assured ID Root G2 */
static const char kDigiCert_Assured_ID_Root_G2Fingerprint[] =
  "8ca6Zwz8iOTfUpc8rkIPCgid1HQUT+WAbEIAZOFZEik=";

/* DigiCert Assured ID Root G3 */
static const char kDigiCert_Assured_ID_Root_G3Fingerprint[] =
  "Fe7TOVlLME+M+Ee0dzcdjW/sYfTbKwGvWJ58U7Ncrkw=";

/* DigiCert Global Root CA */
static const char kDigiCert_Global_Root_CAFingerprint[] =
  "r/mIkG3eEpVdm+u/ko/cwxzOMo1bk4TyHIlByibiA5E=";

/* DigiCert Global Root G2 */
static const char kDigiCert_Global_Root_G2Fingerprint[] =
  "i7WTqTvh0OioIruIfFR4kMPnBqrS2rdiVPl/s2uC/CY=";

/* DigiCert Global Root G3 */
static const char kDigiCert_Global_Root_G3Fingerprint[] =
  "uUwZgwDOxcBXrQcntwu+kYFpkiVkOaezL0WYEZ3anJc=";

/* DigiCert High Assurance EV Root CA */
static const char kDigiCert_High_Assurance_EV_Root_CAFingerprint[] =
  "WoiWRyIOVNa9ihaBciRSC7XHjliYS9VwUGOIud4PB18=";

/* DigiCert TLS ECC P384 Root G5 */
static const char kDigiCert_TLS_ECC_P384_Root_G5Fingerprint[] =
  "oC+voZLIy4HLE0FVT5wFtxzKKokLDRKY1oNkfJYe+98=";

/* DigiCert TLS RSA4096 Root G5 */
static const char kDigiCert_TLS_RSA4096_Root_G5Fingerprint[] =
  "ape1HIIZ6T5d7GS61YBs3rD4NVvkfnVwELcCRW4Bqv0=";

/* DigiCert Trusted Root G4 */
static const char kDigiCert_Trusted_Root_G4Fingerprint[] =
  "Wd8xe/qfTwq3ylFNd3IpaqLHZbh2ZNCLluVzmeNkcpw=";

/* End Entity Test Cert */
static const char kEnd_Entity_Test_CertFingerprint[] =
  "VCIlmPM9NkgFQtrs4Oa5TeFcDu6MWRTKSNdePEhOgD8=";

/* Entrust Root Certification Authority */
static const char kEntrust_Root_Certification_AuthorityFingerprint[] =
  "bb+uANN7nNc/j7R95lkXrwDg3d9C286sIMF8AnXuIJU=";

/* Entrust Root Certification Authority - EC1 */
static const char kEntrust_Root_Certification_Authority___EC1Fingerprint[] =
  "/qK31kX7pz11PB7Jp4cMQOH3sMVh6Se5hb9xGGbjbyI=";

/* Entrust Root Certification Authority - G2 */
static const char kEntrust_Root_Certification_Authority___G2Fingerprint[] =
  "du6FkDdMcVQ3u8prumAo6t3i3G27uMP2EOhR8R0at/U=";

/* Entrust.net Premium 2048 Secure Server CA */
static const char kEntrust_net_Premium_2048_Secure_Server_CAFingerprint[] =
  "HqPF5D7WbC2imDpCpKebHpBnhs6fG1hiFBmgBGOofTg=";

/* FacebookBackup */
static const char kFacebookBackupFingerprint[] =
  "q4PO2G2cbkZhZ82+JgmRUyGMoAeozA+BSXVXQWB8XWQ=";

/* GOOGLE_PIN_DigiCertECCSecureServerCA */
static const char kGOOGLE_PIN_DigiCertECCSecureServerCAFingerprint[] =
  "PZXN3lRAy+8tBKk2Ox6F7jIlnzr2Yzmwqc3JnyfXoCw=";

/* GOOGLE_PIN_SymantecClass3EVG3 */
static const char kGOOGLE_PIN_SymantecClass3EVG3Fingerprint[] =
  "gMxWOrX4PMQesK9qFNbYBxjBfjUvlkn/vN1n+L9lE5E=";

/* GTS Root R1 */
static const char kGTS_Root_R1Fingerprint[] =
  "hxqRlPTu1bMS/0DITB1SSu0vd4u/8l8TjPgfaAp63Gc=";

/* GTS Root R2 */
static const char kGTS_Root_R2Fingerprint[] =
  "Vfd95BwDeSQo+NUYxVEEIlvkOlWY2SalKK1lPhzOx78=";

/* GTS Root R3 */
static const char kGTS_Root_R3Fingerprint[] =
  "QXnt2YHvdHR3tJYmQIr0Paosp6t/nggsEGD4QJZ3Q0g=";

/* GTS Root R4 */
static const char kGTS_Root_R4Fingerprint[] =
  "mEflZT5enoR1FuXLgYYGqnVEoZvmf9c2bVBpiOjYQ0c=";

/* GlobalSign ECC Root CA - R4 */
static const char kGlobalSign_ECC_Root_CA___R4Fingerprint[] =
  "CLOmM1/OXvSPjw5UOYbAf9GKOxImEp9hhku9W90fHMk=";

/* GlobalSign ECC Root CA - R5 */
static const char kGlobalSign_ECC_Root_CA___R5Fingerprint[] =
  "fg6tdrtoGdwvVFEahDVPboswe53YIFjqbABPAdndpd8=";

/* GlobalSign Root CA */
static const char kGlobalSign_Root_CAFingerprint[] =
  "K87oWBWM9UZfyddvDfoxL+8lpNyoUB2ptGtn0fv6G2Q=";

/* GlobalSign Root CA - R3 */
static const char kGlobalSign_Root_CA___R3Fingerprint[] =
  "cGuxAXyFXFkWm61cF4HPWX8S0srS9j0aSqN0k4AP+4A=";

/* GlobalSign Root CA - R6 */
static const char kGlobalSign_Root_CA___R6Fingerprint[] =
  "aCdH+LpiG4fN07wpXtXKvOciocDANj0daLOJKNJ4fx4=";

/* GlobalSign Root R46 */
static const char kGlobalSign_Root_R46Fingerprint[] =
  "rn+WLLnmp9v3uDP7GPqbcaiRdd+UnCMrap73yz3yu/w=";

/* Go Daddy Class 2 CA */
static const char kGo_Daddy_Class_2_CAFingerprint[] =
  "VjLZe/p3W/PJnd6lL8JVNBCGQBZynFLdZSTIqcO0SJ8=";

/* Go Daddy Root Certificate Authority - G2 */
static const char kGo_Daddy_Root_Certificate_Authority___G2Fingerprint[] =
  "Ko8tivDrEjiY90yGasP6ZpBU4jwXvHqVvQI0GS3GNdA=";

/* GoogleBackup2048 */
static const char kGoogleBackup2048Fingerprint[] =
  "IPMbDAjLVSGntGO3WP53X/zilCVndez5YJ2+vJvhJsA=";

/* ISRG Root X1 */
static const char kISRG_Root_X1Fingerprint[] =
  "C5+lpZ7tcVwmwQIMcRtPbsQtWLABXhQzejna0wHFr8M=";

/* ISRG Root X2 */
static const char kISRG_Root_X2Fingerprint[] =
  "diGVwiVYbubAI3RW4hB9xU8e/CH2GnkuvVFZE8zmgzI=";

/* Starfield Class 2 CA */
static const char kStarfield_Class_2_CAFingerprint[] =
  "FfFKxFycfaIz00eRZOgTf+Ne4POK6FgYPwhBDqgqxLQ=";

/* Starfield Root Certificate Authority - G2 */
static const char kStarfield_Root_Certificate_Authority___G2Fingerprint[] =
  "gI1os/q0iEpflxrOfRBVDXqVoWN3Tz7Dav/7IT++THQ=";

/* TestSPKI */
static const char kTestSPKIFingerprint[] =
  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

/* USERTrust ECC Certification Authority */
static const char kUSERTrust_ECC_Certification_AuthorityFingerprint[] =
  "ICGRfpgmOUXIWcQ/HXPLQTkFPEFPoDyjvH7ohhQpjzs=";

/* USERTrust RSA Certification Authority */
static const char kUSERTrust_RSA_Certification_AuthorityFingerprint[] =
  "x4QzPSC810K5/cMjb05Qm4k3Bw5zBn4lTdO/nEW/Td4=";

/* Pinsets are each an ordered list by the actual value of the fingerprint */
struct StaticFingerprints {
  // See bug 1338873 about making these fields const.
  size_t size;
  const char* const* data;
};

/* PreloadedHPKPins.json pinsets */
static const char* const kPinset_google_root_pems_Data[] = {
  kEntrust_Root_Certification_Authority___EC1Fingerprint,
  kCOMODO_ECC_Certification_AuthorityFingerprint,
  kDigiCert_Assured_ID_Root_G2Fingerprint,
  kCOMODO_Certification_AuthorityFingerprint,
  kGlobalSign_ECC_Root_CA___R4Fingerprint,
  kDigiCert_Assured_ID_Root_G3Fingerprint,
  kStarfield_Class_2_CAFingerprint,
  kEntrust_net_Premium_2048_Secure_Server_CAFingerprint,
  kDigiCert_Assured_ID_Root_CAFingerprint,
  kUSERTrust_ECC_Certification_AuthorityFingerprint,
  kGlobalSign_Root_CAFingerprint,
  kGo_Daddy_Root_Certificate_Authority___G2Fingerprint,
  kAffirmTrust_Premium_ECCFingerprint,
  kGTS_Root_R3Fingerprint,
  kGTS_Root_R2Fingerprint,
  kGo_Daddy_Class_2_CAFingerprint,
  kDigiCert_Trusted_Root_G4Fingerprint,
  kDigiCert_High_Assurance_EV_Root_CAFingerprint,
  kBaltimore_CyberTrust_RootFingerprint,
  kGlobalSign_Root_CA___R6Fingerprint,
  kAffirmTrust_CommercialFingerprint,
  kEntrust_Root_Certification_AuthorityFingerprint,
  kGlobalSign_Root_CA___R3Fingerprint,
  kEntrust_Root_Certification_Authority___G2Fingerprint,
  kGlobalSign_ECC_Root_CA___R5Fingerprint,
  kStarfield_Root_Certificate_Authority___G2Fingerprint,
  kCOMODO_RSA_Certification_AuthorityFingerprint,
  kGTS_Root_R1Fingerprint,
  kDigiCert_Global_Root_G2Fingerprint,
  kAffirmTrust_NetworkingFingerprint,
  kGTS_Root_R4Fingerprint,
  kDigiCert_Global_Root_CAFingerprint,
  kDigiCert_Global_Root_G3Fingerprint,
  kComodo_AAA_Services_rootFingerprint,
  kAffirmTrust_PremiumFingerprint,
  kUSERTrust_RSA_Certification_AuthorityFingerprint,
};
static const StaticFingerprints kPinset_google_root_pems = {
  sizeof(kPinset_google_root_pems_Data) / sizeof(const char*),
  kPinset_google_root_pems_Data
};

static const char* const kPinset_mozilla_services_Data[] = {
  kISRG_Root_X1Fingerprint,
  kDigiCert_High_Assurance_EV_Root_CAFingerprint,
  kDigiCert_TLS_RSA4096_Root_G5Fingerprint,
  kDigiCert_Global_Root_G2Fingerprint,
  kDigiCert_TLS_ECC_P384_Root_G5Fingerprint,
  kDigiCert_Global_Root_CAFingerprint,
};
static const StaticFingerprints kPinset_mozilla_services = {
  sizeof(kPinset_mozilla_services_Data) / sizeof(const char*),
  kPinset_mozilla_services_Data
};

static const char* const kPinset_mozilla_test_Data[] = {
  kEnd_Entity_Test_CertFingerprint,
};
static const StaticFingerprints kPinset_mozilla_test = {
  sizeof(kPinset_mozilla_test_Data) / sizeof(const char*),
  kPinset_mozilla_test_Data
};

/* Chrome static pinsets */
static const char* const kPinset_test_Data[] = {
  kTestSPKIFingerprint,
};
static const StaticFingerprints kPinset_test = {
  sizeof(kPinset_test_Data) / sizeof(const char*),
  kPinset_test_Data
};

static const char* const kPinset_google_Data[] = {
  kGlobalSign_ECC_Root_CA___R4Fingerprint,
  kGoogleBackup2048Fingerprint,
  kGTS_Root_R3Fingerprint,
  kGTS_Root_R2Fingerprint,
  kGTS_Root_R1Fingerprint,
  kGTS_Root_R4Fingerprint,
};
static const StaticFingerprints kPinset_google = {
  sizeof(kPinset_google_Data) / sizeof(const char*),
  kPinset_google_Data
};

static const char* const kPinset_facebook_Data[] = {
  kCOMODO_ECC_Certification_AuthorityFingerprint,
  kISRG_Root_X1Fingerprint,
  kUSERTrust_ECC_Certification_AuthorityFingerprint,
  kGlobalSign_Root_CAFingerprint,
  kGOOGLE_PIN_DigiCertECCSecureServerCAFingerprint,
  kDigiCert_Trusted_Root_G4Fingerprint,
  kDigiCert_High_Assurance_EV_Root_CAFingerprint,
  kGlobalSign_Root_CA___R6Fingerprint,
  kDigiCert_TLS_RSA4096_Root_G5Fingerprint,
  kGlobalSign_Root_CA___R3Fingerprint,
  kISRG_Root_X2Fingerprint,
  kGOOGLE_PIN_SymantecClass3EVG3Fingerprint,
  kCOMODO_RSA_Certification_AuthorityFingerprint,
  kDigiCert_Global_Root_G2Fingerprint,
  kDigiCert_TLS_ECC_P384_Root_G5Fingerprint,
  kFacebookBackupFingerprint,
  kDigiCert_Global_Root_CAFingerprint,
  kGlobalSign_Root_R46Fingerprint,
  kDigiCert_Global_Root_G3Fingerprint,
  kUSERTrust_RSA_Certification_AuthorityFingerprint,
};
static const StaticFingerprints kPinset_facebook = {
  sizeof(kPinset_facebook_Data) / sizeof(const char*),
  kPinset_facebook_Data
};

/* Domainlist */
struct TransportSecurityPreload {
  // See bug 1338873 about making these fields const.
  const char* mHost;
  bool mIncludeSubdomains;
  bool mTestMode;
  bool mIsMoz;
  int32_t mId;
  const StaticFingerprints* pinset;
};

/* Sort hostnames for binary search. */
static const TransportSecurityPreload kPublicKeyPinningPreloadList[] = {
  { "2mdn.net", true, false, false, -1, &kPinset_google_root_pems },
  { "accounts.firefox.com", true, false, true, 4, &kPinset_mozilla_services },
  { "accounts.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "addons.mozilla.net", true, false, true, 2, &kPinset_mozilla_services },
  { "addons.mozilla.org", true, false, true, 1, &kPinset_mozilla_services },
  { "admin.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "android.com", true, false, false, -1, &kPinset_google_root_pems },
  { "api.accounts.firefox.com", true, false, true, 5, &kPinset_mozilla_services },
  { "apis.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "appengine.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "apps.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "aus4.mozilla.org", true, true, true, 3, &kPinset_mozilla_services },
  { "aus5.mozilla.org", true, true, true, 7, &kPinset_mozilla_services },
  { "blogger.com", true, false, false, -1, &kPinset_google_root_pems },
  { "blogspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "bugs.chromium.org", true, false, false, -1, &kPinset_google_root_pems },
  { "build.chromium.org", true, false, false, -1, &kPinset_google_root_pems },
  { "business.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "calendar.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "cdn.ampproject.org", true, false, false, -1, &kPinset_google_root_pems },
  { "cdn.mozilla.net", true, false, true, 16, &kPinset_mozilla_services },
  { "cdn.mozilla.org", true, false, true, 17, &kPinset_mozilla_services },
  { "checkout.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "chrome-devtools-frontend.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "chrome.com", true, false, false, -1, &kPinset_google_root_pems },
  { "chrome.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "chromereporting-pa.googleapis.com", true, false, false, -1, &kPinset_google_root_pems },
  { "chromiumbugs.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "chromiumcodereview.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "classroom.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "cloud.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "code.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "code.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "codereview.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "codereview.chromium.org", true, false, false, -1, &kPinset_google_root_pems },
  { "contributor.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "corp.goog", true, false, false, -1, &kPinset_google_root_pems },
  { "crash-reports-xpsp2.mozilla.com", false, false, true, 11, &kPinset_mozilla_services },
  { "crash-reports.mozilla.com", false, false, true, 10, &kPinset_mozilla_services },
  { "crash-stats.mozilla.org", false, false, true, 12, &kPinset_mozilla_services },
  { "crbug.com", true, false, false, -1, &kPinset_google_root_pems },
  { "crosbug.com", true, false, false, -1, &kPinset_google_root_pems },
  { "crossmediapanel.com", true, false, false, -1, &kPinset_google_root_pems },
  { "crrev.com", true, false, false, -1, &kPinset_google_root_pems },
  { "datastudio.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "developer.android.com", true, false, false, -1, &kPinset_google_root_pems },
  { "developers.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "dl.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "dns.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "docs.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "domains.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "doubleclick.net", true, false, false, -1, &kPinset_google_root_pems },
  { "download.mozilla.org", false, false, true, 14, &kPinset_mozilla_services },
  { "drive.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "encrypted.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "example.test", true, true, false, -1, &kPinset_test },
  { "exclude-subdomains.pinning.example.com", false, false, false, -1, &kPinset_mozilla_test },
  { "facebook.com", true, false, false, -1, &kPinset_facebook },
  { "fi.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "firebaseio.com", true, false, false, -1, &kPinset_google_root_pems },
  { "firefox.com", true, true, true, 15, &kPinset_mozilla_services },
  { "g.co", false, false, false, -1, &kPinset_google_root_pems },
  { "g4w.co", true, false, false, -1, &kPinset_google_root_pems },
  { "ggpht.com", true, false, false, -1, &kPinset_google_root_pems },
  { "glass.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "gmail.com", false, false, false, -1, &kPinset_google_root_pems },
  { "goo.gl", true, false, false, -1, &kPinset_google_root_pems },
  { "google", true, false, false, -1, &kPinset_google_root_pems },
  { "google-analytics.com", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ac", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ad", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ae", true, false, false, -1, &kPinset_google_root_pems },
  { "google.af", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ag", true, false, false, -1, &kPinset_google_root_pems },
  { "google.al", true, false, false, -1, &kPinset_google_root_pems },
  { "google.am", true, false, false, -1, &kPinset_google_root_pems },
  { "google.as", true, false, false, -1, &kPinset_google_root_pems },
  { "google.at", true, false, false, -1, &kPinset_google_root_pems },
  { "google.az", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ba", true, false, false, -1, &kPinset_google_root_pems },
  { "google.be", true, false, false, -1, &kPinset_google_root_pems },
  { "google.bf", true, false, false, -1, &kPinset_google_root_pems },
  { "google.bg", true, false, false, -1, &kPinset_google_root_pems },
  { "google.bi", true, false, false, -1, &kPinset_google_root_pems },
  { "google.bj", true, false, false, -1, &kPinset_google_root_pems },
  { "google.bs", true, false, false, -1, &kPinset_google_root_pems },
  { "google.bt", true, false, false, -1, &kPinset_google_root_pems },
  { "google.by", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ca", true, false, false, -1, &kPinset_google_root_pems },
  { "google.cat", true, false, false, -1, &kPinset_google_root_pems },
  { "google.cc", true, false, false, -1, &kPinset_google_root_pems },
  { "google.cd", true, false, false, -1, &kPinset_google_root_pems },
  { "google.cf", true, false, false, -1, &kPinset_google_root_pems },
  { "google.cg", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ch", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ci", true, false, false, -1, &kPinset_google_root_pems },
  { "google.cl", true, false, false, -1, &kPinset_google_root_pems },
  { "google.cm", true, false, false, -1, &kPinset_google_root_pems },
  { "google.cn", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.ao", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.bw", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.ck", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.cr", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.hu", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.id", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.il", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.im", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.in", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.je", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.jp", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.ke", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.kr", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.ls", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.ma", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.mz", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.nz", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.th", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.tz", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.ug", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.uk", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.uz", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.ve", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.vi", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.za", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.zm", true, false, false, -1, &kPinset_google_root_pems },
  { "google.co.zw", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.af", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.ag", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.ai", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.ar", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.au", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.bd", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.bh", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.bn", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.bo", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.br", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.by", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.bz", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.cn", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.co", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.cu", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.cy", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.do", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.ec", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.eg", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.et", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.fj", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.ge", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.gh", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.gi", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.gr", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.gt", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.hk", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.iq", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.jm", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.jo", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.kh", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.kw", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.lb", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.ly", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.mm", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.mt", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.mx", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.my", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.na", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.nf", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.ng", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.ni", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.np", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.nr", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.om", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.pa", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.pe", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.pg", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.ph", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.pk", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.pl", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.pr", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.py", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.qa", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.ru", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.sa", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.sb", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.sg", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.sl", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.sv", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.tj", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.tn", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.tr", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.tw", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.ua", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.uy", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.vc", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.ve", true, false, false, -1, &kPinset_google_root_pems },
  { "google.com.vn", true, false, false, -1, &kPinset_google_root_pems },
  { "google.cv", true, false, false, -1, &kPinset_google_root_pems },
  { "google.cz", true, false, false, -1, &kPinset_google_root_pems },
  { "google.de", true, false, false, -1, &kPinset_google_root_pems },
  { "google.dj", true, false, false, -1, &kPinset_google_root_pems },
  { "google.dk", true, false, false, -1, &kPinset_google_root_pems },
  { "google.dm", true, false, false, -1, &kPinset_google_root_pems },
  { "google.dz", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ee", true, false, false, -1, &kPinset_google_root_pems },
  { "google.es", true, false, false, -1, &kPinset_google_root_pems },
  { "google.eus", true, false, false, -1, &kPinset_google_root_pems },
  { "google.fi", true, false, false, -1, &kPinset_google_root_pems },
  { "google.fm", true, false, false, -1, &kPinset_google_root_pems },
  { "google.fr", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ga", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ge", true, false, false, -1, &kPinset_google_root_pems },
  { "google.gg", true, false, false, -1, &kPinset_google_root_pems },
  { "google.gl", true, false, false, -1, &kPinset_google_root_pems },
  { "google.gm", true, false, false, -1, &kPinset_google_root_pems },
  { "google.gp", true, false, false, -1, &kPinset_google_root_pems },
  { "google.gr", true, false, false, -1, &kPinset_google_root_pems },
  { "google.gy", true, false, false, -1, &kPinset_google_root_pems },
  { "google.hk", true, false, false, -1, &kPinset_google_root_pems },
  { "google.hn", true, false, false, -1, &kPinset_google_root_pems },
  { "google.hr", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ht", true, false, false, -1, &kPinset_google_root_pems },
  { "google.hu", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ie", true, false, false, -1, &kPinset_google_root_pems },
  { "google.im", true, false, false, -1, &kPinset_google_root_pems },
  { "google.info", true, false, false, -1, &kPinset_google_root_pems },
  { "google.iq", true, false, false, -1, &kPinset_google_root_pems },
  { "google.is", true, false, false, -1, &kPinset_google_root_pems },
  { "google.it", true, false, false, -1, &kPinset_google_root_pems },
  { "google.it.ao", true, false, false, -1, &kPinset_google_root_pems },
  { "google.je", true, false, false, -1, &kPinset_google_root_pems },
  { "google.jo", true, false, false, -1, &kPinset_google_root_pems },
  { "google.jobs", true, false, false, -1, &kPinset_google_root_pems },
  { "google.jp", true, false, false, -1, &kPinset_google_root_pems },
  { "google.kg", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ki", true, false, false, -1, &kPinset_google_root_pems },
  { "google.kz", true, false, false, -1, &kPinset_google_root_pems },
  { "google.la", true, false, false, -1, &kPinset_google_root_pems },
  { "google.li", true, false, false, -1, &kPinset_google_root_pems },
  { "google.lk", true, false, false, -1, &kPinset_google_root_pems },
  { "google.lt", true, false, false, -1, &kPinset_google_root_pems },
  { "google.lu", true, false, false, -1, &kPinset_google_root_pems },
  { "google.lv", true, false, false, -1, &kPinset_google_root_pems },
  { "google.md", true, false, false, -1, &kPinset_google_root_pems },
  { "google.me", true, false, false, -1, &kPinset_google_root_pems },
  { "google.mg", true, false, false, -1, &kPinset_google_root_pems },
  { "google.mk", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ml", true, false, false, -1, &kPinset_google_root_pems },
  { "google.mn", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ms", true, false, false, -1, &kPinset_google_root_pems },
  { "google.mu", true, false, false, -1, &kPinset_google_root_pems },
  { "google.mv", true, false, false, -1, &kPinset_google_root_pems },
  { "google.mw", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ne", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ne.jp", true, false, false, -1, &kPinset_google_root_pems },
  { "google.net", true, false, false, -1, &kPinset_google_root_pems },
  { "google.nl", true, false, false, -1, &kPinset_google_root_pems },
  { "google.no", true, false, false, -1, &kPinset_google_root_pems },
  { "google.nr", true, false, false, -1, &kPinset_google_root_pems },
  { "google.nu", true, false, false, -1, &kPinset_google_root_pems },
  { "google.off.ai", true, false, false, -1, &kPinset_google_root_pems },
  { "google.pk", true, false, false, -1, &kPinset_google_root_pems },
  { "google.pl", true, false, false, -1, &kPinset_google_root_pems },
  { "google.pn", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ps", true, false, false, -1, &kPinset_google_root_pems },
  { "google.pt", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ro", true, false, false, -1, &kPinset_google_root_pems },
  { "google.rs", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ru", true, false, false, -1, &kPinset_google_root_pems },
  { "google.rw", true, false, false, -1, &kPinset_google_root_pems },
  { "google.sc", true, false, false, -1, &kPinset_google_root_pems },
  { "google.se", true, false, false, -1, &kPinset_google_root_pems },
  { "google.sh", true, false, false, -1, &kPinset_google_root_pems },
  { "google.si", true, false, false, -1, &kPinset_google_root_pems },
  { "google.sk", true, false, false, -1, &kPinset_google_root_pems },
  { "google.sm", true, false, false, -1, &kPinset_google_root_pems },
  { "google.sn", true, false, false, -1, &kPinset_google_root_pems },
  { "google.so", true, false, false, -1, &kPinset_google_root_pems },
  { "google.sr", true, false, false, -1, &kPinset_google_root_pems },
  { "google.st", true, false, false, -1, &kPinset_google_root_pems },
  { "google.td", true, false, false, -1, &kPinset_google_root_pems },
  { "google.tg", true, false, false, -1, &kPinset_google_root_pems },
  { "google.tk", true, false, false, -1, &kPinset_google_root_pems },
  { "google.tl", true, false, false, -1, &kPinset_google_root_pems },
  { "google.tm", true, false, false, -1, &kPinset_google_root_pems },
  { "google.tn", true, false, false, -1, &kPinset_google_root_pems },
  { "google.to", true, false, false, -1, &kPinset_google_root_pems },
  { "google.tt", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ua", true, false, false, -1, &kPinset_google_root_pems },
  { "google.us", true, false, false, -1, &kPinset_google_root_pems },
  { "google.uz", true, false, false, -1, &kPinset_google_root_pems },
  { "google.vg", true, false, false, -1, &kPinset_google_root_pems },
  { "google.vu", true, false, false, -1, &kPinset_google_root_pems },
  { "google.ws", true, false, false, -1, &kPinset_google_root_pems },
  { "googleadservices.com", true, false, false, -1, &kPinset_google_root_pems },
  { "googleapis.com", true, false, false, -1, &kPinset_google_root_pems },
  { "googlecode.com", true, false, false, -1, &kPinset_google_root_pems },
  { "googlecommerce.com", true, false, false, -1, &kPinset_google_root_pems },
  { "googlegroups.com", true, false, false, -1, &kPinset_google_root_pems },
  { "googlemail.com", false, false, false, -1, &kPinset_google_root_pems },
  { "googleplex.com", true, false, false, -1, &kPinset_google_root_pems },
  { "googlesource.com", true, false, false, -1, &kPinset_google_root_pems },
  { "googlesyndication.com", true, false, false, -1, &kPinset_google_root_pems },
  { "googletagmanager.com", true, false, false, -1, &kPinset_google_root_pems },
  { "googletagservices.com", true, false, false, -1, &kPinset_google_root_pems },
  { "googleusercontent.com", true, false, false, -1, &kPinset_google_root_pems },
  { "googlevideo.com", true, false, false, -1, &kPinset_google_root_pems },
  { "googleweblight.com", true, false, false, -1, &kPinset_google_root_pems },
  { "goto.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "groups.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "gstatic.cn", true, false, false, -1, &kPinset_google_root_pems },
  { "gstatic.com", true, false, false, -1, &kPinset_google_root_pems },
  { "gvt1.com", true, false, false, -1, &kPinset_google_root_pems },
  { "gvt2.com", true, false, false, -1, &kPinset_google_root_pems },
  { "gvt3.com", true, false, false, -1, &kPinset_google_root_pems },
  { "hangout", true, false, false, -1, &kPinset_google_root_pems },
  { "hangouts.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "history.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "hostedtalkgadget.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "inbox.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "include-subdomains.pinning.example.com", true, false, false, -1, &kPinset_mozilla_test },
  { "lens.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "login.corp.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "m.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "mail-settings.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "mail.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "market.android.com", true, false, false, -1, &kPinset_google_root_pems },
  { "mbasic.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "meet.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "messenger.com", true, false, false, -1, &kPinset_facebook },
  { "mfg-inspector.com", true, false, false, -1, &kPinset_google_root_pems },
  { "mtouch.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "myaccount.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "myactivity.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "oauthaccountmanager.googleapis.com", true, false, false, -1, &kPinset_google_root_pems },
  { "partner.android.com", true, false, false, -1, &kPinset_google_root_pems },
  { "passwords.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "passwordsleakcheck-pa.googleapis.com", true, false, false, -1, &kPinset_google_root_pems },
  { "payments.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "pinning-test.badssl.com", true, false, false, -1, &kPinset_test },
  { "pinningtest.appspot.com", true, false, false, -1, &kPinset_test },
  { "pixel.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "pixel.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "play.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "plus.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "plus.sandbox.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "profiles.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "remotedesktop.corp.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "research.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "script.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "secure.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "security.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "services.mozilla.com", true, false, true, 6, &kPinset_mozilla_services },
  { "sites.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "spreadsheets.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "static.googleadsserving.cn", true, false, false, -1, &kPinset_google_root_pems },
  { "stats.g.doubleclick.net", true, false, false, -1, &kPinset_google_root_pems },
  { "sync.services.mozilla.com", true, false, true, 13, &kPinset_mozilla_services },
  { "t.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "tablet.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "talk.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "talkgadget.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "telemetry.mozilla.org", true, true, true, 8, &kPinset_mozilla_services },
  { "test-mode.pinning.example.com", true, true, false, -1, &kPinset_mozilla_test },
  { "testpilot.firefox.com", false, false, true, 9, &kPinset_mozilla_services },
  { "touch.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "translate.googleapis.com", true, false, false, -1, &kPinset_google_root_pems },
  { "tunnel-staging.googlezip.net", true, false, false, -1, &kPinset_google_root_pems },
  { "tunnel.googlezip.net", true, false, false, -1, &kPinset_google_root_pems },
  { "ua5v.com", true, false, false, -1, &kPinset_google_root_pems },
  { "upload.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "urchin.com", true, false, false, -1, &kPinset_google_root_pems },
  { "w-spotlight.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "wallet.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "webfilings-eu-mirror.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "webfilings-eu.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "webfilings-mirror-hrd.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "webfilings.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "wf-bigsky-master.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "wf-demo-eu.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "wf-demo-hrd.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "wf-dogfood-hrd.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "wf-pentest.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "wf-staging-hr.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "wf-training-hrd.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "wf-training-master.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "wf-trial-hrd.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "withgoogle.com", true, false, false, -1, &kPinset_google_root_pems },
  { "withyoutube.com", true, false, false, -1, &kPinset_google_root_pems },
  { "www.facebook.com", true, false, false, -1, &kPinset_facebook },
  { "www.g.co", false, false, false, -1, &kPinset_google_root_pems },
  { "www.gmail.com", false, false, false, -1, &kPinset_google_root_pems },
  { "www.googlegroups.com", true, false, false, -1, &kPinset_google_root_pems },
  { "www.googlemail.com", false, false, false, -1, &kPinset_google_root_pems },
  { "www.messenger.com", true, false, false, -1, &kPinset_facebook },
  { "xbrlsuccess.appspot.com", true, false, false, -1, &kPinset_google_root_pems },
  { "xn--7xa.google.com", true, false, false, -1, &kPinset_google_root_pems },
  { "youtu.be", true, false, false, -1, &kPinset_google_root_pems },
  { "youtube-nocookie.com", true, false, false, -1, &kPinset_google_root_pems },
  { "youtube.com", true, false, false, -1, &kPinset_google_root_pems },
  { "ytimg.com", true, false, false, -1, &kPinset_google_root_pems },
};

// Pinning Preload List Length = 401;

static const int32_t kUnknownId = -1;

static const PRTime kPreloadPKPinsExpirationTime = INT64_C(1754306456863000);
