/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ContentClassifierPrefMirror_h
#define mozilla_ContentClassifierPrefMirror_h

#include "mozilla/StaticPtr.h"

namespace mozilla {

// Transitional shim that mirrors the classic Enhanced Tracking Protection
// prefs (privacy.trackingprotection.*) onto the ContentClassifier engine
// prefs (privacy.trackingprotection.content.*). This lets the existing
// about:preferences ETP UI drive the content classifier without being
// rewired.
//
// Driven by privacy.trackingprotection.content.mirror.mode, which has three
// states:
//
//   0 (off) - the mirror is down and clears the content prefs if it mirrored
//     them before.
//   1 (on) - the mirror owns the content engine/enabled prefs and recomputes
//     them on every relevant ETP pref change.
//   2 (handover) - the mirror stops mirroring but leaves the mirrored values
//     in place, handing them to whoever owns the content prefs next.
//
// The whole class is meant to be deleted once the migration to the content
// classifier is complete.
class ContentClassifierPrefMirror final {
 public:
  // Idempotently register the master pref callback that drives the singleton's
  // lifecycle.
  // Parent process, main thread only. Called from
  // ContentClassifierService::Init.
  static void Init();

 private:
  ContentClassifierPrefMirror();
  ~ContentClassifierPrefMirror();

  friend class StaticAutoPtr<ContentClassifierPrefMirror>;

  // Registered once on the master pref and never unregistered, so the mirror
  // keeps listening for the master pref even while the singleton is gone.
  static void OnMirrorPrefChange(const char* aPref, void* aData);

  static void Shutdown();

  // Clear the content prefs the mirror wrote, so they fall back to
  // their default-branch values.
  static void ReleaseMirroredPrefs();

  // Registered on every watched ETP source pref while the mirror is up.
  static void OnPrefChange(const char* aPref, void* aData);

  // Request a Sync() on the next turn of the event loop, coalescing bursts of
  // ETP pref changes (e.g. switching the ETP category) into a single recompute
  // so the content engine prefs - and the engine rebuilds they trigger in
  // ContentClassifierService - are touched at most once per turn.
  void ScheduleSync();

  // Recompute the content engine prefs from the current ETP pref state, write
  // them, and take ownership of them. No-op while the mirror is disabled.
  void Sync();

  static StaticAutoPtr<ContentClassifierPrefMirror> sInstance;

  // Whether a coalesced Sync() runnable is already pending. Main thread only.
  bool mSyncScheduled = false;
};

}  // namespace mozilla

#endif  // mozilla_ContentClassifierPrefMirror_h
