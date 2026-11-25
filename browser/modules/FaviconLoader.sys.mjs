/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Bug 1924775 - ESLint doesn't yet know about `ImageDecoder`.
/* globals ImageDecoder:false */

import {
  TYPE_SVG,
  TYPE_ICO,
  TRUSTED_FAVICON_SCHEMES,
  MAX_ICON_SIZE,
  MAX_ICO_IMAGE_SIZE,
  areValidFaviconSizes,
  blobAsDataURL,
} from "moz-src:///toolkit/modules/FaviconUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
});

const STREAM_SEGMENT_SIZE = 4096;
const PR_UINT32_MAX = 0xffffffff;

const BinaryInputStream = Components.Constructor(
  "@mozilla.org/binaryinputstream;1",
  "nsIBinaryInputStream",
  "setInputStream"
);
const StorageStream = Components.Constructor(
  "@mozilla.org/storagestream;1",
  "nsIStorageStream",
  "init"
);
const BufferedOutputStream = Components.Constructor(
  "@mozilla.org/network/buffered-output-stream;1",
  "nsIBufferedOutputStream",
  "init"
);

const SIZES_TELEMETRY_ENUM = {
  NO_SIZES: 0,
  ANY: 1,
  DIMENSION: 2,
  INVALID: 3,
};

const FAVICON_PARSING_TIMEOUT = 100;
// Without an explicit idle timeout DeferredTask waits indefinitely for an idle
// slot, letting a busy content process suppress favicon updates globally.
const FAVICON_PARSING_MAX_IDLE_WAIT = 3000;
const FAVICON_LARGE_ICON_MIN_WIDTH = 96;
const PREFERRED_WIDTH = 16;

const MAX_FAVICON_EXPIRATION = 7 * 24 * 60 * 60 * 1000;

async function decodeImage({
  url,
  type,
  data,
  transfer,
  desiredWidth,
  desiredHeight,
}) {
  let decoder;
  let image;
  try {
    try {
      decoder = new ImageDecoder({
        type,
        data,
        desiredWidth,
        desiredHeight,
        transfer: transfer ? [data] : undefined,
      });

      let result = await decoder.decode({ completeFramesOnly: true });
      image = result.image;
    } catch {
      throw Components.Exception(
        `Favicon at "${url}" could not be decoded.`,
        Cr.NS_ERROR_FAILURE
      );
    }

    if (
      image.displayWidth > MAX_ICON_SIZE ||
      image.displayHeight > MAX_ICON_SIZE
    ) {
      throw Components.Exception(
        `Favicon at "${url}" is too large.`,
        Cr.NS_ERROR_FAILURE
      );
    }

    let imageBuffer = new ArrayBuffer(image.allocationSize());
    await image.copyTo(imageBuffer);
    return {
      blob: new Blob([imageBuffer]),
      format: image.format,
      displayWidth: image.displayWidth,
      displayHeight: image.displayHeight,
    };
  } finally {
    image?.close();
    decoder?.close();
  }
}

// Convert image data bytes to an array of blobs with associated format/size info.
async function convertImage(url, type, data) {
  let icoImageSizes = [];
  if (type == TYPE_ICO) {
    let decoder;
    try {
      decoder = new ImageDecoder({
        type,
        data,
      });
      await decoder.tracks.ready;
      // Deduplicate the sizes, an ICO may contain the same size at different color
      // depths. Images too large in both dimensions are downscaled, the others
      // fail the check below, because the decoder ignores a desired size larger
      // than the intrinsic size.
      icoImageSizes = Array.from(
        new Map(
          decoder.tracks[0].getSizes().map(size => {
            if (
              size.width > MAX_ICO_IMAGE_SIZE &&
              size.height > MAX_ICO_IMAGE_SIZE
            ) {
              size = { width: MAX_ICO_IMAGE_SIZE, height: MAX_ICO_IMAGE_SIZE };
            }
            return [`${size.width}x${size.height}`, size];
          })
        ).values()
      );
      if (
        icoImageSizes.length > 1 &&
        areValidFaviconSizes(icoImageSizes, MAX_ICO_IMAGE_SIZE)
      ) {
        return await Promise.all(
          icoImageSizes.map(({ width, height }) =>
            decodeImage({
              url,
              type,
              // Can't transfer the data buffer, because we decode multiple times.
              transfer: false,
              data,
              // Decode the ICO image at the different sizes of contained images.
              desiredWidth: width,
              desiredHeight: height,
            })
          )
        );
      }
    } catch {
      // Fall back to decoding a single image below.
    } finally {
      decoder?.close();
    }
  }

  // Downscale a single ICO image, but never upscale it.
  let fallbackDesiredSize = icoImageSizes.some(
    ({ width, height }) =>
      width >= MAX_ICO_IMAGE_SIZE && height >= MAX_ICO_IMAGE_SIZE
  )
    ? MAX_ICO_IMAGE_SIZE
    : undefined;
  let image = await decodeImage({
    url,
    type,
    transfer: true,
    data,
    desiredWidth: fallbackDesiredSize,
    desiredHeight: fallbackDesiredSize,
  });
  return [image];
}

