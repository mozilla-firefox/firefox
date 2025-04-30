"use strict";

let timer;

function handleRequest(request, response) {
  response.processAsync();
  const { queryString } = request;
  const DELAY_MS = queryString.split("=")[1];
  timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  timer.init(
    () => {
      response.setHeader("Content-Type", "text/html", false);
      response.write(
        "<body>Slow loading page for netmonitor test. You should never see this.</body>"
      );
      response.finish();
    },
    DELAY_MS,
    Ci.nsITimer.TYPE_ONE_SHOT
  );
}
