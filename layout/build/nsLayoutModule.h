/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsLayoutModule_h
#define nsLayoutModule_h

#include "nsID.h"
#include "nscore.h"

class nsISupports;

// This function initializes various layout statics, as well as XPConnect.
// It should be called only once, and before the first time any XPCOM module in
// nsLayoutModule is used.
void nsLayoutModuleInitialize();

void nsLayoutModuleDtor();

nsresult CreateXMLContentSerializer(const nsID& aIID, void** aResult);
nsresult CreateHTMLContentSerializer(const nsID& aIID, void** aResult);
nsresult CreateXHTMLContentSerializer(const nsID& aIID, void** aResult);
nsresult CreatePlainTextSerializer(const nsID& aIID, void** aResult);
nsresult CreateGlobalMessageManager(const nsID& aIID, void** aResult);
nsresult CreateParentMessageManager(const nsID& aIID, void** aResult);
nsresult CreateChildMessageManager(const nsID& aIID, void** aResult);

nsresult Construct_nsIScriptSecurityManager(const nsIID& aIID, void** aResult);
nsresult LocalStorageManagerConstructor(const nsIID& aIID, void** aResult);
nsresult SessionStorageManagerConstructor(const nsIID& aIID, void** aResult);

#endif  // nsLayoutModule_h
