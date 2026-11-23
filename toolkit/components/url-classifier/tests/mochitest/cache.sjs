/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

const CC = Components.Constructor;
const BinaryInputStream = CC(
  "@mozilla.org/binaryinputstream;1",
  "nsIBinaryInputStream",
  "setInputStream"
);

function handleRequest(request, response) {
  var query = {};
  request.queryString.split("&").forEach(function (val) {
    var idx = val.indexOf("=");
    query[val.slice(0, idx)] = unescape(val.slice(idx + 1));
  });

  var responseBody;

  // Store fullhash in the server side.
  if ("list" in query && "fullhash" in query) {
    // In the server side we will store:
    // 1. All the full hashes for a given list
    // 2. All the lists we have right now
    // data is separate by '\n'
    let list = query.list;
    let hashes = getState(list);

    let hash = atob(query.fullhash);
    hashes += hash + "\n";
    setState(list, hashes);

    let lists = getState("lists");
    if (!lists.includes(list)) {
      lists += list + "\n";
      setState("lists", lists);
    }

    return;
    // gethash count return how many gethash request received.
    // This is used by client to know if a gethash request is triggered by gecko
  } else if ("gethashcount" == request.queryString) {
    let counter = getState("counter");
    responseBody = counter == "" ? "0" : counter;
  } else if ("reset" in query) {
    setState(query.reset, "");
    responseBody = "";
  } else {
    let body = new BinaryInputStream(request.bodyInputStream);
    let avail;
    let bytes = [];

    while ((avail = body.available()) > 0) {
      Array.prototype.push.apply(bytes, body.readByteArray(avail));
    }

    let counter = getState("counter");
    counter = counter == "" ? "1" : (parseInt(counter) + 1).toString();
    setState("counter", counter);

    responseBody = parseV2Request(bytes);
  }

  response.setHeader("Content-Type", "text/plain", false);
  response.write(responseBody);
}

function parseV2Request(bytes) {
  var request = String.fromCharCode.apply(this, bytes);
  var [HEADER, PREFIXES] = request.split("\n");
  var [PREFIXSIZE, LENGTH] = HEADER.split(":").map(val => {
    return parseInt(val);
  });

  var ret = "";
  for (var start = 0; start < LENGTH; start += PREFIXSIZE) {
    getState("lists")
      .split("\n")
      .forEach(function (list) {
        var completions = getState(list).split("\n");

        for (var completion of completions) {
          if (completion.indexOf(PREFIXES.substr(start, PREFIXSIZE)) == 0) {
            ret += list + ":1:32\n";
            ret += completion;
          }
        }
      });
  }

  return ret;
}
