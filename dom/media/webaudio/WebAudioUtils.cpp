/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebAudioUtils.h"

#include <speex/speex_resampler.h>

#include "AudioSampleFormat.h"
#include "AudioSegment.h"
#include "mozilla/SchedulerGroup.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsIConsoleService.h"
#include "nsIScriptError.h"
#include "nsJSUtils.h"
#include "nsServiceManagerUtils.h"

namespace mozilla {

LazyLogModule gWebAudioAPILog("WebAudioAPI");

namespace dom {

int WebAudioUtils::SpeexResamplerProcess(SpeexResamplerState* aResampler,
                                         uint32_t aChannel, const float* aIn,
                                         uint32_t* aInLen, float* aOut,
                                         uint32_t* aOutLen) {
  return speex_resampler_process_float(aResampler, aChannel, aIn, aInLen, aOut,
                                       aOutLen);
}

int WebAudioUtils::SpeexResamplerProcess(SpeexResamplerState* aResampler,
                                         uint32_t aChannel, const int16_t* aIn,
                                         uint32_t* aInLen, float* aOut,
                                         uint32_t* aOutLen) {
  AutoTArray<AudioDataValue, WEBAUDIO_BLOCK_SIZE * 4> tmp;
  tmp.SetLength(*aInLen);
  ConvertAudioSamples(aIn, tmp.Elements(), *aInLen);
  int result = speex_resampler_process_float(
      aResampler, aChannel, tmp.Elements(), aInLen, aOut, aOutLen);
  return result;
}

int WebAudioUtils::SpeexResamplerProcess(SpeexResamplerState* aResampler,
                                         uint32_t aChannel, const int16_t* aIn,
                                         uint32_t* aInLen, int16_t* aOut,
                                         uint32_t* aOutLen) {
  AutoTArray<AudioDataValue, WEBAUDIO_BLOCK_SIZE * 4> tmp1;
  AutoTArray<AudioDataValue, WEBAUDIO_BLOCK_SIZE * 4> tmp2;
  tmp1.SetLength(*aInLen);
  tmp2.SetLength(*aOutLen);
  ConvertAudioSamples(aIn, tmp1.Elements(), *aInLen);
  int result = speex_resampler_process_float(
      aResampler, aChannel, tmp1.Elements(), aInLen, tmp2.Elements(), aOutLen);
  ConvertAudioSamples(tmp2.Elements(), aOut, *aOutLen);
  return result;
}

void WebAudioUtils::LogToDeveloperConsole(uint64_t aWindowID,
                                          const char* aKey) {
  // This implementation is derived from dom/media/VideoUtils.cpp, but we
  // use a windowID so that the message is delivered to the developer console.
  // It is similar to ContentUtils::ReportToConsole, but also works off main
  // thread.
  if (!NS_IsMainThread()) {
    nsCOMPtr<nsIRunnable> task = NS_NewRunnableFunction(
        "dom::WebAudioUtils::LogToDeveloperConsole",
        [aWindowID, aKey] { LogToDeveloperConsole(aWindowID, aKey); });
    SchedulerGroup::Dispatch(task.forget());
    return;
  }

  nsAutoString result;
  nsresult rv = nsContentUtils::GetLocalizedString(
      PropertiesFile::DOM_PROPERTIES, aKey, result);

  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to log message to console.");
    return;
  }

  nsContentUtils::ReportToConsoleByWindowID(result, nsIScriptError::warningFlag,
                                            "Web Audio"_ns, aWindowID);
}

}  // namespace dom
}  // namespace mozilla
