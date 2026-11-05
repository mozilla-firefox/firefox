/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "ToastNotification",
    maxLogLevel: "Warn",
  });
});

ChromeUtils.defineESModuleGetters(lazy, {
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  EnrollmentType: "resource://nimbus/ExperimentAPI.sys.mjs",
  RemoteL10n: "resource:///modules/asrouter/RemoteL10n.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetters(lazy, {
  AlertsService: ["@mozilla.org/alerts-service;1", Ci.nsIAlertsService],
});

export const ToastNotification = {
  // Allow testing to stub the alerts service.
  get AlertsService() {
    return lazy.AlertsService;
  },

  sendUserEventTelemetry(event, message, dispatch) {
    const ping = {
      message_id: message.id,
      event,
    };
    dispatch({
      type: "TOAST_NOTIFICATION_TELEMETRY",
      data: { action: "toast_notification_user_event", ...ping },
    });
  },

  /**
   * Select the appropriate image URL from message content based on the
   * system color scheme and motion preferences.
   *
   * @param {object} content  The message content object.
   * @returns {string|undefined} The selected image URL.
   */
  imageUrlForContent(content) {
    const isDark = Services.appinfo.chromeColorSchemeIsDark;
    const isReducedMotion = Services.appinfo.prefersReducedMotion;
    return (
      (isDark &&
        isReducedMotion &&
        content.dark_mode_reduced_motion_image_url) ||
      (isDark && content.dark_mode_image_url) ||
      (isReducedMotion && content.reduced_motion_image_url) ||
      content.image_url
    );
  },

  /**
   * Show a toast notification.
   *
   * @param message             Message containing content to show.
   * @param dispatch            A function to dispatch resulting actions.
   * @return                    boolean value capturing if toast notification was displayed.
   */
  async showToastNotification(message, dispatch) {
    let { content } = message;
    let title = await lazy.RemoteL10n.formatLocalizableText(content.title);
    let body = await lazy.RemoteL10n.formatLocalizableText(content.body);

    // The only link between background task message experiment and user
    // re-engagement via the notification is the associated "tag".  Said tag is
    // usually controlled by the message content, but for message experiments,
    // we want to avoid a missing tag and to ensure a deterministic tag for
    // easier analysis, including across branches.
    let { tag } = content;

    let experimentMetadata =
      lazy.NimbusFeatures.backgroundTaskMessage.getEnrollmentMetadata(
        lazy.EnrollmentType.EXPERIMENT
      );

    if (experimentMetadata) {
      // Like `my-experiment:my-branch`.
      tag = `${experimentMetadata.slug}:${experimentMetadata.branch}`;
    }

    if (this.AlertsService.isFullscreen?.()) {
      lazy.logConsole.warn("Suppressing toast notification");
      return false;
    }

    // There are two events named `IMPRESSION` the first one refers to telemetry
    // while the other refers to ASRouter impressions used for the frequency cap
    this.sendUserEventTelemetry("IMPRESSION", message, dispatch);
    dispatch({ type: "IMPRESSION", data: message });

    const imageUrl = this.imageUrlForContent(content);

    // To determine whether the image is a `.gif`, we inspect the file name.
    // Because the URL may include query parameters, we use a proper URL parser
    // to extract the canonical file name and consolidate the parsed data in
    // `imgData` to keep all related information in a single structure.
    let imageContainer = null;
    let imageData;
    if (imageUrl) {
      const url = new URL(imageUrl);
      imageData = {
        url,
        name: url.pathname.split("/").pop(),
      };
    }

    // On Windows, animated GIFs are handled as a special case for notifications.
    // The original GIF file is forwarded unchanged to avoid decoding and
    // re-encoding the animation.
    //
    // AlertsService exposes `imagePathUnchecked` (string), which takes precedence
    // over `AlertsService.image`. The latter contains the binary image data as an
    // imageContainer. The naming in this file mirrors that distinction to clearly
    // separate a file path from in-memory image data.
    let alert;
    if (AppConstants.platform === "win" && imageData?.name.endsWith(".gif")) {
      alert = Cc["@mozilla.org/windows-alert-notification;1"].createInstance(
        Ci.nsIWindowsAlertNotification
      );
      try {
        const resp = await fetch(imageData.url);
        if (!resp.ok) {
          throw new Error(`Could not fetch ${imageUrl}`);
        }

        const bytes = new Uint8Array(await resp.arrayBuffer());
        const uuid = Services.uuid.generateUUID().toString();

        let imagePath = PathUtils.join(
          PathUtils.tempDir,
          `${uuid}_${imageData.name}`
        );

        lazy.logConsole.info(`Saved ${imageUrl} to path: ${imagePath}`);

        await IOUtils.write(imagePath, bytes);

        alert.imagePathUnchecked = imagePath;
      } catch (error) {
        lazy.logConsole.warn(`Animated gif notification: ${error}`);
      }
    } else {
      alert = Cc["@mozilla.org/alert-notification;1"].createInstance(
        Ci.nsIAlertNotification
      );

      if (imageData?.url) {
        try {
          const uri = Services.io.newURI(imageData.url);
          const channel = Services.io.newChannelFromURI(
            uri,
            null,
            Services.scriptSecurityManager.getSystemPrincipal(),
            null,
            Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
            Ci.nsIContentPolicy.TYPE_IMAGE
          );
          imageContainer = await ChromeUtils.fetchDecodedImage(uri, channel);
        } catch (e) {
          console.error("showToastNotification image loading failed", e);
        }
      }
    }

    let systemPrincipal = Services.scriptSecurityManager.getSystemPrincipal();
    alert.init(
      tag,
      null /* aImageURL */,
      title,
      body,
      true /* aTextClickable */,
      content.data,
      null /* aDir */,
      null /* aLang */,
      null /* aData */,
      systemPrincipal,
      null /* aInPrivateBrowsing */,
      content.requireInteraction
    );
    alert.image = imageContainer;

    if (content.actions) {
      let actions = Cu.cloneInto(content.actions, {});
      for (let action of actions) {
        if (action.title) {
          action.title = await lazy.RemoteL10n.formatLocalizableText(
            action.title
          );
        }
        if (action.launch_action) {
          action.opaqueRelaunchData = JSON.stringify(action.launch_action);
          delete action.launch_action;
        }
      }
      alert.actions = actions;
    }

    // Populate `opaqueRelaunchData`, prefering `launch_action` if given,
    // falling back to `launch_url` if given.
    let relaunchAction = content.launch_action;
    if (!relaunchAction && content.launch_url) {
      relaunchAction = {
        type: "OPEN_URL",
        data: {
          args: content.launch_url,
          where: "tab",
        },
      };
    }
    if (relaunchAction) {
      alert.opaqueRelaunchData = JSON.stringify(relaunchAction);
    }

    let shownPromise = Promise.withResolvers();
    let obs = (subject, topic) => {
      if (topic === "alertshow") {
        shownPromise.resolve();
      }

      if (topic === "alertfinished" && alert?.imagePathUnchecked) {
        // Notifiactions shown with `imagePathUnchecked` don't delete the
        // provided file, so we have to clean it up ourselves.
        lazy.logConsole.info(`Deleting ${alert.imagePathUnchecked}`);
        IOUtils.remove(alert.imagePathUnchecked);
      }
    };

    this.AlertsService.showAlert(alert, obs);

    await shownPromise;

    return true;
  },
};
