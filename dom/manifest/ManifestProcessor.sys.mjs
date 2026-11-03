/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * ManifestProcessor
 * Implementation of processing algorithms from:
 * http://www.w3.org/2008/webapps/manifest/
 *
 * Creates manifest processor that lets you process a JSON file
 * or individual parts of a manifest object. A manifest is just a
 * standard JS object that has been cleaned up.
 *
 *   .process({jsonText,manifestURL,docURL});
 *
 * Depends on ImageObjectProcessor to process things like
 * icons and splash_screens.
 *
 * TODO: The constructor should accept the UA's supported orientations.
 * TODO: The constructor should accept the UA's supported display modes.
 */

const displayModes = new Set([
  "fullscreen",
  "standalone",
  "minimal-ui",
  "browser",
]);
const orientationTypes = new Set([
  "any",
  "natural",
  "landscape",
  "portrait",
  "portrait-primary",
  "portrait-secondary",
  "landscape-primary",
  "landscape-secondary",
]);
const textDirections = new Set(["ltr", "rtl", "auto"]);

// ValueExtractor is used by the various processors to get values
// from the manifest and to report errors.
import { ValueExtractor } from "moz-src:///dom/manifest/ValueExtractor.sys.mjs";

// ImageObjectProcessor is used to process things like icons and images
import { ImageObjectProcessor } from "moz-src:///dom/manifest/ImageObjectProcessor.sys.mjs";

const lazy = {};
ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () => new Localization(["dom/manifest.ftl"], true)
);

