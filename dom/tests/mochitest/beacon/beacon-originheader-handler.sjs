/*
 * TestSever customized specifically for the needs of:
 * Bug 1280692 - navigator.sendBeacon() should not send origin header
 */

function handleRequest(request, response) {
  response.setHeader("Cache-Control", "no-cache", false);
  response.setHeader("Content-Type", "text/plain", false);

  // case XHR-REQUEST: the xhr-request tries to query the
  // stored header from the beacon request.
  if (request.queryString == "queryheader") {
    var header = getState("originHeader");
    // if the beacon already stored the header - return.
    if (header) {
      response.write(header);
      setState("originHeader", "");
      return;
    }
    // otherwise wait for the beacon request
    response.processAsync();
    setObjectState("xhr-response", response);
    return;
  }

  // case BEACON-REQUEST: get the beacon header and
  // store the header on the server.
  var header = "reset";
  try {
    header = request.getHeader("origin");
  } catch (e) {
    header = "no-header";
  }
  setState("originHeader", header);

  // if there is an xhr-request waiting, return the header now.
  getObjectState("xhr-response", function (xhrResponse) {
    if (!xhrResponse) {
      return;
    }
    setState("originHeader", "");
    setObjectState("xhr-response", null);
    xhrResponse.write(header);
    xhrResponse.finish();
  });
}
