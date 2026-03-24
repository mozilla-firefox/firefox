/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const ENROLLMENT_PREF = "security.tls.client_certificate_enrollment.enabled";
const APPROVED_TOPIC = "psm:site-client-cert-enrollment-approved";
const DENIED_TOPIC = "psm:site-client-cert-enrollment-denied";
const logger = console.createInstance({ prefix: "SiteClientCertEnrollment" });

function log(level, message, details = undefined) {
  let suffix = details ? ` ${JSON.stringify(details)}` : "";
  logger[level](`${message}${suffix}`);
}

function getTopBrowsingContext(browserId) {
  if (!browserId) {
    return null;
  }
  return BrowsingContext.getCurrentTopByBrowserId(browserId);
}

function getPromptModalType(browsingContext) {
  const docViewer = browsingContext?.docShell?.docViewer;
  if (docViewer?.isTabModalPromptAllowed) {
    return Services.prompt.MODAL_TYPE_CONTENT;
  }
  return Services.prompt.MODAL_TYPE_WINDOW;
}

function getDisplayHost(requestingURI) {
  try {
    return requestingURI.displayHost || requestingURI.host;
  } catch {
    return requestingURI.spec;
  }
}

async function promptForEnrollment(requestingURI, enrollmentURI, browsingContext) {
  const title = "Client certificate enrollment request";
  const host = getDisplayHost(requestingURI);
  const text =
    `${host} requested permission to enroll a new client certificate for this browser.\n\n` +
    `Enrollment URL: ${enrollmentURI.spec}\n\n` +
    "Allow this site to start client certificate enrollment?";
  const buttonFlags =
    Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_0 +
    Services.prompt.BUTTON_TITLE_CANCEL * Services.prompt.BUTTON_POS_1;

  if (browsingContext) {
    const result = await Services.prompt.asyncConfirmEx(
      browsingContext,
      getPromptModalType(browsingContext),
      title,
      text,
      buttonFlags,
      "Allow",
      null,
      null,
      null,
      false,
      {}
    );
    return (
      result.QueryInterface(Ci.nsIPropertyBag2).get("buttonNumClicked") == 0
    );
  }

  const browserWindow = Services.wm.getMostRecentBrowserWindow();
  return (
    Services.prompt.confirmEx(
      browserWindow,
      title,
      text,
      buttonFlags,
      "Allow",
      null,
      null,
      null,
      {}
    ) == 0
  );
}

export function SiteClientCertEnrollmentService() {
  this._pendingRequests = new Set();
}

SiteClientCertEnrollmentService.prototype = {
  classID: Components.ID("{8df03baa-2f95-4a97-af2b-3f77c764f8d1}"),
  QueryInterface: ChromeUtils.generateQI([
    "nsISiteClientCertEnrollmentService",
  ]),

  requestEnrollment(requestingURISpec, enrollmentURISpec, browserId) {
    if (!Services.prefs.getBoolPref(ENROLLMENT_PREF, false)) {
      log("debug", "ignoring request while pref is disabled", {
        requestingURISpec,
        enrollmentURISpec,
      });
      return;
    }

    let requestingURI;
    let enrollmentURI;
    try {
      requestingURI = Services.io.newURI(requestingURISpec);
      enrollmentURI = Services.io.newURI(enrollmentURISpec);
    } catch (error) {
      log("error", "failed to parse enrollment request URIs", {
        requestingURISpec,
        enrollmentURISpec,
        error: `${error}`,
      });
      return;
    }

    const requestKey = `${requestingURI.prePath}|${enrollmentURI.spec}|${browserId}`;
    if (this._pendingRequests.has(requestKey)) {
      log("debug", "ignoring duplicate pending enrollment request", {
        requestKey,
      });
      return;
    }

    this._pendingRequests.add(requestKey);
    log("info", "received enrollment request", {
      requestingURI: requestingURI.spec,
      enrollmentURI: enrollmentURI.spec,
      browserId,
    });

    void this._handleEnrollmentRequest(requestingURI, enrollmentURI, browserId)
      .catch(error => {
        log("error", "unexpected enrollment failure", {
          requestingURI: requestingURI.spec,
          enrollmentURI: enrollmentURI.spec,
          error: `${error}`,
        });
      })
      .finally(() => {
        this._pendingRequests.delete(requestKey);
      });
  },

  async _handleEnrollmentRequest(requestingURI, enrollmentURI, browserId) {
    const browsingContext = getTopBrowsingContext(browserId);
    const approved = await promptForEnrollment(
      requestingURI,
      enrollmentURI,
      browsingContext
    );
    const details = JSON.stringify({
      requestingURI: requestingURI.spec,
      enrollmentURI: enrollmentURI.spec,
      browserId,
    });

    if (!approved) {
      log("info", "user denied enrollment request", {
        requestingURI: requestingURI.spec,
        enrollmentURI: enrollmentURI.spec,
      });
      Services.obs.notifyObservers(null, DENIED_TOPIC, details);
      return;
    }

    log("info", "user approved enrollment request", {
      requestingURI: requestingURI.spec,
      enrollmentURI: enrollmentURI.spec,
    });
    Services.obs.notifyObservers(null, APPROVED_TOPIC, details);
    this._beginEnrollment(requestingURI, enrollmentURI, browsingContext);
  },

  _beginEnrollment(requestingURI, enrollmentURI, browsingContext) {
    log("warn", "enrollment backend not implemented yet", {
      requestingURI: requestingURI.spec,
      enrollmentURI: enrollmentURI.spec,
      browserId: browsingContext?.browserId ?? 0,
    });
  },
};
