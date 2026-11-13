// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gtest/gtest.h"

#include <stdint.h>
#include <string.h>
#include <memory>

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#include "mplogic.h"
#include "mpi.h"
namespace nss_test {

void gettime(struct timespec* tp) {
#ifdef __MACH__
  clock_serv_t cclock;
  mach_timespec_t mts;

  host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock);
  clock_get_time(cclock, &mts);
  mach_port_deallocate(mach_task_self(), cclock);

  tp->tv_sec = mts.tv_sec;
  tp->tv_nsec = mts.tv_nsec;
#elif defined(__ANDROID__)
  // Bionic only has timespec_get from API level 29.
  ASSERT_EQ(0, clock_gettime(CLOCK_REALTIME, tp));
#else
  ASSERT_NE(0, timespec_get(tp, TIME_UTC));
#endif
}

class MPITest : public ::testing::Test {
 protected:
  void TestCmp(const std::string a_string, const std::string b_string,
               int result) {
    mp_int a, b;
    MP_DIGITS(&a) = 0;
    MP_DIGITS(&b) = 0;
    ASSERT_EQ(MP_OKAY, mp_init(&a));
    ASSERT_EQ(MP_OKAY, mp_init(&b));

    mp_read_radix(&a, a_string.c_str(), 16);
    mp_read_radix(&b, b_string.c_str(), 16);
    EXPECT_EQ(result, mp_cmp(&a, &b));

    mp_clear(&a);
    mp_clear(&b);
  }

  void TestDiv(const std::string a_string, const std::string b_string,
               const std::string result) {
    mp_int a, b, c;
    MP_DIGITS(&a) = 0;
    MP_DIGITS(&b) = 0;
    MP_DIGITS(&c) = 0;
    ASSERT_EQ(MP_OKAY, mp_init(&a));
    ASSERT_EQ(MP_OKAY, mp_init(&b));
    ASSERT_EQ(MP_OKAY, mp_init(&c));

    mp_read_radix(&a, a_string.c_str(), 16);
    mp_read_radix(&b, b_string.c_str(), 16);
    mp_read_radix(&c, result.c_str(), 16);
    EXPECT_EQ(MP_OKAY, mp_div(&a, &b, &a, &b));
    EXPECT_EQ(0, mp_cmp(&a, &c));

    mp_clear(&a);
    mp_clear(&b);
    mp_clear(&c);
  }

  void dump(const std::string& prefix, const uint8_t* buf, size_t len) {
    auto flags = std::cerr.flags();
    std::cerr << prefix << ": [" << std::dec << len << "] ";
    for (size_t i = 0; i < len; ++i) {
      std::cerr << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(buf[i]);
    }
    std::cerr << std::endl << std::resetiosflags(flags);
  }

  void TestToFixedOctets(const std::vector<uint8_t>& ref, size_t len) {
    mp_int a;
    ASSERT_EQ(MP_OKAY, mp_init(&a));
    ASSERT_EQ(MP_OKAY, mp_read_unsigned_octets(&a, ref.data(), ref.size()));
    std::unique_ptr<uint8_t[]> buf(new uint8_t[len]);
    ASSERT_NE(buf, nullptr);
    ASSERT_EQ(MP_OKAY, mp_to_fixlen_octets(&a, buf.get(), len));
    size_t compare;
    if (len > ref.size()) {
      for (size_t i = 0; i < len - ref.size(); ++i) {
        ASSERT_EQ(0U, buf[i]) << "index " << i << " should be zero";
      }
      compare = ref.size();
    } else {
      compare = len;
    }
    dump("value", ref.data(), ref.size());
    dump("output", buf.get(), len);
    ASSERT_EQ(0, memcmp(buf.get() + len - compare,
                        ref.data() + ref.size() - compare, compare))
        << "comparing " << compare << " octets";
    mp_clear(&a);
  }
};

TEST_F(MPITest, MpiCmp01Test) { TestCmp("0", "1", -1); }
TEST_F(MPITest, MpiCmp10Test) { TestCmp("1", "0", 1); }
TEST_F(MPITest, MpiCmp00Test) { TestCmp("0", "0", 0); }
TEST_F(MPITest, MpiCmp11Test) { TestCmp("1", "1", 0); }
TEST_F(MPITest, MpiDiv32ErrorTest) {
  TestDiv("FFFF00FFFFFFFF000000000000", "FFFF00FFFFFFFFFF", "FFFFFFFFFF");
}

