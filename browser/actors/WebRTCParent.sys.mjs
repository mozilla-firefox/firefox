/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SitePermissions: "resource:///modules/SitePermissions.sys.mjs",
  webrtcUI: "resource:///modules/webrtcUI.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "OSPermissions",
  "@mozilla.org/ospermissionrequest;1",
  Ci.nsIOSPermissionRequest
);

export class WebRTCParent extends JSWindowActorParent {
  didDestroy() {
    // Media stream tracks end on unload, so call stopRecording() on them early
    // *before* we go away, to ensure we're working with the right principal.
    this.stopRecording(this.manager.outerWindowId);
    lazy.webrtcUI.forgetStreamsFromBrowserContext(this.browsingContext);
    // Must clear activePerms here to prevent them from being read by laggard
    // stopRecording() calls, which due to IPC, may come in *after* navigation.
    // This is to prevent granting temporary grace periods to the wrong page.
    lazy.webrtcUI.activePerms.delete(this.manager.outerWindowId);
  }

  getBrowser() {
    return this.browsingContext.top.embedderElement;
  }

  receiveMessage(aMessage) {
    switch (aMessage.name) {
      case "rtcpeer:Request": {
        let params = Object.freeze(
          Object.assign(
            {
              origin: this.manager.documentPrincipal.origin,
            },
            aMessage.data
          )
        );

        let blockers = Array.from(lazy.webrtcUI.peerConnectionBlockers);

        (async function () {
          for (let blocker of blockers) {
            try {
              let result = await blocker(params);
              if (result == "deny") {
                return false;
              }
            } catch (err) {
              console.error(`error in PeerConnection blocker: ${err.message}`);
            }
          }
          return true;
        })().then(decision => {
          let message;
          if (decision) {
            lazy.webrtcUI.emitter.emit("peer-request-allowed", params);
            message = "rtcpeer:Allow";
          } else {
            lazy.webrtcUI.emitter.emit("peer-request-blocked", params);
            message = "rtcpeer:Deny";
          }

          this.sendAsyncMessage(message, {
            callID: params.callID,
            windowID: params.windowID,
          });
        });
        break;
      }
      case "rtcpeer:CancelRequest": {
        let params = Object.freeze({
          origin: this.manager.documentPrincipal.origin,
          callID: aMessage.data,
        });
        lazy.webrtcUI.emitter.emit("peer-request-cancel", params);
        break;
      }
      case "webrtc:Request": {
        let data = aMessage.data;

        // Record third party origins for telemetry.
        let isThirdPartyOrigin =
          this.manager.documentPrincipal.origin !=
          this.manager.topWindowContext.documentPrincipal.origin;
        data.isThirdPartyOrigin = isThirdPartyOrigin;

        data.origin = this.manager.topWindowContext.documentPrincipal.origin;

        prompt(this, this.getBrowser(), data);
        break;
      }
      case "webrtc:StopRecording":
        this.stopRecording(
          aMessage.data.windowID,
          aMessage.data.mediaSource,
          aMessage.data.rawID
        );
        break;
      case "webrtc:CancelRequest": {
        let browser = this.getBrowser();
        // browser can be null when closing the window
        if (browser) {
          removePrompt(browser, aMessage.data);
        }
        break;
      }
      case "webrtc:UpdateIndicators": {
        let { data } = aMessage;
        data.documentURI = this.manager.documentURI?.spec;
        if (data.windowId) {
          if (!data.remove) {
            data.principal = this.manager.topWindowContext.documentPrincipal;
          }
          lazy.webrtcUI.streamAddedOrRemoved(this.browsingContext, data);
        }
        this.updateIndicators(data);
        break;
      }
    }
  }

  updateIndicators(aData) {
    let browsingContext = this.browsingContext;
    let state = lazy.webrtcUI.updateIndicators(browsingContext.top);

    let browser = this.getBrowser();
    if (!browser) {
      return;
    }

    state.browsingContext = browsingContext;
    state.windowId = aData.windowId;

    let tabbrowser = browser.documentGlobal.gBrowser;
    if (tabbrowser) {
      tabbrowser.updateBrowserSharing(browser, {
        webRTC: state,
      });
    }

    if (isSidebarBrowser(browser)) {
      browser._sharingState = { webRTC: state };
      browser.browsingContext.topChromeWindow.SidebarController?._permissions.updateFromBrowserState(
        browser._sharingState
      );
    }
  }

  denyRequest(aRequest) {
    this.sendAsyncMessage("webrtc:Deny", {
      callID: aRequest.callID,
      windowID: aRequest.windowID,
    });
  }

  //
  // Deny the request because the browser does not have access to the
  // camera or microphone due to OS security restrictions. The user may
  // have granted camera/microphone access to the site, but not have
  // allowed the browser access in OS settings.
  //
  denyRequestNoPermission(aRequest) {
    this.sendAsyncMessage("webrtc:Deny", {
      callID: aRequest.callID,
      windowID: aRequest.windowID,
      noOSPermission: true,
    });
  }

  //
  // Check if we have permission to access the camera or screen-sharing and/or
  // microphone at the OS level. Triggers a request to access the device if access
  // is needed and the permission state has not yet been determined.
  //
  async checkOSPermission(camNeeded, micNeeded, scrNeeded) {
    // Don't trigger OS permission requests for fake devices. Fake devices don't
    // require OS permission and the dialogs are problematic in automated testing
    // (where fake devices are used) because they require user interaction.
    if (
      !scrNeeded &&
      Services.prefs.getBoolPref("media.navigator.streams.fake", false)
    ) {
      return true;
    }
    let camStatus = {},
      micStatus = {};
    if (camNeeded || micNeeded) {
      lazy.OSPermissions.getMediaCapturePermissionState(camStatus, micStatus);
    }
    if (camNeeded) {
      let camPermission = camStatus.value;
      let camAccessible = await this.checkAndGetOSPermission(
        camPermission,
        lazy.OSPermissions.requestVideoCapturePermission
      );
      if (!camAccessible) {
        return false;
      }
    }
    if (micNeeded) {
      let micPermission = micStatus.value;
      let micAccessible = await this.checkAndGetOSPermission(
        micPermission,
        lazy.OSPermissions.requestAudioCapturePermission
      );
      if (!micAccessible) {
        return false;
      }
    }
    let scrStatus = {};
    if (scrNeeded) {
      lazy.OSPermissions.getScreenCapturePermissionState(scrStatus);
      if (scrStatus.value == lazy.OSPermissions.PERMISSION_STATE_DENIED) {
        lazy.OSPermissions.maybeRequestScreenCapturePermission();
        return false;
      }
    }
    return true;
  }

  //
  // Given a device's permission, return true if the device is accessible. If
  // the device's permission is not yet determined, request access to the device.
  // |requestPermissionFunc| must return a promise that resolves with true
  // if the device is accessible and false otherwise.
  //
  async checkAndGetOSPermission(devicePermission, requestPermissionFunc) {
    if (
      devicePermission == lazy.OSPermissions.PERMISSION_STATE_DENIED ||
      devicePermission == lazy.OSPermissions.PERMISSION_STATE_RESTRICTED
    ) {
      return false;
    }
    if (devicePermission == lazy.OSPermissions.PERMISSION_STATE_NOTDETERMINED) {
      let deviceAllowed = await requestPermissionFunc();
      if (!deviceAllowed) {
        return false;
      }
    }
    return true;
  }

