/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is loaded into chrome windows with the subscript loader. Wrap in
// a block to prevent accidentally leaking globals onto `window`.
{
  const { TabMetrics } = ChromeUtils.importESModule(
    "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs"
  );
  const { TabStateFlusher } = ChromeUtils.importESModule(
    "resource:///modules/sessionstore/TabStateFlusher.sys.mjs"
  );

  ChromeUtils.importESModule(
    "chrome://browser/content/genai/content/model-optin.mjs",
    {
      global: "current",
    }
  );

  class MozTabbrowserTabGroupMenu extends MozXULElement {
    static COLORS = [
      "blue",
      "purple",
      "cyan",
      "orange",
      "yellow",
      "pink",
      "green",
      "gray",
      "red",
    ];

    static MESSAGE_IDS = {
      blue: "tab-group-editor-color-selector2-blue",
      purple: "tab-group-editor-color-selector2-purple",
      cyan: "tab-group-editor-color-selector2-cyan",
      orange: "tab-group-editor-color-selector2-orange",
      yellow: "tab-group-editor-color-selector2-yellow",
      pink: "tab-group-editor-color-selector2-pink",
      green: "tab-group-editor-color-selector2-green",
      gray: "tab-group-editor-color-selector2-gray",
      red: "tab-group-editor-color-selector2-red",
    };

    static AI_ICON = "chrome://global/skin/icons/highlights.svg";

    static headerSection = /*html*/ `
      <html:div  id="tab-group-default-header">
        <html:div class="panel-header" >
          <html:h1
            id="tab-group-editor-title-create"
            class="tab-group-create-mode-only"
            data-l10n-id="tab-group-editor-title-create">
          </html:h1>
          <html:h1
            id="tab-group-editor-title-edit"
            class="tab-group-edit-mode-only"
            data-l10n-id="tab-group-editor-title-edit">
          </html:h1>
        </html:div>
      </html:div>
    `;

    static editActions = /*html*/ `
      <html:div
        class="panel-body tab-group-edit-actions tab-group-edit-mode-only">
        <toolbarbutton
          tabindex="0"
          id="tabGroupEditor_addNewTabInGroup"
          class="subviewbutton"
          data-l10n-id="tab-group-editor-action-new-tab">
        </toolbarbutton>
        <toolbarbutton
          tabindex="0"
          id="tabGroupEditor_moveGroupToNewWindow"
          class="subviewbutton"
          data-l10n-id="tab-group-editor-action-new-window">
        </toolbarbutton>
        <toolbarbutton
          tabindex="0"
          id="tabGroupEditor_saveAndCloseGroup"
          class="subviewbutton"
          data-l10n-id="tab-group-editor-action-save">
        </toolbarbutton>
        <toolbarbutton
          tabindex="0"
          id="tabGroupEditor_ungroupTabs"
          class="subviewbutton"
          data-l10n-id="tab-group-editor-action-ungroup">
        </toolbarbutton>
      </html:div>

      <toolbarseparator class="tab-group-edit-mode-only" />

      <html:div class="tab-group-edit-mode-only panel-body tab-group-delete">
        <toolbarbutton
          tabindex="0"
          id="tabGroupEditor_deleteGroup"
          class="subviewbutton"
          data-l10n-id="tab-group-editor-action-delete">
        </toolbarbutton>
      </html:div>
    `;

    static suggestionsHeader = /*html*/ `
      <html:div id="tab-group-suggestions-heading" hidden="true">
        <html:div class="panel-header">
          <html:h1 data-l10n-id="tab-group-editor-title-suggest"></html:h1>
        </html:div>
      </html:div>
    `;

    static suggestionsSection = /*html*/ `
      <html:div id="tab-group-suggestions-container" hidden="true">

        <checkbox
          checked="true"
          type="checkbox"
          id="tab-group-select-checkbox"
          data-l10n-id="tab-group-editor-select-suggestions">
        </checkbox>
      
        <html:div id="tab-group-suggestions"></html:div>

        <html:p 
          data-l10n-id="tab-group-editor-information-message">
        </html:p>

        <html:moz-button-group class="panel-body tab-group-create-actions">
          <html:moz-button
            id="tab-group-cancel-suggestions-button"
            data-l10n-id="tab-group-editor-cancel">
          </html:moz-button>
          <html:moz-button
            type="primary"
            id="tab-group-create-suggestions-button"
            data-l10n-id="tab-group-editor-done">
          </html:moz-button>
        </html:moz-button-group>

      </html:div>
    `;

    static suggestionsButton = /*html*/ `
      <html:moz-button
        hidden="true"
        id="tab-group-suggestion-button"
        type="icon ghost"
        data-l10n-id="tab-group-editor-smart-suggest-button-create">
      </html:moz-button>
    `;

    static loadingSection = /*html*/ `
      <html:div id="tab-group-suggestions-loading" hidden="true">
        <html:div
          class="tab-group-suggestions-loading-header"
          data-l10n-id="tab-group-suggestions-loading-header">
        </html:div>
        <html:div class="tab-group-suggestions-loading-block"></html:div>
        <html:div class="tab-group-suggestions-loading-block"></html:div>
        <html:div class="tab-group-suggestions-loading-block"></html:div>
      </html:div>
    `;

    static defaultActions = /*html*/ `
      <html:moz-button-group
        class="tab-group-create-actions tab-group-create-mode-only"
        id="tab-group-default-actions">
        <html:moz-button
          id="tab-group-editor-button-cancel"
          data-l10n-id="tab-group-editor-cancel">
        </html:moz-button>
        <html:moz-button
          type="primary"
          id="tab-group-editor-button-create"
          data-l10n-id="tab-group-editor-done">
        </html:moz-button>
      </html:moz-button-group>
    `;

    static loadingActions = /*html*/ `
      <html:moz-button-group id="tab-group-suggestions-load-actions" hidden="true">
        <html:moz-button
          id="tab-group-suggestions-load-cancel"
          data-l10n-id="tab-group-editor-cancel">
        </html:moz-button>
      </html:moz-button-group>
    `;

    static optinSection = /*html*/ `
      <html:div
        id="tab-group-suggestions-optin-container">
      </html:div>
    `;

    static markup = /*html*/ `
    <panel
        type="arrow"
        class="tab-group-editor-panel"
        orient="vertical"
        role="dialog"
        ignorekeys="true"
        norolluponanchor="true">

        ${this.headerSection}
        ${this.suggestionsHeader}

        <toolbarseparator />

        <html:div
          class="panel-body
          tab-group-editor-name">
          <html:label
            for="tab-group-name"
            data-l10n-id="tab-group-editor-name-label">
          </html:label>
          <html:input
            id="tab-group-name"
            type="text"
            name="tab-group-name"
            value=""
            data-l10n-id="tab-group-editor-name-field"
          />
        </html:div>


      <html:div id="tab-group-main">
        <html:div
          class="panel-body tab-group-editor-swatches"
          role="radiogroup"
          data-l10n-id="tab-group-editor-color-selector"
        />

        <toolbarseparator class="tab-group-edit-mode-only"/>

        ${this.editActions}

        <toolbarseparator id="tab-group-suggestions-separator" hidden="true"/>

        ${this.suggestionsButton}

        <html:div id="tab-group-suggestions-message-container" hidden="true">
          <html:moz-button
            disabled="true"
            type="icon ghost"
            id="tab-group-suggestions-message"
            data-l10n-id="tab-group-editor-no-tabs-found-title">
          </html:moz-button>
          <html:p 
            data-l10n-id="tab-group-editor-no-tabs-found-message">
          </html:p>
        </html:div>

        ${this.defaultActions}

      </html:div>
      
      ${this.loadingSection}
      ${this.loadingActions}
      ${this.suggestionsSection}
      ${this.optinSection}

    </panel>
       `;

    static State = {
      // Standard create mode (No AI UI)
      CREATE_STANDARD_INITIAL: 0,
      // Create mode with AI able to suggest tabs
      CREATE_AI_INITIAL: 1,
      // No ungrouped tabs to suggest (hide AI interactions)
      CREATE_AI_INITIAL_SUGGESTIONS_DISABLED: 2,
      // Create mode with suggestions
      CREATE_AI_WITH_SUGGESTIONS: 3,
      // Create mode with no suggestions
      CREATE_AI_WITH_NO_SUGGESTIONS: 4,
      // Standard edit mode (No AI UI)
      EDIT_STANDARD_INITIAL: 5,
      // Edit mode with AI able to suggest tabs
      EDIT_AI_INITIAL: 6,
      // No ungrouped tabs to suggest
      EDIT_AI_INITIAL_SUGGESTIONS_DISABLED: 7,
      // Edit mode with suggestions
      EDIT_AI_WITH_SUGGESTIONS: 8,
      // Edit mode with no suggestions
      EDIT_AI_WITH_NO_SUGGESTIONS: 9,
      LOADING: 10,
      ERROR: 11,
      // Optin for STG AI
      OPTIN: 12,
    };

    #tabGroupMain;
    #activeGroup;
    #cancelButton;
    #commandButtons;
    #createButton;
    #createMode;
    #keepNewlyCreatedGroup;
    #nameField;
    #panel;
    #swatches;
    #swatchesContainer;
    #defaultActions;
    #suggestionState = MozTabbrowserTabGroupMenu.State.CREATE_STANDARD_INITIAL;
    #suggestionsHeading;
    #defaultHeader;
    /** @type {string} */
    #initialTabGroupName;
    #suggestionsContainer;
    #suggestions;
    #suggestionButton;
    #cancelSuggestionsButton;
    #createSuggestionsButton;
    #suggestionsLoading;
    #selectSuggestionsCheckbox;
    #suggestionsMessage;
    #suggestionsMessageContainer;
    #selectedSuggestedTabs = [];
    #suggestedMlLabel;
    #hasSuggestedMlTabs = false;
    #suggestedTabs = [];
    #suggestionsLoadActions;
    #suggestionsLoadCancel;
    #suggestionsSeparator;
    #smartTabGroupingManager;
    #suggestionsOptinContainer;
    #suggestionsOptin;
    #suggestionsRunToken;

    constructor() {
      super();
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "smartTabGroupsFeatureConfigEnabled",
        "browser.tabs.groups.smart.enabled",
        false,
        this.#onSmartTabGroupsPrefChange.bind(this)
      );

      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "smartTabGroupsUserEnabled",
        "browser.tabs.groups.smart.userEnabled",
        true,
        this.#onSmartTabGroupsPrefChange.bind(this)
      );

      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "smartTabGroupsOptin",
        "browser.tabs.groups.smart.optin",
        false
      );
    }

    connectedCallback() {
      if (this._initialized) {
        return;
      }

      this.textContent = "";
      this.appendChild(this.constructor.fragment);
      this.initializeAttributeInheritance();

      this._initialized = true;

      this.#cancelButton = this.querySelector(
        "#tab-group-editor-button-cancel"
      );
      this.#createButton = this.querySelector(
        "#tab-group-editor-button-create"
      );
      this.#panel = this.querySelector("panel");
      this.#nameField = this.querySelector("#tab-group-name");
      this.#panel.addEventListener("click", e => {
        if (e.target !== this.#nameField) {
          this.#nameField.blur();
        }
      });
      this.#swatchesContainer = this.querySelector(
        ".tab-group-editor-swatches"
      );
      this.#defaultHeader = this.querySelector("#tab-group-default-header");
      this.#defaultActions = this.querySelector("#tab-group-default-actions");
      this.#tabGroupMain = this.querySelector("#tab-group-main");
      this.#initSuggestions();

      this.#populateSwatches();

      this.#cancelButton.addEventListener("click", () => {
        this.#handleMlTelemetry("cancel");
        this.close(false);
      });

      this.#createButton.addEventListener("click", () => {
        this.#handleMlTelemetry("save");
        this.close();
      });

      this.#nameField.addEventListener("input", () => {
        if (this.activeGroup) {
          this.activeGroup.label = this.#nameField.value;
        }
      });

      /**
       * Check if the smart suggest button should be shown
       * If there are no ungrouped tabs, the button should be hidden
       */
      this.canShowAIUserInterface = () => {
        const { tabs } = gBrowser;
        let show = false;
        tabs.forEach(tab => {
          if (tab.group === null) {
            show = true;
          }
        });

        return show;
      };

      this.#commandButtons = {
        addNewTabInGroup: document.getElementById(
          "tabGroupEditor_addNewTabInGroup"
        ),
        moveGroupToNewWindow: document.getElementById(
          "tabGroupEditor_moveGroupToNewWindow"
        ),
        ungroupTabs: document.getElementById("tabGroupEditor_ungroupTabs"),
        saveAndCloseGroup: document.getElementById(
          "tabGroupEditor_saveAndCloseGroup"
        ),
        deleteGroup: document.getElementById("tabGroupEditor_deleteGroup"),
      };

      this.#commandButtons.addNewTabInGroup.addEventListener("command", () => {
        this.#handleNewTabInGroup();
      });

      this.#commandButtons.moveGroupToNewWindow.addEventListener(
        "command",
        () => {
          gBrowser.replaceGroupWithWindow(this.activeGroup);
        }
      );

      this.#commandButtons.ungroupTabs.addEventListener("command", () => {
        this.activeGroup.ungroupTabs({
          isUserTriggered: true,
          telemetrySource: TabMetrics.METRIC_SOURCE.TAB_GROUP_MENU,
        });
      });

      this.#commandButtons.saveAndCloseGroup.addEventListener("command", () => {
        this.activeGroup.saveAndClose({ isUserTriggered: true });
      });

      this.#commandButtons.deleteGroup.addEventListener("command", () => {
        gBrowser.removeTabGroup(
          this.activeGroup,
          TabMetrics.userTriggeredContext(
            TabMetrics.METRIC_SOURCE.TAB_GROUP_MENU
          )
        );
      });

      this.panel.addEventListener("popupshown", this);
      this.panel.addEventListener("popuphidden", this);
      this.panel.addEventListener("keypress", this);
      this.#swatchesContainer.addEventListener("change", this);
    }

    get smartTabGroupsEnabled() {
      return (
        this.smartTabGroupsUserEnabled &&
        this.smartTabGroupsFeatureConfigEnabled &&
        !PrivateBrowsingUtils.isWindowPrivate(this.ownerGlobal)
      );
    }

    #onSmartTabGroupsPrefChange(_preName, _prev, _latest) {
      const icon = this.smartTabGroupsEnabled
        ? MozTabbrowserTabGroupMenu.AI_ICON
        : "";

      this.#suggestionButton.iconSrc = icon;
      this.#suggestionsMessage.iconSrc = icon;
    }

    #initSmartTabGroupsOptin() {
      this.#handleMLOptinTelemetry("step0-optin-shown");
      this.suggestionState = MozTabbrowserTabGroupMenu.State.OPTIN;

      // Init optin component
      this.#suggestionsOptin = document.createElement("model-optin");
      this.#suggestionsOptin.headingL10nId =
        "tab-group-suggestions-optin-title";
      this.#suggestionsOptin.messageL10nId =
        "tab-group-suggestions-optin-message";
      this.#suggestionsOptin.footerMessageL10nId =
        "tab-group-suggestions-optin-message-footer";
      this.#suggestionsOptin.headingIcon = MozTabbrowserTabGroupMenu.AI_ICON;

      // On Confirm
      this.#suggestionsOptin.addEventListener("MlModelOptinConfirm", () => {
        this.#handleMLOptinTelemetry("step1-optin-confirmed");
        Services.prefs.setBoolPref("browser.tabs.groups.smart.optin", true);
        this.#handleFirstDownloadAndSuggest();
      });

      // On Deny
      this.#suggestionsOptin.addEventListener("MlModelOptinDeny", () => {
        this.#handleMLOptinTelemetry("step1-optin-denied");
        this.#smartTabGroupingManager.terminateProcess();
        this.suggestionState = this.createMode
          ? MozTabbrowserTabGroupMenu.State.CREATE_AI_INITIAL
          : MozTabbrowserTabGroupMenu.State.EDIT_AI_INITIAL;
        this.#setFormToDisabled(false);
      });

      // On Cancel Model Download
      this.#suggestionsOptin.addEventListener(
        "MlModelOptinCancelDownload",
        () => {
          this.#suggestionsRunToken = null;
          this.#handleMLOptinTelemetry("step2-optin-cancel-download");
          this.#smartTabGroupingManager.terminateProcess();
          this.suggestionState = this.createMode
            ? MozTabbrowserTabGroupMenu.State.CREATE_AI_INITIAL
            : MozTabbrowserTabGroupMenu.State.EDIT_AI_INITIAL;
          this.#setFormToDisabled(false);
        }
      );

      // On Message link click
      this.#suggestionsOptin.addEventListener(
        "MlModelOptinMessageLinkClick",
        () => {
          this.#handleMLOptinTelemetry("step0-optin-link-click");
          openTrustedLinkIn(
            // this is a placeholder link, it should be replaced with the actual link
            "https://support.mozilla.org",
            "tab"
          );
        }
      );

      // On Footer link click
      this.#suggestionsOptin.addEventListener(
        "MlModelOptinFooterLinkClick",
        () => {
          openTrustedLinkIn("about:preferences", "tab");
        }
      );

      this.#suggestionsOptinContainer.appendChild(this.#suggestionsOptin);
    }

    #initSuggestions() {
      const { SmartTabGroupingManager } = ChromeUtils.importESModule(
        "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs"
      );
      this.#smartTabGroupingManager = new SmartTabGroupingManager();

      // Init Suggestion Button
      this.#suggestionButton = this.querySelector(
        "#tab-group-suggestion-button"
      );
      this.#suggestionButton.iconSrc = this.smartTabGroupsEnabled
        ? MozTabbrowserTabGroupMenu.AI_ICON
        : "";

      // If user has not opted in, show the optin flow
      this.#suggestionButton.addEventListener("click", () => {
        !this.smartTabGroupsOptin
          ? this.#initSmartTabGroupsOptin()
          : this.#handleSmartSuggest();
      });

      // Init Suggestions UI
      this.#suggestionsHeading = this.querySelector(
        "#tab-group-suggestions-heading"
      );
      this.#suggestionsContainer = this.querySelector(
        "#tab-group-suggestions-container"
      );
      this.#suggestions = this.querySelector("#tab-group-suggestions");
      this.#selectSuggestionsCheckbox = this.querySelector(
        "#tab-group-select-checkbox"
      );
      this.#selectSuggestionsCheckbox.addEventListener(
        "CheckboxStateChange",
        () => {
          this.#selectSuggestionsCheckbox.checked
            ? this.#handleSelectAll()
            : this.#handleDeselectAll();
        }
      );
      this.#suggestionsMessageContainer = this.querySelector(
        "#tab-group-suggestions-message-container"
      );
      this.#suggestionsMessage = this.querySelector(
        "#tab-group-suggestions-message"
      );
      this.#suggestionsMessage.iconSrc = this.smartTabGroupsEnabled
        ? MozTabbrowserTabGroupMenu.AI_ICON
        : "";
      this.#createSuggestionsButton = this.querySelector(
        "#tab-group-create-suggestions-button"
      );
      this.#createSuggestionsButton.addEventListener("click", () => {
        this.#handleMlTelemetry("save");
        this.activeGroup.addTabs(this.#selectedSuggestedTabs);
        this.close(true);
      });
      this.#cancelSuggestionsButton = this.querySelector(
        "#tab-group-cancel-suggestions-button"
      );
      this.#cancelSuggestionsButton.addEventListener("click", () => {
        this.#handleMlTelemetry("cancel");
        this.#suggestionsRunToken = null;
        this.close();
      });
      this.#suggestionsSeparator = this.querySelector(
        "#tab-group-suggestions-separator"
      );
      this.#suggestionsOptinContainer = this.querySelector(
        "#tab-group-suggestions-optin-container"
      );

      // Init Loading UI
      this.#suggestionsLoading = this.querySelector(
        "#tab-group-suggestions-loading"
      );
      this.#suggestionsLoadActions = this.querySelector(
        "#tab-group-suggestions-load-actions"
      );
      this.#suggestionsLoadCancel = this.querySelector(
        "#tab-group-suggestions-load-cancel"
      );
      this.#suggestionsLoadCancel.addEventListener("click", () => {
        this.#suggestionsRunToken = null;
        this.#handleLoadSuggestionsCancel();
      });
    }

    #populateSwatches() {
      this.#clearSwatches();
      for (let colorCode of MozTabbrowserTabGroupMenu.COLORS) {
        let input = document.createElement("input");
        input.id = `tab-group-editor-swatch-${colorCode}`;
        input.type = "radio";
        input.name = "tab-group-color";
        input.value = colorCode;
        let label = document.createElement("label");
        label.classList.add("tab-group-editor-swatch");
        label.setAttribute(
          "data-l10n-id",
          MozTabbrowserTabGroupMenu.MESSAGE_IDS[colorCode]
        );
        label.htmlFor = input.id;
        label.style.setProperty(
          "--tabgroup-swatch-color",
          `var(--tab-group-color-${colorCode})`
        );
        label.style.setProperty(
          "--tabgroup-swatch-color-invert",
          `var(--tab-group-color-${colorCode}-invert)`
        );
        this.#swatchesContainer.append(input, label);
        this.#swatches.push(input);
      }
    }

    #clearSwatches() {
      this.#swatchesContainer.innerHTML = "";
      this.#swatches = [];
    }

    get createMode() {
      return this.#createMode;
    }

    set createMode(enableCreateMode) {
      this.#panel.classList.toggle(
        "tab-group-editor-mode-create",
        enableCreateMode
      );
      this.#panel.setAttribute(
        "aria-labelledby",
        enableCreateMode
          ? "tab-group-editor-title-create"
          : "tab-group-editor-title-edit"
      );
      this.#createMode = enableCreateMode;
    }

    get activeGroup() {
      return this.#activeGroup;
    }

    set activeGroup(group = null) {
      this.#activeGroup = group;
      this.#nameField.value = group ? group.label : "";
      this.#swatches.forEach(node => {
        if (group && node.value == group.color) {
          node.checked = true;
        } else {
          node.checked = false;
        }
      });
    }

    get nextUnusedColor() {
      let usedColors = [];
      gBrowser.getAllTabGroups().forEach(group => {
        usedColors.push(group.color);
      });
      let color = MozTabbrowserTabGroupMenu.COLORS.find(
        colorCode => !usedColors.includes(colorCode)
      );
      if (!color) {
        // if all colors are used, pick one randomly
        let randomIndex = Math.floor(
          Math.random() * MozTabbrowserTabGroupMenu.COLORS.length
        );
        color = MozTabbrowserTabGroupMenu.COLORS[randomIndex];
      }
      return color;
    }

    get panel() {
      return this.children[0];
    }

    get #panelPosition() {
      if (gBrowser.tabContainer.verticalMode) {
        return SidebarController._positionStart
          ? "topleft topright"
          : "topright topleft";
      }
      return "bottomleft topleft";
    }

    /**
     * Sets the suggested title for the group
     */
    async #initMlGroupLabel() {
      if (!this.smartTabGroupsEnabled || !this.activeGroup.tabs?.length) {
        return;
      }

      const tabs = this.activeGroup.tabs;
      const otherTabs = gBrowser.visibleTabs.filter(
        t => !tabs.includes(t) && !t.pinned
      );
      let predictedLabel =
        await this.#smartTabGroupingManager.getPredictedLabelForGroup(
          tabs,
          otherTabs
        );
      this.#setMlGroupLabel(predictedLabel);
    }

    /**
     * Check if the label should be updated with the suggested label
     * @returns {boolean}
     */
    #shouldUpdateLabelWithMlLabel() {
      return !this.#nameField.value && this.panel.state !== "closed";
    }

    /**
     * Attempt to set the label of the group to the suggested label
     * @param {MozTabbrowserTabGroup} group
     * @param {string} newLabel
     * @returns
     */
    #setMlGroupLabel(newLabel) {
      if (!this.#shouldUpdateLabelWithMlLabel()) {
        return;
      }
      this.#activeGroup.label = newLabel;
      this.#nameField.value = newLabel;
      this.#nameField.select();
      this.#suggestedMlLabel = newLabel;
    }

    openCreateModal(group) {
      this.activeGroup = group;
      this.createMode = true;
      this.suggestionState = this.smartTabGroupsEnabled
        ? MozTabbrowserTabGroupMenu.State.CREATE_AI_INITIAL
        : MozTabbrowserTabGroupMenu.State.CREATE_STANDARD_INITIAL;

      this.#panel.openPopup(group.firstChild, {
        position: this.#panelPosition,
      });
      // If user has opted in kick off label generation
      this.smartTabGroupsOptin && this.#initMlGroupLabel();
    }

    /*
     * Set the ml generated label - used for testing
     */
    set mlLabel(label) {
      this.#suggestedMlLabel = label;
    }

    get mlLabel() {
      return this.#suggestedMlLabel;
    }

    /*
     * Set if the ml suggest tab flow was done
     */
    set hasSuggestedMlTabs(suggested) {
      this.#hasSuggestedMlTabs = suggested;
    }

    get hasSuggestedMlTabs() {
      return this.#hasSuggestedMlTabs;
    }

    openEditModal(group) {
      this.activeGroup = group;
      this.createMode = false;
      this.suggestionState = this.smartTabGroupsEnabled
        ? MozTabbrowserTabGroupMenu.State.EDIT_AI_INITIAL
        : MozTabbrowserTabGroupMenu.State.EDIT_STANDARD_INITIAL;

      this.#panel.openPopup(group.firstChild, {
        position: this.#panelPosition,
      });
      document.getElementById("tabGroupEditor_moveGroupToNewWindow").disabled =
        gBrowser.openTabs.length == this.activeGroup?.tabs.length;
      this.#maybeDisableOrHideSaveButton();
    }

    #maybeDisableOrHideSaveButton() {
      const saveAndCloseGroup = document.getElementById(
        "tabGroupEditor_saveAndCloseGroup"
      );
      if (PrivateBrowsingUtils.isWindowPrivate(this.ownerGlobal)) {
        saveAndCloseGroup.hidden = true;
        return;
      }

      let flushes = [];
      this.activeGroup.tabs.forEach(tab => {
        flushes.push(TabStateFlusher.flush(tab.linkedBrowser));
      });
      Promise.allSettled(flushes).then(() => {
        saveAndCloseGroup.disabled = !SessionStore.shouldSaveTabGroup(
          this.activeGroup
        );
      });
    }

    close(keepNewlyCreatedGroup = true) {
      if (this.createMode) {
        this.#keepNewlyCreatedGroup = keepNewlyCreatedGroup;
      }
      this.#panel.hidePopup();
    }

    on_popupshown() {
      if (this.createMode) {
        this.#keepNewlyCreatedGroup = true;
      }
      this.#initialTabGroupName = this.activeGroup?.label;
      this.#nameField.focus();

      for (const button of Object.values(this.#commandButtons)) {
        button.tooltipText = button.label;
      }
    }

    on_popuphidden() {
      if (this.createMode) {
        if (this.#keepNewlyCreatedGroup) {
          this.dispatchEvent(
            new CustomEvent("TabGroupCreateDone", { bubbles: true })
          );
          if (
            this.smartTabGroupsEnabled &&
            this.smartTabGroupsOptin &&
            (this.#suggestedMlLabel !== null || this.#hasSuggestedMlTabs)
          ) {
            this.#handleMlTelemetry("save-popup-hidden");
          }
        } else {
          this.activeGroup.ungroupTabs();
        }
      }
      if (this.#nameField.disabled) {
        this.#setFormToDisabled(false);
      }
      if (this.activeGroup?.label != this.#initialTabGroupName) {
        Glean.tabgroup.groupInteractions.rename.add(1);
      }
      this.activeGroup = null;
      this.#smartTabGroupingManager.terminateProcess();
    }

    on_keypress(event) {
      if (event.defaultPrevented) {
        // The event has already been consumed inside of the panel.
        return;
      }

      switch (event.keyCode) {
        case KeyEvent.DOM_VK_ESCAPE:
          this.close(false);
          break;
        case KeyEvent.DOM_VK_RETURN:
          // When focus is on a button, we need to let that handle the Enter key,
          // which should ultimately close the panel as well.
          if (
            event.target.localName != "toolbarbutton" &&
            event.target.localName != "moz-button"
          ) {
            this.close();
          }
          break;
      }
    }

    /**
     * change handler for color input
     */
    on_change(aEvent) {
      if (aEvent.target.name != "tab-group-color") {
        return;
      }
      if (this.activeGroup) {
        this.activeGroup.color = aEvent.target.value;
        Glean.tabgroup.groupInteractions.change_color.add(1);
      }
    }

    async #handleNewTabInGroup() {
      let lastTab = this.activeGroup?.tabs.at(-1);
      let onTabOpened = async aEvent => {
        this.activeGroup?.addTabs([aEvent.target]);
        this.close();
        window.removeEventListener("TabOpen", onTabOpened);
      };
      window.addEventListener("TabOpen", onTabOpened);
      gBrowser.addAdjacentNewTab(lastTab);
    }

    /**
     * @param {number} newState - See MozTabbrowserTabGroupMenu.State
     */
    set suggestionState(newState) {
      if (this.#suggestionState === newState) {
        return;
      }
      this.#suggestionState = newState;
      this.#renderSuggestionState();
    }

    #handleLoadSuggestionsCancel() {
      this.#suggestionsRunToken = null;

      this.suggestionState = this.createMode
        ? MozTabbrowserTabGroupMenu.State.CREATE_AI_INITIAL
        : MozTabbrowserTabGroupMenu.State.EDIT_AI_INITIAL;
    }

    #handleSelectAll() {
      document
        .querySelectorAll(".tab-group-suggestion-checkbox")
        .forEach(checkbox => {
          checkbox.checked = true;
        });
      // Reset selected tabs to all suggested tabs
      this.#selectedSuggestedTabs = this.#suggestedTabs;
    }

    #handleDeselectAll() {
      document
        .querySelectorAll(".tab-group-suggestion-checkbox")
        .forEach(checkbox => {
          checkbox.checked = false;
        });
      this.#selectedSuggestedTabs = [];
    }

    /**
     * Set the state of the form to disabled or enabled
     * @param {boolean} state
     */
    #setFormToDisabled(state) {
      const toolbarButtons =
        this.#tabGroupMain.querySelectorAll("toolbarbutton");

      toolbarButtons.forEach(button => {
        button.disabled = state;
      });

      this.#nameField.disabled = state;

      const swatches = this.#swatchesContainer.querySelectorAll("input");
      swatches.forEach(input => {
        input.disabled = state;
      });
    }

    async #handleFirstDownloadAndSuggest() {
      this.#setFormToDisabled(true);
      this.#suggestionsOptin.headingL10nId =
        "tab-group-suggestions-optin-title-download";
      this.#suggestionsOptin.messageL10nId =
        "tab-group-suggestions-optin-message-download";
      this.#suggestionsOptin.headingIcon = "";
      this.#suggestionsOptin.isLoading = true;

      // Init progress with value to show determiniate progress
      this.#suggestionsOptin.progressStatus = 0;
      const runToken = Date.now();
      this.#suggestionsRunToken = runToken;
      await this.#smartTabGroupingManager.preloadAllModels(prog => {
        this.#suggestionsOptin.progressStatus = prog.percentage;
      });
      // Clean up optin UI
      this.#setFormToDisabled(false);
      this.#suggestionsOptin.isHidden = true;
      this.#suggestionsOptin.isLoading = false;

      if (runToken !== this.#suggestionsRunToken) {
        // User has canceled
        return;
      }

      // Continue on with the suggest flow
      this.#handleMLOptinTelemetry("step3-optin-completed");
      this.#initMlGroupLabel();
      this.#handleSmartSuggest();
    }

    async #handleSmartSuggest() {
      // Loading
      const runToken = Date.now();
      this.#suggestionsRunToken = runToken;

      this.suggestionState = MozTabbrowserTabGroupMenu.State.LOADING;
      const tabs = await this.#smartTabGroupingManager.smartTabGroupingForGroup(
        this.activeGroup,
        gBrowser.tabs
      );
      if (this.#suggestionsRunToken != runToken) {
        // User has canceled
        return;
      }
      if (!tabs.length) {
        // No un-grouped tabs found
        this.suggestionState = this.#createMode
          ? MozTabbrowserTabGroupMenu.State.CREATE_AI_WITH_NO_SUGGESTIONS
          : MozTabbrowserTabGroupMenu.State.EDIT_AI_WITH_NO_SUGGESTIONS;

        // there's no "save" button from the edit ai interaction with
        // no tab suggestions, so we need to capture here
        if (!this.#createMode) {
          this.#hasSuggestedMlTabs = true;
          this.#handleMlTelemetry("save");
        }
        return;
      }

      this.#selectedSuggestedTabs = tabs;
      this.#suggestedTabs = tabs;
      tabs.forEach((tab, index) => {
        this.#createRow(tab, index);
      });

      this.suggestionState = this.#createMode
        ? MozTabbrowserTabGroupMenu.State.CREATE_AI_WITH_SUGGESTIONS
        : MozTabbrowserTabGroupMenu.State.EDIT_AI_WITH_SUGGESTIONS;

      this.#hasSuggestedMlTabs = true;
    }

    /**
     * Sends Glean metrics if smart tab grouping is enabled
     * @param {string} action "save", "save-popup-hidden" or "cancel"
     */
    #handleMlTelemetry(action) {
      if (!this.smartTabGroupsEnabled || !this.smartTabGroupsOptin) {
        return;
      }
      if (this.#suggestedMlLabel !== null) {
        this.#smartTabGroupingManager.handleLabelTelemetry({
          action,
          numTabsInGroup: this.#activeGroup.tabs.length,
          mlLabel: this.#suggestedMlLabel,
          userLabel: this.#nameField.value,
          id: this.#activeGroup.id,
        });
        this.#suggestedMlLabel = null;
      }
      if (this.#hasSuggestedMlTabs) {
        this.#smartTabGroupingManager.handleSuggestTelemetry({
          action,
          numTabsInWindow: gBrowser.tabs.length,
          numTabsInGroup: this.#activeGroup.tabs.length,
          numTabsSuggested: this.#suggestedTabs.length,
          numTabsApproved: this.#selectedSuggestedTabs.length,
          numTabsRemoved:
            this.#suggestedTabs.length - this.#selectedSuggestedTabs.length,
          id: this.#activeGroup.id,
        });
        this.#hasSuggestedMlTabs = false;
      }
    }

    /**
     * Sends Glean metrics for opt-in UI flow
     * @param {string} step contains step number and description of flow
     */
    #handleMLOptinTelemetry(step) {
      Glean.tabgroup.smartTabOptin.record({
        step,
      });
    }

    #createRow(tab, index) {
      // Create Row
      let row = document.createXULElement("toolbaritem");
      row.setAttribute("context", "tabContextMenu");
      row.setAttribute("id", `tab-bar-${index}`);

      // Create Checkbox
      let checkbox = document.createXULElement("checkbox");
      checkbox.value = tab;
      checkbox.setAttribute("checked", true);
      checkbox.classList.add("tab-group-suggestion-checkbox");
      checkbox.addEventListener("CheckboxStateChange", e => {
        const isChecked = e.target.checked;
        const currentTab = e.target.value;

        if (isChecked) {
          this.#selectedSuggestedTabs.push(currentTab);
        } else {
          this.#selectedSuggestedTabs = this.#selectedSuggestedTabs.filter(
            t => t != currentTab
          );
        }
      });

      row.appendChild(checkbox);

      // Create Row Label
      let label = document.createXULElement("toolbarbutton");
      label.classList.add(
        "all-tabs-button",
        "subviewbutton",
        "subviewbutton-iconic",
        "tab-group-suggestion-label"
      );
      label.setAttribute("flex", "1");
      label.setAttribute("crop", "end");
      label.label = tab.label;
      label.image = tab.image;
      label.disabled = true;
      row.appendChild(label);

      // Apply Row to Suggestions
      this.#suggestions.appendChild(row);
    }

    /**
     * Element visibility utility function.
     * Toggles the `hidden` attribute of a DOM element.
     *
     * @param {HTMLElement|XULElement} element - The DOM element to show/hide.
     * @param {boolean} shouldShow - Whether the element should be shown (true) or hidden (false).
     */
    #setElementVisibility(element, shouldShow) {
      element.hidden = !shouldShow;
    }

    #showDefaultTabGroupActions(value) {
      this.#setElementVisibility(this.#defaultActions, value);
    }

    #showSmartSuggestionsContainer(value) {
      this.#setElementVisibility(this.#suggestionsContainer, value);
    }

    #showSuggestionButton(value) {
      this.#setElementVisibility(this.#suggestionButton, value);
    }

    #showSuggestionMessageContainer(value) {
      this.#setElementVisibility(this.#suggestionsMessageContainer, value);
    }

    #showSuggestionsSeparator(value) {
      this.#setElementVisibility(this.#suggestionsSeparator, value);
    }

    #setLoadingState(value) {
      this.#setElementVisibility(this.#suggestionsLoadActions, value);
      this.#setElementVisibility(this.#suggestionsLoading, value);
    }

    #setSuggestionsButtonCreateModeState(value) {
      const translationString = value
        ? "tab-group-editor-smart-suggest-button-create"
        : "tab-group-editor-smart-suggest-button-edit";

      this.#suggestionButton.setAttribute("data-l10n-id", translationString);
    }

    /**
     * Unique state setter for a "3rd" panel state while in suggest Mode
     * that just shows suggestions and hides the majority of the panel
     * @param {boolean} value
     */
    #setSuggestModeSuggestionState(value) {
      this.#setElementVisibility(this.#tabGroupMain, !value);
      this.#setElementVisibility(this.#suggestionsHeading, value);
      this.#setElementVisibility(this.#defaultHeader, !value);
      this.#panel.classList.toggle("tab-group-editor-panel-expanded", value);
    }

    #resetCommonUI() {
      this.#setLoadingState(false);
      this.#setSuggestModeSuggestionState(false);
      this.#suggestedTabs = [];
      this.#selectedSuggestedTabs = [];
      this.#suggestions.innerHTML = "";
      this.#showSmartSuggestionsContainer(false);
      this.#suggestionsOptinContainer.innerHTML = "";
    }

    #renderSuggestionState() {
      switch (this.#suggestionState) {
        // CREATE STANDARD INITIAL
        case MozTabbrowserTabGroupMenu.State.CREATE_STANDARD_INITIAL:
          this.#resetCommonUI();
          this.#showDefaultTabGroupActions(true);
          this.#showSuggestionButton(false);
          this.#showSuggestionMessageContainer(false);
          this.#showSuggestionsSeparator(false);
          break;

        //CREATE AI INITIAL
        case MozTabbrowserTabGroupMenu.State.CREATE_AI_INITIAL:
          this.#resetCommonUI();
          this.#showSuggestionButton(true);
          this.#showDefaultTabGroupActions(true);
          this.#showSuggestionMessageContainer(false);
          this.#setSuggestionsButtonCreateModeState(true);
          this.#showSuggestionsSeparator(true);
          break;

        // CREATE AI INITIAL SUGGESTIONS DISABLED
        case MozTabbrowserTabGroupMenu.State
          .CREATE_AI_INITIAL_SUGGESTIONS_DISABLED:
          this.#resetCommonUI();
          this.#showSuggestionButton(false);
          this.#showSuggestionMessageContainer(true);
          this.#showDefaultTabGroupActions(true);
          this.#showSuggestionsSeparator(true);
          break;

        // CREATE AI WITH SUGGESTIONS
        case MozTabbrowserTabGroupMenu.State.CREATE_AI_WITH_SUGGESTIONS:
          this.#setLoadingState(false);
          this.#showSmartSuggestionsContainer(true);
          this.#showSuggestionButton(false);
          this.#showSuggestionsSeparator(true);
          this.#showDefaultTabGroupActions(false);
          this.#setSuggestModeSuggestionState(true);
          break;

        // CREATE AI WITH NO SUGGESTIONS
        case MozTabbrowserTabGroupMenu.State.CREATE_AI_WITH_NO_SUGGESTIONS:
          this.#setLoadingState(false);
          this.#showSuggestionMessageContainer(true);
          this.#showDefaultTabGroupActions(true);
          this.#showSuggestionButton(false);
          this.#showSuggestionsSeparator(true);
          break;

        // EDIT STANDARD INITIAL
        case MozTabbrowserTabGroupMenu.State.EDIT_STANDARD_INITIAL:
          this.#resetCommonUI();
          this.#showSuggestionButton(false);
          this.#showSuggestionMessageContainer(false);
          this.#showDefaultTabGroupActions(false);
          this.#showSuggestionsSeparator(false);
          break;

        // EDIT AI INITIAL
        case MozTabbrowserTabGroupMenu.State.EDIT_AI_INITIAL:
          this.#resetCommonUI();
          this.#showSuggestionMessageContainer(false);
          this.#showSuggestionButton(true);
          this.#showDefaultTabGroupActions(false);
          this.#setSuggestionsButtonCreateModeState(false);
          this.#showSuggestionsSeparator(true);
          break;

        // EDIT AI INITIAL SUGGESTIONS DISABLED
        case MozTabbrowserTabGroupMenu.State
          .EDIT_AI_INITIAL_SUGGESTIONS_DISABLED:
          this.#resetCommonUI();
          this.#showSuggestionMessageContainer(true);
          this.#showSuggestionButton(false);
          this.#showDefaultTabGroupActions(false);
          this.#showSuggestionsSeparator(true);
          break;

        // EDIT AI WITH SUGGESTIONS
        case MozTabbrowserTabGroupMenu.State.EDIT_AI_WITH_SUGGESTIONS:
          this.#setLoadingState(false);
          this.#showSmartSuggestionsContainer(true);
          this.#setSuggestModeSuggestionState(true);
          this.#showSuggestionsSeparator(false);
          break;

        // EDIT AI WITH NO SUGGESTIONS
        case MozTabbrowserTabGroupMenu.State.EDIT_AI_WITH_NO_SUGGESTIONS:
          this.#setLoadingState(false);
          this.#showSuggestionMessageContainer(true);
          this.#showSuggestionsSeparator(true);
          break;

        // LOADING
        case MozTabbrowserTabGroupMenu.State.LOADING:
          this.#showDefaultTabGroupActions(false);
          this.#showSuggestionButton(false);
          this.#showSuggestionMessageContainer(false);
          this.#setLoadingState(true);
          this.#showSuggestionsSeparator(true);
          this.#showDefaultTabGroupActions(false);
          break;

        // ERROR
        case MozTabbrowserTabGroupMenu.State.ERROR:
          //TODO
          break;

        case MozTabbrowserTabGroupMenu.State.OPTIN:
          this.#showSuggestionButton(false);
          this.#showDefaultTabGroupActions(false);
          break;
      }
    }
  }

  customElements.define("tabgroup-menu", MozTabbrowserTabGroupMenu);
}