#ifdef NSS_X64
// This tests assumes 64-bit mp_digits.
TEST_F(MPITest, MpiCmpUnalignedTest) {
  mp_int a, b, c;
  MP_DIGITS(&a) = 0;
  MP_DIGITS(&b) = 0;
  MP_DIGITS(&c) = 0;
  ASSERT_EQ(MP_OKAY, mp_init(&a));
  ASSERT_EQ(MP_OKAY, mp_init(&b));
  ASSERT_EQ(MP_OKAY, mp_init(&c));

  mp_read_radix(&a, "ffffffffffffffff3b4e802b4e1478", 16);
  mp_read_radix(&b, "ffffffffffffffff3b4e802b4e1478", 16);
  EXPECT_EQ(0, mp_cmp(&a, &b));

  // Now change a and b such that they contain the same numbers but are not
  // aligned.
  // a = ffffffffffffff|ff3b4e802b4e1478
  // b = ffffffffffffffff|3b4e802b4e1478
  MP_DIGITS(&b)[0] &= 0x00ffffffffffffff;
  MP_DIGITS(&b)[1] = 0xffffffffffffffff;
  EXPECT_EQ(-1, mp_cmp(&a, &b));

  ASSERT_EQ(MP_OKAY, mp_sub(&a, &b, &c));
  char c_tmp[40];
  ASSERT_EQ(MP_OKAY, mp_toradix(&c, c_tmp, 16));
  ASSERT_TRUE(strncmp(c_tmp, "feffffffffffffff100000000000000", 31));

  mp_clear(&a);
  mp_clear(&b);
  mp_clear(&c);
}
#endif

// The two follow tests ensure very similar mp_set_* functions are ok.
TEST_F(MPITest, MpiSetUlong) {
  mp_int a, b, c;
  MP_DIGITS(&a) = 0;
  MP_DIGITS(&b) = 0;
  MP_DIGITS(&c) = 0;
  ASSERT_EQ(MP_OKAY, mp_init(&a));
  ASSERT_EQ(MP_OKAY, mp_init(&b));
  ASSERT_EQ(MP_OKAY, mp_init(&c));
  EXPECT_EQ(MP_OKAY, mp_set_ulong(&a, 1));
  EXPECT_EQ(MP_OKAY, mp_set_ulong(&b, 0));
  EXPECT_EQ(MP_OKAY, mp_set_ulong(&c, -1));

  mp_clear(&a);
  mp_clear(&b);
  mp_clear(&c);
}

TEST_F(MPITest, MpiSetInt) {
  mp_int a, b, c;
  MP_DIGITS(&a) = 0;
  MP_DIGITS(&b) = 0;
  MP_DIGITS(&c) = 0;
  ASSERT_EQ(MP_OKAY, mp_init(&a));
  ASSERT_EQ(MP_OKAY, mp_init(&b));
  ASSERT_EQ(MP_OKAY, mp_init(&c));
  EXPECT_EQ(MP_OKAY, mp_set_int(&a, 1));
  EXPECT_EQ(MP_OKAY, mp_set_int(&b, 0));
  EXPECT_EQ(MP_OKAY, mp_set_int(&c, -1));

  mp_clear(&a);
  mp_clear(&b);
  mp_clear(&c);
}

TEST_F(MPITest, MpiFixlenOctetsZero) {
  std::vector<uint8_t> zero = {0};
  TestToFixedOctets(zero, 1);
  TestToFixedOctets(zero, 2);
  TestToFixedOctets(zero, sizeof(mp_digit));
  TestToFixedOctets(zero, sizeof(mp_digit) + 1);
}

TEST_F(MPITest, MpiRadixSizeNeg) {
  char* str;
  mp_int a;
  mp_err rv;
  const char* negative_edge =
      "-5400000000000000003000000002200020090919017007777777777870000090"
      "00000000007500443416610000000000000000000000000000000000000000000"
      "00000000000000000000000000000000000000000000000000000000075049054"
      "18610000800555594485440016000031555550000000000000000220030200909"
      "19017007777777700000000000000000000000000000000000000000000000000"
      "00000000000500000000000000000000000000004668129841661000071000000"
      "00000000000000000000000000000000000000000000000007504434166100000"
      "00000000000000000000000000000000000000000000000000000000000000000"
      "00000000075049054186100008005555944854400184572169555500000000000"
      "0000022003020090919017007777777700000000000000000000";

  rv = mp_init(&a);
  ASSERT_EQ(MP_OKAY, rv);
  rv = mp_read_variable_radix(&a, negative_edge, 10);
  ASSERT_EQ(MP_OKAY, rv);

  const int radixSize = mp_radix_size(&a, 10);
  ASSERT_LE(0, radixSize);

  str = (char*)malloc(radixSize);
  ASSERT_NE(nullptr, str);
  rv = mp_toradix(&a, str, 10);
  ASSERT_EQ(MP_OKAY, rv);
  ASSERT_EQ(0, strcmp(negative_edge, str));
  free(str);
  mp_clear(&a);
}

