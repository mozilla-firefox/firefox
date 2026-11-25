/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  TYPE_ICO,
  SVG_DATA_URI_PREFIX,
  TRUSTED_FAVICON_SCHEMES,
  areValidFaviconSizes,
  blobAsDataURL,
} from "moz-src:///toolkit/modules/FaviconUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  OpenSearchManager:
    "moz-src:///browser/components/search/OpenSearchManager.sys.mjs",
});

let gTestListeners = new Set();

async function drawImageOnCanvas(canvas, image) {
  let data = await image.blob.bytes();
  let frame = new VideoFrame(data, {
    timestamp: 0,
    format: image.format,
    codedWidth: image.displayWidth,
    codedHeight: image.displayHeight,
  });

  try {
    canvas.width = frame.displayWidth;
    canvas.height = frame.displayHeight;
    let ctx = canvas.getContext("2d");
    ctx.drawImage(frame, 0, 0);
  } finally {
    frame.close();
  }
}

// Re-construct the ICO file with different sized PNG images.
// See https://en.wikipedia.org/wiki/ICO_(file_format).
function createICO(images) {
  const ICO_HEADER_SIZE = 6;
  const ICO_DIR_ENTRY_SIZE = 16;

  const metadataSize = ICO_HEADER_SIZE + ICO_DIR_ENTRY_SIZE * images.length;
  const size =
    metadataSize + images.reduce((acc, image) => acc + image.byteLength, 0);

  let buffer = new ArrayBuffer(size);
  let u8 = new Uint8Array(buffer);
  let view = new DataView(buffer);

  view.setUint16(0, 0, true); // idReserved
  view.setUint16(2, 1, true); // idType (1 = ICO)
  view.setUint16(4, images.length, true); // idCount

  let dataOffset = metadataSize; // Append image data directly after the meta data.
  for (let i = 0; i < images.length; i++) {
    const off = ICO_HEADER_SIZE + ICO_DIR_ENTRY_SIZE * i;

    // We use a zero width and height because we always use compressed PNGs,
    // which require this and have their own width/height information.
    view.setUint8(off, 0); // bWidth
    view.setUint8(off + 1, 0); // bHeight
    view.setUint8(off + 2, 0); // bColorCount
    view.setUint8(off + 3, 0); // bReserved
    view.setUint16(off + 4, 1, true); // wPlanes
    view.setUint16(off + 6, 32, true); // wBitCount
    view.setUint32(off + 8, images[i].byteLength, true); // dwBytesInRes
    view.setUint32(off + 12, dataOffset, true); // dwImageOffset

    // Copy the image's bytes into the ICO buffer.
    u8.set(images[i], dataOffset);

    dataOffset += images[i].byteLength;
  }

  return buffer;
}

export class LinkHandlerParent extends JSWindowActorParent {
  static addListenerForTests(listener) {
    gTestListeners.add(listener);
  }

  static removeListenerForTests(listener) {
    gTestListeners.delete(listener);
  }

  receiveMessage(aMsg) {
    let browser = this.browsingContext.top.embedderElement;
    if (!browser) {
      return;
    }

    let win = browser.documentGlobal;

    let gBrowser = win.gBrowser;

    switch (aMsg.name) {
      case "Link:LoadingIcon":
        if (!gBrowser) {
          return;
        }

        if (aMsg.data.isTabIcon) {
          let tab = gBrowser.getTabForBrowser(browser);
          if (tab.hasAttribute("busy")) {
            tab.setAttribute("pendingicon", "true");
          }
        }

        this.notifyTestListeners("LoadingIcon", aMsg.data);
        break;

      case "Link:ExpireFavicons":
        lazy.PlacesUtils.favicons
          .expireFaviconsForPage(this.manager.documentURI)
          .catch(console.error);
        break;

      case "Link:SetIcon":
        if (!gBrowser) {
          return;
        }

        this.setIconFromLink(gBrowser, browser, aMsg.data);

        this.notifyTestListeners("SetIcon", aMsg.data);
        break;

      case "Link:SetFailedIcon":
        if (!gBrowser) {
          return;
        }

        if (aMsg.data.isTabIcon) {
          this.clearPendingIcon(gBrowser, browser);
        }

        this.notifyTestListeners("SetFailedIcon", aMsg.data);
        break;

      case "Link:AddSearch": {
        if (!gBrowser) {
          return;
        }

        let tab = gBrowser.getTabForBrowser(browser);
        if (!tab) {
          break;
        }

        lazy.OpenSearchManager.addEngine(browser, aMsg.data.engine);
        break;
      }
    }
  }

