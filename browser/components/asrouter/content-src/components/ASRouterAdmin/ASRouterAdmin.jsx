/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { ASRouterUtils } from "../../asrouter-utils.mjs";
import React from "react";
import ReactDOM from "react-dom";
import { SimpleHashRouter } from "./SimpleHashRouter";
import { CopyButton } from "./CopyButton";
import { ImpressionsSection } from "./ImpressionsSection";

// Convert a UTF-8 string to a string in which only one byte of each
// 16-bit unit is occupied. This is necessary to comply with `btoa` API constraints.
export function toBinary(string) {
  const codeUnits = new Uint16Array(string.length);
  for (let i = 0; i < codeUnits.length; i++) {
    codeUnits[i] = string.charCodeAt(i);
  }
  return btoa(
    String.fromCharCode(...Array.from(new Uint8Array(codeUnits.buffer)))
  );
}

function relativeTime(timestamp) {
  if (!timestamp) {
    return "";
  }
  const seconds = Math.floor((Date.now() - timestamp) / 1000);
  const minutes = Math.floor((Date.now() - timestamp) / 60000);
  if (seconds < 2) {
    return "just now";
  } else if (seconds < 60) {
    return `${seconds} seconds ago`;
  } else if (minutes === 1) {
    return "1 minute ago";
  } else if (minutes < 600) {
    return `${minutes} minutes ago`;
  }
  return new Date(timestamp).toLocaleString();
}

const PROVIDER_SECTION_ORDER = [
  "local",
  "remote-experiments",
  "cfr",
  "panel_local_testing",
  "other",
];

export class ToggleMessageJSON extends React.PureComponent {
  constructor(props) {
    super(props);
    this.handleClick = this.handleClick.bind(this);
  }

  handleClick() {
    this.props.toggleJSON(this.props.msgId);
  }

  render() {
    let direction = this.props.isCollapsed ? "forward" : "down";
    return (
      <button
        className="message-title-toggle"
        aria-expanded={!this.props.isCollapsed}
        onClick={this.handleClick}
      >
        <span className={`icon small icon-arrowhead-${direction}`} />
        {this.props.children}
      </button>
    );
  }
}

export class ASRouterAdminInner extends React.PureComponent {
  constructor(props) {
    super(props);
    this.handleEnabledToggle = this.handleEnabledToggle.bind(this);
    this.handleUserPrefToggle = this.handleUserPrefToggle.bind(this);
    this.onChangeFilters = this.onChangeFilters.bind(this);
    this.onClearFilters = this.onClearFilters.bind(this);
    this.unblockAll = this.unblockAll.bind(this);
    this.resetAllJSON = this.resetAllJSON.bind(this);
    this.handleExpressionEval = this.handleExpressionEval.bind(this);
    this.onChangeTargetingParameters =
      this.onChangeTargetingParameters.bind(this);
    this.onChangeAttributionParameters =
      this.onChangeAttributionParameters.bind(this);
    this.setAttribution = this.setAttribution.bind(this);
    this.onCopyTargetingParams = this.onCopyTargetingParams.bind(this);
    this.onNewTargetingParams = this.onNewTargetingParams.bind(this);
    this.resetMessageState = this.resetMessageState.bind(this);
    this.toggleJSON = this.toggleJSON.bind(this);
    this.toggleAllMessages = this.toggleAllMessages.bind(this);
    this.resetGroupImpressions = this.resetGroupImpressions.bind(this);
    this.onMessageFromParent = this.onMessageFromParent.bind(this);
    this.setStateFromParent = this.setStateFromParent.bind(this);
    this.setState = this.setState.bind(this);
    this.state = {
      filterGroups: [],
      filterProviders: [],
      filterTemplates: [],
      collapsedMessages: [],
      collapsedProviders: [],
      modifiedMessages: [],
      messageBlockList: [],
      multiProfileMessageBlocklist: [],
      evaluationStatus: {},
      stringTargetingParameters: null,
      newStringTargetingParameters: null,
      copiedToClipboard: false,
      attributionParameters: {
        source: "addons.mozilla.org",
        medium: "referral",
        campaign: "non-fx-button",
        content: `rta:${btoa("uBlock0@raymondhill.net")}`,
        experiment: "ua-onboarding",
        variation: "chrome",
        ua: "Google Chrome 123",
        dltoken: "00000000-0000-0000-0000-000000000000",
      },
    };
  }

  onMessageFromParent({ type, data }) {
    switch (type) {
      case "UpdateAdminState": {
        this.setStateFromParent(data);
        break;
      }
    }
  }

  async setStateFromParent(data) {
    await this.setState(data);
    if (!this.state.stringTargetingParameters) {
      const stringTargetingParameters = {};
      for (const param of Object.keys(data.targetingParameters)) {
        stringTargetingParameters[param] = JSON.stringify(
          data.targetingParameters[param],
          null,
          2
        );
      }
      await this.setState({ stringTargetingParameters });
    }
  }

  componentWillMount() {
    ASRouterUtils.addListener(this.onMessageFromParent);
    const endpoint = ASRouterUtils.getPreviewEndpoint();
    ASRouterUtils.sendMessage({
      type: "ADMIN_CONNECT_STATE",
      data: { endpoint },
    }).then(this.setStateFromParent);
  }

  componentWillUnmount() {
    ASRouterUtils.removeListener(this.onMessageFromParent);
  }

  handleBlock(msg) {
    ASRouterUtils.blockById(msg.id);
  }

  handleUnblock(msg) {
    ASRouterUtils.unblockById(msg.id);
  }

  // Messages are identified by their index in state.messages rather than by
  // id, because some messages share the same id
  messageTextareaId(messageIndex) {
    return `msg-${messageIndex}-textarea`;
  }

