/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/*
 * ImageObjectProcessor
 * Implementation of Image Object processing algorithms from:
 * http://www.w3.org/TR/appmanifest/#image-object-and-its-members
 *
 * This is intended to be used in conjunction with ManifestProcessor.sys.mjs
 *
 * Creates an object to process Image Objects as defined by the
 * W3C specification. This is used to process things like the
 * icon member and the splash_screen member.
 *
 * Usage:
 *
 *   .process(aManifest, aBaseURL, aMemberName);
 *
 */

export function ImageObjectProcessor(aErrors, aExtractor, aGetL10n) {
  this.errors = aErrors;
  this.extractor = aExtractor;
  this.getL10n = aGetL10n;
}

const iconPurposes = Object.freeze(["any", "maskable", "monochrome"]);

function formatList(items) {
  return new Intl.ListFormat(Services.locale.appLocaleAsBCP47, {
    type: "unit",
  }).format(items);
}

// Static getters
Object.defineProperties(ImageObjectProcessor, {
  decimals: {
    get() {
      return /^\d+$/;
    },
  },
  anyRegEx: {
    get() {
      return new RegExp("any", "i");
    },
  },
});

ImageObjectProcessor.prototype.process = function (
  aManifest,
  aBaseURL,
  aMemberName
) {
  const spec = {
    objectName: "manifest",
    object: aManifest,
    property: aMemberName,
    expectedType: "array",
    trim: false,
  };
  const { getL10n, extractor, errors } = this;
  const images = [];
  const value = extractor.extractValue(spec);
  if (Array.isArray(value)) {
    value
      .map(toImageObject)
      // Filter out images that resulted in "failure", per spec.
      .filter(image => image)
      .forEach(image => images.push(image));
  }
  return images;

  function toImageObject(aImageSpec, index) {
    let img; // if "failure" happens below, we return undefined.
    try {
      // can throw
      const src = processSrcMember(aImageSpec, aBaseURL, index);
      // can throw
      const purpose = processPurposeMember(aImageSpec, index);
      const type = processTypeMember(aImageSpec);
      const sizes = processSizesMember(aImageSpec);
      img = {
        src,
        purpose,
        type,
        sizes,
      };
    } catch (err) {
      /* Errors are collected by each process* function */
    }
    return img;
  }

  function processPurposeMember(aImage, index) {
    const spec = {
      objectName: "image",
      object: aImage,
      property: "purpose",
      expectedType: "string",
      trim: true,
      throwTypeError: true,
    };

    // Type errors are treated at "any"...
    let value;
    try {
      value = extractor.extractValue(spec);
    } catch (err) {
      return ["any"];
    }

    // Was only whitespace...
    if (!value) {
      return ["any"];
    }

    const keywords = value.split(/\s+/);

    // Emtpy is treated as "any"...
    if (keywords.length === 0) {
      return ["any"];
    }

    // We iterate over keywords and classify them into:
    const purposes = new Set();
    const unknownPurposes = new Set();
    const repeatedPurposes = new Set();

    for (const keyword of keywords) {
      const canonicalKeyword = keyword.toLowerCase();

      if (purposes.has(canonicalKeyword)) {
        repeatedPurposes.add(keyword);
        continue;
      }

      iconPurposes.includes(canonicalKeyword)
        ? purposes.add(canonicalKeyword)
        : unknownPurposes.add(keyword);
    }

    // Tell developer about unknown purposes...
    if (unknownPurposes.size) {
      const warn = getL10n().formatValueSync(
        "dom-manifest-image-unsupported-purposes",
        {
          memberName: aMemberName,
          index,
          count: unknownPurposes.size,
          purposes: formatList(unknownPurposes),
        }
      );
      errors.push({ warn });
    }

    // Tell developer about repeated purposes...
    if (repeatedPurposes.size) {
      const warn = getL10n().formatValueSync(
        "dom-manifest-image-repeated-purposes",
        {
          memberName: aMemberName,
          index,
          purposes: formatList(repeatedPurposes),
        }
      );
      errors.push({ warn });
    }

    if (purposes.size === 0) {
      const warn = getL10n().formatValueSync("dom-manifest-image-unusable", {
        memberName: aMemberName,
        index,
      });
      errors.push({ warn });
      throw new TypeError(warn);
    }

    return [...purposes];
  }

  function processTypeMember(aImage) {
    const charset = {};
    const hadCharset = {};
    const spec = {
      objectName: "image",
      object: aImage,
      property: "type",
      expectedType: "string",
      trim: true,
    };
    let value = extractor.extractValue(spec);
    if (value) {
      value = Services.io.parseRequestContentType(value, charset, hadCharset);
    }
    return value || undefined;
  }

  function processSrcMember(aImage, aBaseURL, index) {
    const spec = {
      objectName: aMemberName,
      object: aImage,
      property: "src",
      expectedType: "string",
      trim: false,
      throwTypeError: true,
    };
    const value = extractor.extractValue(spec);
    let url;
    if (typeof value === "undefined" || value === "") {
      // We throw here as the value is unusable,
      // but it's not an developer error.
      throw new TypeError();
    }
    if (value && value.length) {
      try {
        url = new URL(value, aBaseURL).href;
      } catch (e) {
        const warn = getL10n().formatValueSync(
          "dom-manifest-image-url-is-invalid",
          {
            memberName: aMemberName,
            index,
            member: "src",
            value,
          }
        );
        errors.push({ warn });
        throw e;
      }
    }
    return url;
  }

  function processSizesMember(aImage) {
    const sizes = new Set();
    const spec = {
      objectName: "image",
      object: aImage,
      property: "sizes",
      expectedType: "string",
      trim: true,
    };
    const value = extractor.extractValue(spec);
    if (value) {
      // Split on whitespace and filter out invalid values.
      value
        .split(/\s+/)
        .filter(isValidSizeValue)
        .reduce((collector, size) => collector.add(size), sizes);
    }
    return sizes.size ? Array.from(sizes) : undefined;
    // Implementation of HTML's link@size attribute checker.
    function isValidSizeValue(aSize) {
      const size = aSize.toLowerCase();
      if (ImageObjectProcessor.anyRegEx.test(aSize)) {
        return true;
      }
      if (!size.includes("x") || size.indexOf("x") !== size.lastIndexOf("x")) {
        return false;
      }
      // Split left of x for width, after x for height.
      const widthAndHeight = size.split("x");
      const w = widthAndHeight.shift();
      const h = widthAndHeight.join("x");
      const validStarts = !w.startsWith("0") && !h.startsWith("0");
      const validDecimals = ImageObjectProcessor.decimals.test(w + h);
      return validStarts && validDecimals;
    }
  }
};
