/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContentDOMReference: "resource://gre/modules/ContentDOMReference.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  InlineSpellCheckerContent:
    "resource://gre/modules/InlineSpellCheckerContent.sys.mjs",
  LoginHelperDOM:
    "moz-src:///toolkit/components/passwordmgr/shared/LoginHelperDOM.sys.mjs",
  LoginManagerChild: "resource://gre/modules/LoginManagerChild.sys.mjs",
  SelectionUtils: "resource://gre/modules/SelectionUtils.sys.mjs",
  SpellCheckHelper: "resource://gre/modules/InlineSpellChecker.sys.mjs",
});

let contextMenus = new WeakMap();

export class ContextMenuChild extends JSWindowActorChild {
  // PUBLIC
  constructor() {
    super();

    this.target = null;
    this.context = null;
    this.lastMenuTarget = null;
  }

  static getTarget(browsingContext, message, key) {
    let actor = contextMenus.get(browsingContext);
    if (!actor) {
      throw new Error(
        "Can't find ContextMenu actor for browsing context with " +
          "ID: " +
          browsingContext.id
      );
    }
    return actor.getTarget(message, key);
  }

  static getLastTarget(browsingContext) {
    let contextMenu = contextMenus.get(browsingContext);
    return contextMenu && contextMenu.lastMenuTarget;
  }

  receiveMessage(message) {
    switch (message.name) {
      case "ContextMenu:GetFrameTitle": {
        let target = lazy.ContentDOMReference.resolve(
          message.data.targetIdentifier
        );
        return Promise.resolve(target.ownerDocument.title);
      }

      case "ContextMenu:Canvas:ToBlobURL": {
        let target = lazy.ContentDOMReference.resolve(
          message.data.targetIdentifier
        );
        return new Promise(resolve => {
          target.toBlob(blob => {
            let blobURL = this.contentWindow.URL.createObjectURL(blob);
            resolve(blobURL);
          });
        });
      }

      case "ContextMenu:Canvas:ToBlob": {
        let target = lazy.ContentDOMReference.resolve(
          message.data.targetIdentifier
        );
        return new Promise(resolve => {
          target.toBlob(blob => resolve(blob));
        });
      }

      case "ContextMenu:Hiding": {
        this.context = null;
        this.target = null;
        break;
      }

      case "ContextMenu:MediaCommand": {
        lazy.E10SUtils.wrapHandlingUserInput(
          this.contentWindow,
          message.data.handlingUserInput,
          () => {
            let media = lazy.ContentDOMReference.resolve(
              message.data.targetIdentifier
            );

            switch (message.data.command) {
              case "play":
                media.play();
                break;
              case "pause":
                media.pause();
                break;
              case "loop":
                media.loop = !media.loop;
                break;
              case "mute":
                media.muted = true;
                break;
              case "unmute":
                media.muted = false;
                break;
              case "playbackRate":
                media.playbackRate = message.data.data;
                break;
              case "hidecontrols":
                media.removeAttribute("controls");
                break;
              case "showcontrols":
                media.setAttribute("controls", "true");
                break;
              case "fullscreen":
                if (this.document.fullscreenEnabled) {
                  media.requestFullscreen();
                }
                break;
              case "pictureinpicture": {
                let event = new this.contentWindow.CustomEvent(
                  "MozTogglePictureInPicture",
                  {
                    bubbles: true,
                    detail: { reason: "ContextMenu" },
                  },
                  this.contentWindow
                );
                this.contentWindow.windowUtils.dispatchEventToChromeOnly(
                  media,
                  event
                );
                break;
              }
            }
          }
        );
        break;
      }

      case "ContextMenu:ReloadFrame": {
        let target = lazy.ContentDOMReference.resolve(
          message.data.targetIdentifier
        );
        target.ownerDocument.location.reload(message.data.forceReload);
        break;
      }

      case "ContextMenu:GetImageText": {
        let img = lazy.ContentDOMReference.resolve(
          message.data.targetIdentifier
        );
        const { direction } = this.contentWindow.getComputedStyle(img);

        return img.recognizeCurrentImageText().then(results => {
          return { results, direction };
        });
      }

      case "ContextMenu:ToggleRevealPassword": {
        let target = lazy.ContentDOMReference.resolve(
          message.data.targetIdentifier
        );
        target.revealPassword = !target.revealPassword;
        break;
      }

      case "ContextMenu:UseRelayMask": {
        const input = lazy.ContentDOMReference.resolve(
          message.data.targetIdentifier
        );
        input.setUserInput(message.data.emailMask);
        break;
      }

      case "ContextMenu:ReloadImage": {
        let image = lazy.ContentDOMReference.resolve(
          message.data.targetIdentifier
        );

        if (image instanceof Ci.nsIImageLoadingContent) {
          image.forceReload();
        }
        break;
      }

      case "ContextMenu:SearchFieldEngineData": {
        let node = lazy.ContentDOMReference.resolve(
          message.data.targetIdentifier
        );
        let charset = node.ownerDocument.characterSet;
        let formBaseURI = Services.io.newURI(node.form.baseURI, charset);
        let method = node.form.method.toUpperCase();

        let formData = new FormData(node.form);
        formData.set(node.name, "{searchTerms}");

        let url = Services.io.newURI(
          node.form.getAttribute("action"),
          charset,
          formBaseURI
        ).spec;

        if (
          !node.name ||
          (method != "POST" && method != "GET") ||
          node.form.enctype != "application/x-www-form-urlencoded" ||
          formData.values().some(v => typeof v != "string")
        ) {
          // This should never happen since these conditions are checked in
          // `isTargetASearchEngineField`.
          return Promise.reject("Cannot create search engine from this form.");
        }

        return Promise.resolve({ url, formData, charset, method });
      }

      case "ContextMenu:SaveVideoFrameAsImage": {
        let video = lazy.ContentDOMReference.resolve(
          message.data.targetIdentifier
        );
        let canvas = this.document.createElementNS(
          "http://www.w3.org/1999/xhtml",
          "canvas"
        );
        canvas.width = video.videoWidth;
        canvas.height = video.videoHeight;

        let ctxDraw = canvas.getContext("2d");
        ctxDraw.drawImage(video, 0, 0);

        // Note: if changing the content type, don't forget to update
        // consumers that also hardcode this content type.
        return Promise.resolve(canvas.toDataURL("image/jpeg", ""));
      }

      case "ContextMenu:SetAsDesktopBackground": {
        let target = lazy.ContentDOMReference.resolve(
          message.data.targetIdentifier
        );

        // Paranoia: check disableSetDesktopBackground again, in case the
        // image changed since the context menu was initiated.
        let disable = this._disableSetDesktopBackground(target);

        if (!disable) {
          try {
            let canvas;
            if (this.contentWindow.HTMLCanvasElement.isInstance(target)) {
              canvas = target;
            } else {
              Services.scriptSecurityManager.checkLoadURIWithPrincipal(
                target.ownerDocument.nodePrincipal,
                target.currentURI
              );
              canvas = this.document.createElement("canvas");
              canvas.width = target.naturalWidth;
              canvas.height = target.naturalHeight;
              let ctx = canvas.getContext("2d");
              ctx.drawImage(target, 0, 0);
            }
            let dataURL = canvas.toDataURL();
            return Promise.resolve({ failed: false, dataURL });
          } catch (e) {
            console.error(e);
          }
        }

        return Promise.resolve({
          failed: true,
          dataURL: null,
        });
      }

      case "ContextMenu:GetTextDirective": {
        const sel = this.contentWindow.getSelection();
        const ranges = !sel.isCollapsed
          ? Array.from({ length: sel.rangeCount }, (_, i) => sel.getRangeAt(i))
          : this.document.fragmentDirective.getTextDirectiveRanges();
        return ranges
          ? this.document.fragmentDirective
              .createTextDirectiveForRanges(ranges)
              .then(textFragment => {
                if (textFragment) {
                  let url = URL.fromURI(this.document?.documentURIObject);
                  url.hash += `:~:${textFragment}`;
                  return url.href;
                }
                return null;
              })
          : null;
      }
      case "ContextMenu:RemoveAllTextFragments": {
        this.document.fragmentDirective.removeAllTextDirectives();
        this.contentWindow.history.replaceState(
          this.contentWindow.history.state,
          "",
          this.contentWindow.location.href
        );
      }
    }

    return undefined;
  }