  stopRecording(aOuterWindowId, aMediaSource, aRawId) {
    for (let { browsingContext, state } of lazy.webrtcUI._streams) {
      if (browsingContext == this.browsingContext) {
        let { principal } = state;
        for (let { mediaSource, rawId } of state.devices) {
          if (aRawId && (aRawId != rawId || aMediaSource != mediaSource)) {
            continue;
          }
          // Deactivate this device (no aRawId means all devices).
          this.deactivateDevicePerm(
            aOuterWindowId,
            mediaSource,
            rawId,
            principal
          );
        }
      }
    }
  }

  /**
   * Add a device record to webrtcUI.activePerms, denoting a device as in use.
   * Important to call for permission grace periods to work correctly.
   */
  activateDevicePerm(aOuterWindowId, aMediaSource, aId) {
    if (!lazy.webrtcUI.activePerms.has(this.manager.outerWindowId)) {
      lazy.webrtcUI.activePerms.set(this.manager.outerWindowId, new Map());
    }
    lazy.webrtcUI.activePerms
      .get(this.manager.outerWindowId)
      .set(aOuterWindowId + aMediaSource + aId, aMediaSource);
  }

  /**
   * Remove a device record from webrtcUI.activePerms, denoting a device as
   * no longer in use by the site. Meaning: gUM requests for this device will
   * no longer be implicitly granted through the webrtcUI.activePerms mechanism.
   *
   * However, if webrtcUI.deviceGracePeriodTimeoutMs is defined, the implicit
   * grant is extended for an additional period of time through SitePermissions.
   */
  deactivateDevicePerm(
    aOuterWindowId,
    aMediaSource,
    aId,
    aPermissionPrincipal
  ) {
    // If we don't have active permissions for the given window anymore don't
    // set a grace period. This happens if there has been a user revoke and
    // webrtcUI clears the permissions.
    if (!lazy.webrtcUI.activePerms.has(this.manager.outerWindowId)) {
      return;
    }
    let map = lazy.webrtcUI.activePerms.get(this.manager.outerWindowId);
    map.delete(aOuterWindowId + aMediaSource + aId);

    // Add a permission grace period for camera and microphone only
    if (
      (aMediaSource != "camera" && aMediaSource != "microphone") ||
      !this.browsingContext.top.embedderElement
    ) {
      return;
    }
    let gracePeriodMs = lazy.webrtcUI.deviceGracePeriodTimeoutMs;
    if (gracePeriodMs > 0) {
      // A grace period is extended (even past navigation) to this outer window
      // + origin + deviceId only. This avoids re-prompting without the user
      // having to persist permission to the site, in a common case of a web
      // conference asking them for the camera in a lobby page, before
      // navigating to the actual meeting room page. Does not survive tab close.
      //
      // Caution: since navigation causes deactivation, we may be in the middle
      // of one. We must pass in a principal & URI for SitePermissions to use
      // instead of browser.currentURI, because the latter may point to a new
      // page already, and we must not leak permission to unrelated pages.
      //
      let permissionName = [aMediaSource, aId].join("^");
      lazy.SitePermissions.setForPrincipal(
        aPermissionPrincipal,
        permissionName,
        lazy.SitePermissions.ALLOW,
        lazy.SitePermissions.SCOPE_TEMPORARY,
        this.browsingContext.top.embedderElement,
        gracePeriodMs
      );
    }
  }

  /**
   * Checks if the principal has sufficient permissions
   * to fulfill the given request. If the request can be
   * fulfilled, a message is sent to the child
   * signaling that WebRTC permissions were given and
   * this function will return true.
   */
  checkRequestAllowed(aRequest, aPrincipal) {
    if (!aRequest.secure) {
      return false;
    }
    // Always prompt for screen sharing
    if (aRequest.sharingScreen) {
      return false;
    }
    let {
      callID,
      windowID,
      audioInputDevices,
      videoInputDevices,
      audioOutputDevices,
      hasInherentAudioConstraints,
      hasInherentVideoConstraints,
      audioOutputId,
    } = aRequest;

    if (audioOutputDevices?.length) {
      // Prompt if a specific device is not requested, available and allowed.
      let device = audioOutputDevices.find(({ id }) => id == audioOutputId);
      if (
        !device ||
        !lazy.SitePermissions.getForPrincipal(
          aPrincipal,
          ["speaker", device.id].join("^"),
          this.getBrowser()
        ).state == lazy.SitePermissions.ALLOW
      ) {
        return false;
      }
      this.sendAsyncMessage("webrtc:Allow", {
        callID,
        windowID,
        devices: [device.deviceIndex],
      });
      return true;
    }

    let { perms } = Services;
    if (
      perms.testExactPermissionFromPrincipal(aPrincipal, "MediaManagerVideo")
    ) {
      perms.removeFromPrincipal(aPrincipal, "MediaManagerVideo");
    }

    // Don't use persistent permissions from the top-level principal
    // if we're handling a potentially insecure third party
    // through a wildcard ("*") allow attribute.
    let limited = aRequest.secondOrigin;

    let map = lazy.webrtcUI.activePerms.get(this.manager.outerWindowId);
    // We consider a camera or mic active if it is active or was active within a
    // grace period of milliseconds ago.
    const isAllowed = ({ mediaSource, rawId }, permissionID) =>
      map?.get(windowID + mediaSource + rawId) ||
      (!limited &&
        (lazy.SitePermissions.getForPrincipal(aPrincipal, permissionID).state ==
          lazy.SitePermissions.ALLOW ||
          lazy.SitePermissions.getForPrincipal(
            aPrincipal,
            [mediaSource, rawId].join("^"),
            this.getBrowser()
          ).state == lazy.SitePermissions.ALLOW));

    let microphone;
    if (audioInputDevices.length) {
      for (let device of audioInputDevices) {
        if (isAllowed(device, "microphone")) {
          microphone = device;
          break;
        }
        if (hasInherentAudioConstraints) {
          // Inherent constraints suggest site is looking for a specific mic
          break;
        }
        // Some sites don't look too hard at what they get, and spam gUM without
        // adjusting what they ask for to match what they got last time. To keep
        // users in charge and reduce prompts, ignore other constraints by
        // returning the most-fit microphone a site already has access to.
      }
      if (!microphone) {
        return false;
      }
    }
    let camera;
    if (videoInputDevices.length) {
      for (let device of videoInputDevices) {
        if (isAllowed(device, "camera")) {
          camera = device;
          break;
        }
        if (hasInherentVideoConstraints) {
          // Inherent constraints suggest site is looking for a specific camera
          break;
        }
        // Some sites don't look too hard at what they get, and spam gUM without
        // adjusting what they ask for to match what they got last time. To keep
        // users in charge and reduce prompts, ignore other constraints by
        // returning the most-fit camera a site already has access to.
      }
      if (!camera) {
        return false;
      }
    }
    let devices = [];
    if (camera) {
      perms.addFromPrincipal(
        aPrincipal,
        "MediaManagerVideo",
        perms.ALLOW_ACTION,
        perms.EXPIRE_SESSION
      );
      devices.push(camera.deviceIndex);
      this.activateDevicePerm(windowID, camera.mediaSource, camera.rawId);
    }
    if (microphone) {
      devices.push(microphone.deviceIndex);
      this.activateDevicePerm(
        windowID,
        microphone.mediaSource,
        microphone.rawId
      );
    }
    this.checkOSPermission(!!camera, !!microphone, false).then(
      havePermission => {
        if (havePermission) {
          this.sendAsyncMessage("webrtc:Allow", { callID, windowID, devices });
        } else {
          this.denyRequestNoPermission(aRequest);
        }
      }
    );
    return true;
  }
}

