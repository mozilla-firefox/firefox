/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsReadableUtils.h"

// Local Includes
#include "nsContentAreaDragDrop.h"

// Helper Classes
#include "nsString.h"

// Interfaces needed to be included
#include "nsCopySupport.h"
#include "nsISelectionController.h"
#include "nsPIDOMWindow.h"
#include "nsIFormControl.h"
#include "nsITransferable.h"
#include "nsComponentManagerUtils.h"
#include "nsXPCOM.h"
#include "nsISupportsPrimitives.h"
#include "nsServiceManagerUtils.h"
#include "nsNetUtil.h"
#include "nsIFile.h"
#include "nsFrameLoader.h"
#include "nsFrameLoaderOwner.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsIContentPolicy.h"
#include "nsIImageLoadingContent.h"
#include "nsUnicharUtils.h"
#include "nsIURL.h"
#include "nsIURIMutator.h"
#include "mozilla/dom/Document.h"
#include "nsICookieJarSettings.h"
#include "nsIPrincipal.h"
#include "nsIWebBrowserPersist.h"
#include "nsEscape.h"
#include "nsContentUtils.h"
#include "nsIMIMEService.h"
#include "imgIContainer.h"
#include "imgIRequest.h"
#include "mozilla/dom/DataTransfer.h"
#include "nsIMIMEInfo.h"
#include "nsRange.h"
#include "BrowserParent.h"
#include "mozilla/TextControlElement.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLAreaElement.h"
#include "mozilla/dom/HTMLAnchorElement.h"
#include "mozilla/dom/Selection.h"
#include "nsVariant.h"
#include "nsQueryObject.h"

using namespace mozilla;
using namespace mozilla::dom;
using mozilla::IgnoreErrors;

class MOZ_STACK_CLASS DragDataProducer {
 public:
  DragDataProducer(nsPIDOMWindowOuter* aWindow, nsIContent* aTarget,
                   nsIContent* aSelectionTargetNode, bool aIsAltKeyPressed);
  nsresult Produce(DataTransfer* aDataTransfer, bool* aCanDrag,
                   Selection** aSelection, nsIContent** aDragNode,
                   nsIContentSecurityPolicy** aCsp,
                   nsICookieJarSettings** aCookieJarSettings);

 private:
  // @param aHidden true, iff the data should be hidden from non-chrome code.
  void AddString(DataTransfer* aDataTransfer, const nsAString& aFlavor,
                 const nsAString& aData, nsIPrincipal* aPrincipal,
                 bool aHidden = false);
  nsresult AddStringsToDataTransfer(nsIContent* aDragNode,
                                    DataTransfer* aDataTransfer);
  nsresult GetImageData(imgIContainer* aImage, imgIRequest* aRequest);
  static nsresult GetDraggableSelectionData(Selection* inSelection,
                                            nsIContent* inRealTargetNode,
                                            nsIContent** outImageOrLinkNode,
                                            bool* outDragSelectedText);
  [[nodiscard]] static nsresult GetAnchorURL(nsIContent* inNode,
                                             nsAString& outURL);
  static void CreateLinkText(const nsAString& inURL, const nsAString& inText,
                             nsAString& outLinkText);

  nsCOMPtr<nsPIDOMWindowOuter> mWindow;
  nsCOMPtr<nsIContent> mTarget;
  nsCOMPtr<nsIContent> mSelectionTargetNode;
  bool mIsAltKeyPressed;

  nsString mUrlString;
  nsString mImageSourceString;
  nsString mImageDestFileName;
#if defined(XP_MACOSX)
  nsString mImageRequestMime;
#endif
  nsString mTitleString;
  // will be filled automatically if you fill urlstring
  nsString mHtmlString;
  nsString mContextString;
  nsString mInfoString;

  bool mIsAnchor;
  nsCOMPtr<imgIContainer> mImage;
};

nsresult nsContentAreaDragDrop::GetDragData(
    nsPIDOMWindowOuter* aWindow, nsIContent* aTarget,
    nsIContent* aSelectionTargetNode, bool aIsAltKeyPressed,
    DataTransfer* aDataTransfer, bool* aCanDrag, Selection** aSelection,
    nsIContent** aDragNode, nsIContentSecurityPolicy** aCsp,
    nsICookieJarSettings** aCookieJarSettings) {
  NS_ENSURE_TRUE(aSelectionTargetNode, NS_ERROR_INVALID_ARG);

  *aCanDrag = true;

  DragDataProducer provider(aWindow, aTarget, aSelectionTargetNode,
                            aIsAltKeyPressed);
  return provider.Produce(aDataTransfer, aCanDrag, aSelection, aDragNode, aCsp,
                          aCookieJarSettings);
}