  /**
   * Returns the event target of the context menu, using a locally stored
   * reference if possible. If not, and aMessage.objects is defined,
   * aMessage.objects[aKey] is returned. Otherwise null.
   *
   * @param  {object} aMessage Message with a objects property
   * @param  {string} aKey     Key for the target on aMessage.objects
   * @return {object}          Context menu target
   */
  getTarget(aMessage, aKey = "target") {
    return this.target || (aMessage.objects && aMessage.objects[aKey]);
  }

  // PRIVATE
  _isXULTextLinkLabel(aNode) {
    const XUL_NS =
      "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
    return (
      aNode.namespaceURI == XUL_NS &&
      aNode.tagName == "label" &&
      aNode.classList.contains("text-link") &&
      aNode.href
    );
  }

  // Generate fully qualified URL for clicked-on link.
  _getLinkURL() {
    let href = this.context.link.href;

    if (href) {
      // Handle SVG links:
      if (typeof href == "object" && href.animVal) {
        return new URL(href.animVal, this.context.link.baseURI).href;
      }

      return href;
    }

    href =
      this.context.link.getAttribute("href") ||
      this.context.link.getAttributeNS("http://www.w3.org/1999/xlink", "href");

    if (!href || !href.match(/\S/)) {
      // Without this we try to save as the current doc,
      // for example, HTML case also throws if empty
      throw new Error("Empty href");
    }

    return new URL(href, this.context.link.baseURI).href;
  }

