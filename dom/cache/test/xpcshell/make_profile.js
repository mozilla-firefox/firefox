/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 *
 * All images in schema_15_profile.zip are from https://github.com/mdn/sw-test/
 * and are CC licensed by https://www.flickr.com/photos/legofenris/.
 */

// Enumerate the directory tree and store results in entryList as
//
//  { path: 'a/b/c', file: <nsIFile> }
//
// The algorithm starts with the first entry already in entryList.
function enumerate_tree(entryList) {
  for (var index = 0; index < entryList.length; ++index) {
    var path = entryList[index].path;
    var file = entryList[index].file;

    if (file.isDirectory()) {
      var dirList = file.directoryEntries;
      while (dirList.hasMoreElements()) {
        var dirFile = dirList.nextFile;
        entryList.push({ path: path + "/" + dirFile.leafName, file: dirFile });
      }
    }
  }
}

function zip_profile(zipFile, profileDir) {
  var zipWriter = Cc["@mozilla.org/zipwriter;1"].createInstance(
    Ci.nsIZipWriter
  );
  zipWriter.open(zipFile, 0x04 | 0x08 | 0x20);

  var root = profileDir.clone();
  root.append("storage");
  root.append("default");
  root.append("chrome");

  var entryList = [{ path: "storage/default/chrome", file: root }];
  enumerate_tree(entryList);

  entryList.forEach(function (entry) {
    if (entry.file.isDirectory()) {
      zipWriter.addEntryDirectory(
        entry.path,
        entry.file.lastModifiedTime * 1000,
        false
      );
    } else {
      var istream = Cc[
        "@mozilla.org/network/file-input-stream;1"
      ].createInstance(Ci.nsIFileInputStream);
      istream.init(entry.file, -1, -1, 0);
      zipWriter.addEntryStream(
        entry.path,
        entry.file.lastModifiedTime * 1000,
        Ci.nsIZipWriter.COMPRESSION_DEFAULT,
        istream,
        false
      );
      istream.close();
    }
  });

  zipWriter.close();
}

function exactGC() {
  return new Promise(function (resolve) {
    var count = 0;
    function doPreciseGCandCC() {
      function scheduleGCCallback() {
        Cu.forceCC();

        if (++count < 2) {
          doPreciseGCandCC();
        } else {
          resolve();
        }
      }
      Cu.schedulePreciseGC(scheduleGCCallback);
    }
    doPreciseGCandCC();
  });
}

function resetQuotaManager() {
  return new Promise(function (resolve) {
    var prefService = Services.prefs;

    // enable quota manager testing mode
    var pref = "dom.quotaManager.testing";
    prefService.getBranch(null).setBoolPref(pref, true);

    var request = Services.qms.reset();
    request.callback = resolve;

    // disable quota manager testing mode
    // prefService.getBranch(null).setBoolPref(pref, false);
  });
}

function run_test() {
  do_test_pending();
  do_get_profile();
  Cu.importGlobalProperties(["caches"]);

  var directoryService = Services.dirsvc;
  var profileDir = directoryService.get("ProfD", Ci.nsIFile);
  var currentDir = directoryService.get("CurWorkD", Ci.nsIFile);

  var zipFile = currentDir.clone();
  zipFile.append("new_profile.zip");
  if (zipFile.exists()) {
    zipFile.remove(false);
  }
  ok(!zipFile.exists());

  caches
    .open("xpcshell-test")
    .then(function (c) {
      var request = new Request("http://example.com/index.html");
      var response = new Response("hello world");
      return c.put(request, response);
    })
    .then(exactGC)
    .then(resetQuotaManager)
    .then(function () {
      zip_profile(zipFile, profileDir);
      dump("### ### created zip at: " + zipFile.path + "\n");
      do_test_finished();
    })
    .catch(function (e) {
      do_test_finished();
      ok(false, e);
    });
}
