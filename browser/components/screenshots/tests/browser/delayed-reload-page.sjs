/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let gTimer;

const PAGE = `<!DOCTYPE html>
<html lang="en">
<head><title>Screenshots</title></head>
<body style="margin: 0;">
  <div id="testPageElement" style="height:500px; width:500px;"></div>
</body>
</html>`;

function sendPage(response) {
  response.setHeader("Content-Type", "text/html", false);
  response.setHeader("Cache-Control", "no-store", false);
  response.write(PAGE);
}

function handleRequest(request, response) {
  let query = request.queryString;
  if (query == "hold" || query == "release") {
    setState("held", query == "hold" ? "1" : "");
    response.setHeader("Content-Type", "text/plain", false);
    response.write("ok");
    return;
  }

  if (getState("held") != "1") {
    sendPage(response);
    return;
  }

  response.processAsync();
  gTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  gTimer.initWithCallback(
    () => {
      if (getState("held") == "1") {
        return;
      }
      gTimer.cancel();
      sendPage(response);
      response.finish();
    },
    50,
    Ci.nsITimer.TYPE_REPEATING_SLACK
  );
}
