/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createFactory,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

const FontAxis = createFactory(
  require("resource://devtools/client/inspector/fonts/components/FontAxis.js")
);
const FontName = createFactory(
  require("resource://devtools/client/inspector/fonts/components/FontName.js")
);
const FontSize = createFactory(
  require("resource://devtools/client/inspector/fonts/components/FontSize.js")
);
const FontStyle = createFactory(
  require("resource://devtools/client/inspector/fonts/components/FontStyle.js")
);
const FontWeight = createFactory(
  require("resource://devtools/client/inspector/fonts/components/FontWeight.js")
);
const LetterSpacing = createFactory(
  require("resource://devtools/client/inspector/fonts/components/LetterSpacing.js")
);
const LineHeight = createFactory(
  require("resource://devtools/client/inspector/fonts/components/LineHeight.js")
);

const {
  getStr,
} = require("resource://devtools/client/inspector/fonts/utils/l10n.js");
const Types = require("resource://devtools/client/inspector/fonts/types.js");

// Maximum number of font families to be shown by default. Any others will be hidden
// under a collapsed <details> element with a toggle to reveal them.
const MAX_FONTS = 3;

class FontEditor extends PureComponent {
  static get propTypes() {
    return {
      fontEditor: PropTypes.shape(Types.fontEditor).isRequired,
      onInstanceChange: PropTypes.func.isRequired,
      onPropertyChange: PropTypes.func.isRequired,
      onToggleFontHighlight: PropTypes.func.isRequired,
    };
  }

  /**
   * Get an array of FontAxis components with editing controls for of the given variable
   * font axes. If no axes were given, return null.
   * If an axis' value was declared on the font-variation-settings CSS property or was
   * changed using the font editor, use that value, otherwise use the axis default.
   *
   * @param  {Array} fontAxes
   *         Array of font axis instances
   * @param  {object} editedAxes
   *         Object with axes and values edited by the user or defined in the CSS
   *         declaration for font-variation-settings.
   * @return {Array|null}
   */
  renderAxes(fontAxes = [], editedAxes) {
    if (!fontAxes.length) {
      return null;
    }

    return fontAxes.map(axis => {
      return FontAxis({
        key: axis.tag,
        axis,
        disabled: this.props.fontEditor.disabled,
        onChange: this.props.onPropertyChange,
        minLabel: true,
        maxLabel: true,
        value: editedAxes[axis.tag] || axis.defaultValue,
      });
    });
  }

  /**
   * Render fonts used on the selected node grouped by font-family.
   *
   * @param {Array} fonts
   *        Fonts used on selected node.
   * @return {DOMNode}
   */
  renderUsedFonts(fonts) {
    if (!fonts.length) {
      return null;
    }

    // Group fonts by family name.
    const fontGroups = fonts.reduce((acc, font) => {
      const family = font.CSSFamilyName.toString();
      acc[family] = acc[family] || [];
      acc[family].push(font);
      return acc;
    }, {});

    const renderedFontGroups = Object.keys(fontGroups).map(family => {
      return this.renderFontGroup(family, fontGroups[family]);
    });

    const topFontsList = renderedFontGroups.slice(0, MAX_FONTS);
    const moreFontsList = renderedFontGroups.slice(
      MAX_FONTS,
      renderedFontGroups.length
    );

    const moreFonts = !moreFontsList.length
      ? null
      : dom.details(
          {},
          dom.summary(
            {},
            dom.span(
              { className: "label-open" },
              getStr("font-inspector-show-more")
            ),
            dom.span(
              { className: "label-close" },
              getStr("font-inspector-show-less")
            )
          ),
          moreFontsList
        );

    return dom.label(
      {
        className: "font-control font-control-used-fonts",
      },
      dom.span(
        {
          className: "font-control-label",
        },
        getStr("font-inspector-fonts-used-label")
      ),
      dom.div(
        {
          className: "font-control-box",
        },
        topFontsList,
        moreFonts
      )
    );
  }

  renderFontGroup(family, fonts = []) {
    const group = fonts.map((font, i) => {
      return FontName({
        key: font.name + ":" + i,
        font,
        onToggleFontHighlight: this.props.onToggleFontHighlight,
      });
    });

    return dom.div(
      {
        key: family,
        className: "font-group",
      },
      dom.div(
        {
          className: "font-family-name",
        },
        family
      ),
      group
    );
  }

  renderFontSize(value) {
    return (
      value !== null &&
      FontSize({
        key: `${this.props.fontEditor.id}:font-size`,
        disabled: this.props.fontEditor.disabled,
        onChange: this.props.onPropertyChange,
        value,
      })
    );
  }