class FaviconLoad {
  constructor(iconInfo) {
    this.icon = iconInfo;

    let securityFlags;
    if (iconInfo.node.crossOrigin === "anonymous") {
      securityFlags = Ci.nsILoadInfo.SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT;
    } else if (iconInfo.node.crossOrigin === "use-credentials") {
      securityFlags =
        Ci.nsILoadInfo.SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT |
        Ci.nsILoadInfo.SEC_COOKIES_INCLUDE;
    } else {
      securityFlags =
        Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT;
    }

    this.channel = Services.io.newChannelFromURI(
      iconInfo.iconUri,
      iconInfo.node,
      iconInfo.node.nodePrincipal,
      iconInfo.node.nodePrincipal,
      securityFlags |
        Ci.nsILoadInfo.SEC_ALLOW_CHROME |
        Ci.nsILoadInfo.SEC_DISALLOW_SCRIPT,
      Ci.nsIContentPolicy.TYPE_INTERNAL_IMAGE_FAVICON
    );

    if (this.channel instanceof Ci.nsIHttpChannel) {
      this.channel.QueryInterface(Ci.nsIHttpChannel);
      let referrerInfo = Cc["@mozilla.org/referrer-info;1"].createInstance(
        Ci.nsIReferrerInfo
      );
      // Sometimes node is a document and sometimes it is an element. We need
      // to set the referrer info correctly either way.
      if (iconInfo.node.nodeType == iconInfo.node.DOCUMENT_NODE) {
        referrerInfo.initWithDocument(iconInfo.node);
      } else {
        referrerInfo.initWithElement(iconInfo.node);
      }
      this.channel.referrerInfo = referrerInfo;
    }
    if (iconInfo.isForceReload) {
      this.channel.loadFlags |=
        Ci.nsIRequest.LOAD_BACKGROUND | Ci.nsIRequest.LOAD_BYPASS_CACHE;
    } else {
      this.channel.loadFlags |=
        Ci.nsIRequest.LOAD_BACKGROUND |
        Ci.nsIRequest.VALIDATE_NEVER |
        Ci.nsIRequest.LOAD_FROM_CACHE;
    }
    // Sometimes node is a document and sometimes it is an element. This is
    // the easiest single way to get to the load group in both those cases.
    this.channel.loadGroup =
      iconInfo.node.documentGlobal.document.documentLoadGroup;
    this.channel.notificationCallbacks = this;

    if (this.channel instanceof Ci.nsIHttpChannelInternal) {
      this.channel.blockAuthPrompt = true;
    }

    if (
      Services.prefs.getBoolPref("network.http.tailing.enabled", true) &&
      this.channel instanceof Ci.nsIClassOfService
    ) {
      this.channel.addClassFlags(
        Ci.nsIClassOfService.Tail | Ci.nsIClassOfService.Throttleable
      );
    }
  }

