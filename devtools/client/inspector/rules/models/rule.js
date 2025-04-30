/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  style: { ELEMENT_STYLE },
} = require("resource://devtools/shared/constants.js");
const CssLogic = require("resource://devtools/shared/inspector/css-logic.js");
const TextProperty = require("resource://devtools/client/inspector/rules/models/text-property.js");

loader.lazyRequireGetter(
  this,
  "promiseWarn",
  "resource://devtools/client/inspector/shared/utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "parseNamedDeclarations",
  "resource://devtools/shared/css/parsing-utils.js",
  true
);

const STYLE_INSPECTOR_PROPERTIES =
  "devtools/shared/locales/styleinspector.properties";
const { LocalizationHelper } = require("resource://devtools/shared/l10n.js");
const STYLE_INSPECTOR_L10N = new LocalizationHelper(STYLE_INSPECTOR_PROPERTIES);

/**
 * Rule is responsible for the following:
 *   Manages a single style declaration or rule.
 *   Applies changes to the properties in a rule.
 *   Maintains a list of TextProperty objects.
 */
class Rule {
  /**
   * @param {ElementStyle} elementStyle
   *        The ElementStyle to which this rule belongs.
   * @param {Object} options
   *        The information used to construct this rule. Properties include:
   *          rule: A StyleRuleActor
   *          inherited: An element this rule was inherited from. If omitted,
   *            the rule applies directly to the current element.
   *          isSystem: Is this a user agent style?
   *          isUnmatched: True if the rule does not match the current selected
   *            element, otherwise, false.
   */
  constructor(elementStyle, options) {
    this.elementStyle = elementStyle;
    this.domRule = options.rule;
    this.compatibilityIssues = null;

    this.matchedSelectorIndexes = options.matchedSelectorIndexes || [];
    this.isSystem = options.isSystem;
    this.isUnmatched = options.isUnmatched || false;
    this.darkColorScheme = options.darkColorScheme;
    this.inherited = options.inherited || null;
    this.pseudoElement = options.pseudoElement || "";
    this.keyframes = options.keyframes || null;
    this.userAdded = options.rule.userAdded;

    this.cssProperties = this.elementStyle.ruleView.cssProperties;
    this.inspector = this.elementStyle.ruleView.inspector;
    this.store = this.elementStyle.ruleView.store;

    // Populate the text properties with the style's current authoredText
    // value, and add in any disabled properties from the store.
    this.textProps = this._getTextProperties();
    this.textProps = this.textProps.concat(this._getDisabledProperties());

    this.getUniqueSelector = this.getUniqueSelector.bind(this);
    this.onStyleRuleFrontUpdated = this.onStyleRuleFrontUpdated.bind(this);

    this.domRule.on("rule-updated", this.onStyleRuleFrontUpdated);
  }

  destroy() {
    if (this._unsubscribeSourceMap) {
      this._unsubscribeSourceMap();
    }

    this.domRule.off("rule-updated", this.onStyleRuleFrontUpdated);
    this.compatibilityIssues = null;
    this.destroyed = true;
  }

  get declarations() {
    return this.textProps;
  }

  get selector() {
    return {
      getUniqueSelector: this.getUniqueSelector,
      matchedSelectorIndexes: this.matchedSelectorIndexes,
      selectors: this.domRule.selectors,
      selectorsSpecificity: this.domRule.selectorsSpecificity,
      selectorWarnings: this.domRule.selectors,
      selectorText: this.keyframes ? this.domRule.keyText : this.selectorText,
    };
  }

  get sourceMapURLService() {
    return this.inspector.toolbox.sourceMapURLService;
  }

  get title() {
    let title = CssLogic.shortSource(this.sheet);
    if (this.domRule.type !== ELEMENT_STYLE && this.ruleLine > 0) {
      title += ":" + this.ruleLine;
    }

    return title;
  }

  get inheritedSectionLabel() {
    if (this._inheritedSectionLabel) {
      return this._inheritedSectionLabel;
    }
    this._inheritedSectionLabel = "";
    if (this.inherited) {
      let eltText = this.inherited.displayName;
      if (this.inherited.id) {
        eltText += "#" + this.inherited.id;
      }
      if (CssLogic.ELEMENT_BACKED_PSEUDO_ELEMENTS.has(this.pseudoElement)) {
        eltText += this.pseudoElement;
      }
      this._inheritedSectionLabel = STYLE_INSPECTOR_L10N.getFormatStr(
        "rule.inheritedFrom",
        eltText
      );
    }
    return this._inheritedSectionLabel;
  }

