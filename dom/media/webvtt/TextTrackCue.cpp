/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/TextTrackCue.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLMediaElement.h"
#include "mozilla/dom/HTMLTrackElement.h"
#include "mozilla/dom/TextTrackList.h"
#include "mozilla/dom/TextTrackRegion.h"
#include "mozilla/intl/Bidi.h"
#include "nsComponentManagerUtils.h"
#include "nsGlobalWindowInner.h"

extern mozilla::LazyLogModule gTextTrackLog;

#define LOG(msg, ...)                                                  \
  MOZ_LOG_FMT(gTextTrackLog, LogLevel::Debug, "TextTrackCue={}, " msg, \
              fmt::ptr(this), ##__VA_ARGS__)

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(TextTrackCue, DOMEventTargetHelper,
                                   mDocument, mTrack, mTrackElement,
                                   mDisplayState, mRegion)

NS_IMPL_ADDREF_INHERITED(TextTrackCue, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(TextTrackCue, DOMEventTargetHelper)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TextTrackCue)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

StaticRefPtr<nsIWebVTTParserWrapper> TextTrackCue::sParserWrapper;

// Set default value for cue, spec https://w3c.github.io/webvtt/#model-cues
void TextTrackCue::SetDefaultCueSettings() {
  mPositionIsAutoKeyword = true;
  // Spec https://www.w3.org/TR/webvtt1/#webvtt-cue-position-automatic-alignment
  mPositionAlign = PositionAlignSetting::Auto;
  mSize = 100.0;
  mPauseOnExit = false;
  mSnapToLines = true;
  mLineIsAutoKeyword = true;
  mAlign = AlignSetting::Center;
  mLineAlign = LineAlignSetting::Start;
  mVertical = DirectionSetting::_empty;
  mActive = false;
}

TextTrackCue::TextTrackCue(nsPIDOMWindowInner* aOwnerWindow, double aStartTime,
                           double aEndTime, const nsAString& aText,
                           ErrorResult& aRv)
    : DOMEventTargetHelper(aOwnerWindow),
      mText(aText),
      mStartTime(aStartTime),
      mEndTime(aEndTime),
      mPosition(0.0),
      mLine(0.0),
      mReset(false, "TextTrackCue::mReset"),
      mHaveStartedWatcher(false),
      mWatchManager(this, AbstractThread::MainThread()) {
  LOG("create TextTrackCue");
  SetDefaultCueSettings();
  MOZ_ASSERT(aOwnerWindow);
  if (NS_FAILED(StashDocument())) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
  }
}

TextTrackCue::TextTrackCue(nsPIDOMWindowInner* aOwnerWindow, double aStartTime,
                           double aEndTime, const nsAString& aText,
                           HTMLTrackElement* aTrackElement, ErrorResult& aRv)
    : DOMEventTargetHelper(aOwnerWindow),
      mText(aText),
      mStartTime(aStartTime),
      mEndTime(aEndTime),
      mTrackElement(aTrackElement),
      mPosition(0.0),
      mLine(0.0),
      mReset(false, "TextTrackCue::mReset"),
      mHaveStartedWatcher(false),
      mWatchManager(this, AbstractThread::MainThread()) {
  LOG("create TextTrackCue");
  SetDefaultCueSettings();
  MOZ_ASSERT(aOwnerWindow);
  if (NS_FAILED(StashDocument())) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
  }
}

TextTrackCue::~TextTrackCue() = default;

/** Save a reference to our creating document so we don't have to
 *  keep getting it from our window.
 */
nsresult TextTrackCue::StashDocument() {
  nsPIDOMWindowInner* window = GetOwnerWindow();
  if (!window) {
    return NS_ERROR_NO_INTERFACE;
  }
  mDocument = window->GetDoc();
  if (!mDocument) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return NS_OK;
}

already_AddRefed<DocumentFragment> TextTrackCue::GetCueAsHTML() {
  // mDocument may be null during cycle collector shutdown.
  // See bug 941701.
  if (!mDocument) {
    return nullptr;
  }

  if (!sParserWrapper) {
    nsresult rv;
    nsCOMPtr<nsIWebVTTParserWrapper> parserWrapper =
        do_CreateInstance(NS_WEBVTTPARSERWRAPPER_CONTRACTID, &rv);
    if (NS_FAILED(rv)) {
      return mDocument->CreateDocumentFragment();
    }
    sParserWrapper = parserWrapper;
    ClearOnShutdown(&sParserWrapper);
  }

  nsPIDOMWindowInner* window = mDocument->GetInnerWindow();
  if (!window) {
    return mDocument->CreateDocumentFragment();
  }

  RefPtr<DocumentFragment> frag;
  sParserWrapper->ConvertCueToDOMTree(window, static_cast<EventTarget*>(this),
                                      getter_AddRefs(frag));
  if (!frag) {
    return mDocument->CreateDocumentFragment();
  }
  return frag.forget();
}