  load() {
    this._deferred = Promise.withResolvers();

    // Clear the references when we succeed or fail.
    let cleanup = () => {
      this.channel = null;
      this.dataBuffer = null;
      this.stream = null;
    };
    this._deferred.promise.then(cleanup, cleanup);

    this.dataBuffer = new StorageStream(STREAM_SEGMENT_SIZE, PR_UINT32_MAX);

    // storage streams do not implement writeFrom so wrap it with a buffered stream.
    this.stream = new BufferedOutputStream(
      this.dataBuffer.getOutputStream(0),
      STREAM_SEGMENT_SIZE * 2
    );

    try {
      this.channel.asyncOpen(this);
    } catch (e) {
      this._deferred.reject(e);
    }

    return this._deferred.promise;
  }

  cancel() {
    if (!this.channel) {
      return;
    }

    this.channel.cancel(Cr.NS_BINDING_ABORTED);
  }

  onStartRequest() {}

  onDataAvailable(request, inputStream, offset, count) {
    this.stream.writeFrom(inputStream, count);
  }

  asyncOnChannelRedirect(oldChannel, newChannel, flags, callback) {
    if (oldChannel == this.channel) {
      this.channel = newChannel;
    }

    callback.onRedirectVerifyCallback(Cr.NS_OK);
  }

  async onStopRequest(request, statusCode) {
    if (request != this.channel) {
      // Indicates that a redirect has occurred. We don't care about the result
      // of the original channel.
      return;
    }

    this.stream.close();
    this.stream = null;

    if (!Components.isSuccessCode(statusCode)) {
      if (statusCode == Cr.NS_BINDING_ABORTED) {
        this._deferred.reject(
          Components.Exception(
            `Favicon load from ${this.icon.iconUri.spec} was cancelled.`,
            statusCode
          )
        );
      } else {
        this._deferred.reject(
          Components.Exception(
            `Favicon at "${this.icon.iconUri.spec}" failed to load.`,
            statusCode
          )
        );
      }
      return;
    }

    if (this.channel instanceof Ci.nsIHttpChannel) {
      if (!this.channel.requestSucceeded) {
        this._deferred.reject(
          Components.Exception(
            `Favicon at "${this.icon.iconUri.spec}" failed to load: ${this.channel.responseStatusText}.`,
            { data: { httpStatus: this.channel.responseStatus } }
          )
        );
        return;
      }
    }

    // By default we don't store icons added after the `pageshow` event as they
    // may be used to show a badge, indicate a service status, or other form
    // of icon animations.
    let canStoreIcon = this.icon.beforePageShow;
    // We make an exception for root icons, as they are unlikely to be used
    // as status indicators, and in general they are always usable.
    if (this.icon.iconUri.filePath == "/favicon.ico") {
      canStoreIcon = true;
    } else {
      // Do not store non-root icons if `Cache-Control: no-store` header is set.
      try {
        if (
          this.channel instanceof Ci.nsIHttpChannel &&
          this.channel.isNoStoreResponse()
        ) {
          canStoreIcon = false;
        }
      } catch (ex) {
        if (ex.result != Cr.NS_ERROR_NOT_AVAILABLE) {
          throw ex;
        }
      }
    }

    // Attempt to get an expiration time from the cache.  If this fails, we'll
    // use this default.
    let expiration = Date.now() + MAX_FAVICON_EXPIRATION;

    // This stuff isn't available after onStopRequest returns (so don't start
    // any async operations before this!).
    if (this.channel instanceof Ci.nsICacheInfoChannel) {
      try {
        expiration = Math.min(
          this.channel.cacheTokenExpirationTime * 1000,
          expiration
        );
      } catch (e) {
        // Ignore failures to get the expiration time.
      }
    }

    try {
      let stream = new BinaryInputStream(this.dataBuffer.newInputStream(0));
      let buffer = new ArrayBuffer(this.dataBuffer.length);
      stream.readArrayBuffer(buffer.byteLength, buffer);

      let type = this.channel.contentType;
      let images, dataURL;
      if (type != "image/svg+xml") {
        let octets = new Uint8Array(buffer);
        let sniffer = Cc["@mozilla.org/image/loader;1"].createInstance(
          Ci.nsIContentSniffer
        );
        type = sniffer.getMIMETypeFromContent(
          this.channel,
          octets,
          octets.length
        );

        if (!type) {
          throw Components.Exception(
            `Favicon at "${this.icon.iconUri.spec}" did not match a known mimetype.`,
            Cr.NS_ERROR_FAILURE
          );
        }

        images = await convertImage(this.icon.iconUri.spec, type, buffer);
      } else {
        dataURL = await blobAsDataURL(new Blob([buffer], { type }));
      }

      this._deferred.resolve({
        expiration,
        images,
        dataURL,
        canStoreIcon,
      });
    } catch (e) {
      this._deferred.reject(e);
    }
  }

