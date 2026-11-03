/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  Integration: "resource://gre/modules/Integration.sys.mjs",
  PermissionUI: "resource:///modules/PermissionUI.sys.mjs",
});
/**
 * ContentPermissionIntegration is responsible for showing the user
 * simple permission prompts when content requests additional
 * capabilities.
 *
 * While there are some built-in permission prompts, createPermissionPrompt
 * can also be overridden by system add-ons or tests to provide new ones.
 *
 * This override ability is provided by Integration.sys.mjs. See
 * PermissionUI.sys.mjs for an example of how to provide a new prompt
 * from an add-on.
 */
const ContentPermissionIntegration = {
  /**
   * Creates a PermissionPrompt for a given permission type and
   * nsIContentPermissionRequest.
   *
   * @param {string} type
   *        The type of the permission request from content. This normally
   *        matches the "type" field of an nsIContentPermissionType, but it
   *        can be something else if the permission does not use the
   *        nsIContentPermissionRequest model. Note that this type might also
   *        be different from the permission key used in the permissions
   *        database.
   *        Example: "geolocation"
   * @param {nsIContentPermissionRequest} request
   *        The request for a permission from content.
   * @returns {PermissionPrompt?} (see PermissionUI.sys.mjs),
   *         or undefined if the type cannot be handled.
   */
  createPermissionPrompt(type, request) {
    switch (type) {
      case "geolocation": {
        return new lazy.PermissionUI.GeolocationPermissionPrompt(request);
      }
      case "xr": {
        return new lazy.PermissionUI.XRPermissionPrompt(request);
      }
      case "desktop-notification": {
        return new lazy.PermissionUI.DesktopNotificationPermissionPrompt(
          request
        );
      }
      case "persistent-storage": {
        return new lazy.PermissionUI.PersistentStoragePermissionPrompt(request);
      }
      case "midi": {
        return new lazy.PermissionUI.MIDIPermissionPrompt(request);
      }
      case "serial": {
        return new lazy.PermissionUI.SerialPermissionPrompt(request);
      }
      case "storage-access": {
        return new lazy.PermissionUI.StorageAccessPermissionPrompt(request);
      }
      case "loopback-network": {
        return new lazy.PermissionUI.LoopbackNetworkPermissionPrompt(request);
      }
      case "local-network": {
        return new lazy.PermissionUI.LocalNetworkPermissionPrompt(request);
      }
      case "speech-recognition-model-download": {
        return new lazy.PermissionUI.SpeechRecognitionModelDownloadPermissionPrompt(
          request
        );
      }
    }
    return undefined;
  },
};

export function ContentPermissionPrompt() {}

ContentPermissionPrompt.prototype = {
  classID: Components.ID("{d8903bf6-68d5-4e97-bcd1-e4d3012f721a}"),

  QueryInterface: ChromeUtils.generateQI(["nsIContentPermissionPrompt"]),

  /**
   * This implementation of nsIContentPermissionPrompt.prompt ensures
   * that there's only one nsIContentPermissionType in the request,
   * and that it's of type nsIContentPermissionType. Failing to
   * satisfy either of these conditions will result in this method
   * throwing NS_ERRORs. If the combined ContentPermissionIntegration
   * cannot construct a prompt for this particular request, an
   * NS_ERROR_FAILURE will be thrown.
   *
   * Any time an error is thrown, the nsIContentPermissionRequest is
   * cancelled automatically.
   *
   * @param {nsIContentPermissionRequest} request
   *        The request that we're to show a prompt for.
   */
  prompt(request) {
    let type;
    try {
      // Only allow exactly one permission request here.
      let types = request.types.QueryInterface(Ci.nsIArray);
      if (types.length != 1) {
        throw Components.Exception(
          "Expected an nsIContentPermissionRequest with only 1 type.",
          Cr.NS_ERROR_UNEXPECTED
        );
      }

      type = types.queryElementAt(0, Ci.nsIContentPermissionType).type;
      let combinedIntegration = lazy.Integration.contentPermission.getCombined(
        ContentPermissionIntegration
      );

      let permissionPrompt = combinedIntegration.createPermissionPrompt(
        type,
        request
      );
      if (!permissionPrompt) {
        throw Components.Exception(
          `Failed to handle permission of type ${type}`,
          Cr.NS_ERROR_FAILURE
        );
      }

      permissionPrompt.prompt();
    } catch (ex) {
      console.error(ex);
      request.cancel();
      throw ex;
    }
  },
};