NS_IMPL_ISUPPORTS(nsContentAreaDragDropDataProvider, nsIFlavorDataProvider)

// SaveURIToFile
// used on platforms where it's possible to drag items (e.g. images)
// into the file system
nsresult nsContentAreaDragDropDataProvider::SaveURIToFile(
    nsIURI* inSourceURI, nsIPrincipal* inTriggeringPrincipal,
    nsICookieJarSettings* inCookieJarSettings, nsIFile* inDestFile,
    nsContentPolicyType inContentPolicyType, bool isPrivate) {
  nsCOMPtr<nsIURL> sourceURL = do_QueryInterface(inSourceURI);
  if (!sourceURL) {
    return NS_ERROR_NO_INTERFACE;
  }

  nsresult rv = inDestFile->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
  NS_ENSURE_SUCCESS(rv, rv);

  // we rely on the fact that the WPB is refcounted by the channel etc,
  // so we don't keep a ref to it. It will die when finished.
  nsCOMPtr<nsIWebBrowserPersist> persist = do_CreateInstance(
      "@mozilla.org/embedding/browser/nsWebBrowserPersist;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  persist->SetPersistFlags(
      nsIWebBrowserPersist::PERSIST_FLAGS_AUTODETECT_APPLY_CONVERSION |
      // Do not HTTPS-Only/-First upgrade this request. If we reach this point,
      // any potential upgrades should have already happened, or the URI may
      // have already been exempt.
      nsIWebBrowserPersist::PERSIST_FLAGS_DISABLE_HTTPS_ONLY);

  // referrer policy can be anything since the referrer is nullptr
  return persist->SaveURI(inSourceURI, inTriggeringPrincipal, 0, nullptr,
                          inCookieJarSettings, nullptr, nullptr, inDestFile,
                          inContentPolicyType, isPrivate);
}

/*
 * Check if the provided filename extension is valid for the MIME type and
 * return the MIME type's primary extension.
 *
 * @param aExtension           [in]  the extension to check
 * @param aMimeType            [in]  the MIME type to check the extension with
 * @param aIsValidExtension    [out] true if |aExtension| is valid for
 *                                   |aMimeType|
 * @param aPrimaryExtension    [out] the primary extension for the MIME type
 *                                   to potentially be used as a replacement
 *                                   for |aExtension|
 */
