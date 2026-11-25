const PIXELS = [
  [0, 220, 0],
  [255, 0, 0],
  [0, 0, 255],
  [128, 128, 128],
];

const LCS_CALIBRATED_RGB = 0x00000000;
const LCS_SRGB = 0x73524742;
const LCS_LINKED = 0x4c494e4b;
const LCS_EMBEDDED = 0x4d424544;

const BIH_SIZE_WIN_V3 = 40;
const BIH_SIZE_ADOBE_V3 = 56;
const BIH_SIZE_WIN_V4 = 108;
const BIH_SIZE_WIN_V5 = 124;

// Must run before the first image decode, because gfxPlatform reads these
// prefs once when it initializes color management.
function setColorManagementPrefs(aMode) {
  Services.prefs.setBoolPref("layers.gpu-process.enabled", false);
  Services.prefs.setBoolPref("gfx.color_management.force_srgb", false);
  Services.prefs.setBoolPref("gfx.color_management.native_srgb", false);
  Services.prefs.setIntPref("gfx.color_management.mode", aMode);
  Services.prefs.setStringPref(
    "gfx.color_management.display_profile",
    do_get_file("ITU-2020.icc").path
  );
}

/**
 * Builds a 24-bit BI_RGB BMP containing PIXELS as a single row.
 *
 * @param {number} aHeaderSize Info header size in bytes.
 * @param {number} [aCsType] Value of the CSType field (V4 and V5 headers only).
 * @param {Uint8Array} [aProfile] Embedded profile data, placed after the
 *   pixels (V5 headers only). An empty array writes a zero profile size.
 */
function makeBmp(aHeaderSize, aCsType, aProfile) {
  const rowBytes = PIXELS.length * 3;
  const dataOffset = 14 + aHeaderSize;
  const profileLength = aProfile ? aProfile.length : 0;
  const bytes = new Uint8Array(dataOffset + rowBytes + profileLength);
  const view = new DataView(bytes.buffer);
  bytes.set([0x42, 0x4d]);
  view.setUint32(2, bytes.length, true);
  view.setUint32(10, dataOffset, true);
  view.setUint32(14, aHeaderSize, true);
  view.setInt32(18, PIXELS.length, true);
  view.setInt32(22, 1, true);
  view.setUint16(26, 1, true);
  view.setUint16(28, 24, true);
  view.setUint32(34, rowBytes, true);
  if (aCsType !== undefined) {
    view.setUint32(14 + 56, aCsType, true);
  }
  if (aProfile) {
    view.setUint32(14 + 112, aHeaderSize + rowBytes, true);
    view.setUint32(14 + 116, profileLength, true);
    bytes.set(aProfile, dataOffset + rowBytes);
  }
  PIXELS.forEach(([r, g, b], i) => {
    bytes.set([b, g, r], dataOffset + i * 3);
  });
  return bytes;
}

function decodePixels(aBmp) {
  const imgTools = Cc["@mozilla.org/image/tools;1"].getService(Ci.imgITools);
  const container = imgTools.decodeImageFromArrayBuffer(
    aBmp.buffer,
    "image/bmp"
  );
  const stream = imgTools.encodeImage(container, "image/bmp");
  const bin = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
    Ci.nsIBinaryInputStream
  );
  bin.setInputStream(stream);
  const out = new Uint8Array(bin.readByteArray(stream.available()));
  const view = new DataView(out.buffer);
  const dataOffset = view.getUint32(10, true);
  const bytesPerPixel = view.getUint16(28, true) / 8;
  return PIXELS.map((_, i) => {
    const o = dataOffset + i * bytesPerPixel;
    return [out[o + 2], out[o + 1], out[o]];
  });
}

function decodeSRGBTaggedBmp() {
  return decodePixels(makeBmp(BIH_SIZE_WIN_V5, LCS_SRGB));
}

// BMPs that carry no usable color space information.
function untaggedBmps() {
  return [
    ["BITMAPINFOHEADER", makeBmp(BIH_SIZE_WIN_V3)],
    ["Adobe BITMAPV3INFOHEADER", makeBmp(BIH_SIZE_ADOBE_V3)],
    ["V5 LCS_LINKED", makeBmp(BIH_SIZE_WIN_V5, LCS_LINKED)],
    [
      "V4 LCS_CALIBRATED_RGB with zero endpoints",
      makeBmp(BIH_SIZE_WIN_V4, LCS_CALIBRATED_RGB),
    ],
    [
      "V5 embedded profile of size zero",
      makeBmp(BIH_SIZE_WIN_V5, LCS_EMBEDDED, new Uint8Array(0)),
    ],
    [
      "V5 embedded profile that does not parse",
      makeBmp(BIH_SIZE_WIN_V5, LCS_EMBEDDED, new Uint8Array(128)),
    ],
  ];
}