void TextTrackCue::SetTrackElement(HTMLTrackElement* aTrackElement) {
  mTrackElement = aTrackElement;
}

JSObject* TextTrackCue::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return VTTCue_Binding::Wrap(aCx, this, aGivenProto);
}

TextTrackRegion* TextTrackCue::GetRegion() { return mRegion; }

void TextTrackCue::SetRegion(TextTrackRegion* aRegion) {
  if (mRegion == aRegion) {
    return;
  }
  mRegion = aRegion;
  mReset = true;
}

double TextTrackCue::ComputedLine() {
  // See spec https://w3c.github.io/webvtt/#cue-computed-line
  if (!mLineIsAutoKeyword && !mSnapToLines && (mLine < 0.0 || mLine > 100.0)) {
    return 100.0;
  } else if (!mLineIsAutoKeyword) {
    return mLine;
  } else if (mLineIsAutoKeyword && !mSnapToLines) {
    return 100.0;
  } else if (!mTrack || !mTrack->GetTextTrackList() ||
             !mTrack->GetTextTrackList()->GetMediaElement()) {
    return -1.0;
  }

  RefPtr<TextTrackList> trackList = mTrack->GetTextTrackList();
  bool dummy;
  uint32_t showingTracksNum = 0;
  for (uint32_t idx = 0; idx < trackList->Length(); idx++) {
    RefPtr<TextTrack> track = trackList->IndexedGetter(idx, dummy);
    if (track->Mode() == TextTrackMode::Showing) {
      showingTracksNum++;
    }

    if (mTrack == track) {
      break;
    }
  }

  return (-1.0) * showingTracksNum;
}

double TextTrackCue::ComputedPosition() {
  // See spec https://w3c.github.io/webvtt/#cue-computed-position
  if (!mPositionIsAutoKeyword) {
    return mPosition;
  }
  // The auto position depends on the cue text alignment only, so start and end
  // alignment use 50 whatever the text direction is.
  if (mAlign == AlignSetting::Left) {
    return 0.0;
  }
  if (mAlign == AlignSetting::Right) {
    return 100.0;
  }
  return 50.0;
}

PositionAlignSetting TextTrackCue::ComputedPositionAlign() {
  // See spec https://w3c.github.io/webvtt/#cue-computed-position-alignment
  if (mPositionAlign != PositionAlignSetting::Auto) {
    return mPositionAlign;
  } else if (mAlign == AlignSetting::Left) {
    return PositionAlignSetting::Line_left;
  } else if (mAlign == AlignSetting::Right) {
    return PositionAlignSetting::Line_right;
  } else if (mAlign == AlignSetting::Start) {
    return IsTextBaseDirectionLTR() ? PositionAlignSetting::Line_left
                                    : PositionAlignSetting::Line_right;
  } else if (mAlign == AlignSetting::End) {
    return IsTextBaseDirectionLTR() ? PositionAlignSetting::Line_right
                                    : PositionAlignSetting::Line_left;
  }
  return PositionAlignSetting::Center;
}

bool TextTrackCue::IsTextBaseDirectionLTR() const {
  // The result returned by `GetBaseDirection` might be `neutral` if the text
  // only contains neutral charaters. In this case, we would treat its base
  // direction as LTR.
  return intl::Bidi::GetBaseDirection(mText) != intl::Bidi::BaseDirection::RTL;
}

void TextTrackCue::NotifyDisplayStatesChanged() {
  if (!mReset) {
    return;
  }

  if (!mTrack || !mTrack->GetTextTrackList() ||
      !mTrack->GetTextTrackList()->GetMediaElement()) {
    return;
  }

  mTrack->GetTextTrackList()
      ->GetMediaElement()
      ->NotifyCueDisplayStatesChanged();
}

void TextTrackCue::SetActive(bool aActive) {
  if (mActive == aActive) {
    return;
  }

  LOG("TextTrackCue, SetActive={}", aActive);
  mActive = aActive;
  mDisplayState = mActive ? mDisplayState : nullptr;
  if (mTrack) {
    mTrack->NotifyCueActiveStateChanged(this);
  }
}

#undef LOG

}  // namespace mozilla::dom