  getInterface(iid) {
    if (iid.equals(Ci.nsIChannelEventSink)) {
      return this;
    }
    throw Components.Exception("", Cr.NS_ERROR_NO_INTERFACE);
  }
}

/**
 * Extract the icon width from the size attribute. It also sends the telemetry
 * about the size type and size dimension info.
 *
 * @param {Array} aSizes An array of strings about size.
 * @return {number} A width of the icon in pixel.
 */
function extractIconSize(aSizes) {
  let width = -1;
  let sizesType;
  const re = /^([1-9][0-9]*)x[1-9][0-9]*$/i;

  if (aSizes.length) {
    for (let size of aSizes) {
      if (size.toLowerCase() == "any") {
        sizesType = SIZES_TELEMETRY_ENUM.ANY;
        break;
      } else {
        let values = re.exec(size);
        if (values && values.length > 1) {
          sizesType = SIZES_TELEMETRY_ENUM.DIMENSION;
          width = parseInt(values[1]);
          break;
        } else {
          sizesType = SIZES_TELEMETRY_ENUM.INVALID;
          break;
        }
      }
    }
  } else {
    sizesType = SIZES_TELEMETRY_ENUM.NO_SIZES;
  }

  // Telemetry probes for measuring the sizes attribute
  // usage and available dimensions.
  Glean.linkIconSizesAttr.usage.accumulateSingleSample(sizesType);
  if (width > 0) {
    Glean.linkIconSizesAttr.dimension.accumulateSingleSample(width);
  }

  return width;
}

/**
 * Get link icon URI from a link dom node.
 *
 * @param {DOMNode} aLink A link dom node.
 * @return {nsIURI} A uri of the icon.
 */
function getLinkIconURI(aLink) {
  let targetDoc = aLink.ownerDocument;
  let uri = Services.io.newURI(aLink.href, targetDoc.characterSet);
  try {
    uri = uri.mutate().setUserPass("").finalize();
  } catch (e) {
    // some URIs are immutable
  }
  return uri;
}

/**
 * Guess a type for an icon based on its declared type or file extension.
 */
function guessType(icon) {
  // No type with no icon
  if (!icon) {
    return "";
  }

  // Use the file extension to guess at a type we're interested in
  if (!icon.type) {
    let extension = icon.iconUri.filePath.split(".").pop();
    switch (extension) {
      case "ico":
        return TYPE_ICO;
      case "svg":
        return TYPE_SVG;
    }
  }

  // Fuzzily prefer the type or fall back to the declared type
  return icon.type == "image/vnd.microsoft.icon" ? TYPE_ICO : icon.type || "";
}

/**
 * Selects the best large icon and tab icon from a list of IconInfo objects.
 *
 * @param {Array} iconInfos A list of IconInfo objects.
 * @param {integer} preferredWidth The preferred width for tab icons.
 */
