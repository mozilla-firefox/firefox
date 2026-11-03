/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/*
 * Helper functions extract values from manifest members
 * and reports conformance errors.
 */

export class ValueExtractor {
  /**
   * @param {Array} errors
   *        Collects the warnings that are reported.
   * @param {Function} getL10n
   *        Returns the Localization used to format warnings.
   */
  constructor(errors, getL10n) {
    this.errors = errors;
    this.getL10n = getL10n;
  }

  /**
   * @param options
   *        The 'spec' object.
   * Note: This function takes a 'spec' object and destructures it to extract
   *        a value. If the value is of the wrong type, it warns the developer
   *        and returns undefined.
   *        expectedType: is the type of a JS primitive (string, number, etc.)
   *        object: is the object from which to extract the value.
   *        objectName: string used to construct the developer warning.
   *        property: the name of the property being extracted.
   *        throwTypeError: boolean, throw a TypeError if the type is incorrect.
   *        trim: boolean, if the value should be trimmed (used by string type).
   */
  extractValue(options) {
    const { expectedType, object, objectName, property, throwTypeError, trim } =
      options;
    const value = object[property];
    const isArray = Array.isArray(value);

    // We need to special-case "array", as it's not a JS primitive.
    const type = isArray ? "array" : typeof value;
    if (type !== expectedType) {
      if (type !== "undefined") {
        const warn = this.getL10n().formatValueSync(
          "dom-manifest-invalid-type",
          {
            objectName,
            property,
            expectedType,
          }
        );
        this.errors.push({ warn });
        if (throwTypeError) {
          throw new TypeError(warn);
        }
      }
      return undefined;
    }

    // Trim string and returned undefined if the empty string.
    const shouldTrim = expectedType === "string" && value && trim;
    if (shouldTrim) {
      return value.trim() || undefined;
    }
    return value;
  }

  extractColorValue(spec) {
    const value = this.extractValue(spec);
    let color;
    if (InspectorUtils.isValidCSSColor(value)) {
      const rgba = InspectorUtils.colorToRGBA(value);
      color =
        "#" +
        rgba.r.toString(16).padStart(2, "0") +
        rgba.g.toString(16).padStart(2, "0") +
        rgba.b.toString(16).padStart(2, "0") +
        Math.round(rgba.a * 255)
          .toString(16)
          .padStart(2, "0");
    } else if (value) {
      const warn = this.getL10n().formatValueSync(
        "dom-manifest-invalid-css-color",
        {
          property: spec.property,
          value,
        }
      );
      this.errors.push({ warn });
    }
    return color;
  }

  extractLanguageValue(spec) {
    let langTag;
    const value = this.extractValue(spec);
    if (value !== undefined) {
      try {
        langTag = Intl.getCanonicalLocales(value)[0];
      } catch (err) {
        const warn = this.getL10n().formatValueSync(
          "dom-manifest-lang-is-invalid",
          {
            property: spec.property,
            value,
          }
        );
        this.errors.push({ warn });
      }
    }
    return langTag;
  }
}