TEST_F(MPITest, MpiFixlenOctetsVarlen) {
  std::vector<uint8_t> packed;
  for (size_t i = 0; i < sizeof(mp_digit) * 2; ++i) {
    packed.push_back(0xa4);  // Any non-zero value will do.
    TestToFixedOctets(packed, packed.size());
    TestToFixedOctets(packed, packed.size() + 1);
    TestToFixedOctets(packed, packed.size() + sizeof(mp_digit));
  }
}

TEST_F(MPITest, MpiFixlenOctetsTooSmall) {
  uint8_t buf[sizeof(mp_digit) * 3];
  std::vector<uint8_t> ref;
  for (size_t i = 0; i < sizeof(mp_digit) * 2; i++) {
    ref.push_back(3);  // Any non-zero value will do.
    dump("ref", ref.data(), ref.size());

    mp_int a;
    ASSERT_EQ(MP_OKAY, mp_init(&a));
    ASSERT_EQ(MP_OKAY, mp_read_unsigned_octets(&a, ref.data(), ref.size()));
#ifndef DEBUG
    EXPECT_EQ(MP_BADARG, mp_to_fixlen_octets(&a, buf, ref.size() - 1));
#elif GTEST_HAS_DEATH_TEST
    // ARGCHK maps to assert() in a debug build. gtest has no death tests on
    // some platforms (iOS), so this goes unchecked there.
    EXPECT_DEATH(mp_to_fixlen_octets(&a, buf, ref.size() - 1), "");
#endif
    ASSERT_EQ(MP_OKAY, mp_to_fixlen_octets(&a, buf, ref.size()));
    ASSERT_EQ(0, memcmp(buf, ref.data(), ref.size()));

    mp_clear(&a);
  }
}

TEST_F(MPITest, MpiSqrMulClamp) {
  mp_int a, r, expect;
  MP_DIGITS(&a) = 0;
  MP_DIGITS(&r) = 0;
  MP_DIGITS(&expect) = 0;

  // Comba32 result is 64 mp_digits. *=2 as this is an ascii representation.
  std::string expect_str((64 * sizeof(mp_digit)) * 2, '0');

  // Set second-highest bit (0x80...^2 == 0x4000...)
  expect_str.replace(0, 1, "4", 1);

  // Test 32, 16, 8, and 4-1 mp_digit values. 32-4 (powers of two) use the comba
  // assembly implementation, if enabled and supported. 3-1 use non-comba.
  int n_digits = 32;
  while (n_digits > 0) {
    ASSERT_EQ(MP_OKAY, mp_init(&r));
    ASSERT_EQ(MP_OKAY, mp_init(&a));
    ASSERT_EQ(MP_OKAY, mp_init(&expect));
    ASSERT_EQ(MP_OKAY, mp_read_radix(&expect, expect_str.c_str(), 16));

    ASSERT_EQ(MP_OKAY, mp_set_int(&a, 1));
    ASSERT_EQ(MP_OKAY, mpl_lsh(&a, &a, (n_digits * sizeof(mp_digit) * 8) - 1));

    ASSERT_EQ(MP_OKAY, mp_sqr(&a, &r));
    EXPECT_EQ(MP_USED(&expect), MP_USED(&r));
    EXPECT_EQ(0, mp_cmp(&r, &expect));
    mp_clear(&r);

    // Take the mul path...
    ASSERT_EQ(MP_OKAY, mp_init(&r));
    ASSERT_EQ(MP_OKAY, mp_mul(&a, &a, &r));
    EXPECT_EQ(MP_USED(&expect), MP_USED(&r));
    EXPECT_EQ(0, mp_cmp(&r, &expect));

    mp_clear(&a);
    mp_clear(&r);
    mp_clear(&expect);

    // Once we're down to 4, check non-powers of two.
    int sub = n_digits > 4 ? n_digits / 2 : 1;
    n_digits -= sub;

    // "Shift right" the string (to avoid mutating |expect_str| with MPI).
    expect_str.resize(expect_str.size() - 2 * 2 * sizeof(mp_digit) * sub);
  }
}

