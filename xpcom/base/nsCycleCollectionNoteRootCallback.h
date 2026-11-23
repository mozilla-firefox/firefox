/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsCycleCollectionNoteRootCallback_h_
#define nsCycleCollectionNoteRootCallback_h_

#include "nscore.h"

class nsCycleCollectionParticipant;
class nsISupports;
class JSObject;

namespace JS {
class GCCellPtr;
}

class nsCycleCollectionNoteRootCallback {
 public:
  // aRoot must be canonical (ie the result of QIing to
  // nsCycleCollectionISupports).
  NS_IMETHOD_(void)
  NoteXPCOMRoot(nsISupports* aRoot,
                nsCycleCollectionParticipant* aParticipant) = 0;

  NS_IMETHOD_(void) NoteJSRoot(JSObject* aRoot) = 0;
  NS_IMETHOD_(void)
  NoteNativeRoot(void* aRoot, nsCycleCollectionParticipant* aParticipant) = 0;

  NS_IMETHOD_(void)
  NoteWeakMapping(JSObject* aMap, JS::GCCellPtr aKey, JS::GCCellPtr aDelegate,
                  JS::GCCellPtr aVal) = 0;

  bool WantAllTraces() const { return mWantAllTraces; }

 protected:
  nsCycleCollectionNoteRootCallback() : mWantAllTraces(false) {}

  bool mWantAllTraces;
};

#endif  // nsCycleCollectionNoteRootCallback_h_