  _getLinkURI() {
    try {
      return Services.io.newURI(this.context.linkURL);
    } catch (ex) {
      // e.g. empty URL string
    }

    return null;
  }

  // Get text of link.
  _getLinkText() {
    let text = this._gatherTextUnder(this.context.link);

    if (!text || !text.match(/\S/)) {
      text = this.context.link.getAttribute("title");
      if (!text || !text.match(/\S/)) {
        text = this.context.link.getAttribute("alt");
        if (!text || !text.match(/\S/)) {
          text = this.context.linkURL;
        }
      }
    }

    return text;
  }

  _getLinkProtocol() {
    if (this.context.linkURI) {
      return this.context.linkURI.scheme; // can be |undefined|
    }

    return null;
  }

  // Returns true if clicked-on link targets a resource that can be saved.
  _isLinkSaveable() {
    // We don't do the Right Thing for news/snews yet, so turn them off
    // until we do.
    return (
      this.context.linkProtocol &&
      !(
        this.context.linkProtocol == "mailto" ||
        this.context.linkProtocol == "tel" ||
        this.context.linkProtocol == "javascript" ||
        this.context.linkProtocol == "news" ||
        this.context.linkProtocol == "snews"
      )
    );
  }

  // Gather all descendent text under given document node.
  // NOTE: Keep this in sync with gatherTextUnder in
  // browser/base/content/utilityOverlay.js
  _gatherTextUnder(root) {
    const encoder = Cu.createDocumentEncoder("text/plain");
    encoder.init(root.ownerDocument, "text/plain", 0);
    encoder.setContainerNode(root);
    return encoder.encodeToString().trim();
  }

  // Returns a "url"-type computed style attribute value, with the url() stripped.
  _getComputedURL(aElem, aProp) {
    let urls = aElem.documentGlobal
      .getComputedStyle(aElem)
      .getCSSImageURLs(aProp);

    if (!urls.length) {
      return null;
    }

    if (urls.length != 1) {
      throw new Error("found multiple URLs");
    }

    return urls[0];
  }

  _isProprietaryDRM() {
    return (
      this.context.target.isEncrypted &&
      this.context.target.mediaKeys &&
      this.context.target.mediaKeys.keySystem != "org.w3.clearkey"
    );
  }

  _isMediaURLReusable(aURL) {
    if (aURL.startsWith("blob:")) {
      return URL.isBoundToBlob(aURL);
    }

    return true;
  }

  /**
   * Finds a video element at the given coordinates using nodesFromRect,
   * which can detect videos beneath overlays (e.g. custom player controls).
   *
   * @param {number} clientX
   * @param {number} clientY
   * @returns {HTMLVideoElement|null}
   */
  _maybeGetVideoElementAtPoint(clientX, clientY) {
    if (
      !Services.prefs.getBoolPref(
        "media.contextmenu.video-overlay-detection",
        false
      )
    ) {
      return null;
    }

    let elements = this.contentWindow.windowUtils.nodesFromRect(
      clientX,
      clientY,
      1,
      1,
      1,
      1,
      true,
      false,
      true,
      0
    );
    for (let el of elements) {
      if (this.contentWindow.HTMLVideoElement.isInstance(el)) {
        return el;
      }
    }
    return null;
  }

  _isTargetATextBox(node) {
    if (this.contentWindow.HTMLInputElement.isInstance(node)) {
      return node.mozIsTextField(false);
    }

    return this.contentWindow.HTMLTextAreaElement.isInstance(node);
  }

  _isSpellCheckEnabled(aNode) {
    // We can always force-enable spellchecking on textboxes
    if (this._isTargetATextBox(aNode)) {
      return true;
    }

    // We can never spell check something which is not content editable
    let editable = aNode.isContentEditable;

    if (!editable && aNode.ownerDocument) {
      editable = aNode.ownerDocument.designMode == "on";
    }

    if (!editable) {
      return false;
    }

    // Otherwise make sure that nothing in the parent chain disables spellchecking
    return aNode.spellcheck;
  }

  _disableSetDesktopBackground(aTarget) {
    if (this.contentWindow.HTMLCanvasElement.isInstance(aTarget)) {
      return false;
    }

    // Disable the Set as Desktop Background menu item if we're still trying
    // to load the image or the load failed.
    if (!(aTarget instanceof Ci.nsIImageLoadingContent)) {
      return true;
    }

    if ("complete" in aTarget && !aTarget.complete) {
      return true;
    }

    if (aTarget.currentURI.schemeIs("javascript")) {
      return true;
    }

    let request = aTarget.getRequest(Ci.nsIImageLoadingContent.CURRENT_REQUEST);

    if (!request) {
      return true;
    }

    return false;
  }