  resetJSON(msg, messageIndex) {
    // reset the displayed JSON for the given message
    const textarea = document.getElementById(
      this.messageTextareaId(messageIndex)
    );
    textarea.value = JSON.stringify(msg, null, 2);
    textarea.classList.remove("errorState");
    this.setState(prevState => ({
      modifiedMessages: prevState.modifiedMessages.filter(
        id => id !== messageIndex
      ),
    }));
  }

  resetAllJSON() {
    // reset the displayed JSON for each modified message
    for (const messageIndex of this.state.modifiedMessages) {
      const msg = this.state.messages[messageIndex];
      const textarea = document.getElementById(
        this.messageTextareaId(messageIndex)
      );
      if (msg && textarea) {
        textarea.value = JSON.stringify(msg, null, 2);
        textarea.classList.remove("errorState");
      }
    }
    this.setState({ modifiedMessages: [] });
  }

  showMessage(msg, messageIndex) {
    const isModified = this.state.modifiedMessages.includes(messageIndex);
    const message = isModified
      ? JSON.parse(
          document.getElementById(this.messageTextareaId(messageIndex)).value
        )
      : msg;
    if (message.template === "pb_newtab") {
      ASRouterUtils.openPBWindow(message.content);
      return;
    }
    const request = isModified
      ? ASRouterUtils.modifyMessageJson(message)
      : ASRouterUtils.overrideMessage(msg.id);
    request.then(state => this.setStateFromParent(state));
  }

  messageRequiresAnchor(msg) {
    const anchorTemplates = ["bookmarks_bar_button"];
    return (
      anchorTemplates.includes(msg.template) ||
      !!msg.content?.anchors ||
      !!msg.content?.screens?.some(screenDef => screenDef.anchors)
    );
  }

  getProviderLabel(providerId) {
    if (providerId === "panel_local_testing") {
      return "Test Messages (PanelTestProvider)";
    }
    const provider =
      this.state.providerPrefs?.find(p => p.id === providerId) || {};
    if (provider.type === "remote-experiments") {
      return "Nimbus (messaging-experiments)";
    }
    if (provider.type === "local") {
      return "Local (onboarding)";
    }
    if (provider.type === "remote-settings") {
      return `Remote Settings (${providerId})`;
    }
    return providerId;
  }

  async resetMessageState() {
    await Promise.all([
      ASRouterUtils.resetMessageImpressions(),
      ASRouterUtils.resetGroupImpressions(),
      ASRouterUtils.resetScreenImpressions(),
      ASRouterUtils.unblockAll(),
    ]);
    let data = await ASRouterUtils.sendMessage({
      type: "ADMIN_CONNECT_STATE",
      data: { endpoint: ASRouterUtils.getPreviewEndpoint() },
    });
    await this.setStateFromParent(data);
  }

  expireCache() {
    ASRouterUtils.sendMessage({ type: "EXPIRE_QUERY_CACHE" });
  }

  resetPref() {
    ASRouterUtils.sendMessage({ type: "RESET_PROVIDER_PREF" });
  }

  resetGroupImpressions() {
    ASRouterUtils.resetGroupImpressions().then(this.setStateFromParent);
  }

  resetMessageImpressions() {
    ASRouterUtils.resetMessageImpressions().then(this.setStateFromParent);
  }

  handleExpressionEval() {
    const context = {};
    for (const param of Object.keys(this.state.stringTargetingParameters)) {
      const value = this.state.stringTargetingParameters[param];
      context[param] = value ? JSON.parse(value) : null;
    }
    ASRouterUtils.sendMessage({
      type: "EVALUATE_JEXL_EXPRESSION",
      data: {
        expression: this.refs.expressionInput.value || "undefined",
        context,
      },
    }).then(this.setStateFromParent);
  }

  onChangeTargetingParameters(event) {
    const { name: eventName } = event.target;
    const { value } = event.target;

    let targetingParametersError = null;
    try {
      JSON.parse(value);
      event.target.classList.remove("errorState");
    } catch (e) {
      console.error(`Error parsing value of parameter ${eventName}`);
      event.target.classList.add("errorState");
      targetingParametersError = { id: eventName };
    }

    this.setState(({ stringTargetingParameters }) => {
      const updatedParameters = { ...stringTargetingParameters };
      updatedParameters[eventName] = value;

      return {
        copiedToClipboard: false,
        evaluationStatus: {},
        stringTargetingParameters: updatedParameters,
        targetingParametersError,
      };
    });
  }

  unblockAll() {
    return ASRouterUtils.unblockAll().then(this.setStateFromParent);
  }

  async handleEnabledToggle(event) {
    const provider = this.state.providerPrefs.find(
      p => p.id === event.target.dataset.provider
    );
    const userPrefInfo = this.state.userPrefs;

    const isUserEnabled =
      provider.id in userPrefInfo ? userPrefInfo[provider.id] : true;
    const isSystemEnabled = provider.enabled;
    const isEnabling = event.target.checked;

    if (isEnabling) {
      if (!isUserEnabled) {
        await ASRouterUtils.sendMessage({
          type: "SET_PROVIDER_USER_PREF",
          data: { id: provider.id, value: true },
        });
      }
      if (!isSystemEnabled) {
        await ASRouterUtils.sendMessage({
          type: "ENABLE_PROVIDER",
          data: provider.id,
        });
      }
    } else {
      await ASRouterUtils.sendMessage({
        type: "DISABLE_PROVIDER",
        data: provider.id,
      });
    }

    this.setState({ filterProviders: [] });
  }

  handleUserPrefToggle(event) {
    const action = {
      type: "SET_PROVIDER_USER_PREF",
      data: { id: event.target.dataset.provider, value: event.target.checked },
    };
    ASRouterUtils.sendMessage(action);
    this.setState({ filterProviders: [] });
  }

