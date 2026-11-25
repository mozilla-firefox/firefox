/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const TYPE_ICO = "image/x-icon";
const TYPE_SVG = "image/svg+xml";

export { TYPE_ICO, TYPE_SVG };

// SVG images are send as raw data URLs from the content process to the parent.
// The raw data: URL should NOT be used directly when displaying, but instead wrapped with a moz-remote-image: for safe re-encoding!
export const SVG_DATA_URI_PREFIX = `data:${TYPE_SVG};base64,`;

// URL schemes that we don't want to load and convert to data URLs.
export const TRUSTED_FAVICON_SCHEMES = Object.freeze([
  "chrome",
  "about",
  "resource",
]);

// Maximum width and height of a decoded favicon image.
export const MAX_ICON_SIZE = 2048;

// ICO files listing more distinct sizes than this are decoded only once.
export const MAX_ICO_IMAGE_COUNT = 16;

// ICO images larger than this in both dimensions are downscaled to it.
export const MAX_ICO_IMAGE_SIZE = 512;

/**
 * Checks whether a list of image sizes, that will be decoded together for a
 * single favicon, stays within the count and size limits. Their total area must
 * not exceed the area of a single image of the maximum size.
 *
 * @param {Array<{width: number, height: number}>} sizes
 * @param {number} [maxSize] Maximum width and height of each image.
 * @returns {boolean}
 */
export function areValidFaviconSizes(sizes, maxSize = MAX_ICON_SIZE) {
  if (
    !Array.isArray(sizes) ||
    !sizes.length ||
    sizes.length > MAX_ICO_IMAGE_COUNT
  ) {
    return false;
  }
  let area = 0;
  for (let size of sizes) {
    let { width, height } = size ?? {};
    if (
      !Number.isInteger(width) ||
      !Number.isInteger(height) ||
      width <= 0 ||
      height <= 0 ||
      width > maxSize ||
      height > maxSize
    ) {
      return false;
    }
    area += width * height;
  }
  return area <= MAX_ICON_SIZE * MAX_ICON_SIZE;
}

/**
 * Creates a moz-remote-image: URL wrapping the specified URL.
 *
 * @param {string} url
 *   The URL to (remotely) load the image from.
 * @param {object} options
 *   Further configuration options for loading.
 * @param {number} [options.size]
 *   Either the desired maximum size of the final image or the desired actual
 *   size, depending on the value of the `stretch` option. If `stretch` is
 *   falsey, this can be undefined, in which case the image will be decoded at
 *   its intrinsic size. However, note that SVGs without a `width` and `height`
 *   on their `<svg>` have no intrinsic size, and an undefined `size` will cause
 *   those image loads to fail.
 * @param {boolean} [options.stretch]
 *   Controls how the image will be resized along with the `size` option. If
 *   true (the default) or the image lacks an intrinsic size, it will be
 *   stretched into a square of side length `size`. If false, `size` is treated
 *   as a maximum bound: If the image is larger than `size` in either dimension,
 *   it will be shrunk and its intrinsic aspect ratio will be preserved; if the
 *   image isn't larger, it won't be resized at all.
 * @param {string} [options.colorScheme]
 *   Either "dark" or "light". Used for SVGs.
 * @param {number} [options.contentParentId]
 *   Which process to render the image in.
 */
function getMozRemoteImageURL(url, options = {}) {
  if (options.size !== undefined) {
    options.height = options.width = options.size;
    delete options.size;
  }

  if (options.hasOwnProperty("stretch")) {
    options.stretch = !!options.stretch;
  } else {
    // TODO: This should default to false. Audit and update callers.
    options.stretch = true;
  }

  let params = new URLSearchParams({
    url,
    ...options,
  });
  return "moz-remote-image://?" + params;
}

export { getMozRemoteImageURL };

/**
 * Converts a Blob into a data: URL.
 *
 * @param {Blob} blob The Blob to be converted.
 * @returns {Promise<string>} Returns a promise that resolves with the data: URL
 *                            or rejects with an error.
 */
export function blobAsDataURL(blob) {
  return new Promise((resolve, reject) => {
    let reader = new FileReader();
    reader.addEventListener("load", () => resolve(reader.result));
    reader.addEventListener("error", reject);
    reader.readAsDataURL(blob);
  });
}

// Shim for tabbrowser.js that uses `defineESModuleGetters`.
export let FaviconUtils = {
  SVG_DATA_URI_PREFIX,
  TRUSTED_FAVICON_SCHEMES,
  getMozRemoteImageURL,
};