  async handleEvent(aEvent) {
    contextMenus.set(this.browsingContext, this);

    let defaultPrevented = aEvent.defaultPrevented;

    if (
      // If the event is not from a chrome-privileged document, and if
      // `dom.event.contextmenu.enabled` is false, force defaultPrevented=false.
      !aEvent.composedTarget.nodePrincipal.isSystemPrincipal &&
      !Services.prefs.getBoolPref("dom.event.contextmenu.enabled")
    ) {
      defaultPrevented = false;
    }

    if (defaultPrevented) {
      return;
    }

    let doc = aEvent.composedTarget.ownerDocument;
    if (!doc && Cu.isInAutomation) {
      // doc has been observed to be null for many years, causing intermittent
      // test failures all over the place (bug 1478596). The rate of failures
      // is too low to debug locally, but frequent enough to be a nuisance.
      // TODO bug 1478596: use these diagnostic logs to resolve the bug.
      dump(
        `doc is unexpectedly null (bug 1478596), composedTarget=${aEvent.composedTarget}\n`
      );
      // A potential fix is to fall back to aEvent.target.ownerDocument, per
      // https://bugzilla.mozilla.org/show_bug.cgi?id=1478596#c1
      // Let's print potentially viable alternatives to see what we should use.
      for (let k of ["target", "originalTarget", "explicitOriginalTarget"]) {
        dump(
          ` Alternative: ${k}=${aEvent[k]} and its doc=${aEvent[k]?.ownerDocument}\n`
        );
      }
    }
    let {
      mozDocumentURIIfNotForErrorPages: docLocation,
      characterSet: charSet,
      baseURI,
    } = doc;
    docLocation = docLocation && docLocation.spec;
    const loginManagerChild = lazy.LoginManagerChild.forWindow(doc.defaultView);
    const docState = loginManagerChild.stateForDocument(doc);
    const loginFillInfo = docState.getFieldContext(aEvent.composedTarget);

    let disableSetDesktopBackground = null;

    // Media related cache info parent needs for saving
    let contentType = null;
    let contentDisposition = null;
    let composedTarget = aEvent.composedTarget;
    if (composedTarget.nodeType == composedTarget.ELEMENT_NODE) {
      let isImage =
        composedTarget instanceof Ci.nsIImageLoadingContent &&
        composedTarget.currentURI;
      if (
        isImage ||
        this.contentWindow.HTMLCanvasElement.isInstance(composedTarget)
      ) {
        disableSetDesktopBackground =
          this._disableSetDesktopBackground(composedTarget);
      }
      if (isImage) {
        try {
          let imageCache = Cc["@mozilla.org/image/tools;1"]
            .getService(Ci.imgITools)
            .getImgCacheForDocument(doc);
          // The image cache's notion of where this image is located is
          // the currentURI of the image loading content.
          let props = imageCache.findEntryProperties(
            aEvent.composedTarget.currentURI,
            doc
          );

          try {
            contentType = props.get("type", Ci.nsISupportsCString).data;
          } catch (e) {}

          try {
            contentDisposition = props.get(
              "content-disposition",
              Ci.nsISupportsCString
            ).data;
          } catch (e) {}
        } catch (e) {}
      }
    }

    let selectionInfo = lazy.SelectionUtils.getSelectionDetails(
      this.contentWindow
    );

    this._setContext(aEvent);
    let context = this.context;
    this.target = context.target;

    let spellInfo = null;
    let editFlags = null;

    let referrerInfo = Cc["@mozilla.org/referrer-info;1"].createInstance(
      Ci.nsIReferrerInfo
    );
    referrerInfo.initWithElement(aEvent.composedTarget);
    referrerInfo = lazy.E10SUtils.serializeReferrerInfo(referrerInfo);

    // In the case "onLink" we may have to send link referrerInfo to use in
    // _openLinkInParameters
    let linkReferrerInfo = null;
    if (context.onLink) {
      linkReferrerInfo = Cc["@mozilla.org/referrer-info;1"].createInstance(
        Ci.nsIReferrerInfo
      );
      linkReferrerInfo.initWithElement(context.link);
    }

    let target = context.target;
    if (target) {
      this._cleanContext();
    }

    editFlags = lazy.SpellCheckHelper.isEditable(
      aEvent.composedTarget,
      this.contentWindow
    );

    if (editFlags & lazy.SpellCheckHelper.SPELLCHECKABLE) {
      spellInfo = lazy.InlineSpellCheckerContent.initContextMenu(
        aEvent,
        editFlags,
        this
      );
    }

    // Set the event target first as the copy image command needs it to
    // determine what was context-clicked on. Then, update the state of the
    // commands on the context menu.
    this.docShell.docViewer
      .QueryInterface(Ci.nsIDocumentViewerEdit)
      .setCommandNode(aEvent.composedTarget);
    aEvent.composedTarget.documentGlobal.updateCommands("contentcontextmenu");

    let data = {
      context,
      charSet,
      baseURI,
      referrerInfo,
      editFlags,
      contentType,
      docLocation,
      loginFillInfo,
      selectionInfo,
      contentDisposition,
      disableSetDesktopBackground,
    };

    if (context.inFrame && !context.inSrcdocFrame) {
      data.frameReferrerInfo = lazy.E10SUtils.serializeReferrerInfo(
        doc.referrerInfo
      );
    }

    if (linkReferrerInfo) {
      data.linkReferrerInfo =
        lazy.E10SUtils.serializeReferrerInfo(linkReferrerInfo);
    }

    // Notify observers (currently only webextensions) of the context menu being
    // prepared, allowing them to set webExtContextData for us.
    let prepareContextMenu = {
      principal: doc.nodePrincipal,
      setWebExtContextData(webExtContextData) {
        data.webExtContextData = webExtContextData;
      },
    };
    Services.obs.notifyObservers(prepareContextMenu, "on-prepare-contextmenu");

    // In the event that the content is running in the parent process, we don't
    // actually want the contextmenu events to reach the parent - we'll dispatch
    // a new contextmenu event after the async message has reached the parent
    // instead.
    aEvent.stopPropagation();

    data.spellInfo = null;
    if (!spellInfo) {
      this.sendAsyncMessage("contextmenu", data);
      return;
    }

    try {
      data.spellInfo = await spellInfo;
    } catch (ex) {}
    this.sendAsyncMessage("contextmenu", data);
  }

