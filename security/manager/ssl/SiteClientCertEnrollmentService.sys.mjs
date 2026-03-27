/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const ENROLLMENT_PREF = "security.tls.client_certificate_enrollment.enabled";
const BACKEND_CONTRACT_ID =
  "@mozilla.org/security/site-client-cert-enrollment-backend;1";
const APPROVED_TOPIC = "psm:site-client-cert-enrollment-approved";
const DENIED_TOPIC = "psm:site-client-cert-enrollment-denied";
const logger = console.createInstance({ prefix: "SiteClientCertEnrollment" });
const ENROLLMENT_REQUEST_CONTENT_TYPE = "application/pkcs10";
const ENROLLMENT_AUTHORIZATION_SCHEME = "Bearer";
const ENROLLMENT_ACCEPT_HEADER =
  "application/json, application/pkix-cert, application/pkcs7-mime, application/x-pem-file, application/pem-certificate-chain, text/plain";

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

function getEnrollmentBackend() {
  return Cc[BACKEND_CONTRACT_ID].getService(
    Ci.nsISiteClientCertEnrollmentBackend
  );
}

function getEnrollmentSubjectCommonName(requestingURI) {
  try {
    return requestingURI.asciiHost || requestingURI.host;
  } catch {
    return "";
  }
}

function stringToUtf8Bytes(value) {
  return new TextEncoder().encode(value);
}

function base64ToBytes(value) {
  let binary = atob(value.replace(/\s+/g, ""));
  let bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index++) {
    bytes[index] = binary.charCodeAt(index);
  }
  return bytes;
}

function normalizeEnrollmentJsonCertificate(payload) {
  if (
    !payload ||
    typeof payload != "object" ||
    typeof payload.certificate != "string"
  ) {
    throw new Error("enrollment JSON response missing certificate");
  }

  if (
    payload.encoding == "pem" ||
    payload.certificate.includes("-----BEGIN")
  ) {
    return stringToUtf8Bytes(payload.certificate);
  }

  if (
    !payload.encoding ||
    payload.encoding == "base64" ||
    payload.encoding == "base64-der"
  ) {
    return base64ToBytes(payload.certificate);
  }

  throw new Error(
    `unsupported enrollment certificate encoding: ${payload.encoding}`
  );
}

async function readEnrollmentCertificateBytes(response) {
  let contentType = (response.headers.get("content-type") || "").toLowerCase();
  if (contentType.includes("application/json")) {
    return normalizeEnrollmentJsonCertificate(await response.json());
  }
  return new Uint8Array(await response.arrayBuffer());
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

  requestEnrollment(
    requestingURISpec,
    enrollmentURISpec,
    enrollmentToken,
    browserId
  ) {
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

    const requestKey =
      `${requestingURI.prePath}|${enrollmentURI.spec}|` +
      `${enrollmentToken}|${browserId}`;
    if (this._pendingRequests.has(requestKey)) {
      log("debug", "ignoring duplicate pending enrollment request", {
        requestingURI: requestingURI.spec,
        enrollmentURI: enrollmentURI.spec,
        browserId,
        hasEnrollmentToken: !!enrollmentToken,
      });
      return;
    }

    this._pendingRequests.add(requestKey);
    log("info", "received enrollment request", {
      requestingURI: requestingURI.spec,
      enrollmentURI: enrollmentURI.spec,
      browserId,
      hasEnrollmentToken: !!enrollmentToken,
    });

    void this._handleEnrollmentRequest(
      requestingURI,
      enrollmentURI,
      enrollmentToken,
      browserId
    )
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

  async _handleEnrollmentRequest(
    requestingURI,
    enrollmentURI,
    enrollmentToken,
    browserId
  ) {
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
    await this._beginEnrollment(
      requestingURI,
      enrollmentURI,
      enrollmentToken,
      browsingContext
    );
  },

  async _beginEnrollment(
    requestingURI,
    enrollmentURI,
    enrollmentToken,
    browsingContext
  ) {
    let backend = getEnrollmentBackend();
    let subjectCommonName = getEnrollmentSubjectCommonName(requestingURI);
    if (!subjectCommonName) {
      log("error", "unable to derive enrollment subject common name", {
        requestingURI: requestingURI.spec,
      });
      return;
    }

    let requestId;
    try {
      requestId = backend.createEnrollmentRequest(subjectCommonName);
      let csr = backend.getEnrollmentRequestCsr(requestId);

      log("info", "submitting client certificate enrollment request", {
        requestingURI: requestingURI.spec,
        enrollmentURI: enrollmentURI.spec,
        browserId: browsingContext?.browserId ?? 0,
        requestId,
        hasEnrollmentToken: !!enrollmentToken,
      });

      let headers = {
        Accept: ENROLLMENT_ACCEPT_HEADER,
        "Content-Type": ENROLLMENT_REQUEST_CONTENT_TYPE,
      };
      if (enrollmentToken) {
        headers.Authorization =
          `${ENROLLMENT_AUTHORIZATION_SCHEME} ${enrollmentToken}`;
      }

      let response = await fetch(enrollmentURI.spec, {
        method: "POST",
        body: csr,
        cache: "no-store",
        credentials: "same-origin",
        redirect: "error",
        headers,
      });

      if (!response.ok) {
        throw new Error(
          `enrollment endpoint returned HTTP ${response.status} ${response.statusText}`
        );
      }

      log("info", "reading enrollment response body", {
        requestingURI: requestingURI.spec,
        enrollmentURI: enrollmentURI.spec,
        browserId: browsingContext?.browserId ?? 0,
        requestId,
        status: response.status,
        contentType: response.headers.get("content-type") || "",
      });
      let certificateBytes = await readEnrollmentCertificateBytes(response);
      log("info", "parsed enrollment response body", {
        requestingURI: requestingURI.spec,
        enrollmentURI: enrollmentURI.spec,
        browserId: browsingContext?.browserId ?? 0,
        requestId,
        certificateBytesLength: certificateBytes.length,
      });
      log("info", "calling enrollment backend completion", {
        requestingURI: requestingURI.spec,
        enrollmentURI: enrollmentURI.spec,
        browserId: browsingContext?.browserId ?? 0,
        requestId,
      });
      backend.completeEnrollment(requestId, certificateBytes);
      log("info", "enrollment backend completion returned", {
        requestingURI: requestingURI.spec,
        enrollmentURI: enrollmentURI.spec,
        browserId: browsingContext?.browserId ?? 0,
        requestId,
      });

      log("info", "completed client certificate enrollment", {
        requestingURI: requestingURI.spec,
        enrollmentURI: enrollmentURI.spec,
        browserId: browsingContext?.browserId ?? 0,
        requestId,
      });
    } catch (error) {
      if (requestId) {
        try {
          backend.abortEnrollment(requestId);
        } catch (abortError) {
          log("error", "failed to clean up aborted enrollment", {
            requestId,
            error: `${abortError}`,
          });
        }
      }

      log("error", "client certificate enrollment failed", {
        requestingURI: requestingURI.spec,
        enrollmentURI: enrollmentURI.spec,
        browserId: browsingContext?.browserId ?? 0,
        requestId: requestId ?? null,
        error: `${error}`,
      });
    }
  },
};
