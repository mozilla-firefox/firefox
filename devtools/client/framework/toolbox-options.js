/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EventEmitter = require("resource://devtools/shared/event-emitter.js");
const {
  gDevTools,
} = require("resource://devtools/client/framework/devtools.js");
const {
  getFormattedSize,
} = require("resource://devtools/client/netmonitor/src/utils/format-utils.js");

const l10n = new Localization(["devtools/client/toolbox-options.ftl"], true);

loader.lazyRequireGetter(
  this,
  "openDocLink",
  "resource://devtools/client/shared/link.js",
  true
);
loader.lazyRequireGetter(
  this,
  "findCssSelector",
  "resource://devtools/shared/inspector/css-logic.js",
  true
);

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "LocalFile", () =>
  Components.Constructor("@mozilla.org/file/local;1", "nsIFile", "initWithPath")
);

ChromeUtils.defineESModuleGetters(lazy, {
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  LocalModeMappings:
    "resource://devtools/client/framework/LocalModeMappings.sys.mjs",
});

const NETMONITOR_BODY_LIMIT_PREF = "devtools.netmonitor.bodyLimit";

function GetPref(name) {
  const type = Services.prefs.getPrefType(name);
  switch (type) {
    case Services.prefs.PREF_STRING:
      return Services.prefs.getCharPref(name);
    case Services.prefs.PREF_INT:
      return Services.prefs.getIntPref(name);
    case Services.prefs.PREF_BOOL:
      return Services.prefs.getBoolPref(name);
    default:
      throw new Error("Unknown type");
  }
}

function SetPref(name, value) {
  const type = Services.prefs.getPrefType(name);
  switch (type) {
    case Services.prefs.PREF_STRING:
      return Services.prefs.setCharPref(name, value);
    case Services.prefs.PREF_INT:
      return Services.prefs.setIntPref(name, value);
    case Services.prefs.PREF_BOOL:
      return Services.prefs.setBoolPref(name, value);
    default:
      throw new Error("Unknown type");
  }
}

function InfallibleGetBoolPref(key) {
  try {
    return Services.prefs.getBoolPref(key);
  } catch (ex) {
    return true;
  }
}

/**
 * Represents the Options Panel in the Toolbox.
 */
class OptionsPanel extends EventEmitter {
  constructor(iframeWindow, toolbox, commands) {
    super();

    this.panelDoc = iframeWindow.document;
    this.panelWin = iframeWindow;

    this.toolbox = toolbox;
    this.commands = commands;
    this.telemetry = toolbox.telemetry;

    this.setupToolsList = this.setupToolsList.bind(this);

    this.disableJSNode = this.panelDoc.getElementById(
      "devtools-disable-javascript"
    );

    this.#addListeners();
  }

  get target() {
    return this.toolbox.target;
  }

  async open() {
    this.setupToolsList();
    this.setupToolbarButtonsList();
    this.setupThemeList();
    this.setupAdditionalOptions();
    await this.populatePreferences();
    this.#setupLocalMode();
    this.setupNetworkBodySizeLimit();
    this.#setUpExperimentalFeatures();
    return this;
  }