  get keyframesName() {
    if (this._keyframesName) {
      return this._keyframesName;
    }
    this._keyframesName = "";
    if (this.keyframes) {
      this._keyframesName = STYLE_INSPECTOR_L10N.getFormatStr(
        "rule.keyframe",
        this.keyframes.name
      );
    }
    return this._keyframesName;
  }

  get keyframesRule() {
    if (!this.keyframes) {
      return null;
    }

    return {
      id: this.keyframes.actorID,
      keyframesName: this.keyframesName,
    };
  }

  get selectorText() {
    return this.domRule.selectors
      ? this.domRule.selectors.join(", ")
      : CssLogic.l10n("rule.sourceElement");
  }

  /**
   * The rule's stylesheet.
   */
  get sheet() {
    return this.domRule ? this.domRule.parentStyleSheet : null;
  }

  /**
   * The rule's line within a stylesheet
   */
  get ruleLine() {
    return this.domRule ? this.domRule.line : -1;
  }

  /**
   * The rule's column within a stylesheet
   */
  get ruleColumn() {
    return this.domRule ? this.domRule.column : null;
  }

  /**
   * Get the declaration block issues from the compatibility actor
   * @returns A promise that resolves with an array of objects in following form:
   *    {
   *      // Type of compatibility issue
   *      type: <string>,
   *      // The CSS declaration that has compatibility issues
   *      property: <string>,
   *      // Alias to the given CSS property
   *      alias: <Array>,
   *      // Link to MDN documentation for the particular CSS rule
   *      url: <string>,
   *      deprecated: <boolean>,
   *      experimental: <boolean>,
   *      // An array of all the browsers that don't support the given CSS rule
   *      unsupportedBrowsers: <Array>,
   *    }
   */
  async getCompatibilityIssues() {
    if (!this.compatibilityIssues) {
      this.compatibilityIssues =
        this.inspector.commands.inspectorCommand.getCSSDeclarationBlockIssues(
          this.domRule.declarations
        );
    }

    return this.compatibilityIssues;
  }

  /**
   * Returns the TextProperty with the given id or undefined if it cannot be found.
   *
   * @param {String|null} id
   *        A TextProperty id.
   * @return {TextProperty|undefined} with the given id in the current Rule or undefined
   * if it cannot be found.
   */
  getDeclaration(id) {
    return id ? this.textProps.find(textProp => textProp.id === id) : undefined;
  }

  /**
   * Returns an unique selector for the CSS rule.
   */
  async getUniqueSelector() {
    let selector = "";

    if (this.domRule.selectors) {
      // This is a style rule with a selector.
      selector = this.domRule.selectors.join(", ");
    } else if (this.inherited) {
      // This is an inline style from an inherited rule. Need to resolve the unique
      // selector from the node which rule this is inherited from.
      selector = await this.inherited.getUniqueSelector();
    } else {
      // This is an inline style from the current node.
      selector = await this.inspector.selection.nodeFront.getUniqueSelector();
    }

    return selector;
  }

  /**
   * Returns true if the rule matches the creation options
   * specified.
   *
   * @param {Object} options
   *        Creation options. See the Rule constructor for documentation.
   */
  matches(options) {
    return this.domRule === options.rule;
  }

  /**
   * Create a new TextProperty to include in the rule.
   *
   * @param {String} name
   *        The text property name (such as "background" or "border-top").
   * @param {String} value
   *        The property's value (not including priority).
   * @param {String} priority
   *        The property's priority (either "important" or an empty string).
   * @param {Boolean} enabled
   *        True if the property should be enabled.
   * @param {TextProperty} siblingProp
   *        Optional, property next to which the new property will be added.
   */
  createProperty(name, value, priority, enabled, siblingProp) {
    const prop = new TextProperty(this, name, value, priority, enabled);

    let ind;
    if (siblingProp) {
      ind = this.textProps.indexOf(siblingProp) + 1;
      this.textProps.splice(ind, 0, prop);
    } else {
      ind = this.textProps.length;
      this.textProps.push(prop);
    }

    this.applyProperties(modifications => {
      modifications.createProperty(ind, name, value, priority, enabled);
      // Now that the rule has been updated, the server might have given us data
      // that changes the state of the property. Update it now.
      prop.updateEditor();
    });

    return prop;
  }

