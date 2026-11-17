/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const SIZES = { big: 100000, small: 10, unknown: 10 };

function handleRequest(request, response) {
  let params = new URLSearchParams(request.queryString);
  let type = params.get("type");
  switch (type) {
    case "page": {
      response.setHeader("Content-Type", "text/html", false);
      let images = params
        .get("resources")
        .split(",")
        .map(r => `<img src="file_browserPersist_progress.sjs?type=${r}">`);
      response.write(`<!DOCTYPE html><meta charset="utf-8">${images.join("")}`);
      break;
    }
    case "big":
    case "small":
      setResourceHeaders(request, response);
      response.write("x".repeat(SIZES[type]));
      break;
    case "unknown": {
      setResourceHeaders(request, response);
      let state = getState("unknown");
      if (!state) {
        response.write("x".repeat(SIZES.unknown));
        break;
      }
      // Asynchronous responses have no Content-Length, so the size is unknown.
      setState("unknown", "");
      response.processAsync();
      if (state == "released") {
        response.write("x".repeat(SIZES.unknown));
        response.finish();
      } else {
        // Hold the response until the test asks to release it.
        setObjectState("persist_progress_unknown", response);
      }
      break;
    }
    case "arm":
      setState("unknown", "hold");
      response.write("OK");
      break;
    case "release":
      getObjectState("persist_progress_unknown", heldResponse => {
        if (heldResponse) {
          heldResponse.write("x".repeat(SIZES.unknown));
          heldResponse.finish();
        } else if (getState("unknown") == "hold") {
          // The request hasn't arrived yet, so answer it as soon as it does.
          setState("unknown", "released");
        }
      });
      setObjectState("persist_progress_unknown", null);
      response.write("OK");
      break;
  }
}

function setResourceHeaders(request, response) {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "application/octet-stream", false);
  response.setHeader("Cache-Control", "no-store", false);
}