  /**
   * Some things are not serializable, so we either have to only send
   * their needed data or regenerate them in nsContextMenu.js
   * - target and target.ownerDocument
   * - link
   * - linkURI
   */
  _cleanContext() {
    const context = this.context;
    const cleanTarget = Object.create(null);

    cleanTarget.ownerDocument = {
      // used for nsContextMenu.initLeaveDOMFullScreenItems and
      // nsContextMenu.initMediaPlayerItems
      fullscreen: context.target.ownerDocument.fullscreen,

      // used for nsContextMenu.initMiscItems
      contentType: context.target.ownerDocument.contentType,
    };

    // used for nsContextMenu.initMediaPlayerItems
    Object.assign(cleanTarget, {
      ended: context.target.ended,
      muted: context.target.muted,
      paused: context.target.paused,
      controls: context.target.controls,
      duration: context.target.duration,
    });

    const onMedia = context.onVideo || context.onAudio;

    if (onMedia) {
      Object.assign(cleanTarget, {
        loop: context.target.loop,
        error: context.target.error,
        networkState: context.target.networkState,
        playbackRate: context.target.playbackRate,
        NETWORK_NO_SOURCE: context.target.NETWORK_NO_SOURCE,
      });

      if (context.onVideo) {
        Object.assign(cleanTarget, {
          readyState: context.target.readyState,
          HAVE_CURRENT_DATA: context.target.HAVE_CURRENT_DATA,
        });
      }
    }

    context.target = cleanTarget;

    if (context.link) {
      context.link = { href: context.linkURL };
    }

    delete context.linkURI;
  }

