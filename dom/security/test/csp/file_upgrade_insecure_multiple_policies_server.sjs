const IMG_BYTES = atob(
  "iVBORw0KGgoAAAANSUhEUgAAAAUAAAAFCAYAAACNbyblAAAAHElEQVQI12" +
    "P4//8/w38GIAXDIBKE0DHxgljNBAAO9TXL0Y4OHwAAAABJRU5ErkJggg=="
);

const IMG_URL =
  "http://example.com/tests/dom/security/test/csp/file_upgrade_insecure_multiple_policies_server.sjs?img";

function handleRequest(request, response) {
  response.setHeader("Cache-Control", "no-cache", false);

  if (request.queryString == "img") {
    if (request.scheme != "https") {
      response.setStatusLine(request.httpVersion, 404, "Not Found");
      return;
    }
    response.setHeader("Content-Type", "image/png", false);
    response.write(IMG_BYTES);
    return;
  }

  let meta = "";
  if (request.queryString == "header") {
    response.setHeader(
      "Content-Security-Policy",
      "upgrade-insecure-requests, img-src https:",
      false
    );
  } else if (request.queryString == "meta") {
    response.setHeader(
      "Content-Security-Policy",
      "upgrade-insecure-requests",
      false
    );
    meta = `<meta http-equiv="Content-Security-Policy" content="img-src https:">`;
  }

  response.setHeader("Content-Type", "text/html", false);
  response.write(`<!DOCTYPE html>
<html>
<head>${meta}</head>
<body>
<img id="img" src="${IMG_URL}">
<script>
  let img = document.getElementById("img");
  img.onload = () => window.parent.postMessage("img-ok", "*");
  img.onerror = () => window.parent.postMessage("img-error", "*");
</script>
</body>
</html>`);
}
