"use strict";

const PATH = "/browser/dom/base/test/file_speculation_tags_redirect.sjs";
const NEXT_HOP = {
  "hop=1": `https://example.org${PATH}?hop=2`,
  "hop=2": `https://example.com${PATH}?hop=3`,
};

function handleRequest(request, response) {
  const next = NEXT_HOP[request.queryString];
  if (next) {
    response.setStatusLine(request.httpVersion, 302, "Found");
    response.setHeader("Location", next, false);
    return;
  }
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/html", false);
  response.setHeader("Cache-Control", "no-store", false);
  response.write("<!DOCTYPE html><title>prefetch target</title>");
}
