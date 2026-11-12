/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

exported_symbols.smokeTest = async function smokeTest() {
  const storage = navigator.storage;
  const subdirectoryNames = new Set(["Documents", "Downloads", "Music"]);
  const allowCreate = { create: true };

  {
    let root = await storage.getDirectory();
    Assert.ok(root, "Can we access the root directory?");

    let it = await root.values();
    Assert.ok(!!it, "Does root have values iterator?");

    let elem = await it.next();
    Assert.ok(elem.done, "Is root directory empty?");

    for (let dirName of subdirectoryNames) {
      await root.getDirectoryHandle(dirName, allowCreate);
      Assert.ok(true, "Was it possible to add subdirectory " + dirName + "?");
    }
  }

  {
    let root = await storage.getDirectory();
    Assert.ok(root, "Can we refresh the root directory?");

    let it = await root.values();
    Assert.ok(!!it, "Does root have values iterator?");

    let hasElements = false;
    let hangGuard = 0;
    for await (let [key, elem] of root.entries()) {
      Assert.ok(elem, "Is element not non-empty?");
      Assert.equal("directory", elem.kind, "Is found item a directory?");
      Assert.ok(
        elem.name.length >= 1 && elem.name.match("^[A-Za-z]{1,64}"),
        "Are names of the elements strings?"
      );
      Assert.equal(key, elem.name);
      Assert.ok(subdirectoryNames.has(elem.name), "Is name among known names?");
      hasElements = true;
      ++hangGuard;
      if (hangGuard == 10) {
        break; // Exit if there is a hang
      }
    }

    Assert.ok(hasElements, "Is values container now non-empty?");
    Assert.equal(3, hangGuard, "Do we only have three elements?");

    {
      it = await root.values();
      Assert.ok(!!it, "Does root have values iterator?");
      let elem = await it.next();

      await elem.value.getDirectoryHandle("Trash", allowCreate);
      let subit = elem.value.values();
      Assert.ok(!!elem, "Is element not non-empty?");
      let subdirResult = await subit.next();
      let subdir = subdirResult.value;
      Assert.ok(!!subdir, "Is element not non-empty?");
      Assert.equal("directory", subdir.kind, "Is found item a directory?");
      Assert.equal("Trash", subdir.name, "Is found item a directory?");
    }

    const wipeEverything = { recursive: true };
    for (let dirName of subdirectoryNames) {
      await root.removeEntry(dirName, wipeEverything);
      Assert.ok(
        true,
        "Was it possible to remove subdirectory " + dirName + "?"
      );
    }
  }

  {
    let root = await storage.getDirectory();
    Assert.ok(root, "Can we refresh the root directory?");

    let it = root.values();
    Assert.ok(!!it, "Does root have values iterator?");

    let elem = await it.next();
    Assert.ok(elem.done, "Is root directory empty?");
  }
};

exported_symbols.quotaTest = async function () {
  const storage = navigator.storage;
  const allowCreate = { create: true };

  {
    let root = await storage.getDirectory();
    Assert.ok(root, "Can we access the root directory?");

    const fileHandle = await root.getFileHandle("test.txt", allowCreate);
    Assert.ok(!!fileHandle, "Can we get file handle?");

    const isPrivate = await Utils.isPrivateBrowsing();

    const usageAtStart = await Utils.getCachedOriginUsage();
    Assert.ok(true, "usageAtStart: " + usageAtStart);

    const writable = await fileHandle.createWritable();
    Assert.ok(!!writable, "Can we create writable file stream?");

    const usageAtWritableCreated = await Utils.getCachedOriginUsage();
    Assert.equal(
      usageAtWritableCreated - usageAtStart,
      0,
      "Did usage increase when writable was created?"
    );

    const buffer = new ArrayBuffer(42);
    Assert.ok(!!buffer, "Can we create array buffer?");

    const result = await writable.write(buffer);
    Assert.equal(result, undefined, "Can we write entire buffer?");

    const usageAtWriteDone = await Utils.getCachedOriginUsage();
    // Note: Usage should change only on close after 1824305
    // In private browsing, the last encrypted block is only written on close.
    Assert.equal(
      usageAtWriteDone - usageAtWritableCreated,
      isPrivate ? 0 : buffer.byteLength,
      "Is write immediately reflected in usage?"
    );

    await writable.close();

    const usageAtWritableClosed = await Utils.getCachedOriginUsage();

    // In private browsing, files are encrypted on disk in blocks of 4096 bytes.
    Assert.equal(
      usageAtWritableClosed - usageAtWritableCreated,
      isPrivate ? 4096 : buffer.byteLength,
      "Did usage increase by the amount of bytes written?"
    );

    await root.removeEntry("test.txt");

    const usageAtFileDeleted = await Utils.getCachedOriginUsage();

    Assert.equal(
      usageAtFileDeleted,
      usageAtWritableCreated,
      "Is usage back to the value before any writing when the file is removed?"
    );
  }
};

exported_symbols.pagedIterationTest = async function () {
  const root = await navigator.storage.getDirectory();

  for await (let contentItem of root.keys()) {
    await root.removeEntry(contentItem, { recursive: true });
  }

  const allowCreate = { create: true };

  // When half of the buffer is iterated, a request for the second half is sent.
  // We test that the this boundary is crossed smoothly.
  // After the buffer is filled, a request for more items is sent. The
  // items are placed in the first half of the buffer.
  // This boundary should also be crossed without problems.
  // Currently, the buffer is half-filled at 1024.
  const itemBatch = 3 + 2 * 1024;
  for (let i = 0; i <= itemBatch; ++i) {
    await root.getDirectoryHandle("" + i, allowCreate);
  }

  let result = 0;
  let sum = 0;
  const handles = new Set();
  let isUnique = true;
  for await (let [key, elem] of root.entries()) {
    result += key.length;
    sum += parseInt(elem.name);
    if (handles.has(key)) {
      // Asserting here is slow and verbose
      isUnique = false;
      break;
    }
    handles.add(key);
  }
  Assert.ok(isUnique);
  Assert.equal(result, 7098);
  Assert.equal(sum, (itemBatch * (itemBatch + 1)) / 2);
};

for (const [key, value] of Object.entries(exported_symbols)) {
  Object.defineProperty(value, "name", {
    value: key,
    writable: false,
  });
}