  onChangeFilters(event) {
    // this function handles both provider filter and group filter. the checkbox
    // will have dataset.provider if it's a provider checkbox, and dataset.group
    // if it's a group checkbox.
    let stateKey;
    let itemValue;
    let { checked } = event.target;
    if (event.target.dataset.provider) {
      stateKey = "filterProviders";
      itemValue = event.target.dataset.provider;
    } else if (event.target.dataset.template) {
      stateKey = "filterTemplates";
      itemValue = event.target.dataset.template;
    } else if (event.target.dataset.group) {
      stateKey = "filterGroups";
      itemValue = event.target.dataset.group;
    } else {
      return;
    }
    this.setState(prevState => {
      let newValue;
      if (checked) {
        newValue = prevState[stateKey].includes(itemValue)
          ? prevState[stateKey]
          : prevState[stateKey].concat(itemValue);
      } else {
        newValue = prevState[stateKey].filter(item => item !== itemValue);
      }
      return { [stateKey]: newValue };
    });
  }

  onClearFilters() {
    this.setState({
      filterProviders: [],
      filterGroups: [],
      filterTemplates: [],
    });
  }

  // Simulate a copy event that sets to clipboard all targeting paramters and values
  onCopyTargetingParams() {
    const stringTargetingParameters = {
      ...this.state.stringTargetingParameters,
    };
    for (const key of Object.keys(stringTargetingParameters)) {
      // If the value is not set the parameter will be lost when we stringify
      if (stringTargetingParameters[key] === undefined) {
        stringTargetingParameters[key] = null;
      }
    }
    const setClipboardData = e => {
      e.preventDefault();
      e.clipboardData.setData(
        "text",
        JSON.stringify(stringTargetingParameters, null, 2)
      );
      document.removeEventListener("copy", setClipboardData);
      this.setState({ copiedToClipboard: true });
    };

    document.addEventListener("copy", setClipboardData);

    document.execCommand("copy");
  }

  onNewTargetingParams(event) {
    this.setState({ newStringTargetingParameters: event.target.value });
    event.target.classList.remove("errorState");
    this.refs.targetingParamsEval.innerText = "";

    try {
      const stringTargetingParameters = JSON.parse(event.target.value);
      this.setState({ stringTargetingParameters });
    } catch (e) {
      event.target.classList.add("errorState");
      this.refs.targetingParamsEval.innerText = e.message;
    }
  }

  toggleJSON(msgId) {
    if (this.state.collapsedMessages.includes(msgId)) {
      let index = this.state.collapsedMessages.indexOf(msgId);
      this.setState(prevState => ({
        collapsedMessages: [
          ...prevState.collapsedMessages.slice(0, index),
          ...prevState.collapsedMessages.slice(index + 1),
        ],
      }));
    } else {
      this.setState(prevState => ({
        collapsedMessages: prevState.collapsedMessages.concat(msgId),
      }));
    }
  }

  onMessageChanged(msgId) {
    if (!this.state.modifiedMessages.includes(msgId)) {
      this.setState(prevState => ({
        modifiedMessages: prevState.modifiedMessages.concat(msgId),
      }));
    }
  }

  renderMessageItem(msg) {
    const messageIndex = this.state.messages.indexOf(msg);
    const textareaId = this.messageTextareaId(messageIndex);
    const isBlockedByGroup = this.state.groups
      .filter(group => msg.groups.includes(group.id))
      .some(group => !group.enabled);
    const msgProvider =
      this.state.providers.find(provider => provider.id === msg.provider) || {};
    const isProviderExcluded =
      msgProvider.exclude && msgProvider.exclude.includes(msg.id);
    const isMessageBlocked =
      this.state.messageBlockList.includes(msg.id) ||
      this.state.messageBlockList.includes(msg.campaign) ||
      this.state.multiProfileMessageBlocklist.includes(msg.id);
    const isBlocked =
      isMessageBlocked || isBlockedByGroup || isProviderExcluded;
    const impressions = this.state.messageImpressions[msg.id]
      ? this.state.messageImpressions[msg.id].length
      : 0;
    const isCollapsed = this.state.collapsedMessages.includes(messageIndex);
    const isModified = this.state.modifiedMessages.includes(messageIndex);
    const previewWarnings = [];
    if (this.messageRequiresAnchor(msg)) {
      previewWarnings.push(
        "This message may not render as it anchors to a browser UI element that may not be " +
          "currently visible."
      );
    }
    if (msg.template === "toast_notification") {
      previewWarnings.push(
        "Toast notifications are shown and limited to native Windows OS notifications via the system alerts service. "
      );
    }
    if (msg.template === "menu_message") {
      previewWarnings.push(
        'To preview this message, add "testingTriggerContext" to the JSON set to either ' +
          '"app_menu" or "pxi_menu" to select which menu it renders in.'
      );
    }
    const aboutMessagePreviewSupported = [
      "infobar",
      "spotlight",
      "feature_callout",
      "pb_newtab",
      "sidebar_chatbot_promo",
    ].includes(msg.template);

    let itemClassName = "message-item";
    if (isBlocked) {
      itemClassName += " blocked";
    }
    if (isCollapsed) {
      itemClassName += " collapsed";
    }

    let messageStats = [];
    let messageStatsString;
    if (impressions) {
      messageStats.push(`${impressions} impressions`);
    }
    if (isMessageBlocked) {
      messageStats.push("message blocked");
    } else if (isBlockedByGroup) {
      messageStats.push("message group blocked");
    } else if (isProviderExcluded) {
      messageStats.push("excluded by provider");
    }
    if (messageStats.length) {
      messageStatsString = `(${messageStats.join(", ")})`;
    }

    return (
      <div className={itemClassName} key={textareaId}>
        <ToggleMessageJSON
          msgId={messageIndex}
          toggleJSON={this.toggleJSON}
          isCollapsed={isCollapsed}
        >
          {previewWarnings.map(warning => (
            <span
              key={warning}
              className="icon icon-warning preview-warning"
              role="img"
              aria-label={warning}
              title={warning}
            />
          ))}
          <span className="message-id monospace">{msg.id}</span>{" "}
          <span className="message-stats small-text">{messageStatsString}</span>
        </ToggleMessageJSON>
        {isCollapsed ? null : (
          <div className="button-box message-actions">
            {isBlocked ? null : (
              <button
                className="primary show"
                onClick={() => this.showMessage(msg, messageIndex)}
              >
                Show
              </button>
            )}
            {isBlocked || !isModified ? null : (
              <button
                className="restore"
                onClick={() => this.resetJSON(msg, messageIndex)}
              >
                Reset
              </button>
            )}
            {aboutMessagePreviewSupported ? (
              <CopyButton
                transformer={text =>
                  `about:messagepreview?json=${encodeURIComponent(
                    toBinary(text)
                  )}`
                }
                label="Share"
                copiedLabel="Copied!"
                inputSelector={`#${textareaId}`}
                className={"share"}
              />
            ) : null}
            <button
              className={`button${isBlocked ? " primary" : ""}`}
              onClick={() =>
                isBlocked ? this.handleUnblock(msg) : this.handleBlock(msg)
              }
            >
              {isBlocked ? "Unblock" : "Block"}
            </button>
          </div>
        )}
        <pre className={isCollapsed ? "collapsed" : "expanded"}>
          <textarea
            id={textareaId}
            name={msg.id}
            className="message-textarea"
            disabled={isBlocked}
            rows="30"
            onChange={event => {
              try {
                JSON.parse(event.target.value);
                event.target.classList.remove("errorState");
              } catch (e) {
                event.target.classList.add("errorState");
              }
              this.onMessageChanged(messageIndex);
            }}
            spellCheck="false"
          >
            {JSON.stringify(msg, null, 2)}
          </textarea>
        </pre>
      </div>
    );
  }