  /**
   * Helper function for applyProperties that is called when the actor
   * does not support as-authored styles.  Store disabled properties
   * in the element style's store.
   */
  _applyPropertiesNoAuthored(modifications) {
    this.elementStyle.onRuleUpdated();

    const disabledProps = [];

    for (const prop of this.textProps) {
      if (prop.invisible) {
        continue;
      }
      if (!prop.enabled) {
        disabledProps.push({
          name: prop.name,
          value: prop.value,
          priority: prop.priority,
        });
        continue;
      }
      if (prop.value.trim() === "") {
        continue;
      }

      modifications.setProperty(-1, prop.name, prop.value, prop.priority);

      prop.updateComputed();
    }

    // Store disabled properties in the disabled store.
    const disabled = this.elementStyle.store.disabled;
    if (disabledProps.length) {
      disabled.set(this.domRule, disabledProps);
    } else {
      disabled.delete(this.domRule);
    }

    return modifications.apply().then(() => {
      const cssProps = {};
      // Note that even though StyleRuleActors normally provide parsed
      // declarations already, _applyPropertiesNoAuthored is only used when
      // connected to older backend that do not provide them. So parse here.
      for (const cssProp of parseNamedDeclarations(
        this.cssProperties.isKnown,
        this.domRule.authoredText
      )) {
        cssProps[cssProp.name] = cssProp;
      }

      for (const textProp of this.textProps) {
        if (!textProp.enabled) {
          continue;
        }
        let cssProp = cssProps[textProp.name];

        if (!cssProp) {
          cssProp = {
            name: textProp.name,
            value: "",
            priority: "",
          };
        }

        textProp.priority = cssProp.priority;
      }
    });
  }

  /**
   * A helper for applyProperties that applies properties in the "as
   * authored" case; that is, when the StyleRuleActor supports
   * setRuleText.
   */
  _applyPropertiesAuthored(modifications) {
    return modifications.apply().then(() => {
      // The rewriting may have required some other property values to
      // change, e.g., to insert some needed terminators.  Update the
      // relevant properties here.
      for (const index in modifications.changedDeclarations) {
        const newValue = modifications.changedDeclarations[index];
        this.textProps[index].updateValue(newValue);
      }
      // Recompute and redisplay the computed properties.
      for (const prop of this.textProps) {
        if (!prop.invisible && prop.enabled) {
          prop.updateComputed();
          prop.updateEditor();
        }
      }
    });
  }

  /**
   * Reapply all the properties in this rule, and update their
   * computed styles.  Will re-mark overridden properties.  Sets the
   * |_applyingModifications| property to a promise which will resolve
   * when the edit has completed.
   *
   * @param {Function} modifier a function that takes a RuleModificationList
   *        (or RuleRewriter) as an argument and that modifies it
   *        to apply the desired edit
   * @return {Promise} a promise which will resolve when the edit
   *        is complete
   */
  applyProperties(modifier) {
    // If there is already a pending modification, we have to wait
    // until it settles before applying the next modification.
    const resultPromise = Promise.resolve(this._applyingModifications)
      .then(() => {
        const modifications = this.domRule.startModifyingProperties(
          this.inspector.panelWin,
          this.cssProperties
        );
        modifier(modifications);
        if (this.domRule.canSetRuleText) {
          return this._applyPropertiesAuthored(modifications);
        }
        return this._applyPropertiesNoAuthored(modifications);
      })
      .then(() => {
        this.elementStyle.onRuleUpdated();

        if (resultPromise === this._applyingModifications) {
          this._applyingModifications = null;
          this.elementStyle._changed();
        }
      })
      .catch(promiseWarn);

    this._applyingModifications = resultPromise;
    return resultPromise;
  }