  _setContext(aEvent) {
    this.context = Object.create(null);
    const context = this.context;

    context.timeStamp = aEvent.timeStamp;
    context.screenXDevPx = aEvent.screenX * this.contentWindow.devicePixelRatio;
    context.screenYDevPx = aEvent.screenY * this.contentWindow.devicePixelRatio;
    context.inputSource = aEvent.inputSource;
    context.clientX = aEvent.clientX;
    context.clientY = aEvent.clientY;

    let node = aEvent.composedTarget;

    // Set the node to containing <video>/<audio>/<embed>/<object> if the node
    // is in the videocontrols UA Widget.
    if (node.containingShadowRoot?.isUAWidget()) {
      const host = node.containingShadowRoot.host;
      if (
        this.contentWindow.HTMLMediaElement.isInstance(host) ||
        this.contentWindow.HTMLEmbedElement.isInstance(host) ||
        this.contentWindow.HTMLObjectElement.isInstance(host)
      ) {
        node = host;
      }
    }

    const XUL_NS =
      "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";

    context.shouldDisplay = true;

    if (
      node.nodeType == node.DOCUMENT_NODE ||
      // Don't display for XUL element unless <label class="text-link">
      (node.namespaceURI == XUL_NS && !this._isXULTextLinkLabel(node))
    ) {
      context.shouldDisplay = false;
      return;
    }

    const isAboutDevtoolsToolbox = this.document.documentURI.startsWith(
      "about:devtools-toolbox"
    );
    const editFlags = lazy.SpellCheckHelper.isEditable(
      node,
      this.contentWindow
    );

    if (
      isAboutDevtoolsToolbox &&
      (editFlags & lazy.SpellCheckHelper.TEXTINPUT) === 0
    ) {
      // Don't display for about:devtools-toolbox page unless the source was text input.
      context.shouldDisplay = false;
      return;
    }

    // Initialize context to be sent to nsContextMenu
    // Keep this consistent with the similar code in nsContextMenu's setContext
    context.bgImageURL = "";
    context.imageDescURL = "";
    context.imageInfo = null;
    context.mediaURL = "";
    context.webExtBrowserType = "";

    context.canSpellCheck = false;
    context.hasBGImage = false;
    context.hasMultipleBGImages = false;
    context.isDesignMode = false;
    context.inFrame = false;
    context.inPDFViewer = false;
    context.inSrcdocFrame = false;
    context.inSyntheticDoc = false;
    context.inTabBrowser = true;
    context.inWebExtBrowser = false;

    context.link = null;
    context.linkDownload = "";
    context.linkProtocol = "";
    context.linkTextStr = "";
    context.linkURL = "";
    context.linkURI = null;

    context.onAudio = false;
    context.onCanvas = false;
    context.onCompletedImage = false;
    context.onDRMMedia = false;
    context.onPiPVideo = false;
    context.onEditable = false;
    context.onImage = false;
    context.onLink = false;
    context.onLoadedImage = false;
    context.onMailtoLink = false;
    context.onTelLink = false;
    context.onMozExtLink = false;
    context.onNumeric = false;
    context.onPassword = false;
    context.passwordRevealed = false;
    context.onSaveableLink = false;
    context.onSpellcheckable = false;
    context.onTextInput = false;
    context.onVideo = false;

    const textDirectiveRanges =
      this.document.fragmentDirective?.getTextDirectiveRanges?.() || [];
    // .hasTextFragments indicates whether the page will show highlights.
    context.hasTextFragments = !!textDirectiveRanges.length;

    // Remember the node and its owner document that was clicked
    // This may be modifed before sending to nsContextMenu
    context.target = node;
    context.targetIdentifier = lazy.ContentDOMReference.get(node);

    context.policyContainer = lazy.E10SUtils.serializePolicyContainer(
      context.target.ownerDocument.policyContainer
    );

    // Check if we are in the PDF Viewer.
    context.inPDFViewer =
      context.target.ownerDocument.nodePrincipal.originNoSuffix ==
      "resource://pdf.js";
    if (context.inPDFViewer) {
      context.pdfStates = context.target.ownerDocument.pdfStates;
    }

    // Check if we are in a synthetic document (stand alone image, video, etc.).
    context.inSyntheticDoc = context.target.ownerDocument.mozSyntheticDocument;

    context.shouldInitInlineSpellCheckerUINoChildren = false;
    context.shouldInitInlineSpellCheckerUIWithChildren = false;

    this._setContextForNodesNoChildren(editFlags);
    this._setContextForNodesWithChildren(editFlags);

    this.lastMenuTarget = {
      // Remember the node for extensions.
      targetRef: Cu.getWeakReference(node),
      // The timestamp is used to verify that the target wasn't changed since the observed menu event.
      timeStamp: context.timeStamp,
    };

    if (isAboutDevtoolsToolbox) {
      // Setup the menu items on text input in about:devtools-toolbox.
      context.inAboutDevtoolsToolbox = true;
      context.canSpellCheck = false;
      context.inTabBrowser = false;
      context.inFrame = false;
      context.inSrcdocFrame = false;
      context.onSpellcheckable = false;
    }
  }