function selectIcons(iconInfos, preferredWidth) {
  if (!iconInfos.length) {
    return {
      largeIcon: null,
      tabIcon: null,
    };
  }

  let preferredIcon;
  let bestSizedIcon;
  // Other links with the "icon" tag are the default icons
  let defaultIcon;
  // Large icons are either rich icons (apple-touch or fluid icons), or the ones
  // with a declared width of at least FAVICON_LARGE_ICON_MIN_WIDTH.
  let bestLargeIcon;

  for (let icon of iconInfos) {
    if (!icon.isRichIcon) {
      // First check for svg. If it's not available check for an icon with a
      // size adapt to the current resolution. If both are not available, prefer
      // ico files. When multiple icons are in the same set, the latest wins.
      if (guessType(icon) == TYPE_SVG) {
        preferredIcon = icon;
      } else if (
        icon.width == preferredWidth &&
        guessType(preferredIcon) != TYPE_SVG
      ) {
        preferredIcon = icon;
      } else if (
        guessType(icon) == TYPE_ICO &&
        (!preferredIcon || guessType(preferredIcon) == TYPE_ICO)
      ) {
        preferredIcon = icon;
      }

      // Check for an icon larger yet closest to preferredWidth, that can be
      // downscaled efficiently.
      if (
        icon.width >= preferredWidth &&
        (!bestSizedIcon || bestSizedIcon.width >= icon.width)
      ) {
        bestSizedIcon = icon;
      }
    }

    // Note that some sites use hi-res icons without specifying them as
    // apple-touch or fluid icons.
    if (icon.isRichIcon || icon.width >= FAVICON_LARGE_ICON_MIN_WIDTH) {
      if (!bestLargeIcon || bestLargeIcon.width < icon.width) {
        bestLargeIcon = icon;
      }
    } else {
      defaultIcon = icon;
    }
  }

  // Now set the favicons for the page in the following order:
  // 1. Set the best large icon if any.
  // 2. Set the preferred one if any, otherwise check if there's a better
  //    sized fit.
  // This order allows smaller icon frames to eventually override large icon
  // frames.

  let tabIcon = null;
  if (preferredIcon) {
    tabIcon = preferredIcon;
  } else if (bestSizedIcon) {
    tabIcon = bestSizedIcon;
  } else if (defaultIcon) {
    tabIcon = defaultIcon;
  }

  return {
    largeIcon: bestLargeIcon,
    tabIcon,
  };
}

class IconLoader {
  constructor(actor) {
    this.actor = actor;
  }

  async load(iconInfo) {
    if (this._loader) {
      // If we're already loading this icon, just let it finish.
      if (this._loader.icon.iconUri.equals(iconInfo.iconUri)) {
        return;
      }
      this._loader.cancel();
    }

    if (TRUSTED_FAVICON_SCHEMES.includes(iconInfo.iconUri.scheme)) {
      // We need to do a manual security check because the channel won't do
      // it for us.
      try {
        Services.scriptSecurityManager.checkLoadURIWithPrincipal(
          iconInfo.node.nodePrincipal,
          iconInfo.iconUri,
          Services.scriptSecurityManager.ALLOW_CHROME
        );
      } catch (ex) {
        return;
      }
      this.actor.sendAsyncMessage("Link:SetIcon", {
        originalURL: iconInfo.iconUri.spec,
        expiration: undefined,
        iconURL: iconInfo.iconUri.spec,
        canStoreIcon:
          iconInfo.beforePageShow && iconInfo.iconUri.schemeIs("data"),
        beforePageShow: iconInfo.beforePageShow,
        isRichIcon: iconInfo.isRichIcon,
        isTabIcon: iconInfo.isTabIcon,
      });
      return;
    }

    // Let the main process that a tab icon is possibly coming.
    this.actor.sendAsyncMessage("Link:LoadingIcon", {
      originalURL: iconInfo.iconUri.spec,
      isRichIcon: iconInfo.isRichIcon,
      isTabIcon: iconInfo.isTabIcon,
    });

    try {
      this._loader = new FaviconLoad(iconInfo);
      let { dataURL, images, expiration, canStoreIcon } =
        await this._loader.load();

      this.actor.sendAsyncMessage("Link:SetIcon", {
        originalURL: iconInfo.iconUri.spec,
        expiration,
        iconURL: dataURL,
        images,
        canStoreIcon,
        beforePageShow: iconInfo.beforePageShow,
        isRichIcon: iconInfo.isRichIcon,
        isTabIcon: iconInfo.isTabIcon,
      });
    } catch (e) {
      if (e.result != Cr.NS_BINDING_ABORTED) {
        if (typeof e.data?.wrappedJSObject?.httpStatus !== "number") {
          console.error(e);
        }

        // Used mainly for tests currently.
        this.actor.sendAsyncMessage("Link:SetFailedIcon", {
          originalURL: iconInfo.iconUri.spec,
          isRichIcon: iconInfo.isRichIcon,
          isTabIcon: iconInfo.isTabIcon,
        });
      }
    } finally {
      this._loader = null;
    }
  }

