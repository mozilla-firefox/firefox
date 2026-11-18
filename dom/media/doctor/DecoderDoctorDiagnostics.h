/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DecoderDoctorDiagnostics_h_
#define DecoderDoctorDiagnostics_h_

#include "MediaResult.h"
#include "mozilla/DefineEnum.h"
#include "mozilla/EnumSet.h"
#include "mozilla/EnumTypeTraits.h"
#include "mozilla/dom/DecoderDoctorNotificationBinding.h"
#include "nsString.h"

namespace IPC {
template <typename T>
struct ParamTraits;
}  // namespace IPC

namespace mozilla {

namespace dom {
class Document;
}

struct DecoderDoctorEvent {
  enum Domain {
    eAudioSinkStartup,
  } mDomain;
  nsresult mResult;
};

// DecoderDoctorDiagnostics class, used to gather data from PDMs/DecoderTraits,
// and then notify the user about issues preventing (or worsening) playback.
//
// The expected usage is:
// 1. Instantiate a DecoderDoctorDiagnostics in a function (close to the point
//    where a webpage is trying to know whether some MIME types can be played,
//    or trying to play a media file).
// 2. Pass a pointer to the DecoderDoctorDiagnostics structure to one of the
//    CanPlayStatus/IsTypeSupported/(others?). During that call, some PDMs may
//    add relevant diagnostic information.
// 3. Analyze the collected diagnostics, and optionally dispatch an event to the
//    UX, to notify the user about potential playback issues and how to resolve
//    them.
//
// This class' methods must be called from the main thread.

class DecoderDoctorDiagnostics {
  friend struct IPC::ParamTraits<mozilla::DecoderDoctorDiagnostics>;

 public:
  // Store the diagnostic information collected so far on a document for a
  // given format. All diagnostics for a document will be analyzed together
  // within a short timeframe.
  // Should only be called once.
  void StoreFormatDiagnostics(dom::Document* aDocument,
                              const nsAString& aFormat, bool aCanPlay,
                              const char* aCallSite);

  void StoreMediaKeySystemAccess(dom::Document* aDocument,
                                 const nsAString& aKeySystem, bool aIsSupported,
                                 const char* aCallSite);

  void StoreEvent(dom::Document* aDocument, const DecoderDoctorEvent& aEvent,
                  const char* aCallSite);

  void StoreDecodeError(dom::Document* aDocument, const MediaResult& aError,
                        const nsString& aMediaSrc, const char* aCallSite);

  void StoreDecodeWarning(dom::Document* aDocument, const MediaResult& aWarning,
                          const nsString& aMediaSrc, const char* aCallSite);

  enum DiagnosticsType {
    eUnsaved,
    eFormatSupportCheck,
    eMediaKeySystemAccessRequest,
    eEvent,
    eDecodeError,
    eDecodeWarning
  };
  DiagnosticsType Type() const { return mDiagnosticsType; }

  // Description string, for logging purposes; only call on stored diags.
  nsCString GetDescription() const;

  // Methods to record diagnostic information:

  MOZ_DEFINE_ENUM_CLASS_AT_CLASS_SCOPE(
      Flags, (CanPlay, WMFFailedToLoad, FFmpegNotFound, LibAVCodecUnsupported,
              GMPPDMFailedToStartup, VideoNotSupported, AudioNotSupported));
  using FlagsSet = mozilla::EnumSet<Flags>;

  const nsAString& Format() const { return mFormat; }
  bool CanPlay() const { return mFlags.contains(Flags::CanPlay); }

  void SetFailureFlags(const FlagsSet& aFlags) { mFlags = aFlags; }
  void SetWMFFailedToLoad() { mFlags += Flags::WMFFailedToLoad; }
  bool DidWMFFailToLoad() const {
    return mFlags.contains(Flags::WMFFailedToLoad);
  }

  void SetFFmpegNotFound() { mFlags += Flags::FFmpegNotFound; }
  bool DidFFmpegNotFound() const {
    return mFlags.contains(Flags::FFmpegNotFound);
  }

  void SetLibAVCodecUnsupported() { mFlags += Flags::LibAVCodecUnsupported; }
  bool IsLibAVCodecUnsupported() const {
    return mFlags.contains(Flags::LibAVCodecUnsupported);
  }

  void SetGMPPDMFailedToStartup() { mFlags += Flags::GMPPDMFailedToStartup; }
  bool DidGMPPDMFailToStartup() const {
    return mFlags.contains(Flags::GMPPDMFailedToStartup);
  }

  void SetVideoNotSupported() { mFlags += Flags::VideoNotSupported; }
  void SetAudioNotSupported() { mFlags += Flags::AudioNotSupported; }

  void SetGMP(const nsACString& aGMP) { mGMP = aGMP; }
  const nsACString& GMP() const { return mGMP; }

  const nsAString& KeySystem() const { return mKeySystem; }
  bool IsKeySystemSupported() const { return mIsKeySystemSupported; }
  enum KeySystemIssue { eUnset, eWidevineWithNoWMF };
  void SetKeySystemIssue(KeySystemIssue aKeySystemIssue) {
    mKeySystemIssue = aKeySystemIssue;
  }
  KeySystemIssue GetKeySystemIssue() const { return mKeySystemIssue; }

  DecoderDoctorEvent event() const { return mEvent; }

  const MediaResult& DecodeIssue() const { return mDecodeIssue; }
  const nsString& DecodeIssueMediaSrc() const { return mDecodeIssueMediaSrc; }

  // This method is only used for testing.
  void SetDecoderDoctorReportType(const dom::DecoderDoctorReportType& aType);

 private:
  // Currently-known type of diagnostics. Set from one of the 'Store...'
  // methods. This helps ensure diagnostics are only stored once, and makes it
  // easy to know what information they contain.
  DiagnosticsType mDiagnosticsType = eUnsaved;

  nsString mFormat;
  FlagsSet mFlags;
  nsCString mGMP;

  nsString mKeySystem;
  bool mIsKeySystemSupported = false;
  KeySystemIssue mKeySystemIssue = eUnset;

  DecoderDoctorEvent mEvent{};

  MediaResult mDecodeIssue = NS_OK;
  nsString mDecodeIssueMediaSrc;
};

// Used for IPDL serialization.
// The 'value' have to be the biggest enum from DecoderDoctorDiagnostics::Flags.
template <>
struct MaxEnumValue<::mozilla::DecoderDoctorDiagnostics::Flags> {
  static constexpr unsigned int value =
      static_cast<unsigned int>(DecoderDoctorDiagnostics::sFlagsCount);
};

}  // namespace mozilla

#endif