  isAnythingCollapsed() {
    return (
      this.state.collapsedMessages.length ||
      this.state.collapsedProviders.length
    );
  }

  toggleAllMessages(messagesToShow) {
    if (this.isAnythingCollapsed()) {
      this.setState({ collapsedMessages: [], collapsedProviders: [] });
    } else {
      const collapsedMessages = messagesToShow.map(msg =>
        this.state.messages.indexOf(msg)
      );
      const collapsedProviders = [
        ...new Set(messagesToShow.map(msg => msg.provider)),
      ];
      this.setState({ collapsedMessages, collapsedProviders });
    }
  }

  filterMessages() {
    let messages = [...this.state.messages];
    if (this.state.filterProviders.length) {
      messages = messages.filter(msg =>
        this.state.filterProviders.includes(msg.provider)
      );
    }
    if (this.state.filterTemplates.length) {
      messages = messages.filter(msg =>
        this.state.filterTemplates.includes(msg.template)
      );
    }
    if (this.state.filterGroups.length) {
      messages = messages.filter(
        msg =>
          msg.groups?.some(group => this.state.filterGroups.includes(group)) ||
          (!msg.groups?.length && this.state.filterGroups.includes("none"))
      );
    }
    return messages;
  }

  toggleProviderCollapse(providerId) {
    this.setState(prevState => ({
      collapsedProviders: prevState.collapsedProviders.includes(providerId)
        ? prevState.collapsedProviders.filter(id => id !== providerId)
        : [...prevState.collapsedProviders, providerId],
    }));
  }

  getProviderSortRank(providerId) {
    const { type } =
      this.state.providerPrefs?.find(p => p.id === providerId) || {};
    const key = PROVIDER_SECTION_ORDER.includes(providerId) ? providerId : type;
    const rank = PROVIDER_SECTION_ORDER.indexOf(key);
    return rank === -1 ? PROVIDER_SECTION_ORDER.indexOf("other") : rank;
  }

  groupMessagesByProvider(messages) {
    const groups = new Map();
    for (const msg of messages) {
      const providerId = msg.provider;
      if (!groups.has(providerId)) {
        groups.set(providerId, []);
      }
      groups.get(providerId).push(msg);
    }
    return [...groups.entries()].sort(
      ([a], [b]) => this.getProviderSortRank(a) - this.getProviderSortRank(b)
    );
  }

  renderMessages() {
    if (!this.state.messages) {
      return null;
    }
    const messagesToShow = this.filterMessages();
    return (
      <div>
        <div className="messages-list">
          {this.groupMessagesByProvider(messagesToShow).map(
            ([providerId, msgs]) => {
              const isProviderCollapsed =
                this.state.collapsedProviders.includes(providerId);
              return (
                <section className="messages-provider-section" key={providerId}>
                  <h3 className="messages-provider-heading">
                    <button
                      className="messages-provider-toggle"
                      aria-expanded={!isProviderCollapsed}
                      onClick={() => this.toggleProviderCollapse(providerId)}
                    >
                      <span
                        className={`icon small icon-small-spacer icon-arrowhead-${
                          isProviderCollapsed ? "forward" : "down"
                        }`}
                      />
                      <span>{this.getProviderLabel(providerId)}</span>
                      <span className="message-stats small-text">
                        ({msgs.length})
                      </span>
                    </button>
                  </h3>
                  {isProviderCollapsed
                    ? null
                    : msgs.map(msg => this.renderMessageItem(msg))}
                </section>
              );
            }
          )}
        </div>
      </div>
    );
  }

  removeFilter(stateKey, value) {
    this.setState(prevState => ({
      [stateKey]: prevState[stateKey].filter(item => item !== value),
    }));
  }