  cancel() {
    if (!this._loader) {
      return;
    }

    this._loader.cancel();
    this._loader = null;
  }
}

export class FaviconLoader {
  constructor(actor) {
    this.actor = actor;
    this.iconInfos = [];

    // Icons added after onPageShow() are likely added by modifying <link> tags
    // through javascript; we want to avoid storing those permanently because
    // they are probably used to show badges, and many of them could be
    // randomly generated. This boolean can be used to track that case.
    this.beforePageShow = true;

    // For every page we attempt to find a large icon and a tab icon. These
    // objects take care of the load process for each.
    this.largeIconLoader = new IconLoader(actor);
    this.tabIconLoader = new IconLoader(actor);

    this.iconTask = new lazy.DeferredTask(
      () => this.loadIcons(),
      FAVICON_PARSING_TIMEOUT,
      FAVICON_PARSING_MAX_IDLE_WAIT
    );
  }

  loadIcons() {
    // If the page is unloaded immediately after the DeferredTask's timer fires
    // we can still attempt to load icons, which will fail since the content
    // window is no longer available. Checking if iconInfos has been cleared
    // allows us to bail out early in this case.
    if (!this.iconInfos.length) {
      return;
    }

    let preferredWidth =
      PREFERRED_WIDTH * Math.ceil(this.actor.contentWindow.devicePixelRatio);
    let { largeIcon, tabIcon } = selectIcons(this.iconInfos, preferredWidth);
    this.iconInfos = [];

    let isForceReload =
      this.beforePageShow && (this.actor.docShell?.isForceReloading ?? false);
    if (isForceReload && (largeIcon || tabIcon)) {
      this.actor.sendAsyncMessage("Link:ExpireFavicons");
    }

    if (largeIcon) {
      largeIcon.isForceReload = isForceReload;
      largeIcon.isTabIcon = false;
      this.largeIconLoader.load(largeIcon).catch(console.error);
    }

    if (tabIcon) {
      tabIcon.isForceReload = isForceReload;
      tabIcon.isTabIcon = true;
      this.tabIconLoader.load(tabIcon).catch(console.error);
    }
  }

  addIconFromLink(aLink, aIsRichIcon) {
    let iconInfo = makeFaviconFromLink(aLink, aIsRichIcon);
    if (iconInfo) {
      iconInfo.beforePageShow = this.beforePageShow;
      this.iconInfos.push(iconInfo);
      this.iconTask.arm();
      return true;
    }
    return false;
  }

  addDefaultIcon(pageUri) {
    // Currently ImageDocuments will just load the default favicon, see bug
    // 403651 for discussion.
    this.iconInfos.push({
      iconUri: pageUri.mutate().setPathQueryRef("/favicon.ico").finalize(),
      width: -1,
      isRichIcon: false,
      type: TYPE_ICO,
      node: this.actor.document,
      beforePageShow: this.beforePageShow,
    });
    this.iconTask.arm();
  }

  onPageShow() {
    // We're likely done with icon parsing so load the pending icons now.
    if (this.iconTask.isArmed) {
      this.iconTask.disarm();
      this.loadIcons();
    }
    this.beforePageShow = false;
  }

  onPageHide() {
    this.largeIconLoader.cancel();
    this.tabIconLoader.cancel();

    this.iconTask.disarm();
    this.iconInfos = [];
  }
}

function makeFaviconFromLink(aLink, aIsRichIcon) {
  let iconUri = getLinkIconURI(aLink);
  if (!iconUri) {
    return null;
  }

  // Extract the size type and width.
  let width = extractIconSize(aLink.sizes);

  return {
    iconUri,
    width,
    isRichIcon: aIsRichIcon,
    type: aLink.type,
    node: aLink,
  };
}
