/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CTTestUtils.h"

#include <stdint.h>
#include <iomanip>

#include "BTTypes.h"
#include "CTSerialization.h"
#include "gtest/gtest.h"
#include "mozpkix/Input.h"
#include "mozpkix/pkix.h"
#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixtypes.h"
#include "mozpkix/Result.h"
#include "mozpkix/pkixcheck.h"
#include "mozpkix/pkixutil.h"
#include "SignedCertificateTimestamp.h"

namespace mozilla {
namespace ct {

using namespace mozilla::pkix;

// The following test vectors are from the CT test data repository at
// https://github.com/google/certificate-transparency/tree/master/test/testdata

// test-cert.pem
const char kDefaultDerCert[] =
    "308202ca30820233a003020102020106300d06092a864886f70d01010505003055310b3009"
    "06035504061302474231243022060355040a131b4365727469666963617465205472616e73"
    "706172656e6379204341310e300c0603550408130557616c65733110300e06035504071307"
    "4572772057656e301e170d3132303630313030303030305a170d3232303630313030303030"
    "305a3052310b30090603550406130247423121301f060355040a1318436572746966696361"
    "7465205472616e73706172656e6379310e300c0603550408130557616c65733110300e0603"
    "55040713074572772057656e30819f300d06092a864886f70d010101050003818d00308189"
    "02818100b1fa37936111f8792da2081c3fe41925008531dc7f2c657bd9e1de4704160b4c9f"
    "19d54ada4470404c1c51341b8f1f7538dddd28d9aca48369fc5646ddcc7617f8168aae5b41"
    "d43331fca2dadfc804d57208949061f9eef902ca47ce88c644e000f06eeeccabdc9dd2f68a"
    "22ccb09dc76e0dbc73527765b1a37a8c676253dcc10203010001a381ac3081a9301d060355"
    "1d0e041604146a0d982a3b62c44b6d2ef4e9bb7a01aa9cb798e2307d0603551d2304763074"
    "80145f9d880dc873e654d4f80dd8e6b0c124b447c355a159a4573055310b30090603550406"
    "1302474231243022060355040a131b4365727469666963617465205472616e73706172656e"
    "6379204341310e300c0603550408130557616c65733110300e060355040713074572772057"
    "656e82010030090603551d1304023000300d06092a864886f70d010105050003818100171c"
    "d84aac414a9a030f22aac8f688b081b2709b848b4e5511406cd707fed028597a9faefc2eee"
    "2978d633aaac14ed3235197da87e0f71b8875f1ac9e78b281749ddedd007e3ecf50645f8cb"
    "f667256cd6a1647b5e13203bb8582de7d6696f656d1c60b95f456b7fcf338571908f1c6972"
    "7d24c4fccd249295795814d1dac0e6";

// key hash of test-cert.pem's issuer (ca-cert.pem)
const char kDefaultIssuerKeyHash[] =
    "02adddca08b8bf9861f035940c940156d8350fdff899a6239c6bd77255b8f8fc";

const char kDefaultDerTbsCert[] =
    "30820233a003020102020107300d06092a864886f70d01010505003055310b300906035504"
    "061302474231243022060355040a131b4365727469666963617465205472616e7370617265"
    "6e6379204341310e300c0603550408130557616c65733110300e0603550407130745727720"
    "57656e301e170d3132303630313030303030305a170d3232303630313030303030305a3052"
    "310b30090603550406130247423121301f060355040a131843657274696669636174652054"
    "72616e73706172656e6379310e300c0603550408130557616c65733110300e060355040713"
    "074572772057656e30819f300d06092a864886f70d010101050003818d0030818902818100"
    "beef98e7c26877ae385f75325a0c1d329bedf18faaf4d796bf047eb7e1ce15c95ba2f80ee4"
    "58bd7db86f8a4b252191a79bd700c38e9c0389b45cd4dc9a120ab21e0cb41cd0e72805a410"
    "cd9c5bdb5d4927726daf1710f60187377ea25b1a1e39eed0b88119dc154dc68f7da8e30caf"
    "158a33e6c9509f4a05b01409ff5dd87eb50203010001a381ac3081a9301d0603551d0e0416"
    "04142031541af25c05ffd8658b6843794f5e9036f7b4307d0603551d230476307480145f9d"
    "880dc873e654d4f80dd8e6b0c124b447c355a159a4573055310b3009060355040613024742"
    "31243022060355040a131b4365727469666963617465205472616e73706172656e63792043"
    "41310e300c0603550408130557616c65733110300e060355040713074572772057656e8201"
    "0030090603551d1304023000";

// DigitallySigned of test-cert.proof
const char kTestDigitallySigned[] =
    "0403004730450220606e10ae5c2d5a1b0aed49dc4937f48de71a4e9784e9c208dfbfe9ef53"
    "6cf7f2022100beb29c72d7d06d61d06bdb38a069469aa86fe12e18bb7cc45689a2c0187ef5"
    "a5";

// test-cert.proof
const char kTestSignedCertificateTimestamp[] =
    "00df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d7640000013d"
    "db27ded900000403004730450220606e10ae5c2d5a1b0aed49dc4937f48de71a4e9784e9c2"
    "08dfbfe9ef536cf7f2022100beb29c72d7d06d61d06bdb38a069469aa86fe12e18bb7cc456"
    "89a2c0187ef5a5";

// The signatures on the following "SCT"s are not actually valid. The intent is
// to test the parsing of extensions.
const char kTestSignedCertificateTimestampWithLeafIndexExtension[] =
    "00df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d7640000013d"
    "db27ded9"
    "0008"              // 8 bytes of extensions
    "0000050000000034"  // leaf_index of 52
    "0403004730450220606e10ae5c2d5a1b0aed49dc4937f48de71a4e9784e9c2"
    "08dfbfe9ef536cf7f2022100beb29c72d7d06d61d06bdb38a069469aa86fe12e18bb7cc456"
    "89a2c0187ef5a5";

const char kTestSignedCertificateTimestampWithTwoLeafIndexExtensions[] =
    "00df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d7640000013d"
    "db27ded9"
    "0010"              // 16 bytes of extensions
    "0000050000000034"  // leaf_index of 52
    "0000050000000051"  // leaf_index of 81
    "0403004730450220606e10ae5c2d5a1b0aed49dc4937f48de71a4e9784e9c2"
    "08dfbfe9ef536cf7f2022100beb29c72d7d06d61d06bdb38a069469aa86fe12e18bb7cc456"
    "89a2c0187ef5a5";

const char kTestSignedCertificateTimestampWithUnknownExtension[] =
    "00df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d7640000013d"
    "db27ded9"
    "0008"              // 8 bytes of extensions
    "0100050000000034"  // an (unknown) extension with id 1
    "0403004730450220606e10ae5c2d5a1b0aed49dc4937f48de71a4e9784e9c2"
    "08dfbfe9ef536cf7f2022100beb29c72d7d06d61d06bdb38a069469aa86fe12e18bb7cc456"
    "89a2c0187ef5a5";

const char kTestSignedCertificateTimestampWithUnknownAndLeafIndexExtensions[] =
    "00df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d7640000013d"
    "db27ded9"
    "0010"              // 16 bytes of extensions
    "0100050000000034"  // an (unknown) extension with id 1
    "0000050000000051"  // leaf_index of 81
    "0403004730450220606e10ae5c2d5a1b0aed49dc4937f48de71a4e9784e9c2"
    "08dfbfe9ef536cf7f2022100beb29c72d7d06d61d06bdb38a069469aa86fe12e18bb7cc456"
    "89a2c0187ef5a5";

const char kTestSignedCertificateTimestampWithTooShortExtension[] =
    "00df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d7640000013d"
    "db27ded9"
    "0008"          // 8 bytes of extensions
    "000005000034"  // 3 bytes of extension data when there should be 5
    "0403004730450220606e10ae5c2d5a1b0aed49dc4937f48de71a4e9784e9c2"
    "08dfbfe9ef536cf7f2022100beb29c72d7d06d61d06bdb38a069469aa86fe12e18bb7cc456"
    "89a2c0187ef5a5";

// ct-server-key-public.pem
const char kEcP256PublicKey[] =
    "3059301306072a8648ce3d020106082a8648ce3d0301070342000499783cb14533c0161a5a"
    "b45bf95d08a29cd0ea8dd4c84274e2be59ad15c676960cf0afa1074a57ac644b23479e5b3f"
    "b7b245eb4b420ef370210371a944beaceb";

// key id (sha256) of ct-server-key-public.pem
const char kTestKeyId[] =
    "df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d764";

// signature field of DigitallySigned from test-cert.proof
const char kTestSCTSignatureData[] =
    "30450220606e10ae5c2d5a1b0aed49dc4937f48de71a4e9784e9c208dfbfe9ef536cf7f202"
    "2100beb29c72d7d06d61d06bdb38a069469aa86fe12e18bb7cc45689a2c0187ef5a5";

// signature field of DigitallySigned from test-embedded-pre-cert.proof
const char kTestSCTPrecertSignatureData[] =
    "30450220482f6751af35dba65436be1fd6640f3dbf9a41429495924530288fa3e5e23e0602"
    "2100e4edc0db3ac572b1e2f5e8ab6a680653987dcf41027dfeffa105519d89edbf08";

// test-embedded-cert.pem
const char kTestEmbeddedCertData[] =
    "30820359308202c2a003020102020107300d06092a864886f70d01010505"
    "003055310b300906035504061302474231243022060355040a131b436572"
    "7469666963617465205472616e73706172656e6379204341310e300c0603"
    "550408130557616c65733110300e060355040713074572772057656e301e"
    "170d3132303630313030303030305a170d3232303630313030303030305a"
    "3052310b30090603550406130247423121301f060355040a131843657274"
    "69666963617465205472616e73706172656e6379310e300c060355040813"
    "0557616c65733110300e060355040713074572772057656e30819f300d06"
    "092a864886f70d010101050003818d0030818902818100beef98e7c26877"
    "ae385f75325a0c1d329bedf18faaf4d796bf047eb7e1ce15c95ba2f80ee4"
    "58bd7db86f8a4b252191a79bd700c38e9c0389b45cd4dc9a120ab21e0cb4"
    "1cd0e72805a410cd9c5bdb5d4927726daf1710f60187377ea25b1a1e39ee"
    "d0b88119dc154dc68f7da8e30caf158a33e6c9509f4a05b01409ff5dd87e"
    "b50203010001a382013a30820136301d0603551d0e041604142031541af2"
    "5c05ffd8658b6843794f5e9036f7b4307d0603551d230476307480145f9d"
    "880dc873e654d4f80dd8e6b0c124b447c355a159a4573055310b30090603"
    "5504061302474231243022060355040a131b436572746966696361746520"
    "5472616e73706172656e6379204341310e300c0603550408130557616c65"
    "733110300e060355040713074572772057656e82010030090603551d1304"
    "02300030818a060a2b06010401d679020402047c047a0078007600df1c2e"
    "c11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d76400"
    "00013ddb27df9300000403004730450220482f6751af35dba65436be1fd6"
    "640f3dbf9a41429495924530288fa3e5e23e06022100e4edc0db3ac572b1"
    "e2f5e8ab6a680653987dcf41027dfeffa105519d89edbf08300d06092a86"
    "4886f70d0101050500038181008a0c4bef099d479279afa0a28e689f91e1"
    "c4421be2d269a2ea6ca4e8215ddeddca1504a11e7c87c4b77e80f0e97903"
    "5268f27ca20e166804ae556f316981f96a394ab7abfd3e255ac0044513fe"
    "76570c6795abe4703133d303f89f3afa6bbcfc517319dfd95b934241211f"
    "634035c3d078307a68c6075a2e20c89f36b8910ca0";

const char kTestTbsCertData[] =
    "30820233a003020102020107300d06092a864886f70d0101050500305531"
    "0b300906035504061302474231243022060355040a131b43657274696669"
    "63617465205472616e73706172656e6379204341310e300c060355040813"
    "0557616c65733110300e060355040713074572772057656e301e170d3132"
    "303630313030303030305a170d3232303630313030303030305a3052310b"
    "30090603550406130247423121301f060355040a13184365727469666963"
    "617465205472616e73706172656e6379310e300c0603550408130557616c"
    "65733110300e060355040713074572772057656e30819f300d06092a8648"
    "86f70d010101050003818d0030818902818100beef98e7c26877ae385f75"
    "325a0c1d329bedf18faaf4d796bf047eb7e1ce15c95ba2f80ee458bd7db8"
    "6f8a4b252191a79bd700c38e9c0389b45cd4dc9a120ab21e0cb41cd0e728"
    "05a410cd9c5bdb5d4927726daf1710f60187377ea25b1a1e39eed0b88119"
    "dc154dc68f7da8e30caf158a33e6c9509f4a05b01409ff5dd87eb5020301"
    "0001a381ac3081a9301d0603551d0e041604142031541af25c05ffd8658b"
    "6843794f5e9036f7b4307d0603551d230476307480145f9d880dc873e654"
    "d4f80dd8e6b0c124b447c355a159a4573055310b30090603550406130247"
    "4231243022060355040a131b4365727469666963617465205472616e7370"
    "6172656e6379204341310e300c0603550408130557616c65733110300e06"
    "0355040713074572772057656e82010030090603551d1304023000";

// test-embedded-with-preca-cert.pem
const char kTestEmbeddedWithPreCaCertData[] =
    "30820359308202c2a003020102020108300d06092a864886f70d01010505"
    "003055310b300906035504061302474231243022060355040a131b436572"
    "7469666963617465205472616e73706172656e6379204341310e300c0603"
    "550408130557616c65733110300e060355040713074572772057656e301e"
    "170d3132303630313030303030305a170d3232303630313030303030305a"
    "3052310b30090603550406130247423121301f060355040a131843657274"
    "69666963617465205472616e73706172656e6379310e300c060355040813"
    "0557616c65733110300e060355040713074572772057656e30819f300d06"
    "092a864886f70d010101050003818d0030818902818100afaeeacac51ab7"
    "cebdf9eacae7dd175295e193955a17989aef8d97ab7cdff7761093c0b823"
    "d2a4e3a51a17b86f28162b66a2538935ebecdc1036233da2dd6531b0c63b"
    "cc68761ebdc854037b77399246b870a7b72b14c9b1667de09a9640ed9f3f"
    "3c725d950b4d26559869fe7f1e919a66eb76d35c0117c6bcd0d8cfd21028"
    "b10203010001a382013a30820136301d0603551d0e04160414612c64efac"
    "79b728397c9d93e6df86465fa76a88307d0603551d230476307480145f9d"
    "880dc873e654d4f80dd8e6b0c124b447c355a159a4573055310b30090603"
    "5504061302474231243022060355040a131b436572746966696361746520"
    "5472616e73706172656e6379204341310e300c0603550408130557616c65"
    "733110300e060355040713074572772057656e82010030090603551d1304"
    "02300030818a060a2b06010401d679020402047c047a0078007600df1c2e"
    "c11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d76400"
    "00013ddb27e05b000004030047304502207aa79604c47480f3727b084f90"
    "b3989f79091885e00484431a2a297cbf3a355c022100b49fd8120b0d644c"
    "d7e75269b4da6317a9356cb950224fc11cc296b2e39b2386300d06092a86"
    "4886f70d010105050003818100a3a86c41ad0088a25aedc4e7b529a2ddbf"
    "9e187ffb362157e9302d961b73b43cba0ae1e230d9e45049b7e8c924792e"
    "bbe7d175baa87b170dfad8ee788984599d05257994084e2e0e796fca5836"
    "881c3e053553e06ab230f919089b914e4a8e2da45f8a87f2c81a25a61f04"
    "fe1cace60155653827d41fad9f0658f287d058192c";

// ca-cert.pem
const char kCaCertData[] =
    "308202d030820239a003020102020100300d06092a864886f70d01010505"
    "003055310b300906035504061302474231243022060355040a131b436572"
    "7469666963617465205472616e73706172656e6379204341310e300c0603"
    "550408130557616c65733110300e060355040713074572772057656e301e"
    "170d3132303630313030303030305a170d3232303630313030303030305a"
    "3055310b300906035504061302474231243022060355040a131b43657274"
    "69666963617465205472616e73706172656e6379204341310e300c060355"
    "0408130557616c65733110300e060355040713074572772057656e30819f"
    "300d06092a864886f70d010101050003818d0030818902818100d58a6853"
    "6210a27119936e778321181c2a4013c6d07b8c76eb9157d3d0fb4b3b516e"
    "cecbd1c98d91c52f743fab635d55099cd13abaf31ae541442451a74c7816"
    "f2243cf848cf2831cce67ba04a5a23819f3cba37e624d9c3bdb299b839dd"
    "fe2631d2cb3a84fc7bb2b5c52fcfc14fff406f5cd44669cbb2f7cfdf86fb"
    "6ab9d1b10203010001a381af3081ac301d0603551d0e041604145f9d880d"
    "c873e654d4f80dd8e6b0c124b447c355307d0603551d230476307480145f"
    "9d880dc873e654d4f80dd8e6b0c124b447c355a159a4573055310b300906"
    "035504061302474231243022060355040a131b4365727469666963617465"
    "205472616e73706172656e6379204341310e300c0603550408130557616c"
    "65733110300e060355040713074572772057656e820100300c0603551d13"
    "040530030101ff300d06092a864886f70d0101050500038181000608cc4a"
    "6d64f2205e146c04b276f92b0efa94a5daf23afc3806606d3990d0a1ea23"
    "3d40295769463b046661e7fa1d179915209aea2e0a775176411227d7c003"
    "07c7470e61584fd7334224727f51d690bc47a9df354db0f6eb25955de189"
    "3c4dd5202b24a2f3e440d274b54e1bd376269ca96289b76ecaa41090e14f"
    "3b0a942e";

// intermediate-cert.pem
const char kIntermediateCertData[] =
    "308202dd30820246a003020102020109300d06092a864886f70d01010505"
    "003055310b300906035504061302474231243022060355040a131b436572"
    "7469666963617465205472616e73706172656e6379204341310e300c0603"
    "550408130557616c65733110300e060355040713074572772057656e301e"
    "170d3132303630313030303030305a170d3232303630313030303030305a"
    "3062310b30090603550406130247423131302f060355040a132843657274"
    "69666963617465205472616e73706172656e637920496e7465726d656469"
    "617465204341310e300c0603550408130557616c65733110300e06035504"
    "0713074572772057656e30819f300d06092a864886f70d01010105000381"
    "8d0030818902818100d76a678d116f522e55ff821c90642508b7074b14d7"
    "71159064f7927efdedb87135a1365ee7de18cbd5ce865f860c78f433b4d0"
    "d3d3407702e7a3ef542b1dfe9bbaa7cdf94dc5975fc729f86f105f381b24"
    "3535cf9c800f5ca780c1d3c84400ee65d16ee9cf52db8adffe50f5c49335"
    "0b2190bf50d5bc36f3cac5a8daae92cd8b0203010001a381af3081ac301d"
    "0603551d0e04160414965508050278479e8773764131bc143a47e229ab30"
    "7d0603551d230476307480145f9d880dc873e654d4f80dd8e6b0c124b447"
    "c355a159a4573055310b300906035504061302474231243022060355040a"
    "131b4365727469666963617465205472616e73706172656e637920434131"
    "0e300c0603550408130557616c65733110300e0603550407130745727720"
    "57656e820100300c0603551d13040530030101ff300d06092a864886f70d"
    "0101050500038181002206dab1c66b71dce095c3f6aa2ef72cf7761be7ab"
    "d7fc39c31a4cfe1bd96d6734ca82f22dde5a0c8bbbdd825d7b6f3e7612ad"
    "8db300a7e21169886023262284c3aa5d2191efda10bf9235d37b3a2a340d"
    "59419b94a48566f3fac3cd8b53d5a4e98270ead297b07210f9ce4a2138b1"
    "8811143b93fa4e7a87dd37e1385f2c2908";

// test-embedded-with-intermediate-cert.pem
const char kTestEmbeddedWithIntermediateCertData[] =
    "30820366308202cfa003020102020102300d06092a864886f70d01010505"
    "003062310b30090603550406130247423131302f060355040a1328436572"
    "7469666963617465205472616e73706172656e637920496e7465726d6564"
    "69617465204341310e300c0603550408130557616c65733110300e060355"
    "040713074572772057656e301e170d3132303630313030303030305a170d"
    "3232303630313030303030305a3052310b30090603550406130247423121"
    "301f060355040a13184365727469666963617465205472616e7370617265"
    "6e6379310e300c0603550408130557616c65733110300e06035504071307"
    "4572772057656e30819f300d06092a864886f70d010101050003818d0030"
    "818902818100bb272b26e5deb5459d4acca027e8f12a4d839ac3730a6a10"
    "9ff7e25498ddbd3f1895d08ba41f8de34967a3a086ce13a90dd5adbb5418"
    "4bdc08e1ac7826adb8dc9c717bfd7da5b41b4db1736e00f1dac3cec9819c"
    "cb1a28ba120b020a820e940dd61f95b5432a4bc05d0818f18ce2154eb38d"
    "2fa7d22d72b976e560db0c7fc77f0203010001a382013a30820136301d06"
    "03551d0e04160414b1b148e658e703f5f7f3105f20b3c384d7eff1bf307d"
    "0603551d23047630748014965508050278479e8773764131bc143a47e229"
    "aba159a4573055310b300906035504061302474231243022060355040a13"
    "1b4365727469666963617465205472616e73706172656e6379204341310e"
    "300c0603550408130557616c65733110300e060355040713074572772057"
    "656e82010930090603551d130402300030818a060a2b06010401d6790204"
    "02047c047a0078007600df1c2ec11500945247a96168325ddc5c7959e8f7"
    "c6d388fc002e0bbd3f74d7640000013ddb27e2a400000403004730450221"
    "00a6d34517f3392d9ec5d257adf1c597dc45bd4cd3b73856c616a9fb99e5"
    "ae75a802205e26c8d1c7e222fe8cda29baeb04a834ee97d34fd81718f1aa"
    "e0cd66f4b8a93f300d06092a864886f70d0101050500038181000f95a5b4"
    "e128a914b1e88be8b32964221b58f4558433d020a8e246cca65a40bcbf5f"
    "2d48933ebc99be6927ca756472fb0bdc7f505f41f462f2bc19d0b299c990"
    "918df8820f3d31db37979e8bad563b17f00ae67b0f8731c106c943a73bf5"
    "36af168afe21ef4adfcae19a3cc074899992bf506bc5ce1decaaf07ffeeb"
    "c805c039";

// test-embedded-with-intermediate-preca-cert.pem
const char kTestEmbeddedWithIntermediatePreCaCertData[] =
    "30820366308202cfa003020102020103300d06092a864886f70d01010505"
    "003062310b30090603550406130247423131302f060355040a1328436572"
    "7469666963617465205472616e73706172656e637920496e7465726d6564"
    "69617465204341310e300c0603550408130557616c65733110300e060355"
    "040713074572772057656e301e170d3132303630313030303030305a170d"
    "3232303630313030303030305a3052310b30090603550406130247423121"
    "301f060355040a13184365727469666963617465205472616e7370617265"
    "6e6379310e300c0603550408130557616c65733110300e06035504071307"
    "4572772057656e30819f300d06092a864886f70d010101050003818d0030"
    "818902818100d4497056cdfc65e1342cc3df6e654b8af0104702acd2275c"
    "7d3fb1fc438a89b212110d6419bcc13ae47d64bba241e6706b9ed627f8b3"
    "4a0d7dff1c44b96287c54bea9d10dc017bceb64f7b6aff3c35a474afec40"
    "38ab3640b0cd1fb0582ec03b179a2776c8c435d14ab4882d59d7b724fa37"
    "7ca6db08392173f9c6056b3abadf0203010001a382013a30820136301d06"
    "03551d0e0416041432da5518d87f1d26ea2767973c0bef286e786a4a307d"
    "0603551d23047630748014965508050278479e8773764131bc143a47e229"
    "aba159a4573055310b300906035504061302474231243022060355040a13"
    "1b4365727469666963617465205472616e73706172656e6379204341310e"
    "300c0603550408130557616c65733110300e060355040713074572772057"
    "656e82010930090603551d130402300030818a060a2b06010401d6790204"
    "02047c047a0078007600df1c2ec11500945247a96168325ddc5c7959e8f7"
    "c6d388fc002e0bbd3f74d7640000013ddb27e3be00000403004730450221"
    "00d9f61a07fee021e3159f3ca2f570d833ff01374b2096cba5658c5e16fb"
    "43eb3002200b76fe475138d8cf76833831304dabf043eb1213c96e13ff4f"
    "a37f7cd3c8dc1f300d06092a864886f70d01010505000381810088ee4e9e"
    "5eed6b112cc764b151ed929400e9406789c15fbbcfcdab2f10b400234139"
    "e6ce65c1e51b47bf7c8950f80bccd57168567954ed35b0ce9346065a5eae"
    "5bf95d41da8e27cee9eeac688f4bd343f9c2888327abd8b9f68dcb1e3050"
    "041d31bda8e2dd6d39b3664de5ce0870f5fc7e6a00d6ed00528458d953d2"
    "37586d73";

// Given the ordered set of data [ 0x00, 0x01, 0x02, deadbeef ],
// the 'inclusion proof' of the leaf of index '2' (for '0x02') is created from
// the Merkle Tree generated for that set of data.
// A Merkle inclusion proof for a leaf in a Merkle Tree is the shortest list
// of additional nodes in the Merkle Tree required to compute the Merkle Tree
// Hash (also called 'Merkle Tree head') for that tree.
// This follows the structure defined in RFC 6962-bis.
//
// https://tools.ietf.org/html/draft-ietf-trans-rfc6962-bis-24#section-2.1

const char kTestInclusionProof[] =
    "020100"            // logId
    "0000000000000004"  // tree size
    "0000000000000002"  // leaf index
    "0042"              // inclusion path length
    "2048c90c8ae24688d6bef5d48a30c2cc8b6754335a8db21793cc0a8e3bed32172"
    "9"  // node
         // hash
         // 0
    "20a20bf9a7cc2dc8a08f5f415a71b19f6ac427bab54d24eec868b5d3103449953"
    "a";  // node
          // hash
          // 1

const char kTestNodeHash0[] =
    "48c90c8ae24688d6bef5d48a30c2cc8b6754335a8db21793cc0a8e3bed321729";

const char kTestNodeHash1[] =
    "a20bf9a7cc2dc8a08f5f415a71b19f6ac427bab54d24eec868b5d3103449953a";

const char kTestInclusionProofUnexpectedData[] = "12345678";

const char kTestInclusionProofInvalidHashSize[] =
    "020100"            // logId
    "0000000000000004"  // treesize
    "0000000000000002"  // leafindex
    "0042"              // inclusion path length
    "3048c90c8ae24688d6bef5d48a30c2cc8b6754335a8db21793cc0a8e3bed32172"
    "9"  // invalid hash size
    "20a20bf9a7cc2dc8a08f5f415a71b19f6ac427bab54d24eec868b5d3103449953"
    "a";  // node hash 1

const char kTestInclusionProofInvalidHash[] =
    "020100"            // logId
    "0000000000000004"  // treesize
    "0000000000000002"  // leafindex
    "0042"              // inclusion path length
    "2048c90c8ae24688d6bef5d48a30c2cc8b6754335a8db21793cc0a8e3bed32172"
    "9"                                        // node
                                               // hash
                                               // 0
    "20a20bf9a7cc2dc8a08f5f415a71b19f6ac427";  // truncated node hash 1

const char kTestInclusionProofMissingLogId[] =
    "0000000000000004"  // treesize
    "0000000000000002"  // leafindex
    "0042"
    "2048c90c8ae24688d6bef5d48a30c2cc8b6754335a8db21793cc0a8e3bed32172"
    "9"  // node
         // hash
         // 0
    "20a20bf9a7cc2dc8a08f5f415a71b19f6ac427bab54d24eec868b5d3103449953"
    "a";  // node
          // hash
          // 1

const char kTestInclusionProofNullPathLength[] =
    "020100"
    "0000000000000004"  // treesize
    "0000000000000002"  // leafindex
    "0000"
    "2048c90c8ae24688d6bef5d48a30c2cc8b6754335a8db21793cc0a8e3bed32172"
    "9"  // node
         // hash
         // 0
    "20a20bf9a7cc2dc8a08f5f415a71b19f6ac427bab54d24eec868b5d3103449953"
    "a";  // node
          // hash
          // 1

const char kTestInclusionProofPathLengthTooSmall[] =
    "020100"
    "0000000000000004"  // treesize
    "0000000000000002"  // leafindex
    "0036"
    "2048c90c8ae24688d6bef5d48a30c2cc8b6754335a8db21793cc0a8e3bed32172"
    "9"  // node
         // hash
         // 0
    "20a20bf9a7cc2dc8a08f5f415a71b19f6ac427bab54d24eec868b5d3103449953"
    "a";  // node
          // hash
          // 1

const char kTestInclusionProofPathLengthTooLarge[] =
    "020100"
    "0000000000000004"  // treesize
    "0000000000000002"  // leafindex
    "0080"
    "2048c90c8ae24688d6bef5d48a30c2cc8b6754335a8db21793cc0a8e3bed32172"
    "9"  // node
         // hash
         // 0
    "20a20bf9a7cc2dc8a08f5f415a71b19f6ac427bab54d24eec868b5d3103449953"
    "a";  // node
          // hash
          // 1

const char kTestInclusionProofNullTreeSize[] =
    "020100"
    "0000000000000000"  // treesize
    "0000000000000002"  // leafindex
    "0042"
    "2048c90c8ae24688d6bef5d48a30c2cc8b6754335a8db21793cc0a8e3bed32172"
    "9"  // node
         // hash
         // 0
    "20a20bf9a7cc2dc8a08f5f415a71b19f6ac427bab54d24eec868b5d3103449953"
    "a";  // node
          // hash
          // 1

const char kTestInclusionProofLeafIndexOutOfBounds[] =
    "020100"
    "0000000000000004"  // treesize
    "0000000000000004"  // leafindex
    "0042"
    "2048c90c8ae24688d6bef5d48a30c2cc8b6754335a8db21793cc0a8e3bed32172"
    "9"  // node
         // hash
         // 0
    "20a20bf9a7cc2dc8a08f5f415a71b19f6ac427bab54d24eec868b5d3103449953"
    "a";  // node
          // hash
          // 1

const char kTestInclusionProofExtraData[] =
    "020100"            // logId
    "0000000000000004"  // tree size
    "0000000000000002"  // leaf index
    "0042"              // inclusion path length
    "2048c90c8ae24688d6bef5d48a30c2cc8b6754335a8db21793cc0a8e3bed32172"
    "9"  // node
         // hash
         // 0
    "20a20bf9a7cc2dc8a08f5f415a71b19f6ac427bab54d24eec868b5d3103449953"
    "a"        // node
               // hash
               // 1
    "123456";  // extra data after the proof

static uint8_t CharToByte(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  abort();
}

Buffer HexToBytes(const char* hexData) {
  size_t hexLen = strlen(hexData);
  if (!(hexLen > 0 && (hexLen % 2 == 0))) {
    abort();
  }
  size_t resultLen = hexLen / 2;
  Buffer result;
  result.reserve(resultLen);
  for (size_t i = 0; i < resultLen; ++i) {
    uint8_t hi = CharToByte(hexData[i * 2]);
    uint8_t lo = CharToByte(hexData[i * 2 + 1]);
    result.push_back((hi << 4) | lo);
  }
  return result;
}

void GetX509CertLogEntry(LogEntry& entry) {
  entry.Reset();
  entry.type = ct::LogEntry::Type::X509;
  entry.leafCertificate = HexToBytes(kDefaultDerCert);
}

Buffer GetDEREncodedX509Cert() { return HexToBytes(kDefaultDerCert); }

void GetPrecertLogEntry(LogEntry& entry) {
  entry.Reset();
  entry.type = ct::LogEntry::Type::Precert;
  entry.issuerKeyHash = HexToBytes(kDefaultIssuerKeyHash);
  entry.tbsCertificate = HexToBytes(kDefaultDerTbsCert);
}

Buffer GetTestDigitallySigned() { return HexToBytes(kTestDigitallySigned); }

Buffer GetTestDigitallySignedData() {
  Buffer encoded = GetTestDigitallySigned();
  // The encoded buffer contains the signature data itself from the 4th byte.
  // The first bytes are:
  // 1 byte of hash algorithm
  // 1 byte of signature algorithm
  // 2 bytes - prefix containing length of the signature data.
  Buffer result;
  result.assign(encoded.begin() + 4, encoded.end());
  return result;
}

Buffer GetTestSignedCertificateTimestamp() {
  return HexToBytes(kTestSignedCertificateTimestamp);
}

Buffer GetTestSignedCertificateTimestampWithLeafIndexExtension() {
  return HexToBytes(kTestSignedCertificateTimestampWithLeafIndexExtension);
}

Buffer GetTestSignedCertificateTimestampWithTwoLeafIndexExtensions() {
  return HexToBytes(kTestSignedCertificateTimestampWithTwoLeafIndexExtensions);
}

Buffer GetTestSignedCertificateTimestampWithUnknownExtension() {
  return HexToBytes(kTestSignedCertificateTimestampWithUnknownExtension);
}

Buffer GetTestSignedCertificateTimestampWithUnknownAndLeafIndexExtensions() {
  return HexToBytes(
      kTestSignedCertificateTimestampWithUnknownAndLeafIndexExtensions);
}

Buffer GetTestSignedCertificateTimestampWithTooShortExtension() {
  return HexToBytes(kTestSignedCertificateTimestampWithTooShortExtension);
}

Buffer GetTestInclusionProof() { return HexToBytes(kTestInclusionProof); }

Buffer GetTestInclusionProofUnexpectedData() {
  return HexToBytes(kTestInclusionProofUnexpectedData);
}

Buffer GetTestInclusionProofInvalidHashSize() {
  return HexToBytes(kTestInclusionProofInvalidHashSize);
}

Buffer GetTestInclusionProofInvalidHash() {
  return HexToBytes(kTestInclusionProofInvalidHash);
}

Buffer GetTestInclusionProofMissingLogId() {
  return HexToBytes(kTestInclusionProofMissingLogId);
}

Buffer GetTestInclusionProofNullPathLength() {
  return HexToBytes(kTestInclusionProofNullPathLength);
}

Buffer GetTestInclusionProofPathLengthTooSmall() {
  return HexToBytes(kTestInclusionProofPathLengthTooSmall);
}

Buffer GetTestInclusionProofPathLengthTooLarge() {
  return HexToBytes(kTestInclusionProofPathLengthTooLarge);
}

Buffer GetTestInclusionProofNullTreeSize() {
  return HexToBytes(kTestInclusionProofNullTreeSize);
}

Buffer GetTestInclusionProofLeafIndexOutOfBounds() {
  return HexToBytes(kTestInclusionProofLeafIndexOutOfBounds);
}

Buffer GetTestInclusionProofExtraData() {
  return HexToBytes(kTestInclusionProofExtraData);
}

Buffer GetTestNodeHash0() { return HexToBytes(kTestNodeHash0); }

Buffer GetTestNodeHash1() { return HexToBytes(kTestNodeHash1); }

Buffer GetTestPublicKey() { return HexToBytes(kEcP256PublicKey); }

Buffer GetTestPublicKeyId() { return HexToBytes(kTestKeyId); }

void GetX509CertSCT(SignedCertificateTimestamp& sct) {
  sct.version = ct::SignedCertificateTimestamp::Version::V1;
  sct.logId = HexToBytes(kTestKeyId);
  // Time the log issued a SCT for this certificate, which is
  // Fri Apr  5 10:04:16.089 2013
  sct.timestamp = INT64_C(1365181456089);
  sct.extensions.clear();

  sct.signature.hashAlgorithm = ct::DigitallySigned::HashAlgorithm::SHA256;
  sct.signature.signatureAlgorithm =
      ct::DigitallySigned::SignatureAlgorithm::ECDSA;
  sct.signature.signatureData = HexToBytes(kTestSCTSignatureData);
}

void GetPrecertSCT(SignedCertificateTimestamp& sct) {
  sct.version = ct::SignedCertificateTimestamp::Version::V1;
  sct.logId = HexToBytes(kTestKeyId);
  // Time the log issued a SCT for this Precertificate, which is
  // Fri Apr  5 10:04:16.275 2013
  sct.timestamp = INT64_C(1365181456275);
  sct.extensions.clear();

  sct.signature.hashAlgorithm = ct::DigitallySigned::HashAlgorithm::SHA256;
  sct.signature.signatureAlgorithm =
      ct::DigitallySigned::SignatureAlgorithm::ECDSA;
  sct.signature.signatureData = HexToBytes(kTestSCTPrecertSignatureData);
}

Buffer GetDefaultIssuerKeyHash() { return HexToBytes(kDefaultIssuerKeyHash); }

Buffer GetDEREncodedTestEmbeddedCert() {
  return HexToBytes(kTestEmbeddedCertData);
}

Buffer GetDEREncodedTestTbsCert() { return HexToBytes(kTestTbsCertData); }

Buffer GetDEREncodedTestEmbeddedWithPreCACert() {
  return HexToBytes(kTestEmbeddedWithPreCaCertData);
}

Buffer GetDEREncodedCACert() { return HexToBytes(kCaCertData); }

Buffer GetDEREncodedIntermediateCert() {
  return HexToBytes(kIntermediateCertData);
}

Buffer GetDEREncodedTestEmbeddedWithIntermediateCert() {
  return HexToBytes(kTestEmbeddedWithIntermediateCertData);
}

Buffer GetDEREncodedTestEmbeddedWithIntermediatePreCACert() {
  return HexToBytes(kTestEmbeddedWithIntermediatePreCaCertData);
}

Buffer ExtractCertSPKI(Input cert) {
  BackCert backCert(cert, EndEntityOrCA::MustBeEndEntity, nullptr);
  if (backCert.Init() != Success) {
    abort();
  }

  Input spkiInput = backCert.GetSubjectPublicKeyInfo();
  Buffer spki;
  InputToBuffer(spkiInput, spki);
  return spki;
}

Buffer ExtractCertSPKI(const Buffer& cert) {
  return ExtractCertSPKI(InputForBuffer(cert));
}

void ExtractEmbeddedSCTList(Input cert, Buffer& result) {
  result.clear();
  BackCert backCert(cert, EndEntityOrCA::MustBeEndEntity, nullptr);
  ASSERT_EQ(Success, backCert.Init());
  const Input* scts = backCert.GetSignedCertificateTimestamps();
  if (scts) {
    Input sctList;
    ASSERT_EQ(Success, ExtractSignedCertificateTimestampListFromExtension(
                           *scts, sctList));
    InputToBuffer(sctList, result);
  }
}

void ExtractEmbeddedSCTList(const Buffer& cert, Buffer& result) {
  ExtractEmbeddedSCTList(InputForBuffer(cert), result);
}

class OCSPExtensionTrustDomain : public TrustDomain {
 public:
  pkix::Result GetCertTrust(EndEntityOrCA, const CertPolicyId&, Input,
                            TrustLevel&) override {
    ADD_FAILURE();
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result FindIssuer(Input, IssuerChecker&, Time) override {
    ADD_FAILURE();
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result CheckRevocation(EndEntityOrCA, const CertID&, Time, Duration,
                               const Input*, const Input*) override {
    ADD_FAILURE();
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result IsChainValid(const DERArray&, Time,
                            const CertPolicyId&) override {
    ADD_FAILURE();
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result DigestBuf(Input item, DigestAlgorithm digestAlg,
                         /*out*/ uint8_t* digestBuf,
                         size_t digestBufLen) override {
    return DigestBufNSS(item, digestAlg, digestBuf, digestBufLen);
  }

  pkix::Result CheckSignatureDigestAlgorithm(DigestAlgorithm, EndEntityOrCA,
                                             Time) override {
    ADD_FAILURE();
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result CheckECDSACurveIsAcceptable(EndEntityOrCA, NamedCurve) override {
    ADD_FAILURE();
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result VerifyECDSASignedData(Input data,
                                     DigestAlgorithm digestAlgorithm,
                                     Input signature,
                                     Input subjectPublicKeyInfo) override {
    return VerifyECDSASignedDataNSS(data, digestAlgorithm, signature,
                                    subjectPublicKeyInfo, nullptr);
  }

  pkix::Result VerifyMLDSASignedData(Input data, Input signature,
                                     Input subjectPublicKeyInfo) override {
    return VerifyMLDSASignedDataNSS(data, signature, subjectPublicKeyInfo,
                                    nullptr);
  }

  pkix::Result CheckRSAPublicKeyModulusSizeInBits(EndEntityOrCA,
                                                  unsigned int) override {
    ADD_FAILURE();
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result VerifyRSAPKCS1SignedData(Input data,
                                        DigestAlgorithm digestAlgorithm,
                                        Input signature,
                                        Input subjectPublicKeyInfo) override {
    return VerifyRSAPKCS1SignedDataNSS(data, digestAlgorithm, signature,
                                       subjectPublicKeyInfo, nullptr);
  }

  pkix::Result VerifyRSAPSSSignedData(Input data,
                                      DigestAlgorithm digestAlgorithm,
                                      Input signature,
                                      Input subjectPublicKeyInfo) override {
    return VerifyRSAPSSSignedDataNSS(data, digestAlgorithm, signature,
                                     subjectPublicKeyInfo, nullptr);
  }

  pkix::Result CheckValidityIsAcceptable(Time, Time, EndEntityOrCA,
                                         KeyPurposeId) override {
    ADD_FAILURE();
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  void NoteAuxiliaryExtension(AuxiliaryExtension extension,
                              Input data) override {
    if (extension != AuxiliaryExtension::SCTListFromOCSPResponse) {
      ADD_FAILURE();
      return;
    }
    InputToBuffer(data, signedCertificateTimestamps);
  }

  Buffer signedCertificateTimestamps;
};

void ExtractSCTListFromOCSPResponse(Input cert, Input issuerSPKI,
                                    Input encodedResponse, Time time,
                                    Buffer& result) {
  result.clear();

  BackCert backCert(cert, EndEntityOrCA::MustBeEndEntity, nullptr);
  ASSERT_EQ(Success, backCert.Init());

  CertID certID(backCert.GetIssuer(), issuerSPKI, backCert.GetSerialNumber());

  bool expired;
  OCSPExtensionTrustDomain trustDomain;
  pkix::Result rv =
      VerifyEncodedOCSPResponse(trustDomain, certID, time, /*time*/
                                1000,                      /*maxLifetimeInDays*/
                                encodedResponse, expired);
  ASSERT_EQ(Success, rv);

  result = std::move(trustDomain.signedCertificateTimestamps);
}

Input InputForBuffer(const Buffer& buffer) {
  Input input;
  if (input.Init(buffer.data(), buffer.size()) != Success) {
    abort();
  }
  return input;
}

Input InputForSECItem(const SECItem& item) {
  Input input;
  if (input.Init(item.data, item.len) != Success) {
    abort();
  }
  return input;
}

}  // namespace ct
}  // namespace mozilla

namespace mozilla {

std::ostream& operator<<(std::ostream& stream, const ct::Buffer& buffer) {
  if (buffer.empty()) {
    stream << "EMPTY";
  } else {
    for (size_t i = 0; i < buffer.size(); ++i) {
      if (i >= 1000) {
        stream << "...";
        break;
      }
      stream << std::hex << std::setw(2) << std::setfill('0')
             << static_cast<unsigned>(buffer[i]);
    }
  }
  stream << std::dec;
  return stream;
}

}  // namespace mozilla
