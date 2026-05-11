/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsXULContentSink_h_
#define nsXULContentSink_h_

#include "mozilla/Span.h"
#include "mozilla/WeakPtr.h"
#include "nsIExpatSink.h"
#include "nsIWeakReferenceUtils.h"
#include "nsIXMLContentSink.h"
#include "nsNodeInfoManager.h"
#include "nsTArray.h"
#include "nsXULElement.h"

class nsIScriptSecurityManager;
class nsAttrName;
class nsXULPrototypeDocument;
class nsXULPrototypeElement;
class nsXULPrototypeNode;

class XULContentSinkImpl final : public nsIXMLContentSink, public nsIExpatSink {
 public:
  XULContentSinkImpl();

  // nsISupports
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSIEXPATSINK

  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(XULContentSinkImpl,
                                           nsIXMLContentSink)

  // nsIContentSink
  NS_IMETHOD WillParse(void) override { return NS_OK; }
  NS_IMETHOD DidBuildModel(bool aTerminated) override;
  NS_IMETHOD WillInterrupt(void) override;
  void WillResume() override;
  NS_IMETHOD SetParser(nsParserBase* aParser) override;
  virtual void FlushPendingNotifications(mozilla::FlushType aType) override {}
  virtual void SetDocumentCharset(NotNull<const Encoding*> aEncoding) override;
  virtual nsISupports* GetTarget() override;

  /**
   * Initialize the content sink, giving it a document with which to communicate
   * with the outside world, and an nsXULPrototypeDocument to build.
   */
  nsresult Init(mozilla::dom::Document* aDocument,
                nsXULPrototypeDocument* aPrototype);

 protected:
  virtual ~XULContentSinkImpl();

  // pseudo-constants
  nsTArray<char16_t> mText;
  bool mConstrainSize;

  nsresult AddAttributes(const char16_t** aAttributes, const uint32_t aAttrLen,
                         nsXULPrototypeElement* aElement);

  nsresult OpenRoot(const char16_t** aAttributes, const uint32_t aAttrLen,
                    mozilla::dom::NodeInfo* aNodeInfo);

  nsresult OpenTag(const char16_t** aAttributes, const uint32_t aAttrLen,
                   const uint32_t aLineNumber,
                   mozilla::dom::NodeInfo* aNodeInfo);

  // If OpenScript returns NS_OK and after it returns our state is eInScript,
  // that means that we created a prototype script and stuck it on
  // mContextStack.  If NS_OK is returned but the state is still
  // eInDocumentElement then we didn't create a prototype script (e.g. the
  // script had an unknown type), and the caller should create a prototype
  // element.
  nsresult OpenScript(const char16_t** aAttributes, const uint32_t aLineNumber);

  bool IsDataInBuffer() const;

  // Text management
  nsresult FlushText(bool aCreateTextNode = true);
  nsresult AddText(mozilla::Span<const char16_t> aNewText);

  RefPtr<nsNodeInfoManager> mNodeInfoManager;

  nsresult NormalizeAttributeString(const char16_t* aExpatName,
                                    nsAttrName& aName);

 public:
  enum State { eInProlog, eInDocumentElement, eInScript, eInEpilog };

 protected:
  State mState;

  // content stack management
  class ContextStack {
   protected:
    struct Entry {
      RefPtr<nsXULPrototypeNode> mNode;
      // a LOT of nodes have children; preallocate for 8
      nsPrototypeArray mChildren;
      State mState;
      Entry* mNext;
      Entry(RefPtr<nsXULPrototypeNode>&& aNode, State aState, Entry* aNext)
          : mNode(std::move(aNode)),
            mChildren(8),
            mState(aState),
            mNext(aNext) {}
    };

    Entry* mTop;
    int32_t mDepth;

   public:
    ContextStack();
    ~ContextStack();

    int32_t Depth() { return mDepth; }

    void Push(RefPtr<nsXULPrototypeNode>&& aNode, State aState);
    nsresult Pop(State* aState);

    nsresult GetTopNode(RefPtr<nsXULPrototypeNode>& aNode);
    nsresult GetTopChildren(nsPrototypeArray** aChildren);

    void Clear();

    void Traverse(nsCycleCollectionTraversalCallback& aCallback);
  };

  friend class ContextStack;
  ContextStack mContextStack;

  mozilla::WeakPtr<mozilla::dom::Document> mDocument;
  nsCOMPtr<nsIURI> mDocumentURL;  // [OWNER]

  RefPtr<nsXULPrototypeDocument> mPrototype;  // [OWNER]

  RefPtr<nsParserBase> mParser;
  nsCOMPtr<nsIScriptSecurityManager> mSecMan;
};

#endif /* nsXULContentSink_h_ */
