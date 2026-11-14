const Timer = Components.Constructor(
  "@mozilla.org/timer;1",
  "nsITimer",
  "initWithCallback"
);

function getParam(request, name) {
  return new URLSearchParams(request.queryString).get(name);
}

function finishWhenRequested(response, key) {
  if (getState(key) == "finish") {
    try {
      response.finish();
    } catch (e) {}
    return;
  }
  new Timer(
    () => finishWhenRequested(response, key),
    50,
    Ci.nsITimer.TYPE_ONE_SHOT
  );
}

function handleRequest(request, response) {
  const key = getParam(request, "key");
  if (getParam(request, "finish") != null) {
    setState(key, "finish");
    response.write("finished");
    return;
  }

  response.setHeader("Content-Type", "video/mp4", false);
  response.setHeader("Content-Length", "1024", false);
  response.processAsync();
  response.write("x");
  finishWhenRequested(response, key);
}
