/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WebAudioUtils_h_
#define WebAudioUtils_h_

#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>

#include "MediaSegment.h"
#include "fdlibm.h"
#include "mozilla/Logging.h"

// Forward declaration
typedef struct SpeexResamplerState_ SpeexResamplerState;

namespace mozilla {

extern LazyLogModule gWebAudioAPILog;
#define WEB_AUDIO_API_LOG(...) \
  MOZ_LOG_FMT(gWebAudioAPILog, LogLevel::Debug, __VA_ARGS__)
#define WEB_AUDIO_API_LOG_TEST(...) \
  MOZ_LOG_TEST(gWebAudioAPILog, LogLevel::Debug)

namespace dom::WebAudioUtils {

// 32 is the minimum required by the spec for createBuffer() and
// createScriptProcessor() and matches what is used by Blink.  The limit
// protects against large memory allocations.
const size_t MaxChannelCount = 32;
// https://webaudio.github.io/web-audio-api/#sample-rates
const uint32_t MinSampleRate = 3000;
const uint32_t MaxSampleRate = 768000;

inline bool FuzzyEqual(float v1, float v2) { return fabsf(v1 - v2) < 1e-7f; }
inline bool FuzzyEqual(double v1, double v2) { return fabs(v1 - v2) < 1e-7; }

/**
 * Converts a linear value to decibels.  Returns aMinDecibels if the linear
 * value is 0.
 */
inline float ConvertLinearToDecibels(float aLinearValue, float aMinDecibels) {
  MOZ_ASSERT(aLinearValue >= 0);
  return aLinearValue > 0.0f ? 20.0f * fdlibm_log10f(aLinearValue)
                             : aMinDecibels;
}

/**
 * Converts a decibel value to a linear value.
 */
inline float ConvertDecibelsToLinear(float aDecibels) {
  // Equivalent to 10^(dB / 20), but expf is much cheaper than powf.
  constexpr float kLn10Over20 = std::numbers::ln10_v<float> / 20;
  return fdlibm_expf(kLn10Over20 * aDecibels);
}

inline void FixNaN(float& aFloat) {
  if (std::isnan(aFloat) || std::isinf(aFloat)) {
    aFloat = 0.0f;
  }
}

inline double DiscreteTimeConstantForSampleRate(double timeConstant,
                                                double sampleRate) {
  return 1.0 - fdlibm_exp(-1.0 / (sampleRate * timeConstant));
}

inline bool IsTimeValid(double aTime) {
  return aTime >= 0 && aTime <= (MEDIA_TIME_MAX >> TRACK_RATE_MAX_BITS);
}

/**
 * Converts a floating point value to an integral type in a safe and
 * platform agnostic way.  The following program demonstrates the kinds
 * of ways things can go wrong depending on the CPU architecture you're
 * compiling for:
 *
 * #include <stdio.h>
 * volatile float r;
 * int main()
 * {
 *   unsigned int q;
 *   r = 1e100;
 *   q = r;
 *   printf("%f %d\n", r, q);
 *   r = -1e100;
 *   q = r;
 *   printf("%f %d\n", r, q);
 *   r = 1e15;
 *   q = r;
 *   printf("%f %x\n", r, q);
 *   r = 0/0.;
 *   q = r;
 *   printf("%f %d\n", r, q);
 * }
 *
 * This program, when compiled for unsigned int, generates the following
 * results depending on the architecture:
 *
 * x86 and x86-64
 * ---
 *  inf 0
 *  -inf 0
 *  999999995904.000000 -727384064 d4a50000
 *  nan 0
 *
 * ARM
 * ---
 *  inf -1
 *  -inf 0
 *  999999995904.000000 -1
 *  nan 0
 *
 * When compiled for int, this program generates the following results:
 *
 * x86 and x86-64
 * ---
 *  inf -2147483648
 *  -inf -2147483648
 *  999999995904.000000 -2147483648
 *  nan -2147483648
 *
 * ARM
 * ---
 *  inf 2147483647
 *  -inf -2147483648
 *  999999995904.000000 2147483647
 *  nan 0
 *
 * Note that the caller is responsible to make sure that the value
 * passed to this function is not a NaN.  This function will abort if
 * it sees a NaN.
 */
template <typename IntType, typename FloatType>
IntType TruncateFloatToInt(FloatType f) {
  using std::numeric_limits;
  static_assert(std::is_integral_v<IntType> == true,
                "IntType must be an integral type");
  static_assert(std::is_floating_point_v<FloatType> == true,
                "FloatType must be a floating point type");

  if (std::isnan(f)) {
    // It is the responsibility of the caller to deal with NaN values.
    // If we ever get to this point, we have a serious bug to fix.
    MOZ_CRASH("We should never see a NaN here");
  }

  // If the floating point value is outside of the range of maximum
  // integral value for this type, just clamp to the maximum value.
  // The equality case must also return max() due to loss of precision when
  // converting max() to float.
  if (f >= FloatType(numeric_limits<IntType>::max())) {
    return numeric_limits<IntType>::max();
  }

  if (f <= FloatType(numeric_limits<IntType>::min())) {
    // If the floating point value is outside of the range of minimum
    // integral value for this type, just clamp to the minimum value.
    return numeric_limits<IntType>::min();
  }

  // Otherwise, this conversion must be well defined.
  return IntType(f);
}

int SpeexResamplerProcess(SpeexResamplerState* aResampler, uint32_t aChannel,
                          const float* aIn, uint32_t* aInLen, float* aOut,
                          uint32_t* aOutLen);

int SpeexResamplerProcess(SpeexResamplerState* aResampler, uint32_t aChannel,
                          const int16_t* aIn, uint32_t* aInLen, float* aOut,
                          uint32_t* aOutLen);

int SpeexResamplerProcess(SpeexResamplerState* aResampler, uint32_t aChannel,
                          const int16_t* aIn, uint32_t* aInLen, int16_t* aOut,
                          uint32_t* aOutLen);

void LogToDeveloperConsole(uint64_t aWindowID, const char* aKey);

}  // namespace dom::WebAudioUtils
}  // namespace mozilla

#endif
