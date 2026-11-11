"use strict";

let timer;

function handleRequest(request, response) {
  response.setHeader("Content-Type", "text/event-stream", false);
  response.setHeader("Cache-Control", "no-cache", false);
  response.processAsync();
  response.write("data: hello\n\n");
  if (request.queryString != "stream") {
    return;
  }
  let remaining = 500;
  timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  timer.initWithCallback(
    () => {
      try {
        response.write("data: more\n\n");
      } catch {
        timer.cancel();
        return;
      }
      if (!--remaining) {
        timer.cancel();
        response.finish();
      }
    },
    10,
    Ci.nsITimer.TYPE_REPEATING_SLACK
  );
}