nsresult CheckAndGetExtensionForMime(const nsCString& aExtension,
                                     const nsCString& aMimeType,
                                     bool* aIsValidExtension,
                                     nsACString* aPrimaryExtension) {
  nsresult rv;

  nsCOMPtr<nsIMIMEService> mimeService = do_GetService("@mozilla.org/mime;1");
  if (NS_WARN_IF(!mimeService)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIMIMEInfo> mimeInfo;
  rv = mimeService->GetFromTypeAndExtension(aMimeType, ""_ns,
                                            getter_AddRefs(mimeInfo));
  NS_ENSURE_SUCCESS(rv, rv);

  mimeInfo->GetPrimaryExtension(*aPrimaryExtension);

  if (aExtension.IsEmpty()) {
    *aIsValidExtension = false;
    return NS_OK;
  }

  rv = mimeInfo->ExtensionExists(aExtension, aIsValidExtension);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

// This is our nsIFlavorDataProvider callback. There are several
// assumptions here that make this work:
//
// 1. Someone put a kFilePromiseURLMime flavor into the transferable
//    with the source URI of the file to save (as a string). We did
//    that in AddStringsToDataTransfer.
//
// 2. Someone put a kFilePromiseDirectoryMime flavor into the
//    transferable with an nsIFile for the directory we are to
//    save in. That has to be done by platform-specific code (in
//    widget), which gets the destination directory from
//    OS-specific drag information.
//
NS_IMETHODIMP
nsContentAreaDragDropDataProvider::GetFlavorData(nsITransferable* aTransferable,
                                                 const char* aFlavor,
                                                 nsISupports** aData) {
  NS_ENSURE_ARG_POINTER(aData);
  *aData = nullptr;

  nsresult rv = NS_ERROR_NOT_IMPLEMENTED;

  if (strcmp(aFlavor, kFilePromiseMime) == 0) {
    // get the URI from the kFilePromiseURLMime flavor
    NS_ENSURE_ARG(aTransferable);
    nsCOMPtr<nsISupports> tmp;
    rv = aTransferable->GetTransferData(kFilePromiseURLMime,
                                        getter_AddRefs(tmp));
    NS_ENSURE_SUCCESS(rv, rv);
    nsCOMPtr<nsISupportsString> supportsString = do_QueryInterface(tmp);
    if (!supportsString) return NS_ERROR_FAILURE;

    nsAutoString sourceURLString;
    supportsString->GetData(sourceURLString);
    if (sourceURLString.IsEmpty()) return NS_ERROR_FAILURE;

    nsCOMPtr<nsIURI> sourceURI;
    rv = NS_NewURI(getter_AddRefs(sourceURI), sourceURLString);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = aTransferable->GetTransferData(kFilePromiseDestFilename,
                                        getter_AddRefs(tmp));
    NS_ENSURE_SUCCESS(rv, rv);
    supportsString = do_QueryInterface(tmp);
    if (!supportsString) return NS_ERROR_FAILURE;

    nsAutoString targetFilename;
    supportsString->GetData(targetFilename);
    if (targetFilename.IsEmpty()) return NS_ERROR_FAILURE;

#if defined(XP_MACOSX)
    // Use the image request's MIME type to ensure the filename's
    // extension is compatible with the OS's handler for this type.
    // If it isn't, or is missing, replace the extension with the
    // primary extension. On Mac, do this in the parent process
    // because sandboxing blocks access to MIME-handler info from
    // content processes.
    if (XRE_IsParentProcess()) {
      rv = aTransferable->GetTransferData(kImageRequestMime,
                                          getter_AddRefs(tmp));
      NS_ENSURE_SUCCESS(rv, rv);
      supportsString = do_QueryInterface(tmp);
      if (!supportsString) return NS_ERROR_FAILURE;

      nsAutoString contentType;
      supportsString->GetData(contentType);

      nsCOMPtr<nsIMIMEService> mimeService =
          do_GetService("@mozilla.org/mime;1");
      if (NS_WARN_IF(!mimeService)) {
        return NS_ERROR_FAILURE;
      }

      mimeService->ValidateFileNameForSaving(
          targetFilename, NS_ConvertUTF16toUTF8(contentType),
          nsIMIMEService::VALIDATE_DEFAULT, targetFilename);
    } else {
      // make the filename safe for the filesystem
      targetFilename.ReplaceChar(
          u"" FILE_PATH_SEPARATOR FILE_ILLEGAL_CHARACTERS, u'-');
    }
#endif /* defined(XP_MACOSX) */

    // get the target directory from the kFilePromiseDirectoryMime
    // flavor
    nsCOMPtr<nsISupports> dirPrimitive;
    rv = aTransferable->GetTransferData(kFilePromiseDirectoryMime,
                                        getter_AddRefs(dirPrimitive));
    NS_ENSURE_SUCCESS(rv, rv);
    nsCOMPtr<nsIFile> destDirectory = do_QueryInterface(dirPrimitive);
    if (!destDirectory) return NS_ERROR_FAILURE;

    nsCOMPtr<nsIFile> file;
    rv = destDirectory->Clone(getter_AddRefs(file));
    NS_ENSURE_SUCCESS(rv, rv);

    file->Append(targetFilename);

    bool isPrivate = aTransferable->GetIsPrivateData();

    nsCOMPtr<nsIPrincipal> principal = aTransferable->GetDataPrincipal();
    nsContentPolicyType contentPolicyType =
        aTransferable->GetContentPolicyType();
    nsCOMPtr<nsICookieJarSettings> cookieJarSettings =
        aTransferable->GetCookieJarSettings();
    rv = SaveURIToFile(sourceURI, principal, cookieJarSettings, file,
                       contentPolicyType, isPrivate);
    // send back an nsIFile
    if (NS_SUCCEEDED(rv)) {
      CallQueryInterface(file, aData);
    }
  }

  return rv;
}

DragDataProducer::DragDataProducer(nsPIDOMWindowOuter* aWindow,
                                   nsIContent* aTarget,
                                   nsIContent* aSelectionTargetNode,
                                   bool aIsAltKeyPressed)
    : mWindow(aWindow),
      mTarget(aTarget),
      mSelectionTargetNode(aSelectionTargetNode),
      mIsAltKeyPressed(aIsAltKeyPressed),
      mIsAnchor(false) {}

static nsIContent* FindDragTarget(nsIContent* aContent) {
  for (nsIContent* content = aContent; content;
       content = content->GetFlattenedTreeParent()) {
    if (nsContentUtils::ContentIsDraggable(content)) {
      return content;
    }
  }
  return nullptr;
}

//
// GetAnchorURL
//
nsresult DragDataProducer::GetAnchorURL(nsIContent* aContent, nsAString& aURL) {
  aURL.Truncate();
  auto* element = Element::FromNodeOrNull(aContent);
  if (!element || !element->IsLink()) {
    return NS_OK;
  }

  nsCOMPtr<nsIURI> linkURI = element->GetHrefURI();
  if (!linkURI) {
    return NS_OK;
  }

  nsAutoCString spec;
  nsresult rv = linkURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);
  nsIScriptSecurityManager* secMan = nsContentUtils::GetSecurityManager();
  rv = secMan->CheckLoadURIStrWithPrincipal(aContent->NodePrincipal(), spec, 0);
  NS_ENSURE_SUCCESS(rv, rv);
  CopyUTF8toUTF16(spec, aURL);
  return NS_OK;
}

//
// CreateLinkText
//
// Creates the html for an anchor in the form
//  <a href="inURL">inText</a>
//
void DragDataProducer::CreateLinkText(const nsAString& inURL,
                                      const nsAString& inText,
                                      nsAString& outLinkText) {
  // use a temp var in case |inText| is the same string as
  // |outLinkText| to avoid overwriting it while building up the
  // string in pieces.
  nsAutoString linkText(u"<a href=\""_ns + inURL + u"\">"_ns + inText +
                        u"</a>"_ns);

  outLinkText = linkText;
}

nsresult DragDataProducer::GetImageData(imgIContainer* aImage,
                                        imgIRequest* aRequest) {
  nsCOMPtr<nsIURI> imgUri = aRequest->GetURI();

  nsCOMPtr<nsIURL> imgUrl(do_QueryInterface(imgUri));
  if (imgUrl) {
    nsAutoCString spec;
    nsresult rv = imgUrl->GetSpec(spec);
    NS_ENSURE_SUCCESS(rv, rv);

    // pass out the image source string
    CopyUTF8toUTF16(spec, mImageSourceString);

    nsCString mimeType;
    aRequest->GetMimeType(getter_Copies(mimeType));

    nsAutoCString fileName;
    aRequest->GetFileName(fileName);

#if defined(XP_MACOSX)
    // Save the MIME type so we can make sure the extension
    // is compatible (and replace it if it isn't) when the
    // image is dropped. On Mac, we need to get the OS MIME
    // handler information in the parent due to sandboxing.
    CopyUTF8toUTF16(mimeType, mImageRequestMime);
    CopyUTF8toUTF16(fileName, mImageDestFileName);
#else
    nsCOMPtr<nsIMIMEService> mimeService = do_GetService("@mozilla.org/mime;1");
    if (NS_WARN_IF(!mimeService)) {
      return NS_ERROR_FAILURE;
    }

    CopyUTF8toUTF16(fileName, mImageDestFileName);
    mimeService->ValidateFileNameForSaving(mImageDestFileName, mimeType,
                                           nsIMIMEService::VALIDATE_DEFAULT,
                                           mImageDestFileName);
#endif

    // and the image object
    mImage = aImage;
  }

  return NS_OK;
}

nsresult DragDataProducer::Produce(DataTransfer* aDataTransfer, bool* aCanDrag,
                                   Selection** aSelection,
                                   nsIContent** aDragNode,
                                   nsIContentSecurityPolicy** aCsp,
                                   nsICookieJarSettings** aCookieJarSettings) {
  MOZ_ASSERT(aCanDrag && aSelection && aDataTransfer && aDragNode,
             "null pointer passed to Produce");
  NS_ASSERTION(mWindow, "window not set");
  NS_ASSERTION(mSelectionTargetNode,
               "selection target node should have been set");

  *aDragNode = nullptr;

  nsresult rv;
  nsIContent* dragNode = nullptr;
  *aSelection = nullptr;

  // Find the selection to see what we could be dragging and if what we're
  // dragging is in what is selected. If this is an editable textbox, use
  // the textbox's selection, otherwise use the window's selection.
  RefPtr<Selection> selection;
  nsIContent* editingElement = mSelectionTargetNode->IsEditable()
                                   ? mSelectionTargetNode->GetEditingHost()
                                   : nullptr;
  RefPtr<TextControlElement> textControlElement =
      TextControlElement::GetTextControlElementFromEditingHost(editingElement);
  if (textControlElement) {
    nsISelectionController* selcon =
        textControlElement->GetSelectionController();
    if (selcon) {
      selection =
          selcon->GetSelection(nsISelectionController::SELECTION_NORMAL);
    }

    if (!selection) return NS_OK;
  } else {
    selection = mWindow->GetSelection();
    if (!selection) return NS_OK;

    // Check if the node is inside a form control. Don't set aCanDrag to false
    // however, as we still want to allow the drag.
    nsCOMPtr<nsIContent> findFormNode = mSelectionTargetNode;
    nsIContent* findFormParent = findFormNode->GetParent();
    while (findFormParent) {
      const auto* form = nsIFormControl::FromNode(findFormParent);
      if (form && !form->AllowDraggableChildren()) {
        return NS_OK;
      }
      findFormParent = findFormParent->GetParent();
    }
  }

  // if set, serialize the content under this node
  nsCOMPtr<nsIContent> nodeToSerialize;

  BrowsingContext* bc = mWindow->GetBrowsingContext();
  const bool isChromeShell = bc && bc->IsChrome();

  // In chrome shells, only allow dragging inside editable areas.
  if (isChromeShell && !editingElement) {
    // This path should already be filtered out in
    // EventStateManager::DetermineDragTargetAndDefaultData.
    MOZ_ASSERT_UNREACHABLE("Shouldn't be generating drag data for chrome");
    return NS_OK;
  }

  if (isChromeShell && textControlElement) {
    // Only use the selection if the target node is in the selection.
    if (!selection->ContainsNode(*mSelectionTargetNode, false, IgnoreErrors()))
      return NS_OK;

    selection.swap(*aSelection);
  } else {
    // In content shells, a number of checks are made below to determine
    // whether an image or a link is being dragged. If so, add additional
    // data to the data transfer. This is also done for chrome shells, but
    // only when in a non-textbox editor.

    bool haveSelectedContent = false;

    // only drag form elements by using the alt key,
    // otherwise buttons and select widgets are hard to use

    // Note that while <object> elements implement nsIFormControl, we should
    // really allow dragging them if they happen to be images.
    if (!mIsAltKeyPressed) {
      const auto* form = nsIFormControl::FromNodeOrNull(mTarget);
      if (form && form->ControlType() != FormControlType::Object) {
        *aCanDrag = false;
        return NS_OK;
      }
    }

    // possible parent link node
    nsCOMPtr<nsIContent> parentLink;
    nsCOMPtr<nsIContent> draggedNode = FindDragTarget(mTarget);

    nsCOMPtr<nsIImageLoadingContent> image;

    nsCOMPtr<nsIContent> selectedImageOrLinkNode;
    GetDraggableSelectionData(selection, mSelectionTargetNode,
                              getter_AddRefs(selectedImageOrLinkNode),
                              &haveSelectedContent);

    // either plain text or anchor text is selected
    if (haveSelectedContent) {
      selection.swap(*aSelection);
    } else if (selectedImageOrLinkNode) {
      // an image is selected
      image = do_QueryInterface(selectedImageOrLinkNode);
    } else {
      // nothing is selected -
      //
      // look for draggable elements under the mouse
      //
      // if the alt key is down, don't start a drag if we're in an
      // anchor because we want to do selection.
      parentLink = nsContentUtils::GetClosestLinkInFlatTree(draggedNode);
      if (parentLink && mIsAltKeyPressed) {
        *aCanDrag = false;
        return NS_OK;
      }
      image = do_QueryInterface(draggedNode);
    }

    {
      // set for linked images, and links
      nsCOMPtr<nsIContent> linkNode;
      if (const auto* areaElem = HTMLAreaElement::FromNodeOrNull(draggedNode)) {
        // use the alt text (or, if missing, the href) as the title
        areaElem->GetAttr(nsGkAtoms::alt, mTitleString);
        if (mTitleString.IsEmpty()) {
          // this can be a relative link
          areaElem->GetAttr(nsGkAtoms::href, mTitleString);
        }

        // gives an absolute link
        nsresult rv = GetAnchorURL(draggedNode, mUrlString);
        NS_ENSURE_SUCCESS(rv, rv);

        // we'll generate HTML like <a href="absurl">alt text</a>
        mIsAnchor = true;

        mHtmlString.AssignLiteral("<a href=\"");
        mHtmlString.Append(mUrlString);
        mHtmlString.AppendLiteral("\">");
        mHtmlString.Append(mTitleString);
        mHtmlString.AppendLiteral("</a>");

        dragNode = draggedNode;
      } else if (image) {
        // grab the href as the url, use alt text as the title of the
        // area if it's there.  the drag data is the image tag and src
        // attribute.
        nsCOMPtr<nsIURI> imageURI;
        image->GetCurrentURI(getter_AddRefs(imageURI));
        nsCOMPtr<Element> imageElement(do_QueryInterface(image));
        if (imageURI) {
          nsAutoCString spec;
          rv = imageURI->GetSpec(spec);
          NS_ENSURE_SUCCESS(rv, rv);
          nsIScriptSecurityManager* secMan =
              nsContentUtils::GetSecurityManager();
          rv = secMan->CheckLoadURIStrWithPrincipal(
              imageElement->NodePrincipal(), spec, 0);
          NS_ENSURE_SUCCESS(rv, rv);
          mIsAnchor = true;
          CopyUTF8toUTF16(spec, mUrlString);
        }

        // XXXbz Shouldn't we use the "title" attr for title?  Using
        // "alt" seems very wrong....
        // XXXbz Also, what if this is an nsIImageLoadingContent
        // that's not an <html:img>?
        if (imageElement) {
          imageElement->GetAttr(nsGkAtoms::alt, mTitleString);
        }

        if (mTitleString.IsEmpty()) {
          mTitleString = mUrlString;
        }

        nsCOMPtr<imgIRequest> imgRequest;

        // grab the image data, and its request.
        nsCOMPtr<imgIContainer> img = nsContentUtils::GetImageFromContent(
            image, getter_AddRefs(imgRequest));
        if (imgRequest) {
          rv = GetImageData(img, imgRequest);
          NS_ENSURE_SUCCESS(rv, rv);
        }

        if (parentLink) {
          // If we are dragging around an image in an anchor, then we
          // are dragging the entire anchor
          linkNode = parentLink;
          nodeToSerialize = linkNode;
        } else {
          nodeToSerialize = draggedNode;
        }
        dragNode = nodeToSerialize;
      } else if (parentLink) {
        // parentLink will always be null if there's selected content
        linkNode = parentLink;
        nodeToSerialize = linkNode;
      } else if (!haveSelectedContent) {
        // nothing draggable
        return NS_OK;
      }

      if (linkNode) {
        rv = GetAnchorURL(linkNode, mUrlString);
        NS_ENSURE_SUCCESS(rv, rv);
        mIsAnchor = true;
        dragNode = linkNode;
      }
    }
  }

  if (nodeToSerialize || *aSelection) {
    mHtmlString.Truncate();
    mContextString.Truncate();
    mInfoString.Truncate();
    mTitleString.Truncate();

    nsCOMPtr<Document> doc = mWindow->GetDoc();
    NS_ENSURE_TRUE(doc, NS_ERROR_FAILURE);

    nsCOMPtr<nsIContentSecurityPolicy> csp = doc->GetCsp();
    if (csp) {
      NS_IF_ADDREF(*aCsp = csp);
    }

    nsCOMPtr<nsICookieJarSettings> cookieJarSettings = doc->CookieJarSettings();
    if (cookieJarSettings) {
      NS_IF_ADDREF(*aCookieJarSettings = cookieJarSettings);
    }

    // if we have selected text, use it in preference to the node
    nsCOMPtr<nsITransferable> transferable;
    if (*aSelection) {
      rv = nsCopySupport::GetTransferableForSelection(
          *aSelection, doc, getter_AddRefs(transferable));
    } else {
      rv = nsCopySupport::GetTransferableForNode(nodeToSerialize, doc,
                                                 getter_AddRefs(transferable));
    }
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsISupports> supports;
    nsCOMPtr<nsISupportsString> data;
    rv = transferable->GetTransferData(kHTMLMime, getter_AddRefs(supports));
    data = do_QueryInterface(supports);
    if (NS_SUCCEEDED(rv)) {
      data->GetData(mHtmlString);
    }
    rv = transferable->GetTransferData(kHTMLContext, getter_AddRefs(supports));
    data = do_QueryInterface(supports);
    if (NS_SUCCEEDED(rv)) {
      data->GetData(mContextString);
    }
    rv = transferable->GetTransferData(kHTMLInfo, getter_AddRefs(supports));
    data = do_QueryInterface(supports);
    if (NS_SUCCEEDED(rv)) {
      data->GetData(mInfoString);
    }
    rv = transferable->GetTransferData(kTextMime, getter_AddRefs(supports));
    data = do_QueryInterface(supports);
    NS_ENSURE_SUCCESS(rv, rv);  // require plain text at a minimum
    data->GetData(mTitleString);
  }

  // default text value is the URL
  if (mTitleString.IsEmpty()) {
    mTitleString = mUrlString;
  }

  // if we haven't constructed a html version, make one now
  if (mHtmlString.IsEmpty() && !mUrlString.IsEmpty())
    CreateLinkText(mUrlString, mTitleString, mHtmlString);

  // if there is no drag node, which will be the case for a selection, just
  // use the selection target node.
  rv = AddStringsToDataTransfer(
      dragNode ? dragNode : mSelectionTargetNode.get(), aDataTransfer);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_IF_ADDREF(*aDragNode = dragNode);
  return NS_OK;
}

void DragDataProducer::AddString(DataTransfer* aDataTransfer,
                                 const nsAString& aFlavor,
                                 const nsAString& aData,
                                 nsIPrincipal* aPrincipal, bool aHidden) {
  RefPtr<nsVariantCC> variant = new nsVariantCC();
  variant->SetAsAString(aData);
  aDataTransfer->SetDataWithPrincipal(aFlavor, variant, 0, aPrincipal, aHidden);
}

nsresult DragDataProducer::AddStringsToDataTransfer(
    nsIContent* aDragNode, DataTransfer* aDataTransfer) {
  NS_ASSERTION(aDragNode, "adding strings for null node");

  // set all of the data to have the principal of the node where the data came
  // from
  nsIPrincipal* principal = aDragNode->NodePrincipal();

  // add a special flavor if we're an anchor to indicate that we have
  // a URL in the drag data
  if (!mUrlString.IsEmpty() && mIsAnchor) {
    nsAutoString dragData(mUrlString);
    dragData.Append('\n');
    // Remove leading and trailing newlines in the title and replace them with
    // space in remaining positions - they confuse PlacesUtils::unwrapNodes
    // that expects url\ntitle formatted data for x-moz-url.
    nsAutoString title(mTitleString);
    title.Trim("\r\n");
    title.ReplaceChar(u"\r\n", ' ');
    dragData += title;

    AddString(aDataTransfer, NS_LITERAL_STRING_FROM_CSTRING(kURLMime), dragData,
              principal);
    AddString(aDataTransfer, NS_LITERAL_STRING_FROM_CSTRING(kURLDataMime),
              mUrlString, principal);
    AddString(aDataTransfer,
              NS_LITERAL_STRING_FROM_CSTRING(kURLDescriptionMime), mTitleString,
              principal);
    AddString(aDataTransfer, u"text/uri-list"_ns, mUrlString, principal);
  }

  // add a special flavor for the html context data
  if (!mContextString.IsEmpty())
    AddString(aDataTransfer, NS_LITERAL_STRING_FROM_CSTRING(kHTMLContext),
              mContextString, principal);

  // add a special flavor if we have html info data
  if (!mInfoString.IsEmpty())
    AddString(aDataTransfer, NS_LITERAL_STRING_FROM_CSTRING(kHTMLInfo),
              mInfoString, principal);

  // add the full html
  if (!mHtmlString.IsEmpty())
    AddString(aDataTransfer, NS_LITERAL_STRING_FROM_CSTRING(kHTMLMime),
              mHtmlString, principal);

  // add the plain text. we use the url for text/plain data if an anchor is
  // being dragged, rather than the title text of the link or the alt text for
  // an anchor image.
  AddString(aDataTransfer, NS_LITERAL_STRING_FROM_CSTRING(kTextMime),
            mIsAnchor ? mUrlString : mTitleString, principal);

  // add image data, if present. For now, all we're going to do with
  // this is turn it into a native data flavor, so indicate that with
  // a new flavor so as not to confuse anyone who is really registered
  // for image/gif or image/jpg.
  if (mImage) {
    RefPtr<nsVariantCC> variant = new nsVariantCC();
    variant->SetAsISupports(mImage);
    aDataTransfer->SetDataWithPrincipal(
        NS_LITERAL_STRING_FROM_CSTRING(kNativeImageMime), variant, 0,
        principal);

    // assume the image comes from a file, and add a file promise. We
    // register ourselves as a nsIFlavorDataProvider, and will use the
    // GetFlavorData callback to save the image to disk.

    nsCOMPtr<nsIFlavorDataProvider> dataProvider =
        new nsContentAreaDragDropDataProvider();
    if (dataProvider) {
      RefPtr<nsVariantCC> variant = new nsVariantCC();
      variant->SetAsISupports(dataProvider);
      aDataTransfer->SetDataWithPrincipal(
          NS_LITERAL_STRING_FROM_CSTRING(kFilePromiseMime), variant, 0,
          principal);
    }

    AddString(aDataTransfer,
              NS_LITERAL_STRING_FROM_CSTRING(kFilePromiseURLMime),
              mImageSourceString, principal);
    AddString(aDataTransfer,
              NS_LITERAL_STRING_FROM_CSTRING(kFilePromiseDestFilename),
              mImageDestFileName, principal);
#if defined(XP_MACOSX)
    AddString(aDataTransfer, NS_LITERAL_STRING_FROM_CSTRING(kImageRequestMime),
              mImageRequestMime, principal, /* aHidden= */ true);
#endif

    // if not an anchor, add the image url
    if (!mIsAnchor) {
      AddString(aDataTransfer, NS_LITERAL_STRING_FROM_CSTRING(kURLDataMime),
                mUrlString, principal);
      AddString(aDataTransfer, u"text/uri-list"_ns, mUrlString, principal);
    }
  }

  return NS_OK;
}

// note that this can return NS_OK, but a null out param (by design)
// static
nsresult DragDataProducer::GetDraggableSelectionData(
    Selection* inSelection, nsIContent* inRealTargetNode,
    nsIContent** outImageOrLinkNode, bool* outDragSelectedText) {
  NS_ENSURE_ARG(inSelection);
  NS_ENSURE_ARG(inRealTargetNode);
  NS_ENSURE_ARG_POINTER(outImageOrLinkNode);

  *outImageOrLinkNode = nullptr;
  *outDragSelectedText = false;

  if (!inSelection->AreNormalAndCrossShadowBoundaryRangesCollapsed()) {
    if (inSelection->ContainsNode(*inRealTargetNode, false, IgnoreErrors())) {
      // track down the anchor node, if any, for the url
      nsINode* selectionStart =
          inSelection->GetMayCrossShadowBoundaryAnchorNode();
      nsINode* selectionEnd = inSelection->GetMayCrossShadowBoundaryFocusNode();

      // look for a selection around a single node, like an image.
      // in this case, drag the image, rather than a serialization of the HTML
      // XXX generalize this to other draggable element types?
      if (selectionStart == selectionEnd) {
        nsCOMPtr<nsIContent> selStartContent =
            nsIContent::FromNodeOrNull(selectionStart);
        if (selStartContent && selStartContent->HasChildNodes()) {
          // see if just one node is selected
          uint32_t anchorOffset = inSelection->AnchorOffset();
          uint32_t focusOffset = inSelection->FocusOffset();
          if (anchorOffset == focusOffset + 1 ||
              focusOffset == anchorOffset + 1) {
            uint32_t childOffset = std::min(anchorOffset, focusOffset);
            nsIContent* childContent =
                selStartContent->GetChildAt_Deprecated(childOffset);
            // if we find an image, we'll fall into the node-dragging code,
            // rather the the selection-dragging code
            if (nsContentUtils::IsDraggableImage(childContent)) {
              NS_ADDREF(*outImageOrLinkNode = childContent);
              return NS_OK;
            }
          }
        }
      }

      // indicate that a link or text is selected
      *outDragSelectedText = true;
    }
  }

  return NS_OK;
}