  /**
   * Sets up the parts of the context menu for when when nodes have no children.
   *
   * @param {Integer} editFlags The edit flags for the node. See SpellCheckHelper
   *                            for the details.
   */
  _setContextForNodesNoChildren(editFlags) {
    const context = this.context;

    if (context.target.nodeType == context.target.TEXT_NODE) {
      // For text nodes, look at the parent node to determine the spellcheck attribute.
      context.canSpellCheck =
        context.target.parentNode && this._isSpellCheckEnabled(context.target);
      return;
    }

    // We only deal with TEXT_NODE and ELEMENT_NODE in this function, so return
    // early if we don't have one.
    if (context.target.nodeType != context.target.ELEMENT_NODE) {
      return;
    }

    let videoElement;

    // See if the user clicked on an image. This check mirrors
    // nsDocumentViewer::GetInImage. Make sure to update both if this is
    // changed.
    if (
      context.target instanceof Ci.nsIImageLoadingContent &&
      (context.target.currentRequestFinalURI || context.target.currentURI)
    ) {
      context.onImage = true;

      context.imageInfo = {
        currentSrc: context.target.currentSrc,
        width: context.target.width,
        height: context.target.height,
        imageText: this.contentWindow.ImageDocument.isInstance(
          context.target.ownerDocument
        )
          ? undefined
          : context.target.title || context.target.alt,
      };
      if (SVGAnimatedLength.isInstance(context.imageInfo.height)) {
        context.imageInfo.height = context.imageInfo.height.animVal.value;
      }
      if (SVGAnimatedLength.isInstance(context.imageInfo.width)) {
        context.imageInfo.width = context.imageInfo.width.animVal.value;
      }

      const request = context.target.getRequest(
        Ci.nsIImageLoadingContent.CURRENT_REQUEST
      );

      if (request && request.imageStatus & request.STATUS_SIZE_AVAILABLE) {
        context.onLoadedImage = true;
      }

      if (
        request &&
        request.imageStatus & request.STATUS_LOAD_COMPLETE &&
        !(request.imageStatus & request.STATUS_ERROR)
      ) {
        context.onCompletedImage = true;
      }

      // The URL of the image before redirects is the currentURI.  This is
      // intended to be used for "Copy Image Link".
      context.originalMediaURL = (() => {
        let currentURI = context.target.currentURI?.spec;
        if (currentURI && this._isMediaURLReusable(currentURI)) {
          return currentURI;
        }
        return "";
      })();

      // The actual URL the image was loaded from (after redirects) is the
      // currentRequestFinalURI.  We should use that as the URL for purposes of
      // deciding on the filename, if it is present. It might not be present
      // if images are blocked.
      //
      // It is important to check both the final and the current URI, as they
      // could be different blob URIs, see bug 1625786.
      context.mediaURL = (() => {
        let finalURI = context.target.currentRequestFinalURI?.spec;
        if (finalURI && this._isMediaURLReusable(finalURI)) {
          return finalURI;
        }
        let currentURI = context.target.currentURI?.spec;
        if (currentURI && this._isMediaURLReusable(currentURI)) {
          return currentURI;
        }
        return "";
      })();

      const descURL = context.target.getAttribute("longdesc");

      if (descURL) {
        context.imageDescURL = new URL(
          descURL,
          context.target.ownerDocument.body.baseURI
        ).href;
      }
    } else if (
      this.contentWindow.HTMLCanvasElement.isInstance(context.target)
    ) {
      context.onCanvas = true;
    } else if (
      (videoElement = this.contentWindow.HTMLVideoElement.isInstance(
        context.target
      )
        ? context.target
        : this._maybeGetVideoElementAtPoint(context.clientX, context.clientY))
    ) {
      // If the target isn't already a video, it means we found one under an
      // overlay via nodesFromRect. Update target and targetIdentifier so that
      // context menu actions (play, pause, mute, etc.) operate on the video.
      context.target = videoElement;
      context.targetIdentifier = lazy.ContentDOMReference.get(videoElement);
      const mediaURL = context.target.currentSrc || context.target.src;

      if (this._isMediaURLReusable(mediaURL)) {
        context.mediaURL = mediaURL;
      }

      if (this._isProprietaryDRM()) {
        context.onDRMMedia = true;
      }

      if (context.target.isCloningElementVisually) {
        context.onPiPVideo = true;
      }

      // Firefox always creates a HTMLVideoElement when loading an ogg file
      // directly. If the media is actually audio, be smarter and provide a
      // context menu with audio operations.
      if (
        context.target.readyState >= context.target.HAVE_METADATA &&
        (context.target.videoWidth == 0 || context.target.videoHeight == 0)
      ) {
        context.onAudio = true;
      } else {
        context.onVideo = true;
      }
    } else if (this.contentWindow.HTMLAudioElement.isInstance(context.target)) {
      context.onAudio = true;
      const mediaURL = context.target.currentSrc || context.target.src;

      if (this._isMediaURLReusable(mediaURL)) {
        context.mediaURL = mediaURL;
      }

      if (this._isProprietaryDRM()) {
        context.onDRMMedia = true;
      }
    } else if (
      editFlags &
      (lazy.SpellCheckHelper.INPUT | lazy.SpellCheckHelper.TEXTAREA)
    ) {
      context.onTextInput = (editFlags & lazy.SpellCheckHelper.TEXTINPUT) !== 0;
      context.onNumeric = (editFlags & lazy.SpellCheckHelper.NUMERIC) !== 0;
      context.onEditable = (editFlags & lazy.SpellCheckHelper.EDITABLE) !== 0;
      context.onPassword = (editFlags & lazy.SpellCheckHelper.PASSWORD) !== 0;

      context.showRelay =
        HTMLInputElement.isInstance(context.target) &&
        !context.target.disabled &&
        !context.target.readOnly &&
        (lazy.LoginHelperDOM.isInferredEmailField(context.target) ||
          lazy.LoginHelperDOM.isInferredUsernameField(context.target));
      context.isDesignMode =
        (editFlags & lazy.SpellCheckHelper.CONTENTEDITABLE) !== 0;
      context.passwordRevealed =
        context.onPassword && context.target.revealPassword;
      context.onSpellcheckable =
        (editFlags & lazy.SpellCheckHelper.SPELLCHECKABLE) !== 0;

      // This is guaranteed to be an input or textarea because of the condition above,
      // so the no-children flag is always correct. We deal with contenteditable elsewhere.
      if (context.onSpellcheckable) {
        context.shouldInitInlineSpellCheckerUINoChildren = true;
      }

      context.onSearchField = editFlags & lazy.SpellCheckHelper.SEARCHENGINE;
    } else if (this.contentWindow.HTMLHtmlElement.isInstance(context.target)) {
      const bodyElt = context.target.ownerDocument.body;

      if (bodyElt) {
        let computedURL;

        try {
          computedURL = this._getComputedURL(bodyElt, "background-image");
          context.hasMultipleBGImages = false;
        } catch (e) {
          context.hasMultipleBGImages = true;
        }

        if (computedURL) {
          context.hasBGImage = true;
          context.bgImageURL = new URL(computedURL, bodyElt.baseURI).href;
        }
      }
    }

    context.canSpellCheck = this._isSpellCheckEnabled(context.target);
  }

