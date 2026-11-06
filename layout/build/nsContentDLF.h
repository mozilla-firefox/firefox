/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsContentDLF_h_
#define nsContentDLF_h_

#include "nsIDocumentLoaderFactory.h"
#include "nsMimeTypes.h"

class nsDocShell;
class nsIChannel;
class nsIDocumentViewer;
class nsILoadGroup;
class nsIStreamListener;

#define CONTENT_DLF_CONTRACTID "@mozilla.org/content/document-loader-factory;1"

class nsContentDLF final : public nsIDocumentLoaderFactory {
 protected:
  virtual ~nsContentDLF();

 public:
  nsContentDLF();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOCUMENTLOADERFACTORY

  /**
   * Create a blank document using the given loadgroup and given
   * principal.  aPrincipal is allowed to be null, in which case the
   * new document will get the about:blank content principal.
   */
  static already_AddRefed<mozilla::dom::Document> CreateBlankDocument(
      nsILoadGroup* aLoadGroup, nsIPrincipal* aPrincipal,
      nsIPrincipal* aPartitionedPrincipal, nsDocShell* aContainer);
};

nsresult NS_NewContentDocumentLoaderFactory(nsIDocumentLoaderFactory** aResult);

#endif