TEST_F(MPITest, MpiInvModLoop) {
  mp_int a;
  mp_int m;
  mp_int c_actual;
  mp_int c_expect;
  MP_DIGITS(&a) = 0;
  MP_DIGITS(&m) = 0;
  MP_DIGITS(&c_actual) = 0;
  MP_DIGITS(&c_expect) = 0;
  ASSERT_EQ(MP_OKAY, mp_init(&a));
  ASSERT_EQ(MP_OKAY, mp_init(&m));
  ASSERT_EQ(MP_OKAY, mp_init(&c_actual));
  ASSERT_EQ(MP_OKAY, mp_init(&c_expect));
  mp_read_radix(&a,
                "3e10b9f4859fb9e8150cc0d94e83ef428d655702a0b6fb1e684f4755eb6be6"
                "5ac6048cdfc533f73a9bad76125801051f",
                16);
  mp_read_radix(&m,
                "ffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372d"
                "df581a0db248b0a77aecec196accc52973",
                16);
  mp_read_radix(&c_expect,
                "12302214814361c15ab6c0f2131150af186099f8c22f6c9d6e77ad496b551c"
                "7c8039e61098bfe2af66474420659435c6",
                16);

  int rv = mp_invmod(&a, &m, &c_actual);
  ASSERT_EQ(MP_OKAY, rv);

  rv = mp_cmp(&c_actual, &c_expect);
  EXPECT_EQ(0, rv);

  mp_clear(&a);
  mp_clear(&m);
  mp_clear(&c_actual);
  mp_clear(&c_expect);
}

// This test is slow. Disable it by default so we can run these tests on CI.
class DISABLED_MPITest : public ::testing::Test {};

TEST_F(DISABLED_MPITest, MpiCmpConstTest) {
  mp_int a, b, c;
  MP_DIGITS(&a) = 0;
  MP_DIGITS(&b) = 0;
  MP_DIGITS(&c) = 0;
  ASSERT_EQ(MP_OKAY, mp_init(&a));
  ASSERT_EQ(MP_OKAY, mp_init(&b));
  ASSERT_EQ(MP_OKAY, mp_init(&c));

  mp_read_radix(
      &a,
      const_cast<char*>(
          "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551"),
      16);
  mp_read_radix(
      &b,
      const_cast<char*>(
          "FF0FFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551"),
      16);
  mp_read_radix(
      &c,
      const_cast<char*>(
          "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632550"),
      16);

#ifdef CT_VERIF
  mp_taint(&b);
  mp_taint(&c);
#endif

  uint32_t runs = 5000000;
  uint32_t time_b = 0, time_c = 0;
  for (uint32_t i = 0; i < runs; ++i) {
    struct timespec start, end;
    gettime(&start);
    int r = mp_cmp(&a, &b);
    gettime(&end);
    unsigned long long used = end.tv_sec * 1000000000L + end.tv_nsec;
    used -= static_cast<unsigned long long>(start.tv_sec * 1000000000L +
                                            start.tv_nsec);
    time_b += used;
    ASSERT_EQ(1, r);
  }
  printf("time b: %u\n", time_b / runs);

  for (uint32_t i = 0; i < runs; ++i) {
    struct timespec start, end;
    gettime(&start);
    int r = mp_cmp(&a, &c);
    gettime(&end);
    unsigned long long used = end.tv_sec * 1000000000L + end.tv_nsec;
    used -= static_cast<unsigned long long>(start.tv_sec * 1000000000L +
                                            start.tv_nsec);
    time_c += used;
    ASSERT_EQ(1, r);
  }
  printf("time c: %u\n", time_c / runs);

  mp_clear(&a);
  mp_clear(&b);
  mp_clear(&c);
}

// Bug 2020188: mp_div_d with q=NULL dereferenced q unconditionally when the
// quotient was zero, causing a crash.  Use a=5, d=1000 (non-power-of-2) so
// that |a| < d and the zero-quotient branch is taken.
TEST_F(MPITest, MpiDivRemOnly) {
  mp_int a;
  mp_digit rem;
  MP_DIGITS(&a) = 0;
  ASSERT_EQ(MP_OKAY, mp_init(&a));
  mp_read_radix(&a, "5", 16);
  EXPECT_EQ(MP_OKAY, mp_div_d(&a, 1000, nullptr, &rem));
  EXPECT_EQ((mp_digit)5, rem);
  mp_clear(&a);
}

// Bug 2020188: SIGN(q) was set before s_mp_exch, making the normalization a
// no-op, so a zero quotient from a negative input would retain the NEG sign.
// Use a=-5, d=1000 (non-power-of-2) so that |a| < d.
TEST_F(MPITest, MpiDivSignNormalization) {
  mp_int a, q;
  mp_digit rem;
  MP_DIGITS(&a) = 0;
  MP_DIGITS(&q) = 0;
  ASSERT_EQ(MP_OKAY, mp_init(&a));
  ASSERT_EQ(MP_OKAY, mp_init(&q));
  mp_read_radix(&a, "5", 16);
  mp_neg(&a, &a);  // a = -5
  EXPECT_EQ(MP_OKAY, mp_div_d(&a, 1000, &q, &rem));
  EXPECT_EQ(MP_EQ, mp_cmp_z(&q));
  EXPECT_EQ((mp_sign)ZPOS, MP_SIGN(&q));
  EXPECT_EQ((mp_digit)5, rem);
  mp_clear(&a);
  mp_clear(&q);
}

}  // namespace nss_test