export var ManifestProcessor = {
  get defaultDisplayMode() {
    return "browser";
  },
  get displayModes() {
    return displayModes;
  },
  get orientationTypes() {
    return orientationTypes;
  },
  get textDirections() {
    return textDirections;
  },
  // process() method processes JSON text into a clean manifest
  // that conforms with the W3C specification. Takes an object
  // expecting the following dictionary items:
  //  * jsonText: the JSON string to be processed.
  //  * manifestURL: the URL of the manifest, to resolve URLs.
  //  * docURL: the URL of the owner doc, for security checks
  //  * checkConformance: boolean. If true, collects any conformance
  //    errors into a "moz_validation" property on the returned manifest.
  process(aOptions) {
    const {
      jsonText,
      manifestURL: aManifestURL,
      docURL: aDocURL,
      checkConformance,
    } = aOptions;

    // The errors get populated by the different process* functions.
    const errors = [];

    let rawManifest = {};
    try {
      rawManifest = JSON.parse(jsonText);
    } catch (e) {
      errors.push({ type: "json", error: e.message });
    }
    if (rawManifest === null) {
      return null;
    }
    if (typeof rawManifest !== "object") {
      const warn = lazy.l10n.formatValueSync("dom-manifest-should-be-object");
      errors.push({ warn });
      rawManifest = {};
    }
    const manifestURL = new URL(aManifestURL);
    const docURL = new URL(aDocURL);
    const getL10n = () => lazy.l10n;
    const extractor = new ValueExtractor(errors, getL10n);
    const imgObjProcessor = new ImageObjectProcessor(
      errors,
      extractor,
      getL10n
    );
    const processedManifest = {
      dir: processDirMember.call(this),
      lang: processLangMember(),
      start_url: processStartURLMember(),
      display: processDisplayMember.call(this),
      orientation: processOrientationMember.call(this),
      name: processNameMember(),
      icons: imgObjProcessor.process(rawManifest, manifestURL, "icons"),
      short_name: processShortNameMember(),
      theme_color: processThemeColorMember(),
      background_color: processBackgroundColorMember(),
    };
    processedManifest.scope = processScopeMember();
    processedManifest.id = processIdMember();
    if (checkConformance) {
      processedManifest.moz_validation = errors;
      processedManifest.moz_manifest_url = manifestURL.href;
    }
    return processedManifest;

    function processDirMember() {
      const spec = {
        objectName: "manifest",
        object: rawManifest,
        property: "dir",
        expectedType: "string",
        trim: true,
      };
      const value = extractor.extractValue(spec);
      if (
        value &&
        typeof value === "string" &&
        this.textDirections.has(value.toLowerCase())
      ) {
        return value.toLowerCase();
      }
      return "auto";
    }

    function processNameMember() {
      const spec = {
        objectName: "manifest",
        object: rawManifest,
        property: "name",
        expectedType: "string",
        trim: true,
      };
      return extractor.extractValue(spec);
    }

    function processShortNameMember() {
      const spec = {
        objectName: "manifest",
        object: rawManifest,
        property: "short_name",
        expectedType: "string",
        trim: true,
      };
      return extractor.extractValue(spec);
    }

    function processOrientationMember() {
      const spec = {
        objectName: "manifest",
        object: rawManifest,
        property: "orientation",
        expectedType: "string",
        trim: true,
      };
      const value = extractor.extractValue(spec);
      if (
        value &&
        typeof value === "string" &&
        this.orientationTypes.has(value.toLowerCase())
      ) {
        return value.toLowerCase();
      }
      return undefined;
    }

    function processDisplayMember() {
      const spec = {
        objectName: "manifest",
        object: rawManifest,
        property: "display",
        expectedType: "string",
        trim: true,
      };
      const value = extractor.extractValue(spec);
      if (
        value &&
        typeof value === "string" &&
        displayModes.has(value.toLowerCase())
      ) {
        return value.toLowerCase();
      }
      return this.defaultDisplayMode;
    }

    function processScopeMember() {
      const spec = {
        objectName: "manifest",
        object: rawManifest,
        property: "scope",
        expectedType: "string",
        trim: false,
      };
      const startURL = new URL(processedManifest.start_url);
      const defaultScope = new URL(".", startURL).href;
      const value = extractor.extractValue(spec);
      if (value === undefined || value === "") {
        return defaultScope;
      }
      let scopeURL = URL.parse(value, manifestURL);
      if (!scopeURL) {
        const warn = lazy.l10n.formatValueSync(
          "dom-manifest-scope-url-invalid"
        );
        errors.push({ warn });
        return defaultScope;
      }
      if (scopeURL.origin !== docURL.origin) {
        const warn = lazy.l10n.formatValueSync(
          "dom-manifest-scope-not-same-origin"
        );
        errors.push({ warn });
        return defaultScope;
      }
      // If start URL is not within scope of scope URL:
      if (
        startURL.origin !== scopeURL.origin ||
        startURL.pathname.startsWith(scopeURL.pathname) === false
      ) {
        const warn = lazy.l10n.formatValueSync(
          "dom-manifest-start-url-outside-scope"
        );
        errors.push({ warn });
        return defaultScope;
      }
      // Drop search params and fragment
      // https://github.com/w3c/manifest/pull/961
      scopeURL.hash = "";
      scopeURL.search = "";
      return scopeURL.href;
    }

    function processStartURLMember() {
      const spec = {
        objectName: "manifest",
        object: rawManifest,
        property: "start_url",
        expectedType: "string",
        trim: false,
      };
      const defaultStartURL = new URL(docURL).href;
      const value = extractor.extractValue(spec);
      if (value === undefined || value === "") {
        return defaultStartURL;
      }
      let potentialResult = URL.parse(value, manifestURL);
      if (!potentialResult) {
        const warn = lazy.l10n.formatValueSync(
          "dom-manifest-start-url-invalid"
        );
        errors.push({ warn });
        return defaultStartURL;
      }
      if (potentialResult.origin !== docURL.origin) {
        const warn = lazy.l10n.formatValueSync(
          "dom-manifest-start-url-should-be-same-origin"
        );
        errors.push({ warn });
        return defaultStartURL;
      }
      return potentialResult.href;
    }

    function processThemeColorMember() {
      const spec = {
        objectName: "manifest",
        object: rawManifest,
        property: "theme_color",
        expectedType: "string",
        trim: true,
      };
      return extractor.extractColorValue(spec);
    }

    function processBackgroundColorMember() {
      const spec = {
        objectName: "manifest",
        object: rawManifest,
        property: "background_color",
        expectedType: "string",
        trim: true,
      };
      return extractor.extractColorValue(spec);
    }

    function processLangMember() {
      const spec = {
        objectName: "manifest",
        object: rawManifest,
        property: "lang",
        expectedType: "string",
        trim: true,
      };
      return extractor.extractLanguageValue(spec);
    }

    function processIdMember() {
      // the start_url serves as the fallback, in case the id is not specified
      // or in error. A start_url is assured.
      const startURL = new URL(processedManifest.start_url);

      const spec = {
        objectName: "manifest",
        object: rawManifest,
        property: "id",
        expectedType: "string",
        trim: false,
      };
      const extractedValue = extractor.extractValue(spec);

      if (typeof extractedValue !== "string" || extractedValue === "") {
        return startURL.href;
      }

      let appId = URL.parse(extractedValue, startURL.origin);
      if (!appId) {
        const warn = lazy.l10n.formatValueSync("dom-manifest-id-is-invalid");
        errors.push({ warn });
        return startURL.href;
      }

      if (appId.origin !== startURL.origin) {
        const warn = lazy.l10n.formatValueSync(
          "dom-manifest-id-not-same-origin"
        );
        errors.push({ warn });
        return startURL.href;
      }

      return appId.href;
    }
  },
};
