// Utility functions.

function bytesEqual(bytes, other) {
  if (bytes.byteLength !== other.byteLength) {
    return false;
  }
  return bytes.every((val, i) => val === other[i]);
}

async function createFile(location, contents = "") {
  if (typeof contents === "string") {
    contents = new TextEncoder().encode(contents);
  }
  await IOUtils.write(location, contents);
  const exists = await fileExists(location);
  ok(exists, `Created temporary file at: ${location}`);
}

async function createDir(location) {
  await IOUtils.makeDirectory(location, {
    ignoreExisting: true,
    createAncestors: true,
  });
  const exists = await dirExists(location);
  ok(exists, `Created temporary directory at: ${location}`);
}

async function fileHasBinaryContents(location, expectedContents) {
  if (!(expectedContents instanceof Uint8Array)) {
    throw new TypeError("expectedContents must be a byte array");
  }
  info(`Opening ${location} for reading`);
  const bytes = await IOUtils.read(location);
  return bytesEqual(bytes, expectedContents);
}

async function fileHasTextContents(location, expectedContents) {
  if (typeof expectedContents !== "string") {
    throw new TypeError("expectedContents must be a string");
  }
  info(`Opening ${location} for reading`);
  const bytes = await IOUtils.read(location);
  const contents = new TextDecoder().decode(bytes);
  return contents === expectedContents;
}

async function fileExists(file) {
  try {
    let { type } = await IOUtils.stat(file);
    return type === "regular";
  } catch (ex) {
    return false;
  }
}

async function dirExists(dir) {
  try {
    let { type } = await IOUtils.stat(dir);
    return type === "directory";
  } catch (ex) {
    return false;
  }
}

async function cleanup(...files) {
  for (const file of files) {
    await IOUtils.remove(file, {
      ignoreAbsent: true,
      recursive: true,
    });
    const exists = await IOUtils.exists(file);
    ok(!exists, `Removed temporary file: ${file}`);
  }
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}
