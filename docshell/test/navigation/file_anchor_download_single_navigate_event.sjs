"use strict";

// ?download: records that the request arrived and never responds, so that no
// actual download happens.
// ?arrived: long-polls, responding once a ?download request has arrived.
// ?release: finishes the pending ?download response and resets the state.
function handleRequest(request, response) {
  response.setHeader("Cache-Control", "no-store", false);

  switch (request.queryString) {
    case "download":
      response.processAsync();
      setState("arrived", "1");
      setObjectState("pendingResponse", response);
      getObjectState("arrivedWaiter", waiter => {
        if (waiter) {
          setObjectState("arrivedWaiter", null);
          waiter.finish();
        }
      });
      break;
    case "arrived":
      if (getState("arrived") !== "1") {
        response.processAsync();
        setObjectState("arrivedWaiter", response);
      }
      break;
    case "release":
      getObjectState("pendingResponse", pending => {
        setObjectState("pendingResponse", null);
        pending?.finish();
      });
      setState("arrived", "");
      break;
  }
}