  /**
   * Renames a property.
   *
   * @param {TextProperty} property
   *        The property to rename.
   * @param {String} name
   *        The new property name (such as "background" or "border-top").
   * @return {Promise}
   */
  setPropertyName(property, name) {
    if (name === property.name) {
      return Promise.resolve();
    }

    const oldName = property.name;
    property.name = name;
    const index = this.textProps.indexOf(property);
    return this.applyProperties(modifications => {
      modifications.renameProperty(index, oldName, name);
    });
  }

  /**
   * Sets the value and priority of a property, then reapply all properties.
   *
   * @param {TextProperty} property
   *        The property to manipulate.
   * @param {String} value
   *        The property's value (not including priority).
   * @param {String} priority
   *        The property's priority (either "important" or an empty string).
   * @return {Promise}
   */
  setPropertyValue(property, value, priority) {
    if (value === property.value && priority === property.priority) {
      return Promise.resolve();
    }

    property.value = value;
    property.priority = priority;

    const index = this.textProps.indexOf(property);
    return this.applyProperties(modifications => {
      modifications.setProperty(index, property.name, value, priority);
    });
  }

  /**
   * Just sets the value and priority of a property, in order to preview its
   * effect on the content document.
   *
   * @param {TextProperty} property
   *        The property which value will be previewed
   * @param {String} value
   *        The value to be used for the preview
   * @param {String} priority
   *        The property's priority (either "important" or an empty string).
   **@return {Promise}
   */
  previewPropertyValue(property, value, priority) {
    this.elementStyle.ruleView.emitForTests("start-preview-property-value");
    const modifications = this.domRule.startModifyingProperties(
      this.inspector.panelWin,
      this.cssProperties
    );
    modifications.setProperty(
      this.textProps.indexOf(property),
      property.name,
      value,
      priority
    );
    return modifications.apply().then(() => {
      // Ensure dispatching a ruleview-changed event
      // also for previews
      this.elementStyle._changed();
    });
  }

  /**
   * Disables or enables given TextProperty.
   *
   * @param {TextProperty} property
   *        The property to enable/disable
   * @param {Boolean} value
   */
  setPropertyEnabled(property, value) {
    if (property.enabled === !!value) {
      return;
    }
    property.enabled = !!value;
    const index = this.textProps.indexOf(property);
    this.applyProperties(modifications => {
      modifications.setPropertyEnabled(index, property.name, property.enabled);
    });
  }

  /**
   * Remove a given TextProperty from the rule and update the rule
   * accordingly.
   *
   * @param {TextProperty} property
   *        The property to be removed
   */
  removeProperty(property) {
    const index = this.textProps.indexOf(property);
    this.textProps.splice(index, 1);
    // Need to re-apply properties in case removing this TextProperty
    // exposes another one.
    this.applyProperties(modifications => {
      modifications.removeProperty(index, property.name);
    });
  }

  /**
   * Event handler for "rule-updated" event fired by StyleRuleActor.
   *
   * @param {StyleRuleFront} front
   */
  onStyleRuleFrontUpdated(front) {
    // Overwritting this reference is not required, but it's here to avoid confusion.
    // Whenever an actor is passed over the protocol, either as a return value or as
    // payload on an event, the `form` of its corresponding front will be automatically
    // updated. No action required.
    // Even if this `domRule` reference here is not explicitly updated, lookups of
    // `this.domRule.declarations` will point to the latest state of declarations set
    // on the actor. Everything on `StyleRuleForm.form` will point to the latest state.
    this.domRule = front;
  }

  /**
   * Get the list of TextProperties from the style. Needs
   * to parse the style's authoredText.
   */
  _getTextProperties() {
    const textProps = [];
    const store = this.elementStyle.store;

    for (const prop of this.domRule.declarations) {
      const name = prop.name;
      // In an inherited rule, we only show inherited properties.
      // However, we must keep all properties in order for rule
      // rewriting to work properly.  So, compute the "invisible"
      // property here.
      const inherits = prop.isCustomProperty
        ? prop.inherits
        : this.cssProperties.isInherited(name);
      const invisible = this.inherited && !inherits;

      const value = store.userProperties.getProperty(
        this.domRule,
        name,
        prop.value
      );
      const textProp = new TextProperty(
        this,
        name,
        value,
        prop.priority,
        !("commentOffsets" in prop),
        invisible
      );
      textProps.push(textProp);
    }

    return textProps;
  }