  /**
   * Sets up the parts of the context menu for when when nodes have children.
   *
   * @param {Integer} editFlags The edit flags for the node. See SpellCheckHelper
   *                            for the details.
   */
  _setContextForNodesWithChildren(editFlags) {
    const context = this.context;

    // Second, bubble out, looking for items of interest that can have childen.
    // Always pick the innermost link, background image, etc.
    let elem = context.target;

    while (elem) {
      if (elem.nodeType == elem.ELEMENT_NODE) {
        // Link?
        const XLINK_NS = "http://www.w3.org/1999/xlink";

        if (
          !context.onLink &&
          // Be consistent with what hrefAndLinkNodeForClickEvent
          // does in BrowserUtils.sys.msj
          ((this.contentWindow.HTMLAnchorElement.isInstance(elem) &&
            elem.href) ||
            (this.contentWindow.HTMLAreaElement.isInstance(elem) &&
              elem.href) ||
            this.contentWindow.HTMLLinkElement.isInstance(elem) ||
            (this.contentWindow.SVGAElement.isInstance(elem) &&
              (elem.href || elem.hasAttributeNS(XLINK_NS, "href"))) ||
            (this.contentWindow.MathMLElement.isInstance(elem) &&
              (elem.localName == "a" ||
                !Services.prefs.getBoolPref(
                  "mathml.href_link_on_non_anchor_element.disabled"
                )) &&
              elem.hasAttribute("href")) ||
            elem.getAttributeNS(XLINK_NS, "type") == "simple" ||
            this._isXULTextLinkLabel(elem))
        ) {
          // Target is a link or a descendant of a link.
          context.onLink = true;

          // Remember corresponding element.
          context.link = elem;
          context.linkURL = this._getLinkURL();
          context.linkURI = this._getLinkURI();
          context.linkTextStr = this._getLinkText();
          context.linkProtocol = this._getLinkProtocol();
          context.onMailtoLink = context.linkProtocol == "mailto";
          context.onTelLink = context.linkProtocol == "tel";
          context.onMozExtLink = context.linkProtocol == "moz-extension";
          context.onSaveableLink = this._isLinkSaveable(context.link);

          context.isSponsoredLink =
            (elem.ownerDocument.URL === "about:newtab" ||
              elem.ownerDocument.URL === "about:home") &&
            elem.dataset.isSponsoredLink === "true";

          try {
            if (elem.download) {
              // Ignore download attribute on cross-origin links
              context.target.ownerDocument.nodePrincipal.checkMayLoad(
                context.linkURI,
                true
              );
              context.linkDownload = elem.download;
            }
          } catch (ex) {}
        }

        // Background image?  Don't bother if we've already found a
        // background image further down the hierarchy.  Otherwise,
        // we look for the computed background-image style.
        if (!context.hasBGImage && !context.hasMultipleBGImages) {
          let bgImgUrl = null;

          try {
            bgImgUrl = this._getComputedURL(elem, "background-image");
            context.hasMultipleBGImages = false;
          } catch (e) {
            context.hasMultipleBGImages = true;
          }

          if (bgImgUrl) {
            context.hasBGImage = true;
            context.bgImageURL = new URL(bgImgUrl, elem.baseURI).href;
          }
        }
      }

      elem = elem.flattenedTreeParentNode;
    }

    // See if the user clicked in a frame.
    const docDefaultView = context.target.documentGlobal;

    if (docDefaultView != docDefaultView.top) {
      context.inFrame = true;

      if (context.target.ownerDocument.isSrcdocDocument) {
        context.inSrcdocFrame = true;
      }
    }

    // if the document is editable, show context menu like in text inputs
    if (!context.onEditable) {
      if (editFlags & lazy.SpellCheckHelper.CONTENTEDITABLE) {
        // If this.onEditable is false but editFlags is CONTENTEDITABLE, then
        // the document itself must be editable.
        context.onTextInput = true;
        context.onImage = false;
        context.onLoadedImage = false;
        context.onCompletedImage = false;
        context.inFrame = false;
        context.inSrcdocFrame = false;
        context.hasBGImage = false;
        context.isDesignMode = true;
        context.onEditable = true;
        context.onSpellcheckable = true;
        context.shouldInitInlineSpellCheckerUIWithChildren = true;
      }
    }
  }

  _destructionObservers = new Set();
  registerDestructionObserver(obj) {
    this._destructionObservers.add(obj);
  }

  unregisterDestructionObserver(obj) {
    this._destructionObservers.delete(obj);
  }

  didDestroy() {
    for (let obs of this._destructionObservers) {
      obs.actorDestroyed(this);
    }
    this._destructionObservers = null;
  }
}