  renderFilterDropdown({ label, stateKey, dataAttr, options }) {
    const selectedCount = this.state[stateKey].length;
    const panelId = `filter-panel-${stateKey}`;
    return (
      <div className="filter-dropdown">
        <button
          type="button"
          className="filter-dropdown-toggle"
          aria-haspopup="true"
          popovertarget={panelId}
        >
          <span className="filter-dropdown-label">
            {label} · {selectedCount || "Any"}
          </span>
          <span className="icon icon-arrowhead-down filter-dropdown-chevron" />
        </button>
        <div
          id={panelId}
          popover="auto"
          className="filter-dropdown-panel"
          role="group"
          aria-label={label}
        >
          {options.map(({ value, label: optionLabel }) => (
            <label key={value} className="filter-option">
              <input
                type="checkbox"
                {...{ [dataAttr]: value }}
                checked={this.state[stateKey].includes(value)}
                onChange={this.onChangeFilters}
              />
              {optionLabel}
            </label>
          ))}
        </div>
      </div>
    );
  }

  renderFilterPills() {
    const pills = [
      ...this.state.filterTemplates.map(value => ({
        stateKey: "filterTemplates",
        value,
      })),
      ...this.state.filterProviders.map(value => ({
        stateKey: "filterProviders",
        value,
      })),
      ...this.state.filterGroups.map(value => ({
        stateKey: "filterGroups",
        value,
      })),
    ];
    if (!pills.length) {
      return null;
    }
    return (
      <div className="filter-pills">
        {pills.map(({ stateKey, value }) => (
          <button
            key={`${stateKey}-${value}`}
            className="filter-pill"
            onClick={() => this.removeFilter(stateKey, value)}
          >
            <span>
              {stateKey === "filterProviders"
                ? this.getProviderLabel(value)
                : value}
            </span>
            <span className="icon small icon-dismiss" />
          </button>
        ))}
      </div>
    );
  }

  renderFilters() {
    const hasFilters =
      this.state.filterTemplates.length ||
      this.state.filterGroups.length ||
      this.state.filterProviders.length;
    const templateOptions = this.state.messages
      ? [...new Set(this.state.messages.map(message => message.template))].map(
          template => ({ value: template, label: template })
        )
      : [];
    const groupOptions = this.state.groups
      ? this.state.groups.map(group => ({ value: group.id, label: group.id }))
      : [];
    const providerOptions = this.state.providers
      ? this.state.providers.map(provider => ({
          value: provider.id,
          label: this.getProviderLabel(provider.id),
        }))
      : [];
    return (
      <div className="filters">
        <div className="filter-controls">
          {this.renderFilterDropdown({
            label: "Template",
            stateKey: "filterTemplates",
            dataAttr: "data-template",
            options: templateOptions,
          })}
          {this.renderFilterDropdown({
            label: "Provider",
            stateKey: "filterProviders",
            dataAttr: "data-provider",
            options: providerOptions,
          })}
          {this.renderFilterDropdown({
            label: "Group",
            stateKey: "filterGroups",
            dataAttr: "data-group",
            options: groupOptions,
          })}
          <div className="filter-actions">
            <button
              className="filter-link"
              onClick={() => this.toggleAllMessages(this.filterMessages())}
            >
              <span
                className={`icon small icon-small-spacer icon-arrowhead-${
                  this.isAnythingCollapsed() ? "forward" : "down"
                }`}
              />
              {this.isAnythingCollapsed() ? "Expand all" : "Collapse all"}
            </button>
            {this.state.modifiedMessages.length ? (
              <button
                className="filter-link messages-reset"
                onClick={this.resetAllJSON}
              >
                <span className="icon small icon-small-spacer icon-undo" />
                <span>Reset all JSON</span>
              </button>
            ) : null}
            {this.state.messageBlockList.length ? (
              <button
                className="filter-link unblock-all"
                onClick={this.unblockAll}
              >
                <span>Unblock all</span>
              </button>
            ) : null}
            {hasFilters ? (
              <button className="filter-link" onClick={this.onClearFilters}>
                Clear all
              </button>
            ) : null}
          </div>
        </div>
        {this.renderFilterPills()}
      </div>
    );
  }

