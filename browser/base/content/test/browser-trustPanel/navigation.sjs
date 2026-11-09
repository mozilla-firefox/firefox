/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// ?redirect=<url> redirects to <url>; anything else responds with
// 204 No Content, so the navigation never commits a new document.
function handleRequest(request, response) {
  const params = new URLSearchParams(request.queryString);
  if (params.has("redirect")) {
    response.setStatusLine(request.httpVersion, 302, "Found");
    response.setHeader("Location", params.get("redirect"), false);
    return;
  }
  response.setStatusLine(request.httpVersion, 204, "No Content");
}
