// Prints Zmij's shortest output for a fixed set of doubles, one per line as
// "<bits> <upstream output> <ecmascript output>", to be compared against
// test/baseline.txt by verify-baseline.sh. Also checks that
// zmij::write_ecmascript stays within zmij::ecmascript_buffer_size.

#include <bit>
#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include "zmij/zmij.h"

using limits = std::numeric_limits<double>;

// Rounding-boundary doubles from upstream's verify.py.
static constexpr uint64_t boundary_bits[] = {
#include "zmij/test/boundary-bits.h"
};

static constexpr double interesting_values[] = {
    0.0,
    -0.0,
    limits::infinity(),
    -limits::infinity(),
    limits::quiet_NaN(),
    -limits::quiet_NaN(),
    limits::max(),
    limits::lowest(),
    limits::min(),
    limits::denorm_min(),
    1.0,
    0.1,
    0.1 + 0.2,
    123.456,
    1234567.0,
    0.0001,
    0.00001,
    43210.0,
    43210.1,
    10000.0,
    -5942736479622170.0,
    9.061488e+15,
    9007199254740992.0,
    6.62607015e-34,
    5.444310685350916e+14,
    1e-323,
    1.2e-322,
    1.5e-323,
    1.24e-322,
    1.234e-320,
    2.2250738585072004e-308,
    -4.932096661796888e-226,
    3.439070283483335e+35,
    6.606854224493745e-17,
    6.079537928711555e+61,
    -1.2345678901234567e+123,
};

static bool failed = false;

static constexpr size_t buffer_size = zmij::ecmascript_buffer_size;

struct ArrayWithCanary {
  // "Canary" bytes on each side of the buffer, to detect writes outside the
  // buffer (which happened while developing these patches.)
  static constexpr size_t canary_size = 15;

  char head_canary[canary_size];
  char buffer[buffer_size];
  char tail_canary[canary_size];

  // Use distinct, printable non-digit ASCII characters for the canary bytes
  // (':' through '~' is the range after digits until DEL.)
  static constexpr char canary_byte(size_t i) { return ':' + i; }

  constexpr ArrayWithCanary() {
    for (size_t i = 0; i < canary_size; ++i) {
      head_canary[i] = canary_byte(i);
      tail_canary[i] = canary_byte(i + canary_size + buffer_size);
    }
    for (size_t i = 0; i < buffer_size; ++i) {
      buffer[i] = canary_byte(i + canary_size);
    }
  }

  bool check_untouched(int* error_index) const {
    for (size_t i = 0; i < canary_size; ++i) {
      if (head_canary[i] != canary_byte(i)) {
        *error_index = int(i) - int(canary_size);
        return false;
      }
      if (tail_canary[i] != canary_byte(i + canary_size + buffer_size)) {
        *error_index = int(i) + int(buffer_size);
        return false;
      }
    }
    return true;
  }
};

static_assert(ArrayWithCanary::canary_byte(0) > '9',
              "first canary byte should be non-digit printable ASCII");
static_assert(ArrayWithCanary::canary_byte(sizeof(ArrayWithCanary) - 1) <= '~',
              "last canary byte should be non-digit printable ASCII");

static std::string check_ecmascript(double value) {
  ArrayWithCanary array;
  char* end = zmij::write_ecmascript(array.buffer, buffer_size, value);
  uint64_t bits = std::bit_cast<uint64_t>(value);
  if (end < array.buffer || end > array.buffer + buffer_size) {
    std::fprintf(stderr, "0x%016" PRIx64 ": end pointer outside of buffer\n",
                 bits);
    failed = true;
    return "";
  }

  int error_index;
  if (!array.check_untouched(&error_index)) {
    std::fprintf(stderr,
                 "0x%016" PRIx64 ": write at offset %d outside of buffer\n",
                 bits, error_index);
    failed = true;
  }
  return std::string{array.buffer, end};
}

static void check(double value) {
  char upstream[zmij::double_buffer_size + 1];
  char* end = zmij::write(upstream, zmij::double_buffer_size, value);
  *end = '\0';

  std::string ecmascript = check_ecmascript(value);

  std::printf("0x%016" PRIx64 " %s %s\n", std::bit_cast<uint64_t>(value),
              upstream, ecmascript.c_str());
}

int main() {
  // special values from upstream's tests
  for (double value : interesting_values) check(value);

  // Every power of ten and its neighbours, to cover each transition in
  // exponent length and between fixed and exponential notation.
  for (int exp = -324; exp <= 308; ++exp) {
    double value = std::strtod(("1e" + std::to_string(exp)).c_str(), nullptr);
    if (value == 0) continue;
    check(std::nextafter(value, 0.0));
    check(value);
    check(std::nextafter(value, limits::infinity()));
  }

  // every binary exponent with significand 0 and 1
  for (uint64_t exp = 0; exp < 0x7ff; ++exp) {
    check(std::bit_cast<double>(exp << 52));
    check(std::bit_cast<double>(exp << 52 | 1));
  }

  // upstream's boundary-bits.h
  for (uint64_t bits : boundary_bits) check(std::bit_cast<double>(bits));

  return failed;
}
