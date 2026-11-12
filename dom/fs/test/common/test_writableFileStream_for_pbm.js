/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// TODO: Merge into test_writableFileStream.js once getFile() is supported in
// private browsing.

const allowCreate = { create: true };

function createPattern(length, seed) {
  const bytes = new Uint8Array(length);
  for (let i = 0; i < length; i++) {
    bytes[i] = (i * 31 + seed) % 251;
  }
  return bytes;
}

async function writeWithSyncAccessHandle(fileHandle, bytes) {
  const handle = await fileHandle.createSyncAccessHandle();
  try {
    handle.truncate(0);
    Assert.equal(handle.write(bytes, { at: 0 }), bytes.byteLength);
    handle.flush();
  } finally {
    handle.close();
  }
}

async function readWithSyncAccessHandle(fileHandle) {
  const handle = await fileHandle.createSyncAccessHandle();
  try {
    const bytes = new Uint8Array(handle.getSize());
    Assert.equal(handle.read(bytes, { at: 0 }), bytes.byteLength);
    return bytes;
  } finally {
    handle.close();
  }
}

function assertBytesEqual(actual, expected, message) {
  Assert.equal(actual.byteLength, expected.byteLength, message + " (size)");
  Assert.ok(
    actual.every((byte, i) => byte === expected[i]),
    message + " (contents)"
  );
}

exported_symbols.writableFromEmpty = async function () {
  const root = await navigator.storage.getDirectory();
  const fileHandle = await root.getFileHandle(
    "writableFromEmpty.bin",
    allowCreate
  );

  await writeWithSyncAccessHandle(fileHandle, createPattern(10000, 1));

  const written = createPattern(5000, 2);
  const writable = await fileHandle.createWritable({
    keepExistingData: false,
  });
  await writable.write(written);
  await writable.close();

  assertBytesEqual(
    await readWithSyncAccessHandle(fileHandle),
    written,
    "Can we read back what a writable wrote from empty?"
  );

  await root.removeEntry("writableFromEmpty.bin");
};

exported_symbols.writableKeepExistingData = async function () {
  const root = await navigator.storage.getDirectory();
  const fileHandle = await root.getFileHandle(
    "writableKeepExistingData.bin",
    allowCreate
  );

  const existing = createPattern(10000, 1);
  await writeWithSyncAccessHandle(fileHandle, existing);

  // Overwrite a range crossing a block boundary of encrypted files, so that
  // both partially overwritten blocks have to be read back first.
  const position = 4000;
  const overwritten = createPattern(100, 2);
  const usageBefore = await Utils.getCachedOriginUsage();
  const writable = await fileHandle.createWritable({ keepExistingData: true });
  await writable.write({ type: "write", position, data: overwritten });
  await writable.close();

  // The copy replaced the file without changing its size.
  Assert.equal(await Utils.getCachedOriginUsage(), usageBefore);

  const expected = existing.slice();
  expected.set(overwritten, position);

  assertBytesEqual(
    await readWithSyncAccessHandle(fileHandle),
    expected,
    "Does a writable keep the existing data around what it overwrote?"
  );

  await root.removeEntry("writableKeepExistingData.bin");
};

exported_symbols.writableRoundtripWithMove = async function () {
  const root = await navigator.storage.getDirectory();
  const subdir = await root.getDirectoryHandle("writableMoveDir", allowCreate);
  const fileHandle = await root.getFileHandle(
    "writableMoveBefore.bin",
    allowCreate
  );

  const written = createPattern(10000, 1);
  {
    const writable = await fileHandle.createWritable();
    await writable.write(written);
    await writable.close();
  }

  // Moving changes the entry, but not the file the contents are stored in.
  await fileHandle.move(subdir, "writableMoveAfter.bin");

  assertBytesEqual(
    await readWithSyncAccessHandle(fileHandle),
    written,
    "Can we read back what a writable wrote after moving the file?"
  );

  const position = 4000;
  const overwritten = createPattern(100, 2);
  {
    const writable = await fileHandle.createWritable({
      keepExistingData: true,
    });
    await writable.write({ type: "write", position, data: overwritten });
    await writable.close();
  }

  const expected = written.slice();
  expected.set(overwritten, position);

  assertBytesEqual(
    await readWithSyncAccessHandle(fileHandle),
    expected,
    "Does a writable keep the existing data of a moved file?"
  );

  await root.removeEntry("writableMoveDir", { recursive: true });
};

for (const [key, value] of Object.entries(exported_symbols)) {
  Object.defineProperty(value, "name", {
    value: key,
    writable: false,
  });
}
