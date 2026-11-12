/*
 *  Copyright 2011 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/ssl_identity.h"

#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/digest.h>
#else
#include <openssl/evp.h>  // IWYU pragma: keep
#endif
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/fake_ssl_identity.h"
#include "rtc_base/logging.h"
#include "rtc_base/message_digest.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/ssl_fingerprint.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

constexpr char kTestCertificate[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIB6TCCAVICAQYwDQYJKoZIhvcNAQEEBQAwWzELMAkGA1UEBhMCQVUxEzARBgNV\n"
    "BAgTClF1ZWVuc2xhbmQxGjAYBgNVBAoTEUNyeXB0U29mdCBQdHkgTHRkMRswGQYD\n"
    "VQQDExJUZXN0IENBICgxMDI0IGJpdCkwHhcNMDAxMDE2MjIzMTAzWhcNMDMwMTE0\n"
    "MjIzMTAzWjBjMQswCQYDVQQGEwJBVTETMBEGA1UECBMKUXVlZW5zbGFuZDEaMBgG\n"
    "A1UEChMRQ3J5cHRTb2Z0IFB0eSBMdGQxIzAhBgNVBAMTGlNlcnZlciB0ZXN0IGNl\n"
    "cnQgKDUxMiBiaXQpMFwwDQYJKoZIhvcNAQEBBQADSwAwSAJBAJ+zw4Qnlf8SMVIP\n"
    "Fe9GEcStgOY2Ww/dgNdhjeD8ckUJNP5VZkVDTGiXav6ooKXfX3j/7tdkuD8Ey2//\n"
    "Kv7+ue0CAwEAATANBgkqhkiG9w0BAQQFAAOBgQCT0grFQeZaqYb5EYfk20XixZV4\n"
    "GmyAbXMftG1Eo7qGiMhYzRwGNWxEYojf5PZkYZXvSqZ/ZXHXa4g59jK/rJNnaVGM\n"
    "k+xIX8mxQvlV0n5O9PIha5BX5teZnkHKgL8aKKLKW1BK7YTngsfSzzaeame5iKfz\n"
    "itAE+OjGF+PFKbwX8Q==\n"
    "-----END CERTIFICATE-----\n";

constexpr unsigned char kTestCertSha1[] = {
    0xA6, 0xC8, 0x59, 0xEA, 0xC3, 0x7E, 0x6D, 0x33, 0xCF, 0xE2,
    0x69, 0x9D, 0x74, 0xE6, 0xF6, 0x8A, 0x9E, 0x47, 0xA7, 0xCA};
constexpr unsigned char kTestCertSha224[] = {
    0xd4, 0xce, 0xc6, 0xcf, 0x28, 0xcb, 0xe9, 0x77, 0x38, 0x36,
    0xcf, 0xb1, 0x3b, 0x4a, 0xd7, 0xbd, 0xae, 0x24, 0x21, 0x08,
    0xcf, 0x6a, 0x44, 0x0d, 0x3f, 0x94, 0x2a, 0x5b};
constexpr unsigned char kTestCertSha256[] = {
    0x41, 0x6b, 0xb4, 0x93, 0x47, 0x79, 0x77, 0x24, 0x77, 0x0b, 0x8b,
    0x2e, 0xa6, 0x2b, 0xe0, 0xf9, 0x0a, 0xed, 0x1f, 0x31, 0xa6, 0xf7,
    0x5c, 0xa1, 0x5a, 0xc4, 0xb0, 0xa2, 0xa4, 0x78, 0xb9, 0x76};
constexpr unsigned char kTestCertSha384[] = {
    0x42, 0x31, 0x9a, 0x79, 0x1d, 0xd6, 0x08, 0xbf, 0x3b, 0xba, 0x36, 0xd8,
    0x37, 0x4a, 0x9a, 0x75, 0xd3, 0x25, 0x6e, 0x28, 0x92, 0xbe, 0x06, 0xb7,
    0xc5, 0xa0, 0x83, 0xe3, 0x86, 0xb1, 0x03, 0xfc, 0x64, 0x47, 0xd6, 0xd8,
    0xaa, 0xd9, 0x36, 0x60, 0x04, 0xcc, 0xbe, 0x7d, 0x6a, 0xe8, 0x34, 0x49};
constexpr unsigned char kTestCertSha512[] = {
    0x51, 0x1d, 0xec, 0x02, 0x3d, 0x51, 0x45, 0xd3, 0xd8, 0x1d, 0xa4,
    0x9d, 0x43, 0xc9, 0xee, 0x32, 0x6f, 0x4f, 0x37, 0xee, 0xab, 0x3f,
    0x25, 0xdf, 0x72, 0xfc, 0x61, 0x1a, 0xd5, 0x92, 0xff, 0x6b, 0x28,
    0x71, 0x58, 0xb3, 0xe1, 0x8a, 0x18, 0xcf, 0x61, 0x33, 0x0e, 0x14,
    0xc3, 0x04, 0xaa, 0x07, 0xf6, 0xa5, 0xda, 0xdc, 0x42, 0x42, 0x22,
    0x35, 0xce, 0x26, 0x58, 0x4a, 0x33, 0x6d, 0xbc, 0xb6};

// These PEM strings were created by generating an identity with
// `SSLIdentity::Create` and invoking `identity->PrivateKeyToPEMString()`,
// `identity->PublicKeyToPEMString()` and
// `identity->certificate().ToPEMString()`. If the crypto library is updated,
// and the update changes the string form of the keys, these will have to be
// updated too.  The fingerprint, fingerprint algorithm and base64 certificate
// were created by calling `identity->certificate().GetStats()`.
constexpr char kRSA_PRIVATE_KEY_PEM[] =
    "-----BEGIN PRI"   // Linebreak to avoid detection of private
    "VATE KEY-----\n"  // keys by linters.
    "MIICdQIBADANBgkqhkiG9w0BAQEFAASCAl8wggJbAgEAAoGBAMQPqDStRlYeDpkX\n"
    "erRmv+a1naM8vSVSY0gG2plnrnofViWRW3MRqWC+020MsIj3hPZeSAnt/y/FL/nr\n"
    "4Ea7NXcwdRo1/1xEK7U/f/cjSg1aunyvHCHwcFcMr31HLFvHr0ZgcFwbgIuFLNEl\n"
    "7kK5HMO9APz1ntUjek8BmBj8yMl9AgMBAAECgYA8FWBC5GcNtSBcIinkZyigF0A7\n"
    "6j081sa+J/uNz4xUuI257ZXM6biygUhhvuXK06/XoIULJfhyN0fAm1yb0HtNhiUs\n"
    "kMOYeon6b8FqFaPjrQf7Gr9FMiIHXNK19uegTMKztXyPZoUWlX84X0iawY95x0Y3\n"
    "73f6P2rN2UOjlVVjAQJBAOKy3l2w3Zj2w0oAJox0eMwl+RxBNt1C42SHrob2mFUT\n"
    "rytpVVYOasr8CoDI0kjacjI94sLum+buJoXXX6YTGO0CQQDdZwlYIEkoS3ftfxPa\n"
    "Ai0YTBzAWvHJg0r8Gk/TkHo6IM+LSsZ9ZYUv/vBe4BKLw1I4hZ+bQvBiq+f8ROtk\n"
    "+TDRAkAPL3ghwoU1h+IRBO2QHwUwd6K2N9AbBi4BP+168O3HVSg4ujeTKigRLMzv\n"
    "T4R2iNt5bhfQgvdCgtVlxcWMdF8JAkBwDCg3eEdt5BuyjwBt8XH+/O4ED0KUWCTH\n"
    "x00k5dZlupsuhE5Fwe4QpzXg3gekwdnHjyCCQ/NCDHvgOMTkmhQxAkA9V03KRX9b\n"
    "bhvEzY/fu8gEp+EzsER96/D79az5z1BaMGL5OPM2xHBPJATKlswnAa7Lp3QKGZGk\n"
    "TxslfL18J71s\n"
    "-----END PRIVATE KEY-----\n";
constexpr char kRSA_PUBLIC_KEY_PEM[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDED6g0rUZWHg6ZF3q0Zr/mtZ2j\n"
    "PL0lUmNIBtqZZ656H1YlkVtzEalgvtNtDLCI94T2XkgJ7f8vxS/56+BGuzV3MHUa\n"
    "Nf9cRCu1P3/3I0oNWrp8rxwh8HBXDK99Ryxbx69GYHBcG4CLhSzRJe5CuRzDvQD8\n"
    "9Z7VI3pPAZgY/MjJfQIDAQAB\n"
    "-----END PUBLIC KEY-----\n";
constexpr char kRSA_CERT_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBnDCCAQWgAwIBAgIJAOEHLgeWYwrpMA0GCSqGSIb3DQEBCwUAMBAxDjAMBgNV\n"
    "BAMMBXRlc3QxMB4XDTE2MDQyNDE4MTAyMloXDTE2MDUyNTE4MTAyMlowEDEOMAwG\n"
    "A1UEAwwFdGVzdDEwgZ8wDQYJKoZIhvcNAQEBBQADgY0AMIGJAoGBAMQPqDStRlYe\n"
    "DpkXerRmv+a1naM8vSVSY0gG2plnrnofViWRW3MRqWC+020MsIj3hPZeSAnt/y/F\n"
    "L/nr4Ea7NXcwdRo1/1xEK7U/f/cjSg1aunyvHCHwcFcMr31HLFvHr0ZgcFwbgIuF\n"
    "LNEl7kK5HMO9APz1ntUjek8BmBj8yMl9AgMBAAEwDQYJKoZIhvcNAQELBQADgYEA\n"
    "C3ehaZFl+oEYN069C2ht/gMzuC77L854RF/x7xRtNZzkcg9TVgXXdM3auUvJi8dx\n"
    "yTpU3ixErjQvoZew5ngXTEvTY8BSQUijJEaLWh8n6NDKRbEGTdAk8nPAmq9hdCFq\n"
    "e3UkexqNHm3g/VxG4NUC1Y+w29ai0/Rgh+VvgbDwK+Q=\n"
    "-----END CERTIFICATE-----\n";
constexpr char kRSA_FINGERPRINT[] =
    "3C:E8:B2:70:09:CF:A9:09:5A:F4:EF:8F:8D:8A:32:FF:EA:04:91:BA:6E:D4:17:78:16"
    ":2A:EE:F9:9A:DD:E2:2B";
constexpr char kRSA_FINGERPRINT_ALGORITHM[] = "sha-256";
constexpr char kRSA_BASE64_CERTIFICATE[] =
    "MIIBnDCCAQWgAwIBAgIJAOEHLgeWYwrpMA0GCSqGSIb3DQEBCwUAMBAxDjAMBgNVBAMMBXRlc3"
    "QxMB4XDTE2MDQyNDE4MTAyMloXDTE2MDUyNTE4MTAyMlowEDEOMAwGA1UEAwwFdGVzdDEwgZ8w"
    "DQYJKoZIhvcNAQEBBQADgY0AMIGJAoGBAMQPqDStRlYeDpkXerRmv+a1naM8vSVSY0gG2plnrn"
    "ofViWRW3MRqWC+020MsIj3hPZeSAnt/y/FL/nr4Ea7NXcwdRo1/1xEK7U/f/cjSg1aunyvHCHw"
    "cFcMr31HLFvHr0ZgcFwbgIuFLNEl7kK5HMO9APz1ntUjek8BmBj8yMl9AgMBAAEwDQYJKoZIhv"
    "cNAQELBQADgYEAC3ehaZFl+oEYN069C2ht/gMzuC77L854RF/x7xRtNZzkcg9TVgXXdM3auUvJ"
    "i8dxyTpU3ixErjQvoZew5ngXTEvTY8BSQUijJEaLWh8n6NDKRbEGTdAk8nPAmq9hdCFqe3Ukex"
    "qNHm3g/VxG4NUC1Y+w29ai0/Rgh+VvgbDwK+Q=";

constexpr char kECDSA_PRIVATE_KEY_PEM[] =
    "-----BEGIN PRI"   // Linebreak to avoid detection of private
    "VATE KEY-----\n"  // keys by linters.
    "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg/AkEA2hklq7dQ2rN\n"
    "ZxYL6hOUACL4pn7P4FYlA3ZQhIChRANCAAR7YgdO3utP/8IqVRq8G4VZKreMAxeN\n"
    "rUa12twthv4uFjuHAHa9D9oyAjncmn+xvZZRyVmKrA56jRzENcEEHoAg\n"
    "-----END PRIVATE KEY-----\n";
constexpr char kECDSA_PUBLIC_KEY_PEM[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEe2IHTt7rT//CKlUavBuFWSq3jAMX\n"
    "ja1GtdrcLYb+LhY7hwB2vQ/aMgI53Jp/sb2WUclZiqwOeo0cxDXBBB6AIA==\n"
    "-----END PUBLIC KEY-----\n";
constexpr char kECDSA_CERT_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBFDCBu6ADAgECAgkArpkxjw62sW4wCgYIKoZIzj0EAwIwEDEOMAwGA1UEAwwF\n"
    "dGVzdDMwHhcNMTYwNDI0MTgxNDM4WhcNMTYwNTI1MTgxNDM4WjAQMQ4wDAYDVQQD\n"
    "DAV0ZXN0MzBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABHtiB07e60//wipVGrwb\n"
    "hVkqt4wDF42tRrXa3C2G/i4WO4cAdr0P2jICOdyaf7G9llHJWYqsDnqNHMQ1wQQe\n"
    "gCAwCgYIKoZIzj0EAwIDSAAwRQIhANyreQ/K5yuPPpirsd0e/4WGLHou6bIOSQks\n"
    "DYzo56NmAiAKOr3u8ol3LmygbUCwEvtWrS8QcJDygxHPACo99hkekw==\n"
    "-----END CERTIFICATE-----\n";
constexpr char kECDSA_FINGERPRINT[] =
    "9F:47:FA:88:76:3D:18:B8:00:A0:59:9D:C3:5D:34:0B:1F:B8:99:9E:68:DA:F3:A5:DA"
    ":50:33:A9:FF:4D:31:89";
constexpr char kECDSA_FINGERPRINT_ALGORITHM[] = "sha-256";
constexpr char kECDSA_BASE64_CERTIFICATE[] =
    "MIIBFDCBu6ADAgECAgkArpkxjw62sW4wCgYIKoZIzj0EAwIwEDEOMAwGA1UEAwwFdGVzdDMwHh"
    "cNMTYwNDI0MTgxNDM4WhcNMTYwNTI1MTgxNDM4WjAQMQ4wDAYDVQQDDAV0ZXN0MzBZMBMGByqG"
    "SM49AgEGCCqGSM49AwEHA0IABHtiB07e60//wipVGrwbhVkqt4wDF42tRrXa3C2G/i4WO4cAdr"
    "0P2jICOdyaf7G9llHJWYqsDnqNHMQ1wQQegCAwCgYIKoZIzj0EAwIDSAAwRQIhANyreQ/K5yuP"
    "Ppirsd0e/4WGLHou6bIOSQksDYzo56NmAiAKOr3u8ol3LmygbUCwEvtWrS8QcJDygxHPACo99h"
    "kekw==";

struct IdentityAndInfo {
  std::unique_ptr<SSLIdentity> identity;
  std::vector<std::string> ders;
  std::vector<std::string> pems;
  std::vector<std::string> fingerprints;
};

IdentityAndInfo CreateFakeIdentityAndInfoFromDers(
    const std::vector<std::string>& ders) {
  RTC_CHECK(!ders.empty());
  IdentityAndInfo info;
  info.ders = ders;
  for (const std::string& der : ders) {
    info.pems.push_back(SSLIdentity::DerToPem(
        "CERTIFICATE", reinterpret_cast<const unsigned char*>(der.c_str()),
        der.length()));
  }
  info.identity.reset(new FakeSSLIdentity(info.pems));
  // Strip header/footer and newline characters of PEM strings.
  for (size_t i = 0; i < info.pems.size(); ++i) {
    absl::StrReplaceAll({{"-----BEGIN CERTIFICATE-----", ""},
                         {"-----END CERTIFICATE-----", ""},
                         {"\n", ""}},
                        &info.pems[i]);
  }
  // Fingerprints for the whole certificate chain, starting with leaf
  // certificate.
  const SSLCertChain& chain = info.identity->cert_chain();
  std::unique_ptr<SSLFingerprint> fp;
  for (size_t i = 0; i < chain.GetSize(); i++) {
    fp = SSLFingerprint::Create("sha-1", chain.Get(i));
    EXPECT_TRUE(fp);
    info.fingerprints.push_back(fp->GetRfc4572Fingerprint());
  }
  EXPECT_EQ(info.ders.size(), info.fingerprints.size());
  return info;
}

class SSLIdentityTest : public ::testing::Test {
 public:
  void SetUp() override {
    identity_rsa1_ = SSLIdentity::Create("test1", KT_RSA);
    identity_rsa2_ = SSLIdentity::Create("test2", KT_RSA);
    identity_ecdsa1_ = SSLIdentity::Create("test3", KT_ECDSA);
    identity_ecdsa2_ = SSLIdentity::Create("test4", KT_ECDSA);

    ASSERT_TRUE(identity_rsa1_);
    ASSERT_TRUE(identity_rsa2_);
    ASSERT_TRUE(identity_ecdsa1_);
    ASSERT_TRUE(identity_ecdsa2_);

    test_cert_ = SSLCertificate::FromPEMString(kTestCertificate);
    ASSERT_TRUE(test_cert_);
  }

  void TestGetSignatureDigestAlgorithm() {
    std::string digest_algorithm;

    ASSERT_TRUE(identity_rsa1_->certificate().GetSignatureDigestAlgorithm(
        &digest_algorithm));
    ASSERT_EQ(DIGEST_SHA_256, digest_algorithm);

    ASSERT_TRUE(identity_rsa2_->certificate().GetSignatureDigestAlgorithm(
        &digest_algorithm));
    ASSERT_EQ(DIGEST_SHA_256, digest_algorithm);

    ASSERT_TRUE(identity_ecdsa1_->certificate().GetSignatureDigestAlgorithm(
        &digest_algorithm));
    ASSERT_EQ(DIGEST_SHA_256, digest_algorithm);

    ASSERT_TRUE(identity_ecdsa2_->certificate().GetSignatureDigestAlgorithm(
        &digest_algorithm));
    ASSERT_EQ(DIGEST_SHA_256, digest_algorithm);

    // The test certificate has an MD5-based signature.
    ASSERT_TRUE(test_cert_->GetSignatureDigestAlgorithm(&digest_algorithm));
    ASSERT_EQ(DIGEST_MD5, digest_algorithm);
  }

  void TestDigestHelper(Buffer& digest,
                        const SSLIdentity* identity,
                        absl::string_view algorithm,
                        size_t expected_len) {
    digest.EnsureCapacity(expected_len);
    digest.Clear();
    EXPECT_TRUE(identity->certificate().ComputeDigest(algorithm, digest));
    EXPECT_EQ(expected_len, digest.size());

    // Repeat digest computation for the identity as a sanity check.
    Buffer digest1 = Buffer::CreateWithCapacity(MessageDigest::kMaxSize);
    std::memset(digest1.data(), 0xff, expected_len);
    EXPECT_TRUE(identity->certificate().ComputeDigest(algorithm, digest1));
    EXPECT_EQ(expected_len, digest1.size());

    EXPECT_EQ(digest, digest1);
  }

  void TestDigestForGeneratedCert(absl::string_view algorithm,
                                  size_t expected_len) {
    std::array<Buffer, 4> digests;

    TestDigestHelper(digests[0], identity_rsa1_.get(), algorithm, expected_len);
    TestDigestHelper(digests[1], identity_rsa2_.get(), algorithm, expected_len);
    TestDigestHelper(digests[2], identity_ecdsa1_.get(), algorithm,
                     expected_len);
    TestDigestHelper(digests[3], identity_ecdsa2_.get(), algorithm,
                     expected_len);

    // Sanity check that all four digests are unique.  This could theoretically
    // fail, since cryptographic hash collisions have a non-zero probability.
    for (size_t i = 0; i < digests.size(); i++) {
      for (size_t j = 0; j < digests.size(); j++) {
        if (i != j)
          EXPECT_NE(digests[i], digests[j]);
      }
    }
  }

  void TestDigestForFixedCert(absl::string_view algorithm,
                              size_t expected_len,
                              const unsigned char* expected_digest) {
    Buffer digest(Buffer::CreateWithCapacity(MessageDigest::kMaxSize));

    ASSERT_TRUE(expected_len <= digest.capacity());

    EXPECT_TRUE(test_cert_->ComputeDigest(algorithm, digest));
    EXPECT_EQ(expected_len, digest.size());
    EXPECT_EQ(0, memcmp(digest.data(), expected_digest, expected_len));
  }

  void TestCloningIdentity(const SSLIdentity& identity) {
    // Convert `identity` to PEM strings and create a new identity by converting
    // back from the string format.
    std::string priv_pem = identity.PrivateKeyToPEMString();
    std::string publ_pem = identity.PublicKeyToPEMString();
    std::string cert_pem = identity.certificate().ToPEMString();
    std::unique_ptr<SSLIdentity> clone =
        SSLIdentity::CreateFromPEMStrings(priv_pem, cert_pem);
    EXPECT_TRUE(clone);

    // Make sure the clone is identical to the original.
    EXPECT_TRUE(identity == *clone);
    ASSERT_EQ(identity.certificate().CertificateExpirationTime(),
              clone->certificate().CertificateExpirationTime());

    // At this point we are confident that the identities are identical. To be
    // extra sure, we compare PEM strings of the clone with the original. Note
    // that the PEM strings of two identities are not strictly guaranteed to be
    // equal (they describe structs whose members could be listed in a different
    // order, for example). But because the same function is used to produce
    // both PEMs, its a good enough bet that this comparison will work. If the
    // assumption stops holding in the future we can always remove this from the
    // unittest.
    std::string clone_priv_pem = clone->PrivateKeyToPEMString();
    std::string clone_publ_pem = clone->PublicKeyToPEMString();
    std::string clone_cert_pem = clone->certificate().ToPEMString();
    ASSERT_EQ(priv_pem, clone_priv_pem);
    ASSERT_EQ(publ_pem, clone_publ_pem);
    ASSERT_EQ(cert_pem, clone_cert_pem);
  }

 protected:
  std::unique_ptr<SSLIdentity> identity_rsa1_;
  std::unique_ptr<SSLIdentity> identity_rsa2_;
  std::unique_ptr<SSLIdentity> identity_ecdsa1_;
  std::unique_ptr<SSLIdentity> identity_ecdsa2_;
  std::unique_ptr<SSLCertificate> test_cert_;
};

TEST_F(SSLIdentityTest, FixedDigestSHA1) {
  TestDigestForFixedCert(DIGEST_SHA_1, SHA_DIGEST_LENGTH, kTestCertSha1);
}

// HASH_AlgSHA224 is not supported in the chromium linux build.
TEST_F(SSLIdentityTest, FixedDigestSHA224) {
  TestDigestForFixedCert(DIGEST_SHA_224, SHA224_DIGEST_LENGTH, kTestCertSha224);
}

TEST_F(SSLIdentityTest, FixedDigestSHA256) {
  TestDigestForFixedCert(DIGEST_SHA_256, SHA256_DIGEST_LENGTH, kTestCertSha256);
}

TEST_F(SSLIdentityTest, FixedDigestSHA384) {
  TestDigestForFixedCert(DIGEST_SHA_384, SHA384_DIGEST_LENGTH, kTestCertSha384);
}

TEST_F(SSLIdentityTest, FixedDigestSHA512) {
  TestDigestForFixedCert(DIGEST_SHA_512, SHA512_DIGEST_LENGTH, kTestCertSha512);
}

// HASH_AlgSHA224 is not supported in the chromium linux build.
TEST_F(SSLIdentityTest, DigestSHA224) {
  TestDigestForGeneratedCert(DIGEST_SHA_224, SHA224_DIGEST_LENGTH);
}

TEST_F(SSLIdentityTest, DigestSHA256) {
  TestDigestForGeneratedCert(DIGEST_SHA_256, SHA256_DIGEST_LENGTH);
}

TEST_F(SSLIdentityTest, DigestSHA384) {
  TestDigestForGeneratedCert(DIGEST_SHA_384, SHA384_DIGEST_LENGTH);
}

TEST_F(SSLIdentityTest, DigestSHA512) {
  TestDigestForGeneratedCert(DIGEST_SHA_512, SHA512_DIGEST_LENGTH);
}

TEST_F(SSLIdentityTest, IdentityComparison) {
  EXPECT_TRUE(*identity_rsa1_ == *identity_rsa1_);
  EXPECT_FALSE(*identity_rsa1_ == *identity_rsa2_);
  EXPECT_FALSE(*identity_rsa1_ == *identity_ecdsa1_);
  EXPECT_FALSE(*identity_rsa1_ == *identity_ecdsa2_);

  EXPECT_TRUE(*identity_rsa2_ == *identity_rsa2_);
  EXPECT_FALSE(*identity_rsa2_ == *identity_ecdsa1_);
  EXPECT_FALSE(*identity_rsa2_ == *identity_ecdsa2_);

  EXPECT_TRUE(*identity_ecdsa1_ == *identity_ecdsa1_);
  EXPECT_FALSE(*identity_ecdsa1_ == *identity_ecdsa2_);
}

TEST_F(SSLIdentityTest, FromPEMStringsRSA) {
  std::unique_ptr<SSLIdentity> identity(
      SSLIdentity::CreateFromPEMStrings(kRSA_PRIVATE_KEY_PEM, kRSA_CERT_PEM));
  EXPECT_TRUE(identity);
  EXPECT_EQ(kRSA_PRIVATE_KEY_PEM, identity->PrivateKeyToPEMString());
  EXPECT_EQ(kRSA_PUBLIC_KEY_PEM, identity->PublicKeyToPEMString());
  EXPECT_EQ(kRSA_CERT_PEM, identity->certificate().ToPEMString());
}

TEST_F(SSLIdentityTest, FromPEMStringsEC) {
  std::unique_ptr<SSLIdentity> identity(SSLIdentity::CreateFromPEMStrings(
      kECDSA_PRIVATE_KEY_PEM, kECDSA_CERT_PEM));
  EXPECT_TRUE(identity);
  EXPECT_EQ(kECDSA_PRIVATE_KEY_PEM, identity->PrivateKeyToPEMString());
  EXPECT_EQ(kECDSA_PUBLIC_KEY_PEM, identity->PublicKeyToPEMString());
  EXPECT_EQ(kECDSA_CERT_PEM, identity->certificate().ToPEMString());
}

TEST_F(SSLIdentityTest, FromPEMChainStrings) {
  // This doesn't form a valid certificate chain, but that doesn't matter for
  // the purposes of the test
  std::string chain(kRSA_CERT_PEM);
  chain.append(kTestCertificate);
  std::unique_ptr<SSLIdentity> identity(
      SSLIdentity::CreateFromPEMChainStrings(kRSA_PRIVATE_KEY_PEM, chain));
  EXPECT_TRUE(identity);
  EXPECT_EQ(kRSA_PRIVATE_KEY_PEM, identity->PrivateKeyToPEMString());
  EXPECT_EQ(kRSA_PUBLIC_KEY_PEM, identity->PublicKeyToPEMString());
  ASSERT_EQ(2u, identity->cert_chain().GetSize());
  EXPECT_EQ(kRSA_CERT_PEM, identity->cert_chain().Get(0).ToPEMString());
  EXPECT_EQ(kTestCertificate, identity->cert_chain().Get(1).ToPEMString());
}

// Errors left in this thread's OpenSSL error queue by unrelated operations
// must not make parsing a valid chain fail.
TEST_F(SSLIdentityTest, FromPEMChainStringsWithStaleOpenSSLError) {
  const uint8_t kNotACertificate[] = {0x00};
  const uint8_t* p = kNotACertificate;
  ASSERT_EQ(d2i_X509(nullptr, &p, sizeof(kNotACertificate)), nullptr);
  ASSERT_NE(ERR_peek_error(), 0u);

  std::string chain(kRSA_CERT_PEM);
  chain.append(kTestCertificate);
  std::unique_ptr<SSLIdentity> identity(
      SSLIdentity::CreateFromPEMChainStrings(kRSA_PRIVATE_KEY_PEM, chain));
  ASSERT_NE(identity, nullptr);
  EXPECT_EQ(2u, identity->cert_chain().GetSize());
  EXPECT_EQ(ERR_peek_error(), 0u);
}

TEST_F(SSLIdentityTest, CloneIdentityRSA) {
  TestCloningIdentity(*identity_rsa1_);
  TestCloningIdentity(*identity_rsa2_);
}

TEST_F(SSLIdentityTest, CloneIdentityECDSA) {
  TestCloningIdentity(*identity_ecdsa1_);
  TestCloningIdentity(*identity_ecdsa2_);
}

TEST_F(SSLIdentityTest, PemDerConversion) {
  std::string der;
  EXPECT_TRUE(SSLIdentity::PemToDer("CERTIFICATE", kTestCertificate, &der));

  EXPECT_EQ(
      kTestCertificate,
      SSLIdentity::DerToPem("CERTIFICATE",
                            reinterpret_cast<const unsigned char*>(der.data()),
                            der.length()));
}

TEST_F(SSLIdentityTest, GetSignatureDigestAlgorithm) {
  TestGetSignatureDigestAlgorithm();
}

TEST_F(SSLIdentityTest, SSLCertificateGetStatsRSA) {
  std::unique_ptr<SSLIdentity> identity(
      SSLIdentity::CreateFromPEMStrings(kRSA_PRIVATE_KEY_PEM, kRSA_CERT_PEM));
  std::unique_ptr<SSLCertificateStats> stats =
      identity->certificate().GetStats();
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->fingerprint, kRSA_FINGERPRINT);
  EXPECT_EQ(stats->fingerprint_algorithm, kRSA_FINGERPRINT_ALGORITHM);
  EXPECT_EQ(stats->base64_certificate, kRSA_BASE64_CERTIFICATE);
  EXPECT_FALSE(stats->issuer);
}

TEST_F(SSLIdentityTest, SSLCertificateGetStatsECDSA) {
  std::unique_ptr<SSLIdentity> identity(SSLIdentity::CreateFromPEMStrings(
      kECDSA_PRIVATE_KEY_PEM, kECDSA_CERT_PEM));
  std::unique_ptr<SSLCertificateStats> stats =
      identity->certificate().GetStats();
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->fingerprint, kECDSA_FINGERPRINT);
  EXPECT_EQ(stats->fingerprint_algorithm, kECDSA_FINGERPRINT_ALGORITHM);
  EXPECT_EQ(stats->base64_certificate, kECDSA_BASE64_CERTIFICATE);
  EXPECT_FALSE(stats->issuer);
}

TEST_F(SSLIdentityTest, SSLCertificateGetStatsWithChain) {
  std::vector<std::string> ders;
  ders.push_back("every der results in");
  ders.push_back("an identity + certificate");
  ders.push_back("in a certificate chain");
  IdentityAndInfo info = CreateFakeIdentityAndInfoFromDers(ders);
  EXPECT_TRUE(info.identity);
  EXPECT_EQ(info.ders, ders);
  EXPECT_EQ(info.pems.size(), info.ders.size());
  EXPECT_EQ(info.fingerprints.size(), info.ders.size());

  std::unique_ptr<SSLCertificateStats> first_stats =
      info.identity->cert_chain().GetStats();
  SSLCertificateStats* cert_stats = first_stats.get();
  for (size_t i = 0; i < info.ders.size(); ++i) {
    EXPECT_EQ(cert_stats->fingerprint, info.fingerprints[i]);
    EXPECT_EQ(cert_stats->fingerprint_algorithm, "sha-1");
    EXPECT_EQ(cert_stats->base64_certificate, info.pems[i]);
    cert_stats = cert_stats->issuer.get();
    EXPECT_EQ(static_cast<bool>(cert_stats), i + 1 < info.ders.size());
  }
}

class SSLIdentityExpirationTest : public ::testing::Test {
 public:
  SSLIdentityExpirationTest() {
    // Set use of the test RNG to get deterministic expiration timestamp.
    SetRandomTestMode(true);
  }
  ~SSLIdentityExpirationTest() override {
    // Put it back for the next test.
    SetRandomTestMode(false);
  }

  void TestASN1TimeToSec() {
    struct asn_example {
      const char* string;
      bool long_format;
      int64_t want;
    } static const data[] = {
        // clang-format off
      // clang formatting breaks this nice alignment

      // Valid examples.
      {.string="19700101000000Z",  .long_format=true,  .want=0},
      {.string="700101000000Z",    .long_format=false, .want=0},
      {.string="19700101000001Z",  .long_format=true,  .want=1},
      {.string="700101000001Z",    .long_format=false, .want=1},
      {.string="19700101000100Z",  .long_format=true,  .want=60},
      {.string="19700101000101Z",  .long_format=true,  .want=61},
      {.string="19700101010000Z",  .long_format=true,  .want=3600},
      {.string="19700101010001Z",  .long_format=true,  .want=3601},
      {.string="19700101010100Z",  .long_format=true,  .want=3660},
      {.string="19700101010101Z",  .long_format=true,  .want=3661},
      {.string="710911012345Z",    .long_format=false, .want=53400225},
      {.string="20000101000000Z",  .long_format=true,  .want=946684800},
      {.string="20000101000000Z",  .long_format=true,  .want=946684800},
      {.string="20151130140156Z",  .long_format=true,  .want=1448892116},
      {.string="151130140156Z",    .long_format=false, .want=1448892116},
      {.string="20491231235959Z",  .long_format=true,  .want=2524607999},
      {.string="491231235959Z",    .long_format=false, .want=2524607999},
      {.string="20500101000000Z",  .long_format=true,  .want=2524607999+1},
      {.string="20700101000000Z",  .long_format=true,  .want=3155760000},
      {.string="21000101000000Z",  .long_format=true,  .want=4102444800},
      {.string="24000101000000Z",  .long_format=true,  .want=13569465600},

      // Invalid examples.
      // Long format: missing Z, X instead of Z, 0 instead of Z,
      // excess digits.
      {.string="19700101000000",    .long_format=true,  .want=-1},
      {.string="19700101000000X",   .long_format=true,  .want=-1},
      {.string="197001010000000",   .long_format=true,  .want=-1},
      {.string="1970010100000000Z", .long_format=true,  .want=-1},
      // Short format: missing Z, X instead of Z, 0 instead of Z,
      // excess digits.
      {.string="700101000000",      .long_format=false, .want=-1},
      {.string="700101000000X",     .long_format=false, .want=-1},
      {.string="7001010000000",     .long_format=false, .want=-1},
      {.string="70010100000000Z",   .long_format=false, .want=-1},
      // Invalid character.
      {.string=":9700101000000Z",   .long_format=true,  .want=-1},
      {.string="1:700101000001Z",   .long_format=true,  .want=-1},
      {.string="19:00101000100Z",   .long_format=true,  .want=-1},
      {.string="197:0101000101Z",   .long_format=true,  .want=-1},
      {.string="1970:101010000Z",   .long_format=true,  .want=-1},
      {.string="19700:01010001Z",   .long_format=true,  .want=-1},
      {.string="197001:1010100Z",   .long_format=true,  .want=-1},
      {.string="1970010:010101Z",   .long_format=true,  .want=-1},
      {.string="70010100:000Z",     .long_format=false, .want=-1},
      {.string="700101000:01Z",     .long_format=false, .want=-1},
      {.string="2000010100:000Z",   .long_format=true,  .want=-1},
      {.string="21000101000:00Z",   .long_format=true,  .want=-1},
      {.string="240001010000:0Z",   .long_format=true,  .want=-1},
      // Dates prior to unix epoch (January 1st, 1970).
      {.string="500101000000Z",     .long_format=false, .want=-1},
      {.string="691231235959Z",     .long_format=false, .want=-1},
      {.string="19611118043000Z",   .long_format=false, .want=-1},
      // clang-format off
    };

    unsigned char buf[EVP_MAX_MD_SIZE];

    // Run all examples and check for the expected result.
    for (const auto& entry : data) {
      size_t length = strlen(entry.string);
      memcpy(buf, entry.string, length);    // Copy the ASN1 string...
      buf[length] = CreateRandomId();  // ...and terminate it with junk.
      int64_t res = ASN1TimeToSec(buf, length, entry.long_format);
      RTC_LOG(LS_VERBOSE) << entry.string;
      ASSERT_EQ(entry.want, res);
    }
    // Run all examples again, but with an invalid length.
    for (const auto& entry : data) {
      size_t length = strlen(entry.string);
      memcpy(buf, entry.string, length);    // Copy the ASN1 string...
      buf[length] = CreateRandomId();  // ...and terminate it with junk.
      int64_t res = ASN1TimeToSec(buf, length - 1, entry.long_format);
      RTC_LOG(LS_VERBOSE) << entry.string;
      ASSERT_EQ(-1, res);
    }
  }

  void TestExpireTime(int times) {
    // We test just ECDSA here since what we're out to exercise is the
    // interfaces for expiration setting and reading.
    for (int i = 0; i < times; i++) {
      // We limit the time to < 2^31 here, i.e., we stay before 2038, since else
      // we hit time offset limitations in OpenSSL on some 32-bit systems.
      time_t time_before_generation = time(nullptr);
      time_t lifetime =
          CreateRandomId() % (0x80000000 - time_before_generation);
      KeyParams key_params = KeyParams::ECDSA(EC_NIST_P256);
      auto identity =
          SSLIdentity::Create("", key_params, lifetime);
      time_t time_after_generation = time(nullptr);
      EXPECT_LE(time_before_generation + lifetime,
                identity->certificate().CertificateExpirationTime());
      EXPECT_GE(time_after_generation + lifetime,
                identity->certificate().CertificateExpirationTime());
    }
  }
};

TEST_F(SSLIdentityExpirationTest, TestASN1TimeToSec) {
  TestASN1TimeToSec();
}

TEST_F(SSLIdentityExpirationTest, TestExpireTime) {
  TestExpireTime(500);
}

}  // namespace
}  // namespace webrtc
