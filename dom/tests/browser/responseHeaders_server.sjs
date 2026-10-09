function handleRequest(request, response) {
  if (request.queryString == "reset") {
    setState("counter", "0");
    setState("log", "");

    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader("Content-Type", "text/text", false);
    const body = "reset";
    response.bodyOutputStream.write(body, body.length);
    return;
  }

  if (request.queryString == "log") {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader("Content-Type", "text/text", false);
    const body = getState("log");
    response.bodyOutputStream.write(body, body.length);
    return;
  }

  setState("log", getState("log") + "," + request.queryString);

  let counter = parseInt(getState("counter"));
  setState("counter", (counter + 1).toString());

  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Cache-Control", "max-age=10000", false);
  if (request.queryString == "vary") {
    response.setHeader("Vary", "Cookie", false);
  }
  response.setHeader("Content-Type", "text/javascript", false);
  const body = `
document.body.setAttribute("counter", "${counter}");
document.cookie = "${counter}";
`;
  response.bodyOutputStream.write(body, body.length);
}
