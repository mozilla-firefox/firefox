/*
 * Tests for bug 1241377: A channel with nsIFormPOSTActionChannel interface
 * should be able to accept form POST.
 */

"use strict";

const SCHEME = "x-bug1241377";

const FORM_BASE = SCHEME + "://dummy/form/";
const NORMAL_FORM_URI = FORM_BASE + "normal.html";
const UPLOAD_FORM_URI = FORM_BASE + "upload.html";
const POST_FORM_URI = FORM_BASE + "post.html";

const ACTION_BASE = SCHEME + "://dummy/action/";
const NORMAL_ACTION_URI = ACTION_BASE + "normal.html";
const UPLOAD_ACTION_URI = ACTION_BASE + "upload.html";
const POST_ACTION_URI = ACTION_BASE + "post.html";

function CustomProtocolHandler() {}
CustomProtocolHandler.prototype = {
  /** nsIProtocolHandler */
  get scheme() {
    return SCHEME;
  },
  newChannel(aURI, aLoadInfo) {
    return new CustomChannel(aURI, aLoadInfo);
  },
  allowPort(port) {
    return port != -1;
  },

  /** nsISupports */
  QueryInterface: ChromeUtils.generateQI(["nsIProtocolHandler"]),
};

function CustomChannel(aURI, aLoadInfo) {
  this.uri = aURI;
  this.loadInfo = aLoadInfo;

  this._uploadStream = null;

  var interfaces = [Ci.nsIRequest, Ci.nsIChannel];
  if (this.uri.spec == POST_ACTION_URI) {
    interfaces.push(Ci.nsIFormPOSTActionChannel);
  } else if (this.uri.spec == UPLOAD_ACTION_URI) {
    interfaces.push(Ci.nsIUploadChannel);
  }
  this.QueryInterface = ChromeUtils.generateQI(interfaces);
}
CustomChannel.prototype = {
  /** nsIUploadChannel */
  get uploadStream() {
    return this._uploadStream;
  },
  set uploadStream(val) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },
  setUploadStream(aStream) {
    this._uploadStream = aStream;
  },

  /** nsIChannel */
  get originalURI() {
    return this.uri;
  },
  get URI() {
    return this.uri;
  },
  owner: Services.scriptSecurityManager.getSystemPrincipal(),
  notificationCallbacks: null,
  get securityInfo() {
    return null;
  },
  get contentType() {
    return "text/html";
  },
  set contentType(val) {},
  contentCharset: "UTF-8",
  get contentLength() {
    return -1;
  },
  set contentLength(val) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },
  open() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },
  asyncOpen(aListener) {
    var data = `
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>test bug 1241377</title>
</head>
<body>
`;

    if (this.uri.spec.startsWith(FORM_BASE)) {
      data += `
<form id="form" action="${this.uri.spec.replace(FORM_BASE, ACTION_BASE)}"
      method="post" enctype="text/plain" target="frame">
<input type="hidden" name="foo" value="bar">
<input type="submit">
</form>

<iframe id="frame" name="frame" width="200" height="200"></iframe>
`;
    } else if (this.uri.spec.startsWith(ACTION_BASE)) {
      var postData = "";
      var headers = {};
      if (this._uploadStream) {
        var bstream = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
          Ci.nsIBinaryInputStream
        );
        bstream.setInputStream(this._uploadStream);
        postData = bstream.readBytes(bstream.available());

        if (this._uploadStream instanceof Ci.nsIMIMEInputStream) {
          this._uploadStream.visitHeaders((name, value) => {
            headers[name] = value;
          });
        }
      }
      data += `
<input id="upload_stream" value="${this._uploadStream ? "yes" : "no"}">
<input id="post_data" value="${btoa(postData)}">
<input id="upload_headers" value='${JSON.stringify(headers)}'>
`;
    }

    data += `
</body>
</html>
`;

    var stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
      Ci.nsIStringInputStream
    );
    stream.setByteStringData(data);

    var runnable = {
      run: () => {
        try {
          aListener.onStartRequest(this, null);
        } catch (e) {}
        try {
          aListener.onDataAvailable(this, stream, 0, stream.available());
        } catch (e) {}
        try {
          aListener.onStopRequest(this, null, Cr.NS_OK);
        } catch (e) {}
      },
    };
    Services.tm.dispatchToMainThread(runnable);
  },

  /** nsIRequest */
  get name() {
    return this.uri.spec;
  },
  isPending() {
    return false;
  },
  get status() {
    return Cr.NS_OK;
  },
  cancel() {},
  loadGroup: null,
  loadFlags:
    Ci.nsIRequest.LOAD_NORMAL |
    Ci.nsIRequest.INHIBIT_CACHING |
    Ci.nsIRequest.LOAD_BYPASS_CACHE,
};

async function loadTestTab(uri) {
  // The protocol handler only exists in the parent process, so the pages must
  // load there: this needs a non-remote tab, a system principal owner on the
  // channel and URI_IS_UI_RESOURCE on the protocol.
  gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, uri, {
    forceNotRemote: true,
  });
  let browser = gBrowser.selectedBrowser;

  // The page is loaded with a system principal, whose CSP doesn't allow
  // inline scripts, so submit the form from here.
  let form;
  await TestUtils.waitForCondition(() => {
    form = browser.contentDocument?.getElementById("form");
    return form;
  }, "form is loaded");
  form.submit();

  let frameDoc;
  await TestUtils.waitForCondition(() => {
    frameDoc = browser.contentDocument.getElementById("frame")?.contentDocument;
    return frameDoc?.getElementById("upload_headers");
  }, "action page is loaded in the iframe");

  return [
    frameDoc.getElementById("upload_stream").value,
    atob(frameDoc.getElementById("post_data").value),
    JSON.parse(frameDoc.getElementById("upload_headers").value),
  ];
}

add_task(async function () {
  var handler = new CustomProtocolHandler();
  Services.io.registerProtocolHandler(
    SCHEME,
    handler,
    Ci.nsIProtocolHandler.URI_NORELATIVE |
      Ci.nsIProtocolHandler.URI_IS_LOCAL_RESOURCE |
      Ci.nsIProtocolHandler.URI_IS_UI_RESOURCE,
    -1
  );
  registerCleanupFunction(function () {
    Services.io.unregisterProtocolHandler(SCHEME);
  });
});

add_task(async function () {
  var [hasUploadStream] = await loadTestTab(NORMAL_FORM_URI);
  is(hasUploadStream, "no", "normal action should not have uploadStream");

  gBrowser.removeCurrentTab();
});

add_task(async function () {
  var [hasUploadStream] = await loadTestTab(UPLOAD_FORM_URI);
  is(hasUploadStream, "no", "upload action should not have uploadStream");

  gBrowser.removeCurrentTab();
});

add_task(async function () {
  var [hasUploadStream, postData, headers] = await loadTestTab(POST_FORM_URI);

  is(hasUploadStream, "yes", "post action should have uploadStream");
  is(postData, "foo=bar\r\n", "POST data is received correctly");

  is(headers["Content-Type"], "text/plain", "Content-Type header is correct");
  is(headers["Content-Length"], undefined, "Content-Length header is correct");

  gBrowser.removeCurrentTab();
});
