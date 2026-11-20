"use strict";

function handleRequest(request, response) {
  if (request.queryString == "continue") {
    getObjectState("heldDownload", held => held?.wrappedJSObject.finish());
    response.write("OK");
    return;
  }

  response.processAsync();
  response.setHeader("Content-Type", "application/octet-stream", false);
  response.write("first chunk");
  let held = {
    finish() {
      try {
        response.write("second chunk");
        response.finish();
      } catch (ex) {}
    },
  };
  held.wrappedJSObject = held;
  held.QueryInterface = () => held;
  setObjectState("heldDownload", held);
}