function prompt(aActor, aBrowser, aRequest) {
  let {
    audioInputDevices,
    videoInputDevices,
    audioOutputDevices,
    sharingScreen,
    sharingAudio,
    requestTypes,
    isHandlingUserInput,
  } = aRequest;

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      aRequest.origin
    );

  // For add-on principals, we immediately check for permission instead
  // of waiting for the notification to focus. This allows for supporting
  // cases such as browserAction popups where no prompt is shown.
  if (principal.addonPolicy) {
    let isPopup = false;
    let isBackground = false;

    for (let view of principal.addonPolicy.extension.views) {
      if (view.viewType == "popup" && view.xulBrowser == aBrowser) {
        isPopup = true;
      }
      if (view.viewType == "background" && view.xulBrowser == aBrowser) {
        isBackground = true;
      }
    }

    // Recording from background pages is considered too sensitive and will
    // always be denied.
    if (isBackground) {
      aActor.denyRequest(aRequest);
      return;
    }

    // If the request comes from a popup, we don't want to show the prompt,
    // but we do want to allow the request if the user previously gave permission.
    if (isPopup) {
      if (!aActor.checkRequestAllowed(aRequest, principal, aBrowser)) {
        aActor.denyRequest(aRequest);
      }
      return;
    }
  }

  // If the user has already denied access once in this tab,
  // deny again without even showing the notification icon.
  for (const type of requestTypes) {
    const permissionID =
      type == "AudioCapture" ? "microphone" : type.toLowerCase();
    if (
      lazy.SitePermissions.getForPrincipal(principal, permissionID, aBrowser)
        .state == lazy.SitePermissions.BLOCK
    ) {
      aActor.denyRequest(aRequest);
      return;
    }
  }

  // promptBrowser drives UI placement only. Permissions, grace periods and
  // indicators stay keyed on aBrowser, since that's what owns the stream.
  let promptBrowser = getPromptBrowser(aBrowser, aRequest);

  // If promptBrowser is sidebar browser, its ownerDocument is sidebar panel
  // document. We need top chrome window's document to access notification
  // elements.
  let chromeDoc = promptBrowser.browsingContext.topChromeWindow?.document;
  const localization = new Localization(
    ["browser/webrtcIndicator.ftl", "branding/brand.ftl"],
    true
  );

  /** @type {"Screen" | "Camera" | null} */
  let reqVideoInput = null;
  if (videoInputDevices.length) {
    reqVideoInput = sharingScreen ? "Screen" : "Camera";
  }
  /** @type {"AudioCapture" | "Microphone" | null} */
  let reqAudioInput = null;
  if (audioInputDevices.length) {
    reqAudioInput = sharingAudio ? "AudioCapture" : "Microphone";
  }
  const reqAudioOutput = !!audioOutputDevices.length;

  const isFile = principal.schemeIs("file");
  const stringId = getPromptMessageId(
    reqVideoInput,
    reqAudioInput,
    reqAudioOutput,
    !!aRequest.secondOrigin,
    isFile
  );
  let message;
  let originToShow;
  if (isFile) {
    message = localization.formatValueSync(stringId);
    originToShow = null;
  } else {
    message = localization.formatValueSync(stringId, {
      origin: "<>",
      thirdParty: "{}",
    });
    originToShow = lazy.webrtcUI.getHostOrExtensionName(principal.URI);
  }
  let notification; // Used by action callbacks.
  let stopWatchingPromptWindow; // Set when prompting in a document PiP window.
  const actionL10nIds = [{ id: "webrtc-action-allow" }];

  let notificationSilencingEnabled = Services.prefs.getBoolPref(
    "privacy.webrtc.allowSilencingNotifications"
  );

  const isNotNowLabelEnabled =
    reqAudioOutput || allowedOrActiveCameraOrMicrophone(aBrowser);
  let secondaryActions = [];
  if (reqAudioOutput || (notificationSilencingEnabled && sharingScreen)) {
    // We want to free up the checkbox at the bottom of the permission
    // panel for the notification silencing option, so we use a
    // different configuration for the permissions panel when
    // notification silencing is enabled.

    let permissionName = reqAudioOutput ? "speaker" : "screen";
    // When selecting speakers, we always offer 'Not now' instead of 'Block'.
    // When selecting screens, we offer 'Not now' if and only if we have a
    // (temporary) allow permission for some mic/cam device.
    const id = isNotNowLabelEnabled
      ? "webrtc-action-not-now"
      : "webrtc-action-block";
    actionL10nIds.push({ id }, { id: "webrtc-action-always-block" });
    secondaryActions = [
      {
        disableSecurityDelay: true,
        callback() {
          aActor.denyRequest(aRequest);
          if (!isNotNowLabelEnabled) {
            lazy.SitePermissions.setForPrincipal(
              principal,
              permissionName,
              lazy.SitePermissions.BLOCK,
              lazy.SitePermissions.SCOPE_TEMPORARY,
              // Resolved now rather than captured above: tearing the tab into
              // its own window replaces the browser element, and temporary
              // permissions are keyed on it.
              aActor.getBrowser()
            );
          }
        },
      },
      {
        disableSecurityDelay: true,
        callback() {
          aActor.denyRequest(aRequest);
          lazy.SitePermissions.setForPrincipal(
            principal,
            permissionName,
            lazy.SitePermissions.BLOCK,
            lazy.SitePermissions.SCOPE_PERSISTENT,
            aActor.getBrowser()
          );
        },
      },
    ];
  } else {
    // We have a (temporary) allow permission for some device
    // hence we offer a 'Not now' label instead of 'Block'.
    const id = isNotNowLabelEnabled
      ? "webrtc-action-not-now"
      : "webrtc-action-block";
    actionL10nIds.push({ id });
    secondaryActions = [
      {
        disableSecurityDelay: true,
        callback(aState) {
          aActor.denyRequest(aRequest);

          const isPersistent = aState?.checkboxChecked;

          // Choosing 'Not now' will not set a block permission
          // we just deny the request. This enables certain use cases
          // where sites want to switch devices, but users back out of the permission request
          // (See Bug 1609578).
          // Selecting 'Remember this decision' and clicking 'Not now' will set a persistent block
          if (!isPersistent && isNotNowLabelEnabled) {
            return;
          }

          // Denying a camera / microphone prompt means we set a temporary or
          // persistent permission block. There may still be active grace period
          // permissions at this point. We need to remove them.
          const actorBrowser = aActor.getBrowser();
          clearTemporaryGrants(
            actorBrowser,
            reqVideoInput === "Camera",
            !!reqAudioInput
          );

          const scope = isPersistent
            ? lazy.SitePermissions.SCOPE_PERSISTENT
            : lazy.SitePermissions.SCOPE_TEMPORARY;
          if (reqAudioInput) {
            if (!isPersistent) {
              // After a temporary block, having permissions.query() calls
              // persistently report "granted" would be misleading
              maybeClearAlwaysAsk(principal, "microphone", actorBrowser);
            }
            lazy.SitePermissions.setForPrincipal(
              principal,
              "microphone",
              lazy.SitePermissions.BLOCK,
              scope,
              actorBrowser
            );
          }
          if (reqVideoInput) {
            if (!isPersistent && !sharingScreen) {
              // After a temporary block, having permissions.query() calls
              // persistently report "granted" would be misleading
              maybeClearAlwaysAsk(principal, "camera", actorBrowser);
            }
            lazy.SitePermissions.setForPrincipal(
              principal,
              sharingScreen ? "screen" : "camera",
              lazy.SitePermissions.BLOCK,
              scope,
              actorBrowser
            );
          }
        },
      },
    ];
  }

  // The formatMessagesSync method returns an array of results
  // for each message that was requested, and for the ones with
  // attributes, returns an attributes array with objects like:
  //     { name: "label", value: "somevalue" }
  const [mainMessage, ...secondaryMessages] = localization
    .formatMessagesSync(actionL10nIds)
    .map(msg =>
      msg.attributes.reduce(
        (acc, { name, value }) => ({ ...acc, [name]: value }),
        {}
      )
    );

  const mainAction = {
    label: mainMessage.label,
    accessKey: mainMessage.accesskey,
    // The real callback will be set during the "showing" event. The
    // empty function here is so that PopupNotifications.show doesn't
    // reject the action.
    callback() {},
  };

  for (let i = 0; i < secondaryActions.length; ++i) {
    secondaryActions[i].label = secondaryMessages[i].label;
    secondaryActions[i].accessKey = secondaryMessages[i].accesskey;
  }

  let options = {
    name: originToShow,
    persistent: true,
    hideClose: true,
    eventCallback(aTopic, aNewBrowser, withoutUserResponse) {
      if (aTopic == "swapping") {
        return true;
      }

      let doc = this.browser?.browsingContext?.topChromeWindow?.document;
      // Sudden sidebar close when PopupNotification is open
      if (!doc) {
        return false;
      }

      // Clean-up video streams of screensharing and camera previews.
      if (
        (reqVideoInput != "Screen" && reqVideoInput != "Camera") ||
        aTopic == "dismissed" ||
        aTopic == "removed"
      ) {
        // Hide the webrtc preview section.
        let webrtcPreviewSection = doc.getElementById("webRTC-preview-section");
        webrtcPreviewSection.hidden = true;

        // Remove event listeners for camera and window selection menus.
        for (const id of [
          "webRTC-selectWindow-menupopup",
          "webRTC-selectCamera-menupopup",
        ]) {
          let menuPopup = doc.getElementById(id);
          if (menuPopup?._commandEventListener) {
            menuPopup.removeEventListener(
              "command",
              menuPopup._commandEventListener
            );
            menuPopup._commandEventListener = null;
          }
        }
      }

      if (aTopic == "removed") {
        stopWatchingPromptWindow?.();
        stopWatchingPromptWindow = null;
      }

      if (aTopic == "removed" && notification && withoutUserResponse) {
        // The notification has been cancelled (e.g. due to entering
        // full-screen).  Also cancel the webRTC request.
        aActor.denyRequest(aRequest);
      } else if (
        aTopic == "shown" &&
        !notification.wasDismissed &&
        reqAudioOutput
      ) {
        let focusElement =
          audioOutputDevices.length > 1
            ? doc.getElementById("webRTC-selectSpeaker-richlistbox") // Focus the list on first show so that arrow keys select the speaker.
            : doc.querySelector("moz-button.popup-notification-primary-button"); // Or if the list is hidden (only 1 device), focus the primary button.
        focusElement.focus();
      }

      const isRequestingCamera = reqVideoInput === "Camera";

      if (aTopic == "shown") {
        if (!notification.wasDismissed && isRequestingCamera) {
          onCameraPromptShown(doc, isHandlingUserInput);
        }
      }

      if (aTopic != "showing") {
        return false;
      }

      // If BLOCK has been set persistently in the permission manager or has
      // been set on the tab, then it is handled synchronously before we add
      // the notification.
      // Handling of ALLOW is delayed until the popupshowing event,
      // to avoid granting permissions automatically to background tabs.
      if (aActor.checkRequestAllowed(aRequest, principal, aBrowser)) {
        this.remove();
        return true;
      }

      /**
       * Prepare the device selector for one kind of device.
       *
       * @param {object[]} devices - available devices of this kind.
       * @param {string} IDPrefix - indicating kind of device and so
       *   associated UI elements.
       * @param {string[]} describedByIDs - an array to which might be
       *   appended ids of elements that describe the panel, for the caller to
       *   use in the aria-describedby attribute.
       */
      function listDevices(devices, IDPrefix, describedByIDs) {
        let labelID = `${IDPrefix}-single-device-label`;
        let list;
        let itemParent;
        if (IDPrefix == "webRTC-selectSpeaker") {
          list = doc.getElementById(`${IDPrefix}-richlistbox`);
          itemParent = list;
        } else {
          itemParent = doc.getElementById(`${IDPrefix}-menupopup`);
          list = itemParent.parentNode; // menulist
        }
        while (itemParent.lastChild) {
          itemParent.removeChild(itemParent.lastChild);
        }

        // Removing the child nodes of a menupopup doesn't clear the value
        // attribute of its menulist. Similary for richlistbox state. This can
        // have unfortunate side effects when the list is rebuilt with a
        // different content, so we set the selectedIndex explicitly to reset
        // state.
        let defaultIndex = 0;

        for (let device of devices) {
          let item = addDeviceToList(list, device.name, device.deviceIndex);
          // Add deviceId property for camera devices so that we can preview them on selection.
          if (IDPrefix == "webRTC-selectCamera") {
            item.deviceId = device.rawId;
          }
          if (IDPrefix == "webRTC-selectSpeaker") {
            item.addEventListener("dblclick", event => {
              // Allow the chosen speakers via
              // .popup-notification-primary-button so that
              // "security.notification_enable_delay" is checked.
              event.target.closest("popupnotification").button.click();
            });
            if (device.id == aRequest.audioOutputId) {
              defaultIndex = device.deviceIndex;
            }
          }
        }
        list.selectedIndex = defaultIndex;

        let label = doc.getElementById(labelID);
        if (devices.length == 1) {
          describedByIDs.push(`${IDPrefix}-icon`, labelID);
          label.value = devices[0].name;
          label.hidden = false;
          list.hidden = true;
        } else {
          label.hidden = true;
          list.hidden = false;
        }
      }

      let notificationElement = doc.getElementById(
        "webRTC-shareDevices-notification"
      );

      function checkDisabledWindowMenuItem() {
        let list = doc.getElementById("webRTC-selectWindow-menulist");
        let item = list.selectedItem;
        if (!item || item.hasAttribute("disabled")) {
          notificationElement.setAttribute("invalidselection", "true");
        } else {
          notificationElement.removeAttribute("invalidselection");
        }
      }

      function listScreenShareDevices(menupopup, devices) {
        while (menupopup.lastChild) {
          menupopup.removeChild(menupopup.lastChild);
        }

        // Removing the child nodes of the menupopup doesn't clear the value
        // attribute of the menulist. This can have unfortunate side effects
        // when the list is rebuilt with a different content, so we remove
        // the value attribute and unset the selectedItem explicitly.
        menupopup.parentNode.removeAttribute("value");
        menupopup.parentNode.selectedItem = null;

        // "Select a Window or Screen" is the default because we can't and don't
        // want to pick a 'default' window to share (Full screen is "scary").
        addDeviceToList(
          menupopup.parentNode,
          localization.formatValueSync("webrtc-pick-window-or-screen"),
          "-1"
        );
        menupopup.appendChild(doc.createXULElement("menuseparator"));

        let isPipeWireDetected = false;

        // Build the list of 'devices'.
        let monitorIndex = 1;
        for (let i = 0; i < devices.length; ++i) {
          let device = devices[i];
          let type = device.mediaSource;
          let name;
          if (device.canRequestOsLevelPrompt) {
            // When we share content by PipeWire add only one item to the device
            // list. When it's selected PipeWire portal dialog is opened and
            // user confirms actual window/screen sharing there.
            // Don't mark it as scary as there's an extra confirmation step by
            // PipeWire portal dialog.

            isPipeWireDetected = true;
            let item = addDeviceToList(
              menupopup.parentNode,
              localization.formatValueSync("webrtc-share-pipe-wire-portal"),
              i,
              type
            );
            item.deviceId = device.rawId;
            item.mediaSource = type;

            // In this case the OS sharing dialog will be the only option and
            // can be safely pre-selected.
            menupopup.parentNode.selectedItem = item;
            continue;
          } else if (type == "screen") {
            // Building screen list from available screens.
            if (device.name == "Primary Monitor") {
              name = localization.formatValueSync("webrtc-share-entire-screen");
            } else {
              name = localization.formatValueSync("webrtc-share-monitor", {
                monitorIndex,
              });
              ++monitorIndex;
            }
          } else {
            name = device.name;

            if (type == "application") {
              // The application names returned by the platform are of the form:
              // <window count>\x1e<application name>
              const [count, appName] = name.split("\x1e");
              name = localization.formatValueSync("webrtc-share-application", {
                appName,
                windowCount: parseInt(count),
              });
            }
          }
          let item = addDeviceToList(menupopup.parentNode, name, i, type);
          item.deviceId = device.rawId;
          item.mediaSource = type;
          if (device.scary) {
            item.scary = true;
          }
        }

        // Always re-select the "No <type>" item.
        doc
          .getElementById("webRTC-selectWindow-menulist")
          .removeAttribute("value");
        doc.getElementById("webRTC-all-windows-shared").hidden = true;

        const webrtcPreview = getOrCreateWebRTCPreviewEl(doc);
        webrtcPreview.showPreviewControlButtons = false;

        menupopup._commandEventListener = event => {
          checkDisabledWindowMenuItem();

          const { deviceId, mediaSource, scary } = event.target;
          if (deviceId == undefined) {
            webrtcPreviewSection.hidden = true;
            return;
          }
          // We have a valid device
          webrtcPreviewSection.hidden = false;

          let warning = doc.getElementById("webRTC-previewWarning");
          let warningBox = doc.getElementById("webRTC-previewWarningBox");
          warningBox.hidden = !scary;
          if (scary) {
            const warnId =
              mediaSource == "screen"
                ? "webrtc-share-screen-warning"
                : "webrtc-share-browser-warning";
            doc.l10n.setAttributes(warning, warnId);

            // On Catalina, we don't want to blow our chance to show the
            // OS-level helper prompt to enable screen recording if the user
            // intends to reject anyway. OTOH showing it when they click Allow
            // is too late. A happy middle is to show it when the user makes a
            // choice in the picker. This already happens implicitly if the
            // user chooses "Entire desktop", as a side-effect of our preview,
            // we just need to also do it if they choose "Firefox". These are
            // the lone two options when permission is absent on Catalina.
            // Ironically, these are the two sources marked "scary" from a
            // web-sharing perspective, which is why this code resides here.
            // A restart doesn't appear to be necessary in spite of OS wording.
            let scrStatus = {};
            lazy.OSPermissions.getScreenCapturePermissionState(scrStatus);
            if (scrStatus.value == lazy.OSPermissions.PERMISSION_STATE_DENIED) {
              lazy.OSPermissions.maybeRequestScreenCapturePermission();
            }
          }

          let perms = Services.perms;
          let chromePrincipal =
            Services.scriptSecurityManager.getSystemPrincipal();
          perms.addFromPrincipal(
            chromePrincipal,
            "MediaManagerVideo",
            perms.ALLOW_ACTION,
            perms.EXPIRE_SESSION
          );

          // We don't have access to any screen content besides our browser tabs
          // on Wayland, therefore there are no previews we can show.
          if (
            (!isPipeWireDetected || mediaSource == "browser") &&
            Services.prefs.getBoolPref(
              "media.getdisplaymedia.previews.enabled",
              true
            )
          ) {
            webrtcPreview.startPreview({
              deviceId,
              mediaSource,
              showPreviewControlButtons: false,
            });
          }
        };
        menupopup.addEventListener("command", menupopup._commandEventListener);
      }

      function addDeviceToList(list, deviceName, deviceIndex, type) {
        let item = list.appendItem(deviceName, deviceIndex);
        item.setAttribute("tooltiptext", deviceName);
        if (type) {
          item.setAttribute("devicetype", type);
        }

        if (deviceIndex == "-1") {
          item.setAttribute("disabled", true);
        }

        return item;
      }

      doc.getElementById("webRTC-selectCamera").hidden = !isRequestingCamera;
      doc.getElementById("webRTC-selectWindowOrScreen").hidden =
        reqVideoInput !== "Screen";
      doc.getElementById("webRTC-selectMicrophone").hidden =
        reqAudioInput !== "Microphone";
      doc.getElementById("webRTC-selectSpeaker").hidden = !reqAudioOutput;

      let describedByIDs = ["webRTC-shareDevices-notification-description"];

      // Hide the webrtc preview section.
      let webrtcPreviewSection = doc.getElementById("webRTC-preview-section");
      webrtcPreviewSection.hidden = true;

      if (sharingScreen) {
        let windowMenupopup = doc.getElementById(
          "webRTC-selectWindow-menupopup"
        );
        listScreenShareDevices(windowMenupopup, videoInputDevices);
        checkDisabledWindowMenuItem();
      } else {
        notificationElement.removeAttribute("invalidselection");

        // Make sure the screen share warning is hidden before showing the permission panel.
        let warningBox = doc.getElementById("webRTC-previewWarningBox");
        warningBox.hidden = true;

        if (isRequestingCamera) {
          listDevices(videoInputDevices, "webRTC-selectCamera", describedByIDs);

          let cameraMenuPopup = doc.getElementById(
            "webRTC-selectCamera-menupopup"
          );

          // Set up initial camera preview.
          let webrtcPreview = getOrCreateWebRTCPreviewEl(doc);
          webrtcPreview.showPreviewControlButtons = true;
          // Apply the initial selection.
          webrtcPreview.deviceId =
            cameraMenuPopup.querySelector("[selected]")?.deviceId;
          webrtcPreview.mediaSource = "camera";

          // Show the preview section.
          webrtcPreviewSection.hidden = false;

          if (cameraMenuPopup) {
            cameraMenuPopup._commandEventListener = event => {
              let { deviceId } = event.target;
              if (deviceId == undefined) {
                webrtcPreviewSection.hidden = true;
                return;
              }
              // Start preview on selection change.
              webrtcPreview.startPreview({ deviceId, mediaSource: "camera" });
            };
            cameraMenuPopup.addEventListener(
              "command",
              cameraMenuPopup._commandEventListener
            );
          }
        }
      }
      if (!sharingAudio) {
        listDevices(
          audioInputDevices,
          "webRTC-selectMicrophone",
          describedByIDs
        );
      }
      listDevices(audioOutputDevices, "webRTC-selectSpeaker", describedByIDs);

      // PopupNotifications knows to clear the aria-describedby attribute
      // when hiding, so we don't have to worry about cleaning it up ourselves.
      chromeDoc.defaultView.PopupNotifications.panel.setAttribute(
        "aria-describedby",
        describedByIDs.join(" ")
      );

      this.mainAction.callback = async function (aState) {
        let remember = false;
        let silenceNotifications = false;

        if (notificationSilencingEnabled && sharingScreen) {
          silenceNotifications = aState && aState.checkboxChecked;
        } else {
          remember = aState && aState.checkboxChecked;
        }

        let allowedDevices = [];
        let perms = Services.perms;
        if (reqVideoInput) {
          let listId = sharingScreen
            ? "webRTC-selectWindow-menulist"
            : "webRTC-selectCamera-menulist";
          let videoDeviceIndex = doc.getElementById(listId).value;
          let allowVideoDevice = videoDeviceIndex != "-1";
          if (allowVideoDevice) {
            allowedDevices.push(videoDeviceIndex);
            // Session permission will be removed after use
            // (it's really one-shot, not for the entire session)
            perms.addFromPrincipal(
              principal,
              "MediaManagerVideo",
              perms.ALLOW_ACTION,
              perms.EXPIRE_SESSION
            );
            let { mediaSource, rawId } = videoInputDevices.find(
              ({ deviceIndex }) => deviceIndex == videoDeviceIndex
            );
            aActor.activateDevicePerm(aRequest.windowID, mediaSource, rawId);
            if (!sharingScreen) {
              persistGrantOrPromptPermission(principal, "camera", remember);
            }
          }
        }

        if (reqAudioInput === "Microphone") {
          let audioDeviceIndex = doc.getElementById(
            "webRTC-selectMicrophone-menulist"
          ).value;
          let allowMic = audioDeviceIndex != "-1";
          if (allowMic) {
            allowedDevices.push(audioDeviceIndex);
            let { mediaSource, rawId } = audioInputDevices.find(
              ({ deviceIndex }) => deviceIndex == audioDeviceIndex
            );
            aActor.activateDevicePerm(aRequest.windowID, mediaSource, rawId);
            persistGrantOrPromptPermission(principal, "microphone", remember);
          }
        } else if (reqAudioInput === "AudioCapture") {
          // Only one device possible for audio capture.
          allowedDevices.push(0);
        }

        if (reqAudioOutput) {
          let audioDeviceIndex = doc.getElementById(
            "webRTC-selectSpeaker-richlistbox"
          ).value;
          let allowSpeaker = audioDeviceIndex != "-1";
          if (allowSpeaker) {
            allowedDevices.push(audioDeviceIndex);
            let { id } = audioOutputDevices.find(
              ({ deviceIndex }) => deviceIndex == audioDeviceIndex
            );
            lazy.SitePermissions.setForPrincipal(
              principal,
              ["speaker", id].join("^"),
              lazy.SitePermissions.ALLOW
            );
          }
        }

        if (!allowedDevices.length) {
          aActor.denyRequest(aRequest);
          return;
        }

        const camNeeded = reqVideoInput === "Camera";
        const micNeeded = !!reqAudioInput;
        const scrNeeded = reqVideoInput === "Screen";
        const havePermission = await aActor.checkOSPermission(
          camNeeded,
          micNeeded,
          scrNeeded
        );
        if (!havePermission) {
          aActor.denyRequestNoPermission(aRequest);
          return;
        }

        aActor.sendAsyncMessage("webrtc:Allow", {
          callID: aRequest.callID,
          windowID: aRequest.windowID,
          devices: allowedDevices,
          suppressNotifications: silenceNotifications,
        });
      };

      // If we haven't handled the permission yet, we want to show the doorhanger.
      return false;
    },
  };

  function shouldShowAlwaysRemember() {
    // Don't offer "always remember" action in PB mode
    if (lazy.PrivateBrowsingUtils.isBrowserPrivate(aBrowser)) {
      return false;
    }

    // Don't offer "always remember" action in maybe unsafe permission
    // delegation
    if (aRequest.secondOrigin) {
      return false;
    }

    // Speaker grants are always remembered, so no checkbox is required.
    if (reqAudioOutput) {
      return false;
    }

    return true;
  }

  function getRememberCheckboxLabel() {
    if (reqVideoInput == "Camera") {
      if (reqAudioInput == "Microphone") {
        return "webrtc-remember-allow-checkbox-camera-and-microphone";
      }
      return "webrtc-remember-allow-checkbox-camera";
    }

    if (reqAudioInput == "Microphone") {
      return "webrtc-remember-allow-checkbox-microphone";
    }

    return "webrtc-remember-allow-checkbox";
  }

  if (shouldShowAlwaysRemember()) {
    // Disable the permanent 'Allow' action if the connection isn't secure, or for
    // screen/audio sharing (because we can't guess which window the user wants to
    // share without prompting). Note that we never enter this block for private
    // browsing windows.
    let reason = "";
    if (sharingScreen) {
      reason = "webrtc-reason-for-no-permanent-allow-screen";
    } else if (sharingAudio) {
      reason = "webrtc-reason-for-no-permanent-allow-audio";
    } else if (!aRequest.secure) {
      reason = "webrtc-reason-for-no-permanent-allow-insecure";
    }

    options.checkbox = {
      label: localization.formatValueSync(getRememberCheckboxLabel()),
      checked: principal.isAddonOrExpandedAddonPrincipal,
      checkedState: reason
        ? {
            disableMainAction: true,
            warningLabel: localization.formatValueSync(reason),
          }
        : undefined,
    };
  }

  // If the notification silencing feature is enabled and we're sharing a
  // screen, then the checkbox for the permission panel is what controls
  // notification silencing.
  if (notificationSilencingEnabled && sharingScreen) {
    options.checkbox = {
      label: localization.formatValueSync("webrtc-mute-notifications-checkbox"),
      checked: false,
      checkedState: {
        disableMainAction: false,
      },
    };
  }

  let anchorId = "webRTC-shareDevices-notification-icon";
  if (reqVideoInput === "Screen") {
    anchorId = "webRTC-shareScreen-notification-icon";
  } else if (!reqVideoInput) {
    if (reqAudioInput && !reqAudioOutput) {
      anchorId = "webRTC-shareMicrophone-notification-icon";
    } else if (!reqAudioInput && reqAudioOutput) {
      anchorId = "webRTC-shareSpeaker-notification-icon";
    }
  }

  if (aRequest.secondOrigin) {
    options.secondName = lazy.webrtcUI.getHostOrExtensionName(
      null,
      aRequest.secondOrigin
    );
  }

  // sidebar anchor handling
  let sidebarPopupNotification = maybeShowPopupNotificationInSidebar(
    aRequest,
    aBrowser,
    message,
    mainAction,
    secondaryActions,
    options
  );

  if (sidebarPopupNotification) {
    notification = sidebarPopupNotification;
    return;
  }

  notification = chromeDoc.defaultView.PopupNotifications.show(
    promptBrowser,
    "webRTC-shareDevices",
    message,
    anchorId,
    mainAction,
    secondaryActions,
    options
  );
  notification.callID = aRequest.callID;

  if (promptBrowser != aBrowser) {
    // Closing a window tears its tabs down without firing TabClose, so
    // PopupNotifications never fires its removal callback for a prompt in a
    // document PiP window. Deny the request here instead, or the site is left
    // holding a promise that never settles, since its window outlives the PiP.
    const promptWindow = chromeDoc.defaultView;
    const onUnload = () => {
      stopWatchingPromptWindow = null;
      if (!aActor.manager || aActor.manager.isClosed) {
        return;
      }
      aActor.denyRequest(aRequest);
    };
    promptWindow.addEventListener("unload", onUnload, { once: true });
    stopWatchingPromptWindow = () =>
      promptWindow.removeEventListener("unload", onUnload);
  }
}

