"use strict";

function handleRequest(request, response) {
  const params = new URLSearchParams(request.queryString);
  response.setHeader("Cache-Control", "no-store", false);

  if (params.has("image")) {
    setSharedState(params.get("image"), request.scheme);
    response.setHeader("Content-Type", "image/gif", false);
    response.write(
      atob("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7")
    );
    return;
  }

  if (params.has("scheme")) {
    response.setHeader("Content-Type", "text/plain", false);
    response.write(getSharedState(params.get("scheme")));
    return;
  }

  response.setHeader("Content-Type", "text/html", false);
  response.setHeader("Connection-Allowlist", params.get("allowlist"), false);
  if (params.has("uir")) {
    response.setHeader(
      "Content-Security-Policy",
      "upgrade-insecure-requests",
      false
    );
  }

  const src = JSON.stringify(params.get("img"));
  response.write(`<!DOCTYPE html>
<script>
  let img = new Image();
  img.onload = () => parent.postMessage("loaded", "*");
  img.onerror = () => parent.postMessage("blocked", "*");
  img.src = ${src};
</script>`);
}
