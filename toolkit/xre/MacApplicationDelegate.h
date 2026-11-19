/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file defines the interface between Cocoa-specific Obj-C++ and generic
// C++, so it itself cannot have any Obj-C bits in it.

#ifndef MacApplicationDelegate_h_
#define MacApplicationDelegate_h_

#include "nsTArray.h"
#include "nsStringFwd.h"

void EnsureUseCocoaDockAPI();
void SetupMacApplicationDelegate(bool* gRestartedByOS);
void ActivateMacAppAfterLaunch();
void InitializeMacApp();
nsTArray<nsCString> TakeStartupURLs();
void RegisterEarlyUserDefaults();

#endif