/**
 * @param {"Screen" | "Camera" | null} reqVideoInput
 * @param {"AudioCapture" | "Microphone" | null} reqAudioInput
 * @param {boolean} reqAudioOutput
 * @param {boolean} delegation - Is the access delegated to a third party?
 * @param {boolean} isFile - Is the request coming from a file?
 * @returns {string} Localization message identifier
 */
function getPromptMessageId(
  reqVideoInput,
  reqAudioInput,
  reqAudioOutput,
  delegation,
  isFile
) {
  switch (reqVideoInput) {
    case "Camera":
      switch (reqAudioInput) {
        case "Microphone":
          if (isFile) {
            return "webrtc-allow-share-camera-and-microphone-with-file";
          }
          if (delegation) {
            return "webrtc-allow-share-camera-and-microphone-unsafe-delegation";
          }
          return "webrtc-allow-share-camera-and-microphone";
        case "AudioCapture":
          if (isFile) {
            return "webrtc-allow-share-camera-and-audio-capture-with-file";
          }
          if (delegation) {
            return "webrtc-allow-share-camera-and-audio-capture-unsafe-delegation";
          }
          return "webrtc-allow-share-camera-and-audio-capture";
        default:
          if (isFile) {
            return "webrtc-allow-share-camera-with-file";
          }
          if (delegation) {
            return "webrtc-allow-share-camera-unsafe-delegation";
          }
          return "webrtc-allow-share-camera";
      }

    case "Screen":
      switch (reqAudioInput) {
        case "Microphone":
          if (isFile) {
            return "webrtc-allow-share-screen-and-microphone-with-file";
          }
          if (delegation) {
            return "webrtc-allow-share-screen-and-microphone-unsafe-delegation";
          }
          return "webrtc-allow-share-screen-and-microphone";
        case "AudioCapture":
          if (isFile) {
            return "webrtc-allow-share-screen-and-audio-capture-with-file";
          }
          if (delegation) {
            return "webrtc-allow-share-screen-and-audio-capture-unsafe-delegation";
          }
          return "webrtc-allow-share-screen-and-audio-capture";
        default:
          if (isFile) {
            return "webrtc-allow-share-screen-with-file";
          }
          if (delegation) {
            return "webrtc-allow-share-screen-unsafe-delegation";
          }
          return "webrtc-allow-share-screen";
      }

    default:
      switch (reqAudioInput) {
        case "Microphone":
          if (isFile) {
            return "webrtc-allow-share-microphone-with-file";
          }
          if (delegation) {
            return "webrtc-allow-share-microphone-unsafe-delegation";
          }
          return "webrtc-allow-share-microphone";
        case "AudioCapture":
          if (isFile) {
            return "webrtc-allow-share-audio-capture-with-file";
          }
          if (delegation) {
            return "webrtc-allow-share-audio-capture-unsafe-delegation";
          }
          return "webrtc-allow-share-audio-capture";
        default:
          // This should be always true, if we've reached this far.
          if (reqAudioOutput) {
            if (isFile) {
              return "webrtc-allow-share-speaker-with-file";
            }
            if (delegation) {
              return "webrtc-allow-share-speaker-unsafe-delegation";
            }
            return "webrtc-allow-share-speaker";
          }
          return undefined;
      }
  }
}

