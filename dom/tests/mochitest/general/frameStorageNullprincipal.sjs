// This is a sjs file which serves frameStoragePrevented.html with a CSP sandbox.
// This produces a document with the null principal, which should be unable to access storage.

function handleRequest(request, response) {
  // Get the nsIFile for frameStoragePrevented.html
  var file;
  getObjectState("SERVER_ROOT", function (serverRoot) {
    file = serverRoot.getFile(
      "/tests/dom/tests/mochitest/general/frameStoragePrevented.html"
    );
  });

  // Set up the file streams to read in the file as UTF-8
  let fstream = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsIFileInputStream
  );
  fstream.init(file, -1, 0, 0);
  let cstream = Cc["@mozilla.org/intl/converter-input-stream;1"].createInstance(
    Ci.nsIConverterInputStream
  );
  cstream.init(fstream, "UTF-8", 0, 0);

  // Read in the file, and concatenate it onto the data string
  let data = "";
  let str = {};
  let read = 0;
  do {
    read = cstream.readString(0xffffffff, str);
    data += str.value;
  } while (read != 0);

  response.setHeader("Content-Type", "text/html", false);
  response.setHeader("Content-Security-Policy", "sandbox allow-scripts", false);
  response.write(data);
}
