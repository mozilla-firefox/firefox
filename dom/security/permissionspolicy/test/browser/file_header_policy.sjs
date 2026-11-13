"use strict";

// Serves a document whose Permissions-Policy header is the query string.
function handleRequest(request, response) {
  response.setHeader("Content-Type", "text/html; charset=utf-8", false);
  response.setHeader(
    "Permissions-Policy",
    decodeURIComponent(request.queryString),
    false
  );
  response.write("<!DOCTYPE html>");
}