/**
 * Checks whether we have a microphone/camera in use by checking the activePerms map
 * or if we have an allow permission for a microphone/camera in sitePermissions
 *
 * @param {Browser} browser - Browser to find all active and allowed microphone and camera devices for
 * @return true if one of the above conditions is met
 */
function allowedOrActiveCameraOrMicrophone(browser) {
  // Do we have an allow permission for cam/mic in the permissions manager?
  if (
    lazy.SitePermissions.getAllForBrowser(browser).some(perm => {
      return (
        perm.state == lazy.SitePermissions.ALLOW &&
        (perm.id.startsWith("camera") || perm.id.startsWith("microphone"))
      );
    })
  ) {
    // Return early, no need to check for active devices
    return true;
  }

  // Do we have an active device?
  return (
    // Find all windowIDs that belong to our browsing contexts
    browser.browsingContext
      .getAllBrowsingContextsInSubtree()
      // Only keep the outerWindowIds
      .map(bc => bc.currentWindowGlobal?.outerWindowId)
      .filter(id => id != null)
      // We have an active device if one of our windowIds has a non empty map in the activePerms map
      // that includes one device of type "camera" or "microphone"
      .some(id => {
        let map = lazy.webrtcUI.activePerms.get(id);
        if (!map) {
          // This windowId has no active device
          return false;
        }
        // Let's see if one of the devices is a camera or a microphone
        let types = [...map.values()];
        return types.includes("microphone") || types.includes("camera");
      })
  );
}

