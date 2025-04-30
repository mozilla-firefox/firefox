/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

const BinaryInputStream = Components.Constructor(
  "@mozilla.org/binaryinputstream;1",
  "nsIBinaryInputStream",
  "setInputStream"
);

function readStream(inputStream) {
  let available = 0;
  let result = [];
  while ((available = inputStream.available()) > 0) {
    result.push(inputStream.readBytes(available));
  }
  return result.join("");
}

function handleRequest(request, response) {
  if (request.method != "POST") {
    response.setStatusLine(request.httpVersion, 405, "Method Not Allowed");
    return;
  }
  if (
    !request.hasHeader("Cookie") ||
    request.getHeader("Cookie") != "credential=authcookieval"
  ) {
    response.setStatusLine(request.httpVersion, 400, "Bad Request");
    return;
  }
  if (
    !request.hasHeader("Sec-Fetch-Dest") ||
    request.getHeader("Sec-Fetch-Dest") != "webidentity"
  ) {
    response.setStatusLine(request.httpVersion, 400, "Bad Request");
    return;
  }
  if (
    !request.hasHeader("Origin") ||
    request.getHeader("Origin") != "https://example.com"
  ) {
    response.setStatusLine(request.httpVersion, 400, "Bad Request");
    return;
  }

  response.setHeader("Access-Control-Allow-Origin", "https://example.com");
  response.setHeader("Access-Control-Allow-Credentials", "true");
  response.setHeader("Content-Type", "application/json");
  let requestContent = readStream(
    new BinaryInputStream(request.bodyInputStream)
  );
  let responseContent = {
    token: requestContent,
  };
  let body = JSON.stringify(responseContent);
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.write(body);
}
