/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This runs the xpcshell binary with different cases for the executable path.
 * They should all result in the same installation hash.
 */

const { Subprocess } = ChromeUtils.importESModule(
  "resource://gre/modules/Subprocess.sys.mjs"
);

const XRE = Cc["@mozilla.org/xre/directory-provider;1"].getService(
  Ci.nsIXREDirProvider
);
const HASH = XRE.getInstallHash(false);
const EXE = Services.dirsvc.get("XREExeF", Ci.nsIFile);
const GREBIND = Services.dirsvc.get("GreBinD", Ci.nsIFile);
const SCRIPT = do_get_file("show_hash.js", false);

async function getHash(bin) {
  try {
    // If this test is running through firefox.exe -xpcshell, we need
    // to make sure to execute the script through it as well.
    let args = [];
    if (!bin.leafName.toLowerCase().startsWith("xpcshell")) {
      args.push("-xpcshell");
    }
    args.push("-g", GREBIND.path);
    args.push(SCRIPT.path);

    let proc = await Subprocess.call({
      command: bin.path,
      arguments: args,
    });

    let result = "";
    let string;
    while ((string = await proc.stdout.readString())) {
      result += string;
    }

    return result.trim();
  } catch (e) {
    if (e.errorCode == Subprocess.ERROR_BAD_EXECUTABLE) {
      return null;
    }
    throw e;
  }
}

// Walks through a path's entries and calls a mutator function to change the
// case of each.
function mutatePath(path, mutator) {
  let parts = [];
  let index = 0;
  while (path.parent != null) {
    parts.push(mutator(path.leafName, index++));
    path = path.parent;
  }

  while (parts.length) {
    path.append(parts.pop());
  }

  return path;
}

// Counts how many path parts a mutator will be called for.
function countParts(path) {
  let index = 0;
  while (path.parent != null) {
    path = path.parent;
    index++;
  }
  return index;
}

add_task(async function testSameBinary() {
  // Running with the same binary path should definitely work and give the same
  // hash.
  Assert.equal(
    await getHash(EXE),
    HASH,
    "Should have the same hash when running the same binary."
  );
});

add_task(async function testUpperCase() {
  let upper = mutatePath(EXE, p => p.toLocaleUpperCase());
  let hash = await getHash(upper);

  // We may not get a hash if any part of the filesystem is case sensitive.
  if (hash) {
    Assert.equal(
      hash,
      HASH,
      `Should have seen the same hash from ${upper.path}.`
    );
  }
});

add_task(async function testLowerCase() {
  let lower = mutatePath(EXE, p => p.toLocaleLowerCase());
  let hash = await getHash(lower);

  // We may not get a hash if any part of the filesystem is case sensitive.
  if (hash) {
    Assert.equal(
      hash,
      HASH,
      `Should have seen the same hash from ${lower.path}.`
    );
  }
});

add_task(async function testEachPart() {
  // We need to check the case where only some of the directories in the path
  // are case insensitive.

  let count = countParts(EXE);
  for (let i = 0; i < count; i++) {
    let upper = mutatePath(EXE, (p, index) =>
      index == i ? p.toLocaleUpperCase() : p
    );
    let lower = mutatePath(EXE, (p, index) =>
      index == i ? p.toLocaleLowerCase() : p
    );

    let upperHash = await getHash(upper);
    if (upperHash) {
      Assert.equal(
        upperHash,
        HASH,
        `Should have seen the same hash from ${upper.path}.`
      );
    }

    let lowerHash = await getHash(lower);
    if (lowerHash) {
      Assert.equal(
        lowerHash,
        HASH,
        `Should have seen the same hash from ${lower.path}.`
      );
    }
  }
});
