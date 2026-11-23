function handleRequest(request, response) {
  response.setHeader("Content-Type", "text/javascript", false);

  var key = "alreadySent" + request.queryString;
  if (!getState(key)) {
    setState(key, "1");
  } else {
    response.setStatusLine("1.1", 404, "Not Found");
  }

  response.write("42");
}
