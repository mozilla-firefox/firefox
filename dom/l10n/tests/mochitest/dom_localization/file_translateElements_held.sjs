"use strict";

const HELD_FTL = "count = Held: { $n }\n";

function finish(response, ftl = "count = Count: { $n }\n") {
  response.setHeader("Cache-Control", "no-cache", false);
  response.setHeader("Content-Type", "text/plain", false);
  response.write(ftl);
}

// Serves held.ftl only after a ?release request, so a test can control when
// a resource load completes. Any other resource is served immediately.
function handleRequest(request, response) {
  const query = request.queryString;

  if (query === "release") {
    let held = null;
    getObjectState("l10n_held_response", x => {
      held = x?.wrappedJSObject.response;
    });
    if (held) {
      setObjectState("l10n_held_response", null);
      finish(held, HELD_FTL);
      held.finish();
    } else {
      setState("l10n_release_requested", "1");
    }
    response.setHeader("Cache-Control", "no-cache", false);
    response.write("ok");
    return;
  }

  if (query === "held.ftl") {
    if (getState("l10n_release_requested") === "1") {
      setState("l10n_release_requested", "");
      finish(response, HELD_FTL);
    } else {
      response.processAsync();
      const x = {
        response,
        QueryInterface() {
          return this;
        },
      };
      x.wrappedJSObject = x;
      setObjectState("l10n_held_response", x);
    }
    return;
  }

  finish(response);
}