  notifyTestListeners(name, data) {
    for (let listener of gTestListeners) {
      listener(name, data);
    }
  }

  clearPendingIcon(gBrowser, aBrowser) {
    let tab = gBrowser.getTabForBrowser(aBrowser);
    tab.removeAttribute("pendingicon");
  }

  async setIconFromLink(
    gBrowser,
    browser,
    {
      originalURL,
      expiration,
      iconURL,
      images,
      canStoreIcon,
      beforePageShow,
      isRichIcon,
      isTabIcon,
    }
  ) {
    let tab = gBrowser.getTabForBrowser(browser);
    if (!tab) {
      return;
    }

    if (images) {
      if (
        !Array.isArray(images) ||
        !areValidFaviconSizes(
          images.map(image => ({
            width: image?.displayWidth,
            height: image?.displayHeight,
          }))
        )
      ) {
        console.error(`Ignoring invalid images for favicon "${originalURL}".`);
        if (isTabIcon) {
          this.clearPendingIcon(gBrowser, browser);
        }
        return;
      }

      let canvas = tab.ownerDocument.createElement("canvas");

      // We have multiple images, need to create an ICO file to collect them.
      if (images.length > 1) {
        // Convert all images to PNG bytes.
        let blobs = [];
        for (let image of images) {
          await drawImageOnCanvas(canvas, image);
          blobs.push(await new Promise(resolve => canvas.toBlob(resolve)));
        }
        let buffers = await Promise.all(blobs.map(blob => blob.bytes()));

        // Create an ICO "file" containing all the PNGs.
        let ico = createICO(buffers);

        // Convert the ICO bytes to a data URL.
        iconURL = await blobAsDataURL(new Blob([ico], { type: TYPE_ICO }));
      } else {
        await drawImageOnCanvas(canvas, images[0]);
        iconURL = canvas.toDataURL();
      }
    }

    // The browser might have gone away during `await` above.
    if (!gBrowser.getBrowserForTab(tab)) {
      return;
    }

    if (isTabIcon) {
      this.clearPendingIcon(gBrowser, browser);
    }

    let iconURI;
    try {
      iconURI = Services.io.newURI(iconURL);
    } catch (ex) {
      console.error(ex);
      return;
    }

    // The content process should send decoded images for all schemes except for trusted schemes and SVGs, which should not be rasterized.
    if (
      !images &&
      !TRUSTED_FAVICON_SCHEMES.includes(iconURI.scheme) &&
      !iconURL.startsWith(SVG_DATA_URI_PREFIX)
    ) {
      console.error(
        `Not allowed to set favicon "${iconURL}" with that scheme!`
      );
      return;
    }

    if (!iconURI.schemeIs("data")) {
      try {
        Services.scriptSecurityManager.checkLoadURIWithPrincipal(
          browser.contentPrincipal,
          iconURI,
          Services.scriptSecurityManager.ALLOW_CHROME
        );
      } catch (ex) {
        return;
      }
    }
    if (canStoreIcon) {
      try {
        lazy.PlacesUtils.favicons
          .setFaviconForPage(
            this.manager.documentURI,
            Services.io.newURI(originalURL),
            iconURI,
            expiration && lazy.PlacesUtils.toPRTime(expiration),
            isRichIcon
          )
          .catch(console.error);
      } catch (ex) {
        console.error(ex);
      }
    }

    if (isTabIcon) {
      gBrowser.setIcon(tab, iconURL, originalURL, beforePageShow);
    }
  }
}