  renderLineHeight(value) {
    return (
      value !== null &&
      LineHeight({
        key: `${this.props.fontEditor.id}:line-height`,
        disabled: this.props.fontEditor.disabled,
        onChange: this.props.onPropertyChange,
        value,
      })
    );
  }

  renderLetterSpacing(value) {
    return (
      value !== null &&
      LetterSpacing({
        key: `${this.props.fontEditor.id}:letter-spacing`,
        disabled: this.props.fontEditor.disabled,
        onChange: this.props.onPropertyChange,
        value,
      })
    );
  }

  renderFontStyle(value) {
    return (
      value &&
      FontStyle({
        onChange: this.props.onPropertyChange,
        disabled: this.props.fontEditor.disabled,
        value,
      })
    );
  }

  renderFontWeight(value) {
    return (
      value !== null &&
      FontWeight({
        onChange: this.props.onPropertyChange,
        disabled: this.props.fontEditor.disabled,
        value,
      })
    );
  }

  /**
   * Get a dropdown which allows selecting between variation instances defined by a font.
   *
   * @param {Array} fontInstances
   *        Named variation instances as provided with the font file.
   * @param {object} selectedInstance
   *        Object with information about the currently selected variation instance.
   *        Example:
   *        {
   *          name: "Custom",
   *          values: []
   *        }
   * @return {DOMNode}
   */
  renderInstances(fontInstances = [], selectedInstance = {}) {
    // Append a "Custom" instance entry which represents the latest manual axes changes.
    const customInstance = {
      name: getStr("font-inspector-custom-instance-name"),
      values: this.props.fontEditor.customInstanceValues,
    };
    fontInstances = [...fontInstances, customInstance];

    // Generate the <option> elements for the dropdown.
    const instanceOptions = fontInstances.map(instance =>
      dom.option(
        {
          key: instance.name,
          value: instance.name,
        },
        instance.name
      )
    );

    // Generate the dropdown.
    const instanceSelect = dom.select(
      {
        className: "font-control-input font-value-select",
        value: selectedInstance.name || customInstance.name,
        onChange: e => {
          const instance = fontInstances.find(
            inst => e.target.value === inst.name
          );
          instance &&
            this.props.onInstanceChange(instance.name, instance.values);
        },
      },
      instanceOptions
    );

    return dom.label(
      {
        className: "font-control",
      },
      dom.span(
        {
          className: "font-control-label",
        },
        getStr("font-inspector-font-instance-label")
      ),
      instanceSelect
    );
  }

  renderWarning(warning) {
    return dom.div(
      {
        id: "font-editor",
      },
      dom.div(
        {
          className: "devtools-sidepanel-no-result",
        },
        warning
      )
    );
  }

  render() {
    const { fontEditor } = this.props;
    const { fonts, axes, instance, properties, warning } = fontEditor;
    // Pick the first font to show editor controls regardless of how many fonts are used.
    const font = fonts[0];
    const hasFontAxes = font?.variationAxes;
    const hasFontInstances = font?.variationInstances?.length > 0;
    const hasSlantOrItalicAxis = font?.variationAxes?.find(axis => {
      return axis.tag === "slnt" || axis.tag === "ital";
    });
    const hasWeightAxis = font?.variationAxes?.find(axis => {
      return axis.tag === "wght";
    });

    // Show the empty state with a warning message when a used font was not found.
    if (!font) {
      return this.renderWarning(warning);
    }

    return dom.div(
      {
        id: "font-editor",
      },
      // Always render UI for used fonts.
      this.renderUsedFonts(fonts),
      // Render UI for font variation instances if they are defined.
      hasFontInstances &&
        this.renderInstances(font.variationInstances, instance),
      // Always render UI for font size.
      this.renderFontSize(properties["font-size"]),
      // Always render UI for line height.
      this.renderLineHeight(properties["line-height"]),
      // Always render UI for letter spacing.
      this.renderLetterSpacing(properties["letter-spacing"]),
      // Render UI for font weight if no "wght" registered axis is defined.
      !hasWeightAxis && this.renderFontWeight(properties["font-weight"]),
      // Render UI for font style if no "slnt" or "ital" registered axis is defined.
      !hasSlantOrItalicAxis && this.renderFontStyle(properties["font-style"]),
      // Render UI for each variable font axis if any are defined.
      hasFontAxes && this.renderAxes(font.variationAxes, axes)
    );
  }
}

module.exports = FontEditor;
