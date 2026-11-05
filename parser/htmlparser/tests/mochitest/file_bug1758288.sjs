function handleRequest(request, response) {
  if (request.queryString == "result") {
    response.setHeader("Cache-Control", "no-cache", false);
    response.setHeader("Content-Type", "text/json", false);
    response.write(JSON.stringify(getState("status") ? "FAIL" : "PASS"));
  } else {
    response.setHeader("Cache-Control", "no-cache", false);
    response.setHeader("Content-Type", "text/plain", false);
    response.write("FAIL");
    setState("status", "FAIL");
  }
}
