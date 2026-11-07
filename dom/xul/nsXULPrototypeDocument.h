/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsXULPrototypeDocument_h_
#define nsXULPrototypeDocument_h_

#include <functional>

#include "js/TracingAPI.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsISerializable.h"
#include "nsTArray.h"

class nsAtom;
class nsIPrincipal;
class nsIURI;
class nsNodeInfoManager;
class nsXULPrototypeElement;
class nsXULPrototypePI;

namespace mozilla::dom {
class Element;
}

/**
 * A "prototype" document that stores shared document information
 * for the XUL cache.
 * Among other things, stores the tree of nsXULPrototype*
 * objects, from which the real DOM tree is built later in
 * PrototypeDocumentContentSink::ResumeWalk.
 */
class nsXULPrototypeDocument final : public nsISerializable {
 public:
  using Callback = std::function<void()>;

  // nsISupports interface
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL

  // nsISerializable interface
  NS_DECL_NSISERIALIZABLE

  nsresult InitPrincipal(nsIURI* aURI, nsIPrincipal* aPrincipal);
  nsIURI* GetURI();

  /**
   * Get/set the root nsXULPrototypeElement of the document.
   */
  nsXULPrototypeElement* GetRootElement();
  void SetRootElement(nsXULPrototypeElement* aElement);

  /**
   * Add a processing instruction to the prolog. Note that only
   * PI nodes are currently stored in a XUL prototype document's
   * prolog and that they're handled separately from the rest of
   * prototype node tree.
   *
   * @param aPI an already adrefed PI proto to add. This method takes
   *            ownership of the passed PI.
   */
  nsresult AddProcessingInstruction(nsXULPrototypePI* aPI);
  /**
   * @note GetProcessingInstructions retains the ownership (the PI
   *       protos only get deleted when the proto document is deleted)
   */
  const nsTArray<RefPtr<nsXULPrototypePI> >& GetProcessingInstructions() const;

  nsIPrincipal* DocumentPrincipal();
  void SetDocumentPrincipal(nsIPrincipal* aPrincipal);

  /**
   * If current prototype document has not yet finished loading,
   * appends aDocument to the list of documents to notify (via
   * PrototypeDocumentContentSink::OnPrototypeLoadDone()) and
   * sets aLoaded to false. Otherwise sets aLoaded to true.
   */
  nsresult AwaitLoadDone(Callback&& aCallback, bool* aResult);

  /**
   * Notifies each document registered via AwaitLoadDone on this
   * prototype document that the prototype has finished loading.
   * The notification is performed by calling
   * PrototypeDocumentContentSink::OnPrototypeLoadDone on the
   * registered documents.
   */
  nsresult NotifyLoadDone();

  nsNodeInfoManager* GetNodeInfoManager();

  void MarkInCCGeneration(uint32_t aCCGeneration);

  NS_DECL_CYCLE_COLLECTION_CLASS(nsXULPrototypeDocument)

  void MaybeRebuildPrototypeAfterTranslation(mozilla::dom::Element*,
                                             bool aDeep);

 protected:
  void RebuildPrototypeFromElement(nsXULPrototypeElement* aPrototype,
                                   mozilla::dom::Element* aElement, bool aDeep);

  nsCOMPtr<nsIURI> mURI;
  RefPtr<nsXULPrototypeElement> mRoot;
  nsTArray<RefPtr<nsXULPrototypePI> > mProcessingInstructions;

  nsTArray<Callback> mPrototypeWaiters;

  RefPtr<nsNodeInfoManager> mNodeInfoManager;

  uint32_t mCCGeneration = 0;
  bool mLoaded = false;

  nsXULPrototypeDocument();
  virtual ~nsXULPrototypeDocument();
  nsresult Init();

  friend NS_IMETHODIMP NS_NewXULPrototypeDocument(
      nsXULPrototypeDocument** aResult);
};

#endif  // nsXULPrototypeDocument_h_
