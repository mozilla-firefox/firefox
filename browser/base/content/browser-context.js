/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

document.addEventListener(
  "DOMContentLoaded",
  () => {
    const IS_WEBEXT_PANELS =
      document.documentElement.id === "webextpanels-window";

    const contextMenuPopup = document.getElementById("contentAreaContextMenu");

    // eslint-disable-next-line complexity
    contextMenuPopup.addEventListener("command", event => {
      switch (event.target.id) {
        case "context-viewsource-goToLine":
          gViewSourceUtils
            .getPageActor(gContextMenu.browser)
            .promptAndGoToLine();
          break;
        case "context-viewsource-wrapLongLines":
          Services.prefs.setBoolPref(
            "view_source.wrap_long_lines",
            !Services.prefs.getBoolPref("view_source.wrap_long_lines", false)
          );
          break;
        case "context-viewsource-highlightSyntax":
          Services.prefs.setBoolPref(
            "view_source.syntax_highlight",
            !Services.prefs.getBoolPref("view_source.syntax_highlight", false)
          );
          break;
        case "spell-add-to-dictionary":
          InlineSpellCheckerUI.addToDictionary();
          break;
        case "spell-undo-add-to-dictionary":
          InlineSpellCheckerUI.undoAddToDictionary();
          break;
        case "context-openlinkincurrent":
          gContextMenu.openLinkInCurrent();
          break;
        case "context-openlinkincontainertab":
        case "context-openlinkintab":
          gContextMenu.openLinkInTab(event);
          break;
        case "context-openlink":
          gContextMenu.openLink();
          break;
        case "context-openlinkinsplitview":
          gContextMenu.openLinkInSplitView(event);
          break;
        case "context-openlinkprivate":
          gContextMenu.openLinkInPrivateWindow();
          break;
        case "context-openlinksmartwindow":
          gContextMenu.openLinkInSmartWindow();
          break;
        case "context-bookmarklink":
          gContextMenu.bookmarkLink();
          break;
        case "context-savelink":
          gContextMenu.saveLink();
          break;
        case "context-copyemail":
          gContextMenu.copyEmail();
          break;
        case "context-copyphone":
          gContextMenu.copyPhone();
          break;
        case "context-copylink":
          gContextMenu.copyLink();
          break;
        case "context-previewlink":
          gContextMenu.previewLink();
          break;
        case "context-stripOnShareLink":
          gContextMenu.copyStrippedLink();
          break;
        case "context-media-play":
          gContextMenu.mediaCommand("play");
          break;
        case "context-media-pause":
          gContextMenu.mediaCommand("pause");
          break;
        case "context-media-mute":
          gContextMenu.mediaCommand("mute");
          break;
        case "context-media-unmute":
          gContextMenu.mediaCommand("unmute");
          break;
        case "context-media-playbackrate-050x":
          gContextMenu.mediaCommand("playbackRate", 0.5);
          break;
        case "context-media-playbackrate-100x":
          gContextMenu.mediaCommand("playbackRate", 1.0);
          break;
        case "context-media-playbackrate-125x":
          gContextMenu.mediaCommand("playbackRate", 1.25);
          break;
        case "context-media-playbackrate-150x":
          gContextMenu.mediaCommand("playbackRate", 1.5);
          break;
        case "context-media-playbackrate-200x":
          gContextMenu.mediaCommand("playbackRate", 2.0);
          break;
        case "context-media-loop":
          gContextMenu.mediaCommand("loop");
          break;
        case "context-leave-dom-fullscreen":
          gContextMenu.leaveDOMFullScreen();
          break;
        case "context-video-fullscreen":
          gContextMenu.mediaCommand("fullscreen");
          break;
        case "context-media-hidecontrols":
          gContextMenu.mediaCommand("hidecontrols");
          break;
        case "context-media-showcontrols":
          gContextMenu.mediaCommand("showcontrols");
          break;
        case "context-viewimage":
        case "context-viewvideo":
          gContextMenu.viewMedia(event);
          break;
        case "context-video-pictureinpicture":
          gContextMenu.mediaCommand("pictureinpicture");
          break;
        case "context-reloadimage":
          gContextMenu.reloadImage();
          break;
        case "context-video-saveimage":
          gContextMenu.saveVideoFrameAsImage();
          break;
        case "context-saveaudio":
        case "context-saveimage":
        case "context-savevideo":
          gContextMenu.saveMedia();
          break;
        case "context-copyimage-contents":
          if (gContextMenu.onCanvas) {
            gContextMenu.copyCanvasImage();
          } else {
            goDoCommand("cmd_copyImage");
          }
          break;
        case "context-copyaudiourl":
        case "context-copyimage":
        case "context-copyvideourl":
          gContextMenu.copyMediaLocation();
          break;
        case "context-sendaudio":
        case "context-sendimage":
        case "context-sendvideo":
          gContextMenu.sendMedia();
          break;
        case "context-imagetext":
          gContextMenu.getImageText();
          break;
        case "context-viewimageinfo":
          gContextMenu.viewImageInfo();
          break;
        case "context-viewimagedesc":
          gContextMenu.viewImageDesc(event);
          break;
        case "context-setDesktopBackground":
          gContextMenu.setDesktopBackground();
          break;
        case "context-bookmarkpage":
          gContextMenu.bookmarkThisPage();
          break;
        case "context-savepage":
          gContextMenu.savePageAs();
          break;
        case "fill-login-generated-password":
          gContextMenu.useGeneratedPassword();
          break;
        case "use-relay-mask":
          gContextMenu.useRelayMask();
          break;
        case "manage-saved-logins":
          gContextMenu.openPasswordManager();
          break;
        case "context-reveal-password":
          gContextMenu.toggleRevealPassword();
          break;
        case "context-print-selection":
          gContextMenu.printSelection();
          break;
        case "context-take-screenshot":
          gContextMenu.takeScreenshot();
          break;
        case "context-use-mini-window":
          gContextMenu.useMiniWindow();
          break;
        case "context-add-engine":
          if (!gContextMenu) {
            throw new Error("Context menu doesn't seem to be open.");
          }
          gContextMenu.addSearchFieldAsEngine().catch(console.error);
          break;
        case "context-searchselect":
        case "context-searchselect-private":
          gContextMenu.loadSearch({ event });
          break;
        case "context-visual-search": {
          let { SearchUtils } = ChromeUtils.importESModule(
            "moz-src:///toolkit/components/search/SearchUtils.sys.mjs"
          );
          gContextMenu.loadSearch({
            event,
            searchUrlType: SearchUtils.URL_TYPE.VISUAL_SEARCH,
          });
          break;
        }
        case "context-translate-selection":
          gContextMenu.openSelectTranslationsPanel(event);
          break;
        case "context-turn-on-highlight-menu":
          gContextMenu.turnOnHighlightMenu();
          break;
        case "context-create-aitab":
          gContextMenu.createAITab();
          break;
        case "context-showonlythisframe":
          gContextMenu.showOnlyThisFrame();
          break;
        case "context-openframeintab":
          gContextMenu.openFrameInTab();
          break;
        case "context-openframe":
          gContextMenu.openFrame();
          break;
        case "context-reloadframe":
          gContextMenu.reloadFrame(event);
          break;
        case "context-bookmarkframe":
          gContextMenu.addBookmarkForFrame();
          break;
        case "context-saveframe":
          gContextMenu.saveFrame();
          break;
        case "context-printframe":
          gContextMenu.printFrame();
          break;
        case "context-viewframesource":
          gContextMenu.viewFrameSource();
          break;
        case "context-viewframeinfo":
          gContextMenu.viewFrameInfo();
          break;
        case "spell-check-enabled":
          InlineSpellCheckerUI.toggleEnabled(window);
          break;
        case "spell-add-dictionaries":
        case "spell-add-dictionaries-main":
          gContextMenu.addDictionaries();
          break;
        case "context-bidi-page-direction-toggle":
          gContextMenu.switchPageDirection();
          break;
        case "context-viewpartialsource-selection":
          gContextMenu.viewPartialSource();
          break;
        case "context-viewsource":
          BrowserCommands.viewSource(gContextMenu.browser);
          break;
        case "context-inspect-a11y":
          gContextMenu.inspectA11Y();
          break;
        case "context-inspect":
          gContextMenu.inspectNode();
          break;
        case "context-media-eme-learnmore":
          gContextMenu.drmLearnMore(event);
          break;
        case "context-copy-link-to-highlight":
          gContextMenu.copyLinkToHighlight();
          break;
        case "context-copy-clean-link-to-highlight":
          gContextMenu.copyLinkToHighlight(/* stripSiteTracking */ true);
          break;
        case "context-remove-highlight":
          gContextMenu.removeAllTextFragments();
          break;
      }
      if (event.target.id.startsWith("context-pdfjs-")) {
        gContextMenu.pdfjsContextMenu.cmd(event.target.id);
      }
    });
    contextMenuPopup.addEventListener("popupshowing", event => {
      switch (event.target.id) {
        case "contentAreaContextMenu": {
          // eslint-disable-next-line no-global-assign
          gContextMenu = new nsContextMenu(contextMenuPopup, event.shiftKey);
          if (!gContextMenu.shouldDisplay) {
            event.preventDefault();
            return;
          }

          if (!IS_WEBEXT_PANELS) {
            updateEditUIVisibility();
          }

          // attempts to generate the text fragment directive of selected text
          // Note: This is kicking off an async operation that might update
          // the context menu while it's open (enables an entry).
          if (gContextMenu.isContentSelected || gContextMenu.hasTextFragments) {
            gContextMenu.getTextDirective();
          }
          break;
        }
        case "context-openlinkinusercontext-popup":
          gContextMenu.createContainerMenu(event);
          break;
        case "context-sendlinktodevice-popup":
          gSync.populateSendTabToDevicesMenu(
            event.target,
            gContextMenu.linkURI,
            gContextMenu.linkTextStr,
            {
              contextMenuType: "link",
            }
          );
          break;
        case "context-sendpagetodevice-popup":
          gSync.populateSendTabToDevicesMenu(
            event.target,
            gBrowser.currentURI,
            gBrowser.contentTitle,
            {
              contextMenuType: "page",
            }
          );
          break;
      }
    });
    contextMenuPopup.addEventListener("popuphiding", event => {
      if (event.target != contextMenuPopup) {
        return;
      }

      gContextMenu.hiding(contextMenuPopup);
      // eslint-disable-next-line no-global-assign
      gContextMenu = null;
      if (!IS_WEBEXT_PANELS) {
        updateEditUIVisibility();
      }
      // Reset Send Tab exposure tracking when context menu closes
      gSync._resetSendTabExposureTracking();
    });

    // The command events bubble up to the popup element.
    let userContextPopup = document.getElementById(
      "context-openlinkinusercontext-popup"
    );
    userContextPopup.addEventListener("command", event =>
      gContextMenu.openLinkInTab(event)
    );
  },
  { once: true }
);