function removePrompt(aBrowser, aCallId) {
  // The prompt lives in exactly one window: either aBrowser's, or that of a
  // document PiP window opened by aBrowser. See getPromptBrowser().
  if (!removePromptFrom(aBrowser, aCallId)) {
    removePromptFrom(getDocumentPiPBrowser(aBrowser), aCallId);
  }
}

function removePromptFrom(aBrowser, aCallId) {
  let chromeWin = aBrowser?.browsingContext?.topChromeWindow;
  let notification = chromeWin?.PopupNotifications.getNotification(
    "webRTC-shareDevices",
    aBrowser
  );
  if (!notification || notification.callID != aCallId) {
    return false;
  }
  notification.remove();
  return true;
}

/**
 * Clears temporary permission grants used for WebRTC device grace periods.
 *
 * @param browser - Browser element to clear permissions for.
 * @param {boolean} clearCamera - Clear camera grants.
 * @param {boolean} clearMicrophone - Clear microphone grants.
 */
function clearTemporaryGrants(browser, clearCamera, clearMicrophone) {
  if (!clearCamera && !clearMicrophone) {
    // Nothing to clear.
    return;
  }
  let perms = lazy.SitePermissions.getAllForBrowser(browser);
  perms
    .filter(perm => {
      let [id, key] = perm.id.split(lazy.SitePermissions.PERM_KEY_DELIMITER);
      // We only want to clear WebRTC grace periods. These are temporary, device
      // specifc (double-keyed) microphone or camera permissions.
      return (
        key &&
        perm.state == lazy.SitePermissions.ALLOW &&
        perm.scope == lazy.SitePermissions.SCOPE_TEMPORARY &&
        ((clearCamera && id == "camera") ||
          (clearMicrophone && id == "microphone"))
      );
    })
    .forEach(perm =>
      lazy.SitePermissions.removeFromPrincipal(null, perm.id, browser)
    );
}