  /**
   * Return the list of disabled properties from the store for this rule.
   */
  _getDisabledProperties() {
    const store = this.elementStyle.store;

    // Include properties from the disabled property store, if any.
    const disabledProps = store.disabled.get(this.domRule);
    if (!disabledProps) {
      return [];
    }

    const textProps = [];

    for (const prop of disabledProps) {
      const value = store.userProperties.getProperty(
        this.domRule,
        prop.name,
        prop.value
      );
      const textProp = new TextProperty(this, prop.name, value, prop.priority);
      textProp.enabled = false;
      textProps.push(textProp);
    }

    return textProps;
  }

  /**
   * Reread the current state of the rules and rebuild text
   * properties as needed.
   */
  refresh(options) {
    this.matchedSelectorIndexes = options.matchedSelectorIndexes || [];
    const colorSchemeChanged = this.darkColorScheme !== options.darkColorScheme;
    this.darkColorScheme = options.darkColorScheme;

    const newTextProps = this._getTextProperties();

    // The element style rule behaves differently on refresh. We basically need to update
    // it to reflect the new text properties exactly. The order might have changed, some
    // properties might have been removed, etc. And we don't need to mark anything as
    // disabled here. The element style rule should always reflect the content of the
    // style attribute.
    if (this.domRule.type === ELEMENT_STYLE) {
      this.textProps = newTextProps;

      if (this.editor) {
        this.editor.populate(true);
      }

      return;
    }

    // Update current properties for each property present on the style.
    // This will mark any touched properties with _visited so we
    // can detect properties that weren't touched (because they were
    // removed from the style).
    // Also keep track of properties that didn't exist in the current set
    // of properties.
    const brandNewProps = [];
    for (const newProp of newTextProps) {
      if (!this._updateTextProperty(newProp)) {
        brandNewProps.push(newProp);
      }
    }

    // Refresh editors and disabled state for all the properties that
    // were updated.
    for (const prop of this.textProps) {
      // Properties that weren't touched during the update
      // process must no longer exist on the node.  Mark them disabled.
      if (!prop._visited) {
        prop.enabled = false;
        prop.updateEditor();
      } else {
        delete prop._visited;
      }

      // Valid properties that aren't disabled might need to get updated in some condition
      if (
        prop.enabled &&
        prop.isValid() &&
        // Update if it's using light-dark and the color scheme changed
        colorSchemeChanged &&
        prop.value.includes("light-dark")
      ) {
        prop.updateEditor();
      }
    }

    // Add brand new properties.
    this.textProps = this.textProps.concat(brandNewProps);

    // Refresh the editor if one already exists.
    if (this.editor) {
      this.editor.populate();
    }
  }

  /**
   * Update the current TextProperties that match a given property
   * from the authoredText.  Will choose one existing TextProperty to update
   * with the new property's value, and will disable all others.
   *
   * When choosing the best match to reuse, properties will be chosen
   * by assigning a rank and choosing the highest-ranked property:
   *   Name, value, and priority match, enabled. (6)
   *   Name, value, and priority match, disabled. (5)
   *   Name and value match, enabled. (4)
   *   Name and value match, disabled. (3)
   *   Name matches, enabled. (2)
   *   Name matches, disabled. (1)
   *
   * If no existing properties match the property, nothing happens.
   *
   * @param {TextProperty} newProp
   *        The current version of the property, as parsed from the
   *        authoredText in Rule._getTextProperties().
   * @return {Boolean} true if a property was updated, false if no properties
   *         were updated.
   */
  _updateTextProperty(newProp) {
    const match = { rank: 0, prop: null };

    for (const prop of this.textProps) {
      if (prop.name !== newProp.name) {
        continue;
      }

      // Mark this property visited.
      prop._visited = true;

      // Start at rank 1 for matching name.
      let rank = 1;

      // Value and Priority matches add 2 to the rank.
      // Being enabled adds 1.  This ranks better matches higher,
      // with priority breaking ties.
      if (prop.value === newProp.value) {
        rank += 2;
        if (prop.priority === newProp.priority) {
          rank += 2;
        }
      }

      if (prop.enabled) {
        rank += 1;
      }

      if (rank > match.rank) {
        if (match.prop) {
          // We outrank a previous match, disable it.
          match.prop.enabled = false;
          match.prop.updateEditor();
        }
        match.rank = rank;
        match.prop = prop;
      } else if (rank) {
        // A previous match outranks us, disable ourself.
        prop.enabled = false;
        prop.updateEditor();
      }
    }

    // If we found a match, update its value with the new text property
    // value.
    if (match.prop) {
      match.prop.set(newProp);
      return true;
    }

    return false;
  }

