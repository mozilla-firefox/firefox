function handleRequest(request, response) {
  const [action, key] = request.queryString.split("&");
  response.setHeader("Cache-Control", "no-cache", false);
  if (action == "start") {
    response.processAsync();
    response.setHeader("Content-Type", "application/xml", false);
    response.write("<root>");
    setObjectState(key, response);
    return;
  }
  getObjectState(key, xmlResponse => {
    xmlResponse.write("</root>");
    xmlResponse.finish();
  });
  response.setHeader("Content-Type", "text/plain", false);
  response.write("ok");
}