  renderProviders() {
    const providersConfig = this.state.providerPrefs;
    const providerInfo = this.state.providers;
    const userPrefInfo = this.state.userPrefs;

    return (
      <table className="bordered-table" id="providers-table">
        <thead>
          <tr>
            <td className="fixed-width" />
            <td className="no-wrap">Provider</td>
            <td>Source</td>
            <td className="no-wrap">Last Updated</td>
          </tr>
        </thead>
        <tbody>
          {providersConfig.map((provider, i) => {
            const isTestProvider = provider.id.includes("_local_testing");
            const info = providerInfo.find(p => p.id === provider.id) || {};
            const isUserEnabled =
              provider.id in userPrefInfo ? userPrefInfo[provider.id] : true;
            const isSystemEnabled = isTestProvider || provider.enabled;

            let label = "local";
            if (provider.type === "remote") {
              label = (
                <span>
                  endpoint (
                  <a
                    className="small-text"
                    target="_blank"
                    href={info.url}
                    rel="noopener noreferrer"
                  >
                    {info.url}
                  </a>
                  )
                </span>
              );
            } else if (provider.type === "remote-settings") {
              label = (
                <span>
                  remote settings (
                  <a
                    className="small-text"
                    target="_blank"
                    href={`https://firefox.settings.services.mozilla.com/v1/buckets/main/collections/${provider.collection}/records`}
                    rel="noopener noreferrer"
                  >
                    {provider.collection}
                  </a>
                  )
                </span>
              );
            } else if (provider.type === "remote-experiments") {
              label = (
                <span>
                  remote settings (
                  <a
                    className="small-text"
                    target="_blank"
                    href="https://firefox.settings.services.mozilla.com/v1/buckets/main/collections/nimbus-desktop-experiments/records"
                    rel="noopener noreferrer"
                  >
                    nimbus-desktop-experiments
                  </a>
                  )
                </span>
              );
            }

            let reasonsDisabled = [];
            if (!isSystemEnabled) {
              reasonsDisabled.push("system pref");
            }
            if (!isUserEnabled) {
              reasonsDisabled.push("user pref");
            }
            if (reasonsDisabled.length) {
              label = `disabled via ${reasonsDisabled.join(", ")}`;
            }

            return (
              <tr key={i}>
                <td>
                  {isTestProvider ? (
                    <input
                      type="checkbox"
                      disabled={true}
                      readOnly={true}
                      checked={true}
                    />
                  ) : (
                    <input
                      type="checkbox"
                      data-provider={provider.id}
                      checked={isUserEnabled && isSystemEnabled}
                      onChange={this.handleEnabledToggle}
                    />
                  )}
                </td>
                <td>{provider.id}</td>
                <td>
                  <span
                    className={`sourceLabel${
                      isUserEnabled && isSystemEnabled ? "" : " isDisabled"
                    }`}
                  >
                    {label}
                  </span>
                </td>
                <td className="no-wrap">
                  {info.lastUpdated ? relativeTime(info.lastUpdated) : ""}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
    );
  }

  renderMessageGroups() {
    return (
      <table className="bordered-table" id="groups-table">
        <thead>
          <tr>
            <td className="fixed-width" />
            <td className="no-wrap">Group</td>
            <td className="no-wrap">Impressions</td>
            <td>Frequency caps</td>
            <td>User preferences</td>
          </tr>
        </thead>
        <tbody>
          {this.state.groups &&
            this.state.groups.map(
              ({ id, enabled, frequency, userPreferences = [] }) => {
                let frequencyCaps = [];
                if (!frequency) {
                  frequencyCaps.push("n/a");
                } else {
                  if (frequency.custom) {
                    for (let f of frequency.custom) {
                      let { period } = f;
                      let periodString = "";
                      if (
                        period >= 2419200000 &&
                        period % 2419200000 < 604800000
                      ) {
                        let months = Math.round(period / 2419200000);
                        periodString =
                          months === 1 ? "/month" : ` in ${months}mos`;
                      } else if (
                        period >= 604800000 &&
                        period % 604800000 < 86400000
                      ) {
                        let weeks = Math.round(period / 604800000);
                        periodString =
                          weeks === 1 ? "/week" : ` in ${weeks}wks`;
                      } else if (
                        period >= 86400000 &&
                        period % 86400000 < 3600000
                      ) {
                        let days = Math.round(period / 86400000);
                        periodString = days === 1 ? "/day" : ` in ${days}d`;
                      } else {
                        periodString = ` in ${period}ms`;
                      }
                      frequencyCaps.push(`${f.cap}${periodString}`);
                    }
                  }
                  if ("lifetime" in frequency) {
                    frequencyCaps.push(`${frequency.lifetime}/lifetime`);
                  }
                }
                return (
                  <tr key={id}>
                    <td className="fixed-width">
                      <input
                        type="checkbox"
                        checked={enabled}
                        disabled={true}
                      />
                    </td>
                    <td className="no-wrap">{id}</td>
                    <td className="no-wrap">
                      {this._getGroupImpressionsCount(id, frequency)}
                    </td>
                    <td>
                      <span>{frequencyCaps.join(", ")}</span>
                    </td>
                    <td>
                      <span className="monospace small-text">
                        {userPreferences.join(", ")}
                      </span>
                    </td>
                  </tr>
                );
              }
            )}
        </tbody>
      </table>
    );
  }

  renderTargetingParameters() {
    // There was no error and the result is truthy
    const success =
      this.state.evaluationStatus.success &&
      !!this.state.evaluationStatus.result;
    const result =
      JSON.stringify(this.state.evaluationStatus.result, null, 2) || "";

    return (
      <table className="targeting-table">
        <tbody>
          <tr>
            <td colSpan="2">
              <h2>Evaluate JEXL expression</h2>
            </td>
          </tr>
          <tr className="jexl-evaluator-row">
            <td colSpan="2">
              <div className="jexl-evaluator">
                <div className="jexl-evaluator-textareas">
                  <div className="jexl-evaluator-input">
                    <textarea
                      className="monospace no-margins"
                      ref="expressionInput"
                      rows="10"
                      cols="60"
                      placeholder="Evaluate JEXL expressions and mock parameters by changing their values below"
                      spellCheck="false"
                    />
                    <button
                      className="primary no-margins"
                      onClick={this.handleExpressionEval}
                    >
                      Evaluate
                    </button>
                  </div>
                  <div className="jexl-evaluator-output">
                    <textarea
                      className="monospace no-margins"
                      readOnly={true}
                      rows="10"
                      cols="40"
                      placeholder="<evaluation result>"
                      value={result}
                      spellCheck="false"
                    />
                    <span className="jexl-status">
                      Status: {success ? "✅" : "❌"}
                    </span>
                  </div>
                </div>
              </div>
            </td>
          </tr>
          <tr>
            <td colSpan="2">
              <h2>Modify targeting parameters</h2>
            </td>
          </tr>
          <tr>
            <td>
              <button
                className="no-margins"
                onClick={this.onCopyTargetingParams}
                disabled={this.state.copiedToClipboard}
              >
                {this.state.copiedToClipboard
                  ? "Parameters copied!"
                  : "Copy parameters"}
              </button>
            </td>
          </tr>
          {this.state.stringTargetingParameters &&
            Object.keys(this.state.stringTargetingParameters).map(
              (param, i) => {
                const value = this.state.stringTargetingParameters[param];
                const errorState =
                  this.state.targetingParametersError &&
                  this.state.targetingParametersError.id === param;
                const largeEditor = value?.length > 30 || value?.match(/[\nR]/);
                const className = `monospace no-margins targeting-editor${
                  errorState ? " errorState" : ""
                }${largeEditor ? " large" : " small"}`;
                const inputComp = (
                  <textarea
                    name={param}
                    className={className}
                    value={value}
                    rows={largeEditor ? "10" : "1"}
                    cols={largeEditor ? "60" : "28"}
                    onChange={this.onChangeTargetingParameters}
                    spellCheck="false"
                  />
                );

                return (
                  <tr key={i}>
                    <td>{param}</td>
                    <td>{inputComp}</td>
                  </tr>
                );
              }
            )}

          <tr>
            <td colSpan="2">
              <h2>Attribution parameters</h2>
            </td>
          </tr>
          <tr>
            <td colSpan="2">
              <p>
                This forces the browser to set some attribution parameters,
                useful for testing the Return To AMO feature. Clicking on 'Force
                Attribution', with the default values in each field, will demo
                the Return To AMO flow with the addon called 'uBlock Origin'. If
                you wish to try different attribution parameters, enter them in
                the text boxes. If you wish to try a different addon with the
                Return To AMO flow, make sure the 'content' text box has a
                string that is 'rta:base64(addonID)', the base64 string of the
                addonID prefixed with 'rta:'. The addon must currently be a
                recommended addon on AMO. Then click 'Force Attribution'.
                Clicking on 'Force Attribution' with blank text boxes reset
                attribution data.
              </p>
            </td>
          </tr>
          <tr>
            <td>Source</td>
            <td>
              <input
                className="monospace no-margins"
                type="text"
                size="36"
                name="source"
                placeholder="addons.mozilla.org"
                value={this.state.attributionParameters.source}
                onChange={this.onChangeAttributionParameters}
              />
            </td>
          </tr>
          <tr>
            <td>Medium</td>
            <td>
              <input
                className="monospace no-margins"
                type="text"
                size="36"
                name="medium"
                placeholder="referral"
                value={this.state.attributionParameters.medium}
                onChange={this.onChangeAttributionParameters}
              />
            </td>
          </tr>
          <tr>
            <td>Campaign</td>
            <td>
              <input
                className="monospace no-margins"
                type="text"
                size="36"
                name="campaign"
                placeholder="non-fx-button"
                value={this.state.attributionParameters.campaign}
                onChange={this.onChangeAttributionParameters}
              />
            </td>
          </tr>
          <tr>
            <td>Content</td>
            <td>
              <input
                className="monospace no-margins"
                type="text"
                size="36"
                name="content"
                placeholder={`rta:${btoa("uBlock0@raymondhill.net")}`}
                value={this.state.attributionParameters.content}
                onChange={this.onChangeAttributionParameters}
              />
            </td>
          </tr>
          <tr>
            <td>Experiment</td>
            <td>
              <input
                className="monospace no-margins"
                type="text"
                size="36"
                name="experiment"
                placeholder="ua-onboarding"
                value={this.state.attributionParameters.experiment}
                onChange={this.onChangeAttributionParameters}
              />
            </td>
          </tr>
          <tr>
            <td>Variation</td>
            <td>
              <input
                className="monospace no-margins"
                type="text"
                size="36"
                name="variation"
                placeholder="chrome"
                value={this.state.attributionParameters.variation}
                onChange={this.onChangeAttributionParameters}
              />
            </td>
          </tr>
          <tr>
            <td>User Agent</td>
            <td>
              <input
                className="monospace no-margins"
                type="text"
                size="36"
                name="ua"
                placeholder="Google Chrome 123"
                value={this.state.attributionParameters.ua}
                onChange={this.onChangeAttributionParameters}
              />
            </td>
          </tr>
          <tr>
            <td>Download Token</td>
            <td>
              <input
                className="monospace no-margins"
                type="text"
                size="36"
                name="dltoken"
                placeholder="00000000-0000-0000-0000-000000000000"
                value={this.state.attributionParameters.dltoken}
                onChange={this.onChangeAttributionParameters}
              />
            </td>
          </tr>
          <tr>
            <td colSpan="2">
              <button
                className="primary no-margins"
                onClick={this.setAttribution}
              >
                Force attribution
              </button>
            </td>
          </tr>
        </tbody>
      </table>
    );
  }

  onChangeAttributionParameters(event) {
    const { name: eventName, value } = event.target;

    this.setState(({ attributionParameters }) => {
      const updatedParameters = { ...attributionParameters };
      updatedParameters[eventName] = value;

      return { attributionParameters: updatedParameters };
    });
  }

  setAttribution() {
    ASRouterUtils.sendMessage({
      type: "FORCE_ATTRIBUTION",
      data: this.state.attributionParameters,
    }).then(this.setStateFromParent);
  }

  _getGroupImpressionsCount(id, frequency) {
    if (frequency) {
      return this.state.groupImpressions[id]
        ? this.state.groupImpressions[id].length
        : 0;
    }

    return "n/a";
  }

  renderErrorMessage({ id, errors }) {
    const providerId = <td rowSpan={errors.length}>{id}</td>;
    // .reverse() so that the last error (most recent) is first
    return errors
      .map(({ error, timestamp }, cellKey) => (
        <tr key={cellKey}>
          {cellKey === errors.length - 1 ? providerId : null}
          <td>{error.message}</td>
          <td>{relativeTime(timestamp)}</td>
        </tr>
      ))
      .reverse();
  }

  renderErrors() {
    const providersWithErrors =
      this.state.providers &&
      this.state.providers.filter(p => p.errors && p.errors.length);

    if (providersWithErrors && providersWithErrors.length) {
      return (
        <table className="errorReporting">
          <thead>
            <tr>
              <th>Provider</th>
              <th>Message</th>
              <th>Timestamp</th>
            </tr>
          </thead>
          <tbody>{providersWithErrors.map(this.renderErrorMessage)}</tbody>
        </table>
      );
    }

    return <p>No errors</p>;
  }

  renderResetButton({ label, description, onClick, className = "" }) {
    return (
      <button
        className={`small reset-button ${className}`.trim()}
        title={description}
        onClick={onClick}
      >
        <span>{label}</span>
        <span className="icon small icon-info" aria-hidden="true" />
      </button>
    );
  }

  renderSectionInfo(description, docHref, linkText) {
    const lines = Array.isArray(description) ? description : [description];
    return (
      <p className="helpLink section-info">
        <span className="icon icon-small-spacer icon-info" />
        <span>
          {lines.map((line, index) => (
            <React.Fragment key={line}>
              {index ? <br /> : null}
              {line}
            </React.Fragment>
          ))}{" "}
          {docHref ? (
            <a target="_blank" rel="noopener noreferrer" href={docHref}>
              {linkText}
            </a>
          ) : null}
        </span>
      </p>
    );
  }

  renderSection() {
    const [section] = this.props.location.routes;
    switch (section) {
      case "targeting":
        return (
          <React.Fragment>
            <h2>Targeting utilities</h2>
            <div className="button-box">
              <button
                className="no-margins"
                onClick={this.expireCache}
                title="Values are cached for some targeting attributes (see ASRouterTargeting). This expires the query cache."
              >
                Expire cache
              </button>
            </div>
            {this.renderTargetingParameters()}
          </React.Fragment>
        );
      case "impressions":
        return (
          <React.Fragment>
            <h2>Impressions</h2>
            <ImpressionsSection
              messageImpressions={this.state.messageImpressions}
              groupImpressions={this.state.groupImpressions}
              screenImpressions={this.state.screenImpressions}
              multiProfileMessageImpressions={
                this.state.multiProfileMessageImpressions
              }
            />
          </React.Fragment>
        );
      case "errors":
        return (
          <React.Fragment>
            <h2>ASRouter errors</h2>
            {this.renderErrors()}
          </React.Fragment>
        );
      default:
        return (
          <React.Fragment>
            <h2>
              Message providers
              {this.renderResetButton({
                label: "Restore default prefs",
                description:
                  "Restores all message provider prefs (which providers are enabled and their sources) to the defaults that ship with Firefox. Does not clear impressions or blocks.",
                onClick: this.resetPref,
              })}
            </h2>
            {this.renderSectionInfo([
              "Sources that supply messages to the Firefox Messaging System.",
              "Includes: local (in-tree), Remote Settings collections, and Nimbus experiments.",
            ])}
            {this.state.providers ? this.renderProviders() : null}
            <h2>
              Message groups
              {this.renderResetButton({
                label: "Reset group impressions",
                description:
                  "Clears the recorded impression counts for message groups, resetting group-level frequency caps.",
                onClick: this.resetGroupImpressions,
              })}
            </h2>
            {this.renderSectionInfo([
              "When multiple messages point to the same message group configuration, any impression from one of the messages counts against the total allowed for the group.",
              "A message can show only if all of its groups are enabled and under their caps.",
            ])}
            {this.state.groups ? this.renderMessageGroups() : null}
            <h2>
              Messages
              {this.renderResetButton({
                label: "Reset message impressions",
                description:
                  "Clears the recorded impression counts for individual messages, resetting per-message frequency caps.",
                onClick: this.resetMessageImpressions,
              })}
            </h2>
            <p className="helpLink section-info">
              <span className="icon icon-small-spacer icon-info" />
              <span>
                The list of messages currently loaded from all enabled
                providers.
                <ul>
                  <li>
                    Click "Show" to preview a message. To preview edits, change
                    the JSON first, then click "Show" again.
                  </li>
                  <li>Click "Reset" to restore the JSON to the original.</li>
                  <li>
                    Click "Share" to copy a link to the clipboard that can be
                    used to preview the message by opening the link in
                    Nightly/local builds.
                  </li>
                </ul>
              </span>
            </p>
            {this.renderFilters()}
            {this.renderMessages()}
          </React.Fragment>
        );
    }
  }

  render() {
    if (!this.state.devtoolsEnabled) {
      return (
        <div className="asrouter-admin">
          You must enable the ASRouter Admin page by setting{" "}
          <code>
            browser.newtabpage.activity-stream.asrouter.devtoolsEnabled
          </code>{" "}
          to <code>true</code> and then reloading this page.
        </div>
      );
    }

    const [section] = this.props.location.routes;

    return (
      <div className="asrouter-admin">
        <aside className="sidebar">
          <ul>
            <li>
              <a
                href="#devtools"
                className="category"
                data-selected={section ? null : ""}
              >
                General
              </a>
            </li>
            <li>
              <a
                href="#devtools-targeting"
                className="category"
                data-selected={section === "targeting" ? "" : null}
              >
                Targeting
              </a>
            </li>
            <li>
              <a
                href="#devtools-impressions"
                className="category"
                data-selected={section === "impressions" ? "" : null}
              >
                Impressions
              </a>
            </li>
            <li>
              <a
                href="#devtools-errors"
                className="category"
                data-selected={section === "errors" ? "" : null}
              >
                Errors
              </a>
            </li>
          </ul>
        </aside>
        <main className="main-panel">
          <h1>
            ASRouter Admin
            {this.renderResetButton({
              label: "Reset FxMS state",
              description:
                "Resets all Firefox Messaging System user state: clears message, group, and screen impressions and unblocks every message.",
              onClick: this.resetMessageState,
            })}
          </h1>

          <p className="helpLink section-info">
            <span className="icon icon-small-spacer icon-info" />
            <span>
              Need help using these tools? Check out our{" "}
              <a
                target="blank"
                href="https://firefox-source-docs.mozilla.org/browser/components/asrouter/docs/debugging-docs.html"
              >
                documentation
              </a>
            </span>
          </p>

          {this.renderSection()}
        </main>
      </div>
    );
  }
}

export const ASRouterAdmin = props => (
  <SimpleHashRouter>
    <ASRouterAdminInner {...props} />
  </SimpleHashRouter>
);

export function renderASRouterAdmin() {
  ReactDOM.render(<ASRouterAdmin />, document.getElementById("root"));
}
