// Stands in for the MediaDrm provisioning server. The count query returns how
// many provisioning requests were made since the last reset query.
function handleRequest(request, response) {
  const params = request.queryString.split("&");
  response.setHeader("Cache-Control", "no-store", false);
  response.setHeader("Content-Type", "text/plain", false);

  if (params.includes("reset")) {
    setState("count", "0");
    response.write("0");
    return;
  }

  const count = Number(getState("count") || "0");
  if (params.includes("count")) {
    response.write(String(count));
    return;
  }

  setState("count", String(count + 1));
  if (params.includes("mode=error")) {
    response.setStatusLine(request.httpVersion, 500, "Internal Server Error");
    response.write("provisioning failed");
    return;
  }

  response.setHeader("Content-Type", "application/octet-stream", false);
  response.write("not a provisioning response");
}