  /**
   * Jump between editable properties in the UI. If the focus direction is
   * forward, begin editing the next property name if available or focus the
   * new property editor otherwise. If the focus direction is backward,
   * begin editing the previous property value or focus the selector editor if
   * this is the first element in the property list.
   *
   * @param {TextProperty} textProperty
   *        The text property that will be left to focus on a sibling.
   * @param {Number} direction
   *        The move focus direction number.
   */
  editClosestTextProperty(textProperty, direction) {
    let index = this.textProps.indexOf(textProperty);

    if (direction === Services.focus.MOVEFOCUS_FORWARD) {
      for (++index; index < this.textProps.length; ++index) {
        if (!this.textProps[index].invisible) {
          break;
        }
      }
      if (index === this.textProps.length) {
        textProperty.rule.editor.closeBrace.click();
      } else {
        this.textProps[index].editor.nameSpan.click();
      }
    } else if (direction === Services.focus.MOVEFOCUS_BACKWARD) {
      for (--index; index >= 0; --index) {
        if (!this.textProps[index].invisible) {
          break;
        }
      }
      if (index < 0) {
        textProperty.editor.ruleEditor.selectorText.click();
      } else {
        this.textProps[index].editor.valueSpan.click();
      }
    }
  }

  /**
   * Return a string representation of the rule.
   */
  stringifyRule() {
    const selectorText = this.selectorText;
    let cssText = "";
    const terminator = Services.appinfo.OS === "WINNT" ? "\r\n" : "\n";

    for (const textProp of this.textProps) {
      if (!textProp.invisible) {
        cssText += "\t" + textProp.stringifyProperty() + terminator;
      }
    }

    return selectorText + " {" + terminator + cssText + "}";
  }

  /**
   * @returns {Boolean} Whether or not the rule is in a layer
   */
  isInLayer() {
    return this.domRule.ancestorData.some(({ type }) => type === "layer");
  }

  /**
   * Return whether this rule and the one passed are in the same layer,
   * (as in described in the spec; this is not checking that the 2 rules are children
   * of the same CSSLayerBlockRule)
   *
   * @param {Rule} otherRule: The rule we want to compare with
   * @returns {Boolean}
   */
  isInDifferentLayer(otherRule) {
    const filterLayer = ({ type }) => type === "layer";
    const thisLayers = this.domRule.ancestorData.filter(filterLayer);
    const otherRuleLayers = otherRule.domRule.ancestorData.filter(filterLayer);

    if (thisLayers.length !== otherRuleLayers.length) {
      return true;
    }

    return thisLayers.some((layer, i) => {
      const otherRuleLayer = otherRuleLayers[i];
      // For named layers, we can compare the layer name directly, since we want to identify
      // the actual layer, not the specific CSSLayerBlockRule.
      // For nameless layers though, we don't have a choice and we can only identify them
      // via their CSSLayerBlockRule, so we're using the rule actorID.
      return (
        (layer.value || layer.actorID) !==
        (otherRuleLayer.value || otherRuleLayer.actorID)
      );
    });
  }

  /**
   * @returns {Boolean} Whether or not the rule is in a @starting-style rule
   */
  isInStartingStyle() {
    return this.domRule.ancestorData.some(
      ({ type }) => type === "starting-style"
    );
  }

  /**
   * See whether this rule has any non-invisible properties.
   * @return {Boolean} true if there is any visible property, or false
   *         if all properties are invisible
   */
  hasAnyVisibleProperties() {
    for (const prop of this.textProps) {
      if (!prop.invisible) {
        return true;
      }
    }
    return false;
  }
}

module.exports = Rule;