/**
 * Persist an ALLOW state if the remember option is true.
 * Otherwise, persist PROMPT so that we can later tell the site
 * that permission was granted once before.
 * This makes Firefox seem much more like Chrome to sites that
 * expect a one-off, persistent permission grant for cam/mic.
 *
 * @param principal - Principal to add permission to.
 * @param {string} permissionName - name of permission.
 * @param remember - whether the grant should be persisted.
 */
function persistGrantOrPromptPermission(principal, permissionName, remember) {
  // There are cases like unsafe delegation where a prompt appears
  // even in ALLOW state, so make sure to not overwrite it (there's
  // no remember checkbox in those cases)
  if (
    lazy.SitePermissions.getForPrincipal(principal, permissionName).state ==
    lazy.SitePermissions.ALLOW
  ) {
    return;
  }

  lazy.SitePermissions.setForPrincipal(
    principal,
    permissionName,
    remember ? lazy.SitePermissions.ALLOW : lazy.SitePermissions.PROMPT
  );
}

/**
 * Clears any persisted PROMPT (aka Always Ask) permission.
 *
 * @param principal - Principal to remove permission from.
 * @param {string} permissionName - name of permission.
 * @param browser - Browser element to clear permission for.
 */
function maybeClearAlwaysAsk(principal, permissionName, browser) {
  // For the "Always Ask" user choice, only persisted PROMPT is used,
  // so no need to scan through temporary permissions.
  if (
    lazy.SitePermissions.getForPrincipal(principal, permissionName).state ==
    lazy.SitePermissions.PROMPT
  ) {
    lazy.SitePermissions.removeFromPrincipal(
      principal,
      permissionName,
      browser
    );
  }
}

