// Server side js file for bug 910139, see file test_bug910139.html for details.

const { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);

function loadResponseFromFile(path) {
  var testHTMLFile = Cc["@mozilla.org/file/directory_service;1"]
    .getService(Ci.nsIProperties)
    .get("CurWorkD", Ci.nsIFile);
  var dirs = path.split("/");
  for (var i = 0; i < dirs.length; i++) {
    testHTMLFile.append(dirs[i]);
  }
  var testHTMLFileStream = Cc[
    "@mozilla.org/network/file-input-stream;1"
  ].createInstance(Ci.nsIFileInputStream);
  testHTMLFileStream.init(testHTMLFile, -1, 0, 0);
  var testHTML = NetUtil.readInputStreamToString(
    testHTMLFileStream,
    testHTMLFileStream.available()
  );
  return testHTML;
}

var policies = {
  allowed: "default-src 'self'; script-src 'self'", // CSP for checkAllowed
  blocked: "default-src 'self'; script-src *.example.com", // CSP for checkBlocked
};

function handleRequest(request, response) {
  // avoid confusing cache behaviors
  response.setHeader("Cache-Control", "no-cache", false);

  // set the required CSP
  response.setHeader(
    "Content-Security-Policy",
    policies[request.queryString],
    false
  );

  // return the requested XML file.
  response.write(
    loadResponseFromFile("tests/dom/security/test/csp/file_bug910139.xml")
  );
}