  #observedPreferences = [
    "devtools.cache.disabled",
    "devtools.theme",
    "devtools.source-map.client-service.enabled",
    "devtools.toolbox.splitconsole.enabled",
    NETMONITOR_BODY_LIMIT_PREF,
  ];

  #addListeners() {
    for (const prefName of this.#observedPreferences) {
      Services.prefs.addObserver(prefName, this.#prefChanged);
    }

    gDevTools.on("theme-registered", this.#themeRegistered);
    gDevTools.on("theme-unregistered", this.#themeUnregistered);

    // Refresh the tools list when a new tool or webextension has been
    // registered to the toolbox.
    this.toolbox.on("tool-registered", this.setupToolsList);
    this.toolbox.on("webextension-registered", this.setupToolsList);
    // Refresh the tools list when a new tool or webextension has been
    // unregistered from the toolbox.
    this.toolbox.on("tool-unregistered", this.setupToolsList);
    this.toolbox.on("webextension-unregistered", this.setupToolsList);
    lazy.LocalModeMappings.on("updated", this.#updateLocalModeMappings);
  }

  #removeListeners() {
    for (const prefName of this.#observedPreferences) {
      Services.prefs.removeObserver(prefName, this.#prefChanged);
    }

    this.toolbox.off("tool-registered", this.setupToolsList);
    this.toolbox.off("tool-unregistered", this.setupToolsList);
    this.toolbox.off("webextension-registered", this.setupToolsList);
    this.toolbox.off("webextension-unregistered", this.setupToolsList);
    lazy.LocalModeMappings.off("updated", this.#updateLocalModeMappings);

    gDevTools.off("theme-registered", this.#themeRegistered);
    gDevTools.off("theme-unregistered", this.#themeUnregistered);
  }

  #prefChanged = (subject, topic, prefName) => {
    if (prefName === "devtools.cache.disabled") {
      const cacheDisabled = GetPref(prefName);
      const cbx = this.panelDoc.getElementById("devtools-disable-cache");
      cbx.checked = cacheDisabled;
    } else if (prefName === "devtools.theme") {
      this.updateCurrentTheme();
    } else if (prefName === "devtools.source-map.client-service.enabled") {
      this.updateSourceMapPref();
    } else if (prefName === "devtools.toolbox.splitconsole.enabled") {
      this.toolbox.updateIsSplitConsoleEnabled();
    } else if (prefName === NETMONITOR_BODY_LIMIT_PREF) {
      this.updateNetworkBodySizeLimit();
    }
  };

  #themeRegistered = () => {
    this.setupThemeList();
  };

  #themeUnregistered = theme => {
    const themeBox = this.panelDoc.getElementById("devtools-theme-box");
    const themeInput = themeBox.querySelector(`[value=${theme.id}]`);

    if (themeInput) {
      themeInput.parentNode.remove();
    }
  };

  async setupToolbarButtonsList() {
    // Ensure the toolbox is open, and the buttons are all set up.
    await this.toolbox.isOpen;

    const enabledToolbarButtonsBox = this.panelDoc.getElementById(
      "enabled-toolbox-buttons-box"
    );

    const toolbarButtons = this.toolbox.toolbarButtons;

    if (!toolbarButtons) {
      console.warn("The command buttons weren't initiated yet.");
      return;
    }

    const onCheckboxClick = checkbox => {
      const commandButton = toolbarButtons.filter(
        toggleableButton => toggleableButton.id === checkbox.id
      )[0];

      Services.prefs.setBoolPref(
        commandButton.visibilityswitch,
        checkbox.checked
      );
      this.toolbox.updateToolboxButtonsVisibility();
    };

    const createCommandCheckbox = button => {
      const checkboxLabel = this.panelDoc.createElement("label");
      const checkboxSpanLabel = this.panelDoc.createElement("span");
      checkboxSpanLabel.textContent = button.description;
      const checkboxInput = this.panelDoc.createElement("input");
      checkboxInput.setAttribute("type", "checkbox");
      checkboxInput.setAttribute("id", button.id);

      if (Services.prefs.getBoolPref(button.visibilityswitch, true)) {
        checkboxInput.setAttribute("checked", true);
      }
      checkboxInput.addEventListener(
        "change",
        onCheckboxClick.bind(this, checkboxInput)
      );

      checkboxLabel.appendChild(checkboxInput);
      checkboxLabel.appendChild(checkboxSpanLabel);

      return checkboxLabel;
    };

    for (const button of toolbarButtons) {
      if (!button.isToolSupported(this.toolbox)) {
        continue;
      }

      enabledToolbarButtonsBox.appendChild(createCommandCheckbox(button));
    }
  }

  #setUpExperimentalFeatures() {
    this.panelDoc
      .getElementById("devtools-debugger-features-stylesheets-in-debugger")
      .addEventListener("change", event => {
        if (event.target.checked) {
          Glean.devtoolsDebuggerStylesheets.stylesheetPrefEnabledCount.add(1);
        } else {
          Glean.devtoolsDebuggerStylesheets.stylesheetPrefDisabledCount.add(1);
        }
      });
  }

  setupToolsList() {
    const defaultToolsBox = this.panelDoc.getElementById("default-tools-box");
    const additionalToolsBox = this.panelDoc.getElementById(
      "additional-tools-box"
    );
    const toolsNotSupportedLabel = this.panelDoc.getElementById(
      "tools-not-supported-label"
    );
    let atleastOneToolNotSupported = false;

    // Signal tool registering/unregistering globally (for the tools registered
    // globally) and per toolbox (for the tools registered to a single toolbox).
    // This event handler expect this to be binded to the related checkbox element.
    const onCheckboxClick = function (telemetry, tool) {
      // Set the kill switch pref boolean to true
      Services.prefs.setBoolPref(tool.visibilityswitch, this.checked);

      if (!tool.isWebExtension) {
        gDevTools.emit(
          this.checked ? "tool-registered" : "tool-unregistered",
          tool.id
        );
        // Record which tools were registered and unregistered.
        Glean.devtoolsTool.registered[tool.id].set(this.checked);
      }
    };

    const createToolCheckbox = tool => {
      const checkboxLabel = this.panelDoc.createElement("label");
      const checkboxInput = this.panelDoc.createElement("input");
      checkboxInput.setAttribute("type", "checkbox");
      checkboxInput.setAttribute("id", tool.id);
      checkboxInput.setAttribute("title", tool.tooltip || "");

      const checkboxSpanLabel = this.panelDoc.createElement("span");
      if (tool.isToolSupported(this.toolbox)) {
        checkboxSpanLabel.textContent = tool.label;
      } else {
        atleastOneToolNotSupported = true;
        checkboxSpanLabel.textContent = l10n.formatValueSync(
          "options-tool-not-supported-marker",
          { toolLabel: tool.label }
        );
        checkboxInput.setAttribute("data-unsupported", "true");
        checkboxInput.setAttribute("disabled", "true");
      }

      if (InfallibleGetBoolPref(tool.visibilityswitch)) {
        checkboxInput.setAttribute("checked", "true");
      }

      checkboxInput.addEventListener(
        "change",
        onCheckboxClick.bind(checkboxInput, this.telemetry, tool)
      );

      checkboxLabel.appendChild(checkboxInput);
      checkboxLabel.appendChild(checkboxSpanLabel);

      // We shouldn't have deprecated tools anymore, but we might have one in the future,
      // when migrating the storage inspector to the application panel (Bug 1681059).
      // Let's keep this code for now so we keep the l10n property around and avoid
      // unnecessary translation work if we need it again in the future.
      if (tool.deprecated) {
        const deprecationURL = this.panelDoc.createElement("a");
        deprecationURL.title = deprecationURL.href = tool.deprecationURL;
        deprecationURL.textContent = l10n.formatValueSync(
          "options-deprecation-notice"
        );
        // Cannot use a real link when we are in the Browser Toolbox.
        deprecationURL.addEventListener("click", e => {
          e.preventDefault();
          openDocLink(tool.deprecationURL, { relatedToCurrent: true });
        });

        const checkboxSpanDeprecated = this.panelDoc.createElement("span");
        checkboxSpanDeprecated.className = "deprecation-notice";
        checkboxLabel.appendChild(checkboxSpanDeprecated);
        checkboxSpanDeprecated.appendChild(deprecationURL);
      }

      return checkboxLabel;
    };

    // Clean up any existent default tools content.
    for (const label of defaultToolsBox.querySelectorAll("label")) {
      label.remove();
    }

    // Populating the default tools lists
    const toggleableTools = gDevTools.getDefaultTools().filter(tool => {
      return tool.visibilityswitch;
    });

    const fragment = this.panelDoc.createDocumentFragment();
    for (const tool of toggleableTools) {
      fragment.appendChild(createToolCheckbox(tool));
    }

    const toolsNotSupportedLabelNode = this.panelDoc.getElementById(
      "tools-not-supported-label"
    );
    defaultToolsBox.insertBefore(fragment, toolsNotSupportedLabelNode);

    // Clean up any existent additional tools content.
    for (const label of additionalToolsBox.querySelectorAll("label")) {
      label.remove();
    }

    // Populating the additional tools list.
    let atleastOneAddon = false;
    for (const tool of gDevTools.getAdditionalTools()) {
      atleastOneAddon = true;
      additionalToolsBox.appendChild(createToolCheckbox(tool));
    }

    // Populating the additional tools that came from the installed WebExtension add-ons.
    for (const { uuid, name, pref } of this.toolbox.listWebExtensions()) {
      atleastOneAddon = true;

      additionalToolsBox.appendChild(
        createToolCheckbox({
          isWebExtension: true,

          // Use the preference as the unified webextensions tool id.
          id: `webext-${uuid}`,
          tooltip: name,
          label: name,
          // Disable the devtools extension using the given pref name:
          // the toolbox options for the WebExtensions are not related to a single
          // tool (e.g. a devtools panel created from the extension devtools_page)
          // but to the entire devtools part of a webextension which is enabled
          // by the Addon Manager (but it may be disabled by its related
          // devtools about:config preference), and so the following
          visibilityswitch: pref,

          // Only local tabs are currently supported as targets.
          isToolSupported: toolbox =>
            toolbox.commands.descriptorFront.isLocalTab,
        })
      );
    }

    if (!atleastOneAddon) {
      additionalToolsBox.style.display = "none";
    } else {
      additionalToolsBox.style.display = "";
    }

    if (!atleastOneToolNotSupported) {
      toolsNotSupportedLabel.style.display = "none";
    } else {
      toolsNotSupportedLabel.style.display = "";
    }

    this.panelWin.focus();
  }

  setupThemeList() {
    const themeBox = this.panelDoc.getElementById("devtools-theme-box");
    const themeLabels = themeBox.querySelectorAll("label");
    for (const label of themeLabels) {
      label.remove();
    }

    const createThemeOption = theme => {
      const inputLabel = this.panelDoc.createElement("label");
      const inputRadio = this.panelDoc.createElement("input");
      inputRadio.setAttribute("type", "radio");
      inputRadio.setAttribute("value", theme.id);
      inputRadio.setAttribute("name", "devtools-theme-item");
      inputRadio.addEventListener("change", function (e) {
        SetPref(themeBox.getAttribute("data-pref"), e.target.value);
      });

      const inputSpanLabel = this.panelDoc.createElement("span");
      inputSpanLabel.textContent = theme.label;
      inputLabel.appendChild(inputRadio);
      inputLabel.appendChild(inputSpanLabel);

      return inputLabel;
    };

    // Populating the default theme list
    themeBox.appendChild(
      createThemeOption({
        id: "auto",
        label: l10n.formatValueSync("options-auto-theme-label"),
      })
    );

    const themes = gDevTools.getThemeDefinitionArray();
    for (const theme of themes) {
      themeBox.appendChild(createThemeOption(theme));
    }

    this.updateCurrentTheme();
  }

  /**
   * Add extra checkbox options bound to a boolean preference.
   */
  setupAdditionalOptions() {
    const prefDefinitions = [
      {
        pref: "devtools.custom-formatters.enabled",
        l10nLabelId: "options-enable-custom-formatters-label",
        l10nTooltipId: "options-enable-custom-formatters-tooltip",
        id: "devtools-custom-formatters",
        parentId: "context-options",
      },
    ];

    const createPreferenceOption = ({
      pref,
      label,
      l10nLabelId,
      l10nTooltipId,
      id,
      onChange,
    }) => {
      const inputLabel = this.panelDoc.createElement("label");
      if (l10nTooltipId) {
        this.panelDoc.l10n.setAttributes(inputLabel, l10nTooltipId);
      }
      const checkbox = this.panelDoc.createElement("input");
      checkbox.setAttribute("type", "checkbox");
      if (GetPref(pref)) {
        checkbox.setAttribute("checked", "checked");
      }
      checkbox.setAttribute("id", id);
      checkbox.addEventListener("change", e => {
        SetPref(pref, e.target.checked);
        if (onChange) {
          onChange(e.target.checked);
        }
      });

      const inputSpanLabel = this.panelDoc.createElement("span");
      if (l10nLabelId) {
        this.panelDoc.l10n.setAttributes(inputSpanLabel, l10nLabelId);
      } else if (label) {
        inputSpanLabel.textContent = label;
      }
      inputLabel.appendChild(checkbox);
      inputLabel.appendChild(inputSpanLabel);

      return inputLabel;
    };

    for (const prefDefinition of prefDefinitions) {
      const parent = this.panelDoc.getElementById(prefDefinition.parentId);
      // We want to insert the new definition after the last existing
      // definition, but before any other element.
      // For example in the "Advanced Settings" column there's indeed a <span>
      // text at the end, and we want that it stays at the end.
      // The reference element can be `null` if there's no label or if there's
      // no element after the last label. But that's OK and it will do what we
      // want.
      const referenceElement = parent.querySelector("label:last-of-type + *");
      parent.insertBefore(
        createPreferenceOption(prefDefinition),
        referenceElement
      );
    }
  }

  async populatePreferences() {
    const prefCheckboxes = this.panelDoc.querySelectorAll(
      "input[type=checkbox][data-pref]"
    );
    for (const prefCheckbox of prefCheckboxes) {
      if (GetPref(prefCheckbox.getAttribute("data-pref"))) {
        prefCheckbox.setAttribute("checked", true);
      }
      if (Services.prefs.prefIsLocked(prefCheckbox.getAttribute("data-pref"))) {
        prefCheckbox.disabled = true;
      }
      prefCheckbox.addEventListener("change", e => {
        const checkbox = e.target;
        SetPref(checkbox.getAttribute("data-pref"), checkbox.checked);
        if (checkbox.hasAttribute("data-force-reload")) {
          this.commands.targetCommand.reloadTopLevelTarget();
        }
      });
    }
    // Themes radio inputs are handled in setupThemeList
    const prefRadiogroups = this.panelDoc.querySelectorAll(
      ".radiogroup[data-pref]:not(#devtools-theme-box)"
    );
    for (const radioGroup of prefRadiogroups) {
      const selectedValue = GetPref(radioGroup.getAttribute("data-pref"));

      for (const radioInput of radioGroup.querySelectorAll(
        "input[type=radio]"
      )) {
        if (radioInput.getAttribute("value") == selectedValue) {
          radioInput.setAttribute("checked", true);
        }

        radioInput.addEventListener("change", function (e) {
          SetPref(radioGroup.getAttribute("data-pref"), e.target.value);
        });
      }
    }
    const prefSelects = this.panelDoc.querySelectorAll("select[data-pref]");
    for (const prefSelect of prefSelects) {
      const pref = GetPref(prefSelect.getAttribute("data-pref"));
      const options = [...prefSelect.options];
      options.some(function (option) {
        const value = option.value;
        // non strict check to allow int values.
        if (value == pref) {
          prefSelect.selectedIndex = options.indexOf(option);
          return true;
        }
        return false;
      });

      prefSelect.addEventListener("change", function (e) {
        const select = e.target;
        SetPref(
          select.getAttribute("data-pref"),
          select.options[select.selectedIndex].value
        );
      });
    }

    if (this.commands.descriptorFront.isTabDescriptor) {
      const isJavascriptEnabled =
        await this.commands.targetConfigurationCommand.isJavascriptEnabled();
      this.disableJSNode.checked = !isJavascriptEnabled;
      this.disableJSNode.addEventListener("click", this.#disableJSClicked);
    } else {
      // Hide the checkbox and label
      this.disableJSNode.parentNode.style.display = "none";
    }
  }

  setupNetworkBodySizeLimit() {
    const input = this.panelDoc.querySelector("#netmonitor-body-limit");

    // Int preferences at capped at PR_INT32_MAX == ~2 GB
    const maxValue = 2147483647;

    const editElement = this.panelDoc.querySelector(
      `.netmonitor-body-limit-button`
    );
    editElement.addEventListener("click", this.editNetworkBodySizeLimit);

    const valueElement = this.panelDoc.querySelector(
      `#netmonitor-body-limit-value`
    );
    const editSection = this.panelDoc.querySelector(
      "#netmonitor-body-limit-edit"
    );
    input.addEventListener("keydown", function (event) {
      if (event.key == "Escape") {
        editSection.classList.remove("active");
        // Prevents showing the split console
        event.preventDefault();
        event.stopPropagation();
        valueElement.classList.remove("hidden");
        editElement.classList.remove("hidden");
        input.setCustomValidity("");
        const hasCustomValue = Services.prefs.prefHasUserValue(
          NETMONITOR_BODY_LIMIT_PREF
        );
        resetElement.classList.toggle("hidden", !hasCustomValue);
      } else if (event.key == "Enter") {
        setValue();
        // Prevents submitting the form and navigating the options document
        // to self-reload
        event.preventDefault();
      }
    });
    input.addEventListener("input", function () {
      const limit = parseInt(input.value, 10);
      if (limit < 0 || limit > maxValue) {
        input.setCustomValidity("invalid");
      } else {
        input.setCustomValidity("");
      }
    });

    function setValue() {
      editSection.classList.remove("active");
      valueElement.classList.remove("hidden");
      editElement.classList.remove("hidden");
      input.setCustomValidity("");
      // Check <input pattern> mismatch as parseInt accepts strings like "1000xxx" and returns 1000
      if (!input.validity.patternMismatch) {
        // If this is a valid number, clamp to the closest valid number
        const limit = Math.min(
          Math.max(parseInt(input.value, 10), 0),
          maxValue
        );
        SetPref(NETMONITOR_BODY_LIMIT_PREF, limit);
      }
      const hasCustomValue = Services.prefs.prefHasUserValue(
        NETMONITOR_BODY_LIMIT_PREF
      );
      resetElement.classList.toggle("hidden", !hasCustomValue);
    }
    function reset() {
      editSection.classList.remove("active");
      valueElement.classList.remove("hidden");
      editElement.classList.remove("hidden");
      // Reset to the default value
      Services.prefs.clearUserPref(NETMONITOR_BODY_LIMIT_PREF);
      resetElement.classList.add("hidden");
    }

    const setElement = this.panelDoc.querySelector(
      `.netmonitor-body-limit-set`
    );
    setElement.addEventListener("click", setValue);
    const resetElement = this.panelDoc.querySelector(
      `.netmonitor-body-limit-restore-default`
    );
    resetElement.addEventListener("click", reset);
    const hasCustomValue = Services.prefs.prefHasUserValue(
      NETMONITOR_BODY_LIMIT_PREF
    );
    resetElement.classList.toggle("hidden", !hasCustomValue);

    this.updateNetworkBodySizeLimit();
  }

  updateNetworkBodySizeLimit() {
    const valueElement = this.panelDoc.querySelector(
      `#netmonitor-body-limit-value`
    );
    const value = GetPref(NETMONITOR_BODY_LIMIT_PREF);
    valueElement.textContent =
      value == 0
        ? l10n.formatValueSync("options-netmonitor-body-limit-unlimited-label")
        : getFormattedSize(value);
  }

  /**
   * Show a form to edit the network maximum body size limit.
   * Including an input and a save button which will replace the read-only display.
   */
  editNetworkBodySizeLimit = () => {
    const valueElement = this.panelDoc.querySelector(
      `#netmonitor-body-limit-value`
    );
    valueElement.classList.add("hidden");
    const editElement = this.panelDoc.querySelector(
      `.netmonitor-body-limit-button`
    );
    editElement.classList.add("hidden");

    const editSection = this.panelDoc.querySelector(
      "#netmonitor-body-limit-edit"
    );
    editSection.classList.add("active");

    const resetElement = this.panelDoc.querySelector(
      `.netmonitor-body-limit-restore-default`
    );
    resetElement.classList.add("hidden");

    const input = this.panelDoc.querySelector("#netmonitor-body-limit");
    input.value = GetPref(NETMONITOR_BODY_LIMIT_PREF);

    input.focus();
  };

  updateCurrentTheme() {
    const currentTheme = GetPref("devtools.theme");
    const themeBox = this.panelDoc.getElementById("devtools-theme-box");
    const themeRadioInput = themeBox.querySelector(`[value=${currentTheme}]`);

    if (themeRadioInput) {
      themeRadioInput.checked = true;
    } else {
      // If the current theme does not exist anymore, switch to auto theme
      const autoThemeInputRadio = themeBox.querySelector("[value=auto]");
      autoThemeInputRadio.checked = true;
    }
  }

  #setupLocalMode() {
    if (!this.commands.descriptorFront.isLocalTab) {
      const notice = this.panelDoc.querySelector(
        `.local-mode-only-work-locally`
      );
      notice.classList.remove("hidden");
    }
    const newButton = this.panelDoc.querySelector(`.local-mode-new-mapping`);
    newButton.addEventListener("click", this.#newLocalModeMapping);
    this.#updateLocalModeMappings();
  }

  #newLocalModeMapping = async event => {
    event.preventDefault();
    event.stopPropagation();

    const origin = lazy.LocalModeMappings.getNextAvailableOrigin();

    const path = await this.#chooseLocalModePath(origin);

    this.#focusLocalModeLastMapping = true;
    lazy.LocalModeMappings.createNewMapping(origin, path);

    Glean.devtoolsLocalmodeMappings.addPermanentFromOptions.add(1);
  };

  /**
   * Create all the DOM Elements to control one "Local Mode" mapping.
   *
   * @param {string} origin
   *        Mapping's https origin. e.g. firefox.location
   * @param {string} path
   *        Absolute path from where the origin should be loaded locally
   * @param {boolean} disabled
   *        Is the mapping currently disabled.
   * @param {string} prefPrefix
   *        Preference prefix for this specific mapping
   *        e.g. "devtools.local-mode.mappings.0."
   * @param {Array} mappings
   *        List of all the mappings.
   *        See `LocalModeMappings.getAllMappings()`
   * @return {DOMElement}
   *        The <li> element rendering this mapping.
   */
  #createLocalModeMappingDOM(origin, path, disabled, prefPrefix, mappings) {
    const el = this.panelDoc.createElement("li");
    // Expose the prefix for the notification bar to easily spot the newly created mapping
    el.setAttribute("data-pref-prefix", prefPrefix);
    el.classList.toggle("disabled", disabled);

    const originLine = this.panelDoc.createElement("div");
    originLine.classList.add("local-mode-origin-line");

    const inputId = "origin-" + prefPrefix.replace(/\./g, "-");
    const originLabel = this.panelDoc.createElement("label");
    originLabel.setAttribute("data-l10n-id", "options-local-mode-domain-label");
    originLabel.setAttribute("for", inputId);

    const originValueContainer = this.panelDoc.createElement("div");
    const originPrefixLabel = this.panelDoc.createElement("span");
    originPrefixLabel.textContent = "http(s)://";

    const originElement = this.panelDoc.createElement("input");
    originElement.id = inputId;
    originElement.classList.add("local-mode-origin-input");
    originElement.setAttribute(
      "data-l10n-id",
      "options-local-mode-origin-input"
    );
    originElement.setAttribute("type", "text");
    originElement.setAttribute("value", origin);
    originElement.toggleAttribute("disabled", disabled);

    originElement.addEventListener("keypress", event => {
      if (event.key == "Enter") {
        // Cancel the enter keypress as it would something trigger a click
        // on the first open element and always try to navigate to first mapping URL
        event.preventDefault();
        originElement.blur();
      }
    });
    originElement.addEventListener("input", event => {
      const newOrigin = event.target.value;
      if (!newOrigin) {
        originError.textContent = "";
        originElement.setCustomValidity("");
        return;
      }

      // Check if we may override another mapping
      if (
        newOrigin != origin &&
        mappings.some(mapping => mapping.origin == newOrigin)
      ) {
        originElement.setCustomValidity("invalid");
        originError.textContent = l10n.formatValueSync(
          "options-local-mode-origin-conflict"
        );
      } else if (!URL.canParse(`https://${newOrigin}`)) {
        originElement.setCustomValidity("invalid");
        originError.textContent = l10n.formatValueSync(
          "options-local-mode-origin-invalid"
        );
      } else {
        originError.textContent = "";
        originElement.setCustomValidity("");
      }
    });

    originElement.addEventListener("blur", event => {
      const newOrigin = event.target.value;

      originError.textContent = "";
      originElement.setCustomValidity("");

      if (newOrigin == origin) {
        return;
      }

      // In case of empty or invalid input, reverts back to initial value on blur
      if (
        !newOrigin ||
        mappings.some(mapping => mapping.origin == newOrigin) ||
        !URL.canParse(`https://${newOrigin}`)
      ) {
        // Reset back to previous value
        event.target.value = origin;
        return;
      }

      // Disable DOM updates on preferences changes as it would make us lose the focus
      // and no update are needed as we can simply update the local `origin` variable
      this.#ignoreLocalModeChanges = false;

      // And create a new one with the new origin
      Services.prefs.setStringPref(prefPrefix + "origin", newOrigin);
      origin = newOrigin;

      this.#ignoreLocalModeChanges = false;
    });
    const originError = this.panelDoc.createElement("span");
    originError.classList.add("local-mode-origin-error");

    const openButton = this.panelDoc.createElement("button");
    openButton.id = "navigate-" + prefPrefix;
    openButton.classList.add(
      "devtools-button",
      "local-mode-mapping-navigate-to"
    );
    openButton.setAttribute("data-l10n-id", "options-local-mode-navigate-to");
    openButton.toggleAttribute("disabled", disabled);

    openButton.addEventListener("click", event => {
      event.preventDefault();
      event.stopPropagation();

      this.commands.targetCommand.navigateTo("https://" + origin);
    });
    originValueContainer.append(
      originElement,
      originPrefixLabel,
      originElement,
      openButton,
      originError
    );

    originLine.append(originLabel, originValueContainer);

    const folderLine = this.panelDoc.createElement("div");
    folderLine.classList.add("local-mode-folder-line");
    if (disabled) {
      folderLine.classList.add("disabled");
    }

    const folderLabel = this.panelDoc.createElement("label");
    folderLabel.setAttribute("data-l10n-id", "options-local-mode-folder-label");

    const inputContainer2 = this.panelDoc.createElement("div");
    const folderLinkElement = this.panelDoc.createElement("a");
    folderLinkElement.id = "link-" + prefPrefix;
    folderLinkElement.href = "file://" + path;
    folderLinkElement.textContent = path;
    folderLinkElement.addEventListener("click", function (event) {
      // Request the OS to open the folder in the default file explorer app
      new lazy.LocalFile(path).reveal();
      // Prevent Cmd/Shift+click from opening the file:// URL in a tab
      event.preventDefault();
    });

    // If the path is invalid, replace the link with a label + link
    // to warn the user about that.
    let pathExists = false;
    try {
      pathExists = new lazy.FileUtils.File(path).exists();
    } catch (e) {}
    let folderError = "";
    if (!pathExists) {
      folderError = this.panelDoc.createElement("span");
      folderError.classList.add("local-mode-folder-error");
      folderError.textContent = l10n.formatValueSync(
        "options-local-mode-folder-invalid"
      );
    }

    const folderChooserElement = this.panelDoc.createElement("button");
    folderChooserElement.id = "choose-folder-" + prefPrefix;
    folderChooserElement.classList.add(
      "devtools-button",
      "local-mode-mapping-choose-folder"
    );
    folderChooserElement.setAttribute(
      "data-l10n-id",
      "options-local-mode-choose-folder"
    );
    folderChooserElement.toggleAttribute("disabled", disabled);
    folderChooserElement.addEventListener("click", async event => {
      event.preventDefault();
      event.stopPropagation();

      const newPath = await this.#chooseLocalModePath(origin, path);

      Services.prefs.setStringPref(prefPrefix + "path", newPath);
    });
    inputContainer2.append(folderLinkElement, folderChooserElement);

    folderLine.append(folderLabel, inputContainer2);

    if (folderError) {
      folderLine.append(folderError);
    }

    const footerEl = this.panelDoc.createElement("footer");

    const toggleButton = this.panelDoc.createElement("button");
    toggleButton.id = "toggle-" + prefPrefix;
    toggleButton.setAttribute("data-l10n-id", "options-local-mode-toggle");
    toggleButton.classList.add("devtools-button", "local-mode-mapping-toggle");
    toggleButton.textContent = l10n.formatValueSync(
      disabled
        ? "options-local-mode-toggle-enable"
        : "options-local-mode-toggle-disable"
    );

    toggleButton.addEventListener("click", event => {
      event.preventDefault();

      Services.prefs.setBoolPref(prefPrefix + "disabled", !disabled);
    });

    const removeButton = this.panelDoc.createElement("button");
    removeButton.classList.add("devtools-button", "local-mode-mapping-remove");
    removeButton.append("Remove local mapping");
    removeButton.addEventListener("click", event => {
      event.preventDefault();

      const message = l10n.formatValueSync(
        "options-local-mode-confirm-deletion",
        { mappingOrigin: origin }
      );
      if (!this.panelDoc.defaultView.confirm(message)) {
        return;
      }
      Services.prefs.clearUserPref(prefPrefix + "origin");
      Services.prefs.clearUserPref(prefPrefix + "path");
      Services.prefs.clearUserPref(prefPrefix + "disabled");
    });

    footerEl.append(toggleButton, removeButton);

    el.append(originLine, folderLine, footerEl);
    return el;
  }

  // Internal flag to avoid updated Local Mode mappings when receiving
  // a preferences update notification
  #ignoreLocalModeChanges = false;

  // Should we focus the last displayed mapping on next local mode mappings update
  #focusLocalModeLastMapping = false;

  /**
   * Update the list of all local mode mappings on startup, or when preferences
   * are updated.
   */
  #updateLocalModeMappings = async () => {
    // When the UI updates the prefs, we may not want to update the DOM
    if (this.#ignoreLocalModeChanges) {
      return;
    }

    const mappingsElement = this.panelDoc.querySelector(`#local-mode-mappings`);

    const elements = [];
    const mappings = lazy.LocalModeMappings.getAllMappings();
    for (const { origin, path, disabled, prefPrefix } of mappings) {
      elements.push(
        this.#createLocalModeMappingDOM(
          origin,
          path,
          disabled,
          prefPrefix,
          mappings
        )
      );
    }

    // As we are about to wipe and recreate all the mappings,
    // try to save and restore the currently focused element via their ID
    let focusedId = "";
    const { activeElement } = this.panelDoc;
    if (activeElement?.id && mappingsElement.contains(activeElement)) {
      focusedId = activeElement.id;
    }

    mappingsElement.replaceChildren(...elements);

    if (this.#focusLocalModeLastMapping) {
      const lastMappingOriginInput = mappingsElement.querySelector(
        "li:last-of-type .local-mode-origin-input"
      );
      if (lastMappingOriginInput) {
        lastMappingOriginInput.focus();
        lastMappingOriginInput.select();
      }
      this.#focusLocalModeLastMapping = false;
    } else if (focusedId) {
      const elementToFocus = this.panelDoc.getElementById(focusedId);
      if (elementToFocus) {
        elementToFocus.focus();
      }
    }
  };

  /**
   * Helper to choose a local folder path for a given local mode origin.
   *
   * @param {string} origin
   * @param {string} existingPath
   *        If picking a folder for an existing mapping, the absolute
   *        path to the current folder associated with this mapping.
   * @return {promise<string>}
   *         Absolute path to the local folder
   */
  #chooseLocalModePath(origin, existingPath) {
    const FilePicker = Cc["@mozilla.org/filepicker;1"].createInstance(
      Ci.nsIFilePicker
    );
    FilePicker.init(
      this.panelWin.browsingContext,
      l10n.formatValueSync("options-local-mode-choose-folder-picker-title", {
        url: "https://" + origin,
      }),
      FilePicker.modeGetFolder
    );

    // Try to display the existing path for this mapping, if valid and exists
    try {
      const file = new lazy.FileUtils.File(existingPath);
      if (file.exists()) {
        FilePicker.displayDirectory = file;
      }
    } catch (e) {}

    return new Promise((resolve, reject) => {
      FilePicker.open(rv => {
        if (rv == FilePicker.returnOK) {
          resolve(FilePicker.file.path);
        } else {
          reject();
        }
      });
    });
  }

  updateSourceMapPref() {
    const prefName = "devtools.source-map.client-service.enabled";
    const enabled = GetPref(prefName);
    const box = this.panelDoc.querySelector(`[data-pref="${prefName}"]`);
    box.checked = enabled;
  }

  /**
   * Disables JavaScript for the currently loaded tab. We force a page refresh
   * here because setting browsingContext.allowJavascript to true fails to block
   * JS execution from event listeners added using addEventListener(), AJAX
   * calls and timers. The page refresh prevents these things from being added
   * in the first place.
   *
   * @param {Event} event
   *        The event sent by checking / unchecking the disable JS checkbox.
   */
  #disableJSClicked = event => {
    const checked = event.target.checked;

    this.commands.targetConfigurationCommand.updateConfiguration({
      javascriptEnabled: !checked,
    });
  };

  destroy() {
    if (this.destroyed) {
      return;
    }
    this.destroyed = true;

    this.#removeListeners();

    this.disableJSNode.removeEventListener("click", this.#disableJSClicked);

    this.panelWin = this.panelDoc = this.disableJSNode = this.toolbox = null;
  }
}

exports.OptionsPanel = OptionsPanel;
