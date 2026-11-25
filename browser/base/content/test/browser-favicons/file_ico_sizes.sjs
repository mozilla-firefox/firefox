/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Serves an ICO file containing one 32bpp BMP image per size listed in the
// comma separated "sizes" query parameter.

const BinaryOutputStream = Components.Constructor(
  "@mozilla.org/binaryoutputstream;1",
  "nsIBinaryOutputStream",
  "setOutputStream"
);

function createBMP(size) {
  const headerSize = 40;
  const maskRowSize = Math.ceil(size / 32) * 4;
  const bytes = new Uint8Array(
    headerSize + size * size * 4 + maskRowSize * size
  );
  const view = new DataView(bytes.buffer);
  view.setUint32(0, headerSize, true);
  view.setInt32(4, size, true);
  view.setInt32(8, size * 2, true);
  view.setUint16(12, 1, true);
  view.setUint16(14, 32, true);
  bytes.fill(0xff, headerSize, headerSize + size * size * 4);
  return bytes;
}

function createICO(sizes) {
  const images = sizes.map(createBMP);
  const headerSize = 6 + 16 * sizes.length;
  const bytes = new Uint8Array(
    images.reduce((total, image) => total + image.length, headerSize)
  );
  const view = new DataView(bytes.buffer);
  view.setUint16(2, 1, true);
  view.setUint16(4, sizes.length, true);
  let offset = headerSize;
  sizes.forEach((size, i) => {
    const entry = 6 + 16 * i;
    // Sizes larger than 255 are read from the image itself.
    view.setUint8(entry, size > 255 ? 0 : size);
    view.setUint8(entry + 1, size > 255 ? 0 : size);
    view.setUint16(entry + 4, 1, true);
    view.setUint16(entry + 6, 32, true);
    view.setUint32(entry + 8, images[i].length, true);
    view.setUint32(entry + 12, offset, true);
    bytes.set(images[i], offset);
    offset += images[i].length;
  });
  return bytes;
}

function handleRequest(request, response) {
  const params = new URLSearchParams(request.queryString);
  const sizes = params.get("sizes").split(",").map(Number);
  const ico = createICO(sizes);
  response.setHeader("Content-Type", "image/x-icon", false);
  response.setHeader("Cache-Control", "no-store", false);
  new BinaryOutputStream(response.bodyOutputStream).writeByteArray(ico);
}
