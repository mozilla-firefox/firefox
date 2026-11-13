function handleRequest(request, response) {
  if (getState("reloaded") == "reloaded") {
    setState("loaded", "");
    setState("reloaded", "");
    response.setHeader("Content-Type", "text/html", false);
    response.write("\u00FE\u00FF\u00E4");
  } else {
    response.setHeader("Content-Type", "text/html; charset=Shift_JIS", false);
    if (getState("loaded") == "loaded") {
      setState("reloaded", "reloaded");
    } else {
      setState("loaded", "loaded");
    }
    // kilobyte to force late-detection reload
    response.write("a".repeat(1024));
    response.write("<body>");
    response.write("\u00E4");
  }
}