/**
 * Helper for lazily creating the webrtc-preview element.
 *
 * @param {Document} chromeDoc - The chrome document to create the webrtc-preview element in.
 * @returns {HTMLElement} The webrtc-preview element which has been inserted into the DOM.
 */
function getOrCreateWebRTCPreviewEl(chromeDoc) {
  let previewSection = chromeDoc.getElementById("webRTC-preview-section");
  let previewEl = previewSection.querySelector("#webRTC-preview");
  if (!previewEl) {
    previewEl = chromeDoc.createElement("webrtc-preview");
    previewEl.id = "webRTC-preview";
    previewSection.insertBefore(previewEl, previewSection.firstChild);
  }
  return previewEl;
}

/**
 * On prompt "shown", if a camera permission request was made as the result of
 * user interaction start the camera preview automatically.
 * While websites don't have access to the camera preview, giving websites the
 * ability to turn on the users camera without any user interaction can be
 * scary. If there is no user input we offer the user to start the preview
 * manually.
 *
 * @param {Document} doc - The chrome document containing the prompt.
 * @param {boolean} isHandlingUserInput - Whether the prompt is shown as a
 * result of user interaction.
 */
function onCameraPromptShown(doc, isHandlingUserInput) {
  // Skip if the request was made without user input.
  if (!isHandlingUserInput) {
    return;
  }

  // Skip if the entire preview section is hidden.
  if (doc.getElementById("webRTC-preview-section").hidden) {
    return;
  }

  // Skip if no device is selected.
  let cameraMenuPopup = doc.getElementById("webRTC-selectCamera-menupopup");
  let deviceId = cameraMenuPopup?.querySelector("[selected]")?.deviceId;
  if (!deviceId) {
    return;
  }

  let webrtcPreview = doc.getElementById("webRTC-preview");
  // Pass deviceId and mediaSource to make sure they're up to date,
  // matching the user selection.
  webrtcPreview?.startPreview({ deviceId, mediaSource: "camera" });
}

/**
 * If aBrowser is the opener of a document picture-in-picture window, return
 * that window's browser element, otherwise null.
 *
 * @param {Element} aBrowser - Browser element of the potential PiP opener.
 * @returns {Element?} Browser element of the PiP window, if any.
 */
function getDocumentPiPBrowser(aBrowser) {
  const openerBC = aBrowser.browsingContext;
  if (!openerBC || openerBC.isDocumentPiP) {
    return null;
  }
  const pipBC = openerBC.group
    .getToplevels()
    .find(bc => bc.isDocumentPiP && bc.opener == openerBC);
  return pipBC?.embedderElement ?? null;
}

/**
 * Pick the browser element to anchor the prompt to.
 *
 * Returns the browser of a document picture-in-picture window opened by
 * aBrowser, when that window has focus. Prompting there puts the prompt where
 * the user just interacted, instead of in a background window where
 * PopupNotifications would suppress it until they went looking.
 *
 * Returns aBrowser in every other case, including all camera, microphone and
 * speaker requests. Only screen sharing redirects, because only screen sharing
 * has the use case: presenting from a meeting PiP, where the stream has to
 * outlive the PiP and so must belong to the opener. We can widen this later if
 * other prompts turn out to want it.
 *
 * @param {Element} aBrowser - Browser element the request came from.
 * @param {object} aRequest - The webrtc:Request data.
 * @returns {Element} Browser element to anchor the prompt to.
 */
function getPromptBrowser(aBrowser, aRequest) {
  if (!aRequest.sharingScreen) {
    return aBrowser;
  }
  const activeWindow = Services.focus.activeWindow;
  const pipBrowser = getDocumentPiPBrowser(aBrowser);
  if (
    activeWindow &&
    activeWindow == pipBrowser?.browsingContext?.topChromeWindow &&
    // Keep the prompt in the opener while it's in DOM fullscreen. The spoofing
    // protections in browser-fullScreenAndPointerLock.js only ever see their
    // own window's PopupNotifications, so a prompt in the PiP would neither
    // exit fullscreen nor be cancelled by entering it.
    !aBrowser.browsingContext?.topChromeWindow?.document.fullscreenElement
  ) {
    return pipBrowser;
  }
  return aBrowser;
}

function isSidebarBrowser(browser) {
  const sidebarBrowser =
    browser.browsingContext?.topChromeWindow?.SidebarController?.browser;
  if (!sidebarBrowser) {
    return false;
  }

  const nestedBrowsers =
    sidebarBrowser.contentDocument.querySelectorAll("browser");
  return Array.from(nestedBrowsers).some(b => b === browser);
}

/**
 * Show WebRTC popup inside the sidebar instead of urlbar.
 * Returns the notification object or null.
 */
function maybeShowPopupNotificationInSidebar(
  aRequest,
  aBrowser,
  message,
  mainAction,
  secondaryActions,
  options
) {
  if (!isSidebarBrowser(aBrowser)) {
    return null;
  }

  const win = aBrowser.browsingContext?.topChromeWindow;
  const sidebarPopupNotifications = win?.SidebarPopupNotifications;

  if (!sidebarPopupNotifications) {
    return null;
  }

  const notification = sidebarPopupNotifications.show(
    aBrowser,
    "webRTC-shareDevices",
    message,
    "sidebar-webrtc-microphone-notification-icon",
    mainAction,
    secondaryActions,
    options
  );

  notification.callID = aRequest.callID;
  return notification;
}
