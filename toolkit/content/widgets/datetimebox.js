/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is a UA widget. It runs in per-origin UA widget scope,
// to be loaded by UAWidgetsChild.sys.mjs.

/*
 * This is the class of entry. It will construct the actual implementation
 * according to the value of the "type" property.
 */
this.DateTimeBoxWidget = class {
  constructor(shadowRoot, prefs) {
    this.shadowRoot = shadowRoot;
    this.element = shadowRoot.host;
    this.document = this.element.ownerDocument;
    this.window = this.document.defaultView;
    this.mEnableTimePicker = prefs["dom.forms.datetime.timepicker"];
    // When undefined, DOMLocalization will use the app locales.
    let locales;
    if (prefs["privacy.resistFingerprinting"]) {
      locales = [...this.window.getWebExposedLocales()];
      // Make sure to always include en-US, in case the web exposed languages do
      // not include a translation for the widget.
      if (!locales.includes("en-US")) {
        locales.push("en-US");
      }
    }
    // The DOMLocalization instance needs to allow for sync methods so that
    // the placeholder value may be determined and set during the
    // createEditFieldAndAppend() call.
    this.l10n = new this.window.DOMLocalization(
      ["toolkit/global/datetimebox.ftl"],
      /* aSync = */ true,
      undefined,
      locales
    );
  }

  /*
   * Callback called by UAWidgets right after constructor.
   */
  onsetup() {
    this.onchange(/* aDestroy = */ false);
  }

  /*
   * Callback called by UAWidgets when the "type" property changes.
   */
  onchange(aDestroy = true) {
    let newType = this.element.type;
    if (this.type == newType) {
      return;
    }

    if (aDestroy) {
      this.teardown();
    }
    this.type = newType;
    this.setup();
  }

  shouldShowTime() {
    return this.type == "time" || this.type == "datetime-local";
  }

  shouldShowDate() {
    return this.type == "date" || this.type == "datetime-local";
  }

  teardown() {
    this.mInputElement.removeEventListener("keydown", this, {
      capture: true,
      mozSystemGroup: true,
    });
    this.mInputElement.removeEventListener("click", this, {
      mozSystemGroup: true,
    });

    this.CONTROL_EVENTS.forEach(eventName => {
      this.mDateTimeBoxElement.removeEventListener(eventName, this);
    });

    this.l10n.disconnectRoot(this.shadowRoot);

    this.removeEditFields();
    this.removeEventListenersToField(this.mPickerButton);

    this.mInputElement = null;

    this.shadowRoot.firstChild.remove();
  }

  removeEditFields() {
    this.removeEventListenersToField(this.mYearField);
    this.removeEventListenersToField(this.mMonthField);
    this.removeEventListenersToField(this.mDayField);
    this.removeEventListenersToField(this.mHourField);
    this.removeEventListenersToField(this.mMinuteField);
    this.removeEventListenersToField(this.mSecondField);
    this.removeEventListenersToField(this.mMillisecField);
    this.removeEventListenersToField(this.mDayPeriodField);

    this.mYearField = null;
    this.mMonthField = null;
    this.mDayField = null;
    this.mHourField = null;
    this.mMinuteField = null;
    this.mSecondField = null;
    this.mMillisecField = null;
    this.mDayPeriodField = null;

    let root = this.shadowRoot.getElementById("edit-wrapper");
    while (root.firstChild) {
      root.firstChild.remove();
    }
  }

  rebuildEditFieldsIfNeeded() {
    if (
      this.shouldShowSecondField() == !!this.mSecondField &&
      this.shouldShowMillisecField() == !!this.mMillisecField
    ) {
      return;
    }

    let focused = this.mInputElement.matches(":focus");

    this.removeEditFields();
    this.buildEditFields();

    if (focused) {
      this._focusFirstField();
    }
  }

  _focusFirstField() {
    this.shadowRoot.querySelector(".datetime-edit-field")?.focus();
  }

  setup() {
    this.DEBUG = false;

    this.l10n.connectRoot(this.shadowRoot);

    this.generateContent();

    this.mDateTimeBoxElement = this.shadowRoot.firstChild;
    this.mPickerButton = this.shadowRoot.getElementById("picker-button");
    this.mInputElement = this.element;
    this.mLocales = this.window.getWebExposedLocales();

    this.mIsRTL = false;
    let intlUtils = this.window.intlUtils;
    if (intlUtils) {
      this.mIsRTL = intlUtils.isAppLocaleRTL();
    }

    if (this.mIsRTL) {
      let inputBoxWrapper = this.shadowRoot.getElementById("input-box-wrapper");
      inputBoxWrapper.dir = "rtl";
    }

    if (this.type == "time") {
      this.mPickerButton.setAttribute("data-l10n-id", "datetime-time");
    }

    this.mIsPickerOpen = false;

    this.mMinMonth = 1;
    this.mMaxMonth = 12;
    this.mMinDay = 1;
    this.mMaxDay = 31;
    this.mMinYear = 1;
    // Maximum year limited by ISO 8601.
    this.mMaxYear = 9999;
    this.mMonthDayLength = 2;
    this.mYearLength = 4;
    this.mMonthPageUpDownInterval = 3;
    this.mDayPageUpDownInterval = 7;
    this.mYearPageUpDownInterval = 10;

    const kDefaultAMString = "AM";
    const kDefaultPMString = "PM";

    let { amString, pmString } = this.getStringsForLocale(this.mLocales);

    this.mAMIndicator = amString || kDefaultAMString;
    this.mPMIndicator = pmString || kDefaultPMString;

    this.mHour12 = this.is12HourTime(this.mLocales);
    this.mMillisecSeparatorText = ".";
    this.mMaxLength = 2;
    this.mMillisecMaxLength = 3;
    this.mDefaultStep = 60 * 1000; // in milliseconds

    this.mMinHour = this.mHour12 ? 1 : 0;
    this.mMaxHour = this.mHour12 ? 12 : 23;
    this.mMinMinute = 0;
    this.mMaxMinute = 59;
    this.mMinSecond = 0;
    this.mMaxSecond = 59;
    this.mMinMillisecond = 0;
    this.mMaxMillisecond = 999;

    this.mHourPageUpDownInterval = 3;
    this.mMinSecPageUpDownInterval = 10;

    this.mInputElement.addEventListener(
      "keydown",
      this,
      {
        capture: true,
        mozSystemGroup: true,
      },
      false
    );
    // This is to open the picker when input element is tapped on Android
    // or for type=time inputs (this includes padding area).
    this.isAndroid = this.window.navigator.appVersion.includes("Android");
    if (this.showPickerOnClick) {
      this.mInputElement.addEventListener(
        "click",
        this,
        { mozSystemGroup: true },
        false
      );
    }

    // Those events are dispatched to <div class="datetimebox"> with bubble set
    // to false. They are trapped inside UA Widget Shadow DOM and are not
    // dispatched to the document.
    this.CONTROL_EVENTS.forEach(eventName => {
      this.mDateTimeBoxElement.addEventListener(eventName, this, {}, false);
    });

    this.buildEditFields();
    this.buildPickerBtn();
    this.updateEditAttributes();

    if (this.mInputElement.value) {
      this.setFieldsFromInputValue();
    }

    if (this.mInputElement.matches(":focus")) {
      this._focusFirstField();
    }
  }

  generateContent() {
    const parser = new this.window.DOMParser();
    let parserDoc = parser.parseFromSafeString(
      `<div class="datetimebox" id="datetimebox" xmlns="http://www.w3.org/1999/xhtml" role="none">
        <link rel="stylesheet" type="text/css" href="resource://content-accessible/datetimebox.css" />
        <div class="datetime-input-box-wrapper" id="input-box-wrapper" role="presentation">
          <span class="datetime-input-edit-wrapper"
                     id="edit-wrapper">
            <!-- Each of the date/time input types will append their input child
               - elements here -->
          </span>
          <button data-l10n-id="datetime-calendar" class="datetime-picker-button" id="picker-button" aria-expanded="false">
            <svg role="none" class="datetime-calendar-button-svg" xmlns="http://www.w3.org/2000/svg" id="calendar-16" viewBox="0 0 16 16" width="16" height="16">
              <path d="M13.5 2H13V1c0-.6-.4-1-1-1s-1 .4-1 1v1H5V1c0-.6-.4-1-1-1S3 .4 3 1v1h-.5C1.1 2 0 3.1 0 4.5v9C0 14.9 1.1 16 2.5 16h11c1.4 0 2.5-1.1 2.5-2.5v-9C16 3.1 14.9 2 13.5 2zm0 12.5h-11c-.6 0-1-.4-1-1V6h13v7.5c0 .6-.4 1-1 1z"/>
            </svg>
            <svg role="none" class="datetime-time-button-svg" xmlns="http://www.w3.org/2000/svg" id="time-16" viewBox="0 0 16 16" width="16" height="16">
              <path d="M7.625 1.75c3.446 0 6.25 2.804 6.25 6.25s-2.804 6.25-6.25 6.25-6.25-2.804-6.25-6.25 2.804-6.25 6.25-6.25m0-1.25a7.5 7.5 0 1 0 0 15 7.5 7.5 0 0 0 0-15z"/>
              <path d="M10.624 10.424a.625.625 0 0 1-.312-.084l-3-1.732L7 8.066l0-3.441a.625.625 0 0 1 1.25 0l0 3.081 2.688 1.552a.626.626 0 0 1-.314 1.166z"/>
            </svg>
          </button>
        </div>
      </div>`,
      "application/xml"
    );

    this.shadowRoot.importNodeAndAppendChildAt(
      this.shadowRoot,
      parserDoc.documentElement,
      true
    );
    this.l10n.translateRoots();
  }

  get FIELD_EVENTS() {
    return ["focus", "copy", "cut", "paste"];
  }

  get CONTROL_EVENTS() {
    return [
      "MozDateTimeWillBlur",
      "MozDateTimeValueChanged",
      "MozNotifyMinMaxStepAttrChanged",
      "MozDateTimeAttributeChanged",
      "MozPickerValueChanged",
      "MozSetDateTimePickerState",
      "MozDateTimeShowPickerForJS",
    ];
  }

  get showPickerOnClick() {
    return this.isAndroid;
  }

  addEventListenersToField(aElement) {
    // These events don't bubble out of the Shadow DOM, so we'll have to add
    // event listeners specifically on each of the fields, not just
    // on the <input>
    this.FIELD_EVENTS.forEach(eventName => {
      aElement.addEventListener(
        eventName,
        this,
        { mozSystemGroup: true },
        /* wantsUntrusted = */ false
      );
    });
  }

  removeEventListenersToField(aElement) {
    if (!aElement) {
      return;
    }

    this.FIELD_EVENTS.forEach(eventName => {
      aElement.removeEventListener(eventName, this, { mozSystemGroup: true });
    });
  }

  log(aMsg) {
    if (this.DEBUG) {
      this.window.dump("[DateTimeBox] " + aMsg + "\n");
    }
  }

  createEditFieldAndAppend(
    aL10nId,
    aPlaceholderId,
    aIsNumeric,
    aMinDigits,
    aMaxLength,
    aMinValue,
    aMaxValue,
    aPageUpDownInterval
  ) {
    let root = this.shadowRoot.getElementById("edit-wrapper");
    let field = this.shadowRoot.createElementAndAppendChildAt(root, "span");
    field.classList.add("datetime-edit-field");
    field.setAttribute("aria-valuetext", "");
    this.setFieldTabIndexAttribute(field);

    const placeholder = this.l10n.formatValueSync(aPlaceholderId);
    field.placeholder = placeholder;
    field.textContent = placeholder;
    this.l10n.setAttributes(field, aL10nId);

    // Used to store the non-formatted value, cleared when value is
    // cleared.
    // DateTimeInputTypeBase::HasBadInput() will read this to decide
    // if the input has value.
    field.setAttribute("value", "");

    if (aIsNumeric) {
      field.classList.add("numeric");
      // Maximum value allowed.
      field.setAttribute("min", aMinValue);
      // Minumim value allowed.
      field.setAttribute("max", aMaxValue);
      // Interval when pressing pageUp/pageDown key.
      field.setAttribute("pginterval", aPageUpDownInterval);
      // Used to store what the user has already typed in the field,
      // cleared when value is cleared and when field is blurred.
      field.setAttribute("typeBuffer", "");
      // Minimum digits to display, padded with leading 0s.
      field.setAttribute("mindigits", aMinDigits);
      // Maximum length for the field, will be advance to the next field
      // automatically if exceeded.
      field.setAttribute("maxlength", aMaxLength);
      // Set spinbutton ARIA role
      field.setAttribute("role", "spinbutton");
    } else {
      // Set generic textbox ARIA role
      field.setAttribute("role", "textbox");
    }

    if (this.mIsRTL) {
      // Set "embed" so that the field does not change order based on contents.
      field.style.unicodeBidi = "embed";
    }

    return field;
  }

  updatePickerButtonState(isExpanded) {
    this.mPickerButton.setAttribute("aria-expanded", isExpanded);
  }

  notifyInputElementValueChanged() {
    this.log("inputElementValueChanged");
    this.setFieldsFromInputValue();
  }

  notifyMinMaxStepAttrChanged() {
    // Second and millisecond part are optional, rebuild edit fields if
    // needed.
    this.rebuildEditFieldsIfNeeded();
    // Fill in values again.
    this.setFieldsFromInputValue();
  }

  setValueFromPicker(aValue) {
    if (aValue) {
      this.setFieldsFromPicker(aValue);
    } else {
      this.clearInputFields();
    }
  }

  advanceToNextField(aReverse) {
    this.log("advanceToNextField");

    let focusedInput = this.mLastFocusedElement;
    let next = aReverse
      ? focusedInput.previousElementSibling
      : focusedInput.nextElementSibling;
    if (!next && !aReverse) {
      this.setInputValueFromFields();
      return;
    }

    while (next) {
      if (next.matches("span.datetime-edit-field")) {
        next.focus();
        break;
      }
      next = aReverse ? next.previousElementSibling : next.nextElementSibling;
    }
  }

  setPickerState(aIsOpen) {
    this.log("picker is now " + (aIsOpen ? "opened" : "closed"));
    this.mIsPickerOpen = aIsOpen;
    this.mInputElement.setOpenState(aIsOpen);
    // Picker button's expanded state mirrors this.mIsPickerOpen
    this.updatePickerButtonState(this.mIsPickerOpen);
  }

  setFieldTabIndexAttribute(field) {
    field.tabIndex = this.mInputElement.tabIndex;
  }

  updateEditAttributes() {
    this.log("updateEditAttributes");

    let editRoot = this.shadowRoot.getElementById("edit-wrapper");

    for (let child of editRoot.querySelectorAll(
      ":scope > span.datetime-edit-field"
    )) {
      this.setFieldTabIndexAttribute(child);
    }
  }

  isEmpty(aValue) {
    return aValue == undefined || 0 === aValue.length;
  }

  getFieldValue(aField) {
    if (!aField || !aField.classList.contains("numeric")) {
      return undefined;
    }

    let value = aField.getAttribute("value");
    // Avoid returning 0 when field is empty.
    return this.isEmpty(value) ? undefined : Number(value);
  }

  clearFieldValue(aField) {
    aField.textContent = aField.placeholder;
    aField.setAttribute("value", "");
    aField.setAttribute("aria-valuetext", "");
    if (aField.classList.contains("numeric")) {
      aField.setAttribute("typeBuffer", "");
    }
  }

  openDateTimePicker() {
    this.mInputElement.openDateTimePicker(this.getCurrentValue());
  }

  closeDateTimePicker() {
    if (this.mIsPickerOpen) {
      this.mInputElement.closeDateTimePicker();
    }
  }

  isDisabled() {
    return this.mInputElement.matches(":disabled");
  }

  isReadonly() {
    return this.mInputElement.matches(":read-only");
  }

  isEditable() {
    return !this.isDisabled() && !this.isReadonly();
  }

  isRequired() {
    return this.mInputElement.hasAttribute("required");
  }

  containingTree() {
    return this.mInputElement.containingShadowRoot || this.document;
  }

  handleEvent(aEvent) {
    this.log("handleEvent: " + aEvent.type);

    if (!aEvent.isTrusted) {
      return;
    }

    switch (aEvent.type) {
      case "MozDateTimeValueChanged": {
        this.notifyInputElementValueChanged();
        break;
      }
      case "MozNotifyMinMaxStepAttrChanged": {
        this.notifyMinMaxStepAttrChanged();
        break;
      }
      case "MozDateTimeAttributeChanged": {
        this.updateEditAttributes();
        break;
      }
      case "MozPickerValueChanged": {
        this.setValueFromPicker(aEvent.detail);
        break;
      }
      case "MozSetDateTimePickerState": {
        this.setPickerState(aEvent.detail);
        break;
      }
      case "MozDateTimeShowPickerForJS": {
        this.openDateTimePicker();
        break;
      }
      case "keydown": {
        this.onKeyDown(aEvent);
        break;
      }
      case "click": {
        this.onClick(aEvent);
        break;
      }
      case "focus": {
        this.onFocus(aEvent);
        break;
      }
      case "MozDateTimeWillBlur": {
        // The event detail is the blur event.
        this.onWillBlur(aEvent.detail);
        break;
      }
      case "mousedown":
      case "copy":
      case "cut":
      case "paste": {
        aEvent.preventDefault();
        break;
      }
      default:
        break;
    }
  }

  onFocus(aEvent) {
    this.log("onFocus originalTarget: " + aEvent.originalTarget);
    if (this.containingTree().activeElement != this.mInputElement) {
      return;
    }

    let target = aEvent.originalTarget;
    if (target.matches(".datetime-edit-field,.datetime-picker-button")) {
      if (target.disabled) {
        return;
      }
      this.mLastFocusedElement = target;
      this.mInputElement.setFocusState(true);
    }
    if (this.mIsPickerOpen && this.isPickerIrrelevantField(target)) {
      this.closeDateTimePicker();
    }
  }

  onWillBlur(aEvent) {
    this.log(
      "onWillBlur originalTarget: " +
        aEvent.originalTarget +
        " target: " +
        aEvent.target +
        " rt: " +
        aEvent.relatedTarget +
        " open: " +
        this.mIsPickerOpen
    );

    let target = aEvent.originalTarget;
    if (
      target.getRootNode() !== this.shadowRoot ||
      !target.matches(".datetime-edit-field")
    ) {
      // We only care about the blurring of our inner fields.
      return;
    }

    target.setAttribute("typeBuffer", "");

    this.setInputValueFromFields();
    // No need to set and unset the focus state (or closing the picker) if the
    // focus is staying within our input.
    if (aEvent.relatedTarget == this.mInputElement) {
      return;
    }

    // If we're in chrome and the focus moves to a separate document
    // (relatedTarget is null) we also don't want to close it, since it
    // could've moved to the datetime popup itself.
    if (
      !aEvent.relatedTarget &&
      this.window.isChromeWindow &&
      this.window == this.window.top
    ) {
      return;
    }

    this.mInputElement.setFocusState(false);
    if (this.mIsPickerOpen) {
      this.closeDateTimePicker();
    }
  }

  isTimeField(field) {
    return (
      field == this.mHourField ||
      field == this.mMinuteField ||
      field == this.mSecondField ||
      field == this.mDayPeriodField
    );
  }

  shouldOpenDateTimePickerOnKeyDown() {
    if (!this.mLastFocusedElement) {
      return true;
    }
    return !this.isPickerIrrelevantField(this.mLastFocusedElement);
  }

  shouldOpenDateTimePickerOnClick(target) {
    return !this.isPickerIrrelevantField(target);
  }

  // Whether a given field is irrelevant for the purposes of the datetime
  // picker. This is useful for datetime-local, which as of right now only
  // shows a date picker (not a time picker).
  isPickerIrrelevantField(field) {
    if (this.type != "datetime-local") {
      return false;
    }
    return this.isTimeField(field) && !this.mEnableTimePicker;
  }

  onKeyDown(aEvent) {
    this.log("onKeyDown key: " + aEvent.key);

    if (aEvent.defaultPrevented || this.isDisabled()) {
      return;
    }

    switch (aEvent.key) {
      // Toggle the date/time picker on Space/Enter on picker button or Space on input,
      // time picker on Space on input, close picker on Escape anywhere.
      case "Escape": {
        if (this.mIsPickerOpen) {
          this.closeDateTimePicker();
          aEvent.preventDefault();
        }
        break;
      }
      case "Enter":
      case " ": {
        // always close, if opened
        if (this.mIsPickerOpen) {
          this.closeDateTimePicker();
        } else if (
          // open on Space from anywhere within the input
          aEvent.key == " " &&
          this.shouldOpenDateTimePickerOnKeyDown()
        ) {
          this.openDateTimePicker();
        } else if (
          // open from the picker button on either keydown
          aEvent.originalTarget == this.mPickerButton &&
          this.shouldOpenDateTimePickerOnKeyDown()
        ) {
          this.openDateTimePicker();
        } else {
          // Don't preventDefault();
          break;
        }
        aEvent.preventDefault();
        break;
      }
      case "Delete":
      case "Backspace": {
        if (aEvent.originalTarget == this.mPickerButton) {
          // Do not remove picker button
          aEvent.preventDefault();
          break;
        }
        // Ctrl+Backspace/Delete on non-macOS and
        // Cmd+Backspace/Delete on macOS to clear the field
        if (aEvent.getModifierState("Accel")) {
          // Clear the input's value
          this.clearInputFields(false);
        } else {
          let targetField = aEvent.originalTarget;
          this.clearFieldValue(targetField);
          this.setInputValueFromFields();
        }
        aEvent.preventDefault();
        break;
      }
      case "ArrowRight":
      case "ArrowLeft": {
        this.advanceToNextField(!(aEvent.key == "ArrowRight"));
        aEvent.preventDefault();
        break;
      }
      case "ArrowUp":
      case "ArrowDown":
      case "PageUp":
      case "PageDown":
      case "Home":
      case "End": {
        this.handleKeyboardNav(aEvent);
        aEvent.preventDefault();
        break;
      }
      default: {
        // Handle printable characters (e.g. letters, digits and numpad digits)
        if (
          aEvent.key.length === 1 &&
          !(aEvent.ctrlKey || aEvent.altKey || aEvent.metaKey)
        ) {
          this.handleKeydown(aEvent);
          aEvent.preventDefault();
        }
        break;
      }
    }
  }

  onClick(aEvent) {
    this.log(
      "onClick originalTarget: " +
        aEvent.originalTarget +
        " target: " +
        aEvent.target
    );

    if (aEvent.defaultPrevented || !this.isEditable()) {
      return;
    }

    // We toggle the picker on click on the picker button on any platform.
    // For Android, we also toggle the picker when clicking on the input field.
    //
    // We do not toggle the picker when clicking the input field on desktop
    // to avoid interfering with the default input behavior.
    if (aEvent.originalTarget == this.mPickerButton || this.showPickerOnClick) {
      if (
        !this.mIsPickerOpen &&
        this.shouldOpenDateTimePickerOnClick(aEvent.originalTarget)
      ) {
        this.openDateTimePicker();
      } else {
        this.closeDateTimePicker();
      }
    }
  }

  buildEditFields() {
    let root = this.shadowRoot.getElementById("edit-wrapper");

    let options = {};

    if (this.shouldShowTime()) {
      options.hour = options.minute = "numeric";
      options.hour12 = this.mHour12;
      if (this.shouldShowSecondField()) {
        options.second = "numeric";
      }
    }

    if (this.shouldShowDate()) {
      options.year = options.month = options.day = "numeric";
    }

    let formatter = Intl.DateTimeFormat(this.mLocales, options);
    formatter.formatToParts(Date.now()).map(part => {
      switch (part.type) {
        case "year":
          this.mYearField = this.createEditFieldAndAppend(
            "datetime-year",
            "datetime-year-placeholder",
            true,
            this.mYearLength,
            this.mMaxYear.toString().length,
            this.mMinYear,
            this.mMaxYear,
            this.mYearPageUpDownInterval
          );
          this.addEventListenersToField(this.mYearField);
          break;
        case "month":
          this.mMonthField = this.createEditFieldAndAppend(
            "datetime-month",
            "datetime-month-placeholder",
            true,
            this.mMonthDayLength,
            this.mMonthDayLength,
            this.mMinMonth,
            this.mMaxMonth,
            this.mMonthPageUpDownInterval
          );
          this.addEventListenersToField(this.mMonthField);
          break;
        case "day":
          this.mDayField = this.createEditFieldAndAppend(
            "datetime-day",
            "datetime-day-placeholder",
            true,
            this.mMonthDayLength,
            this.mMonthDayLength,
            this.mMinDay,
            this.mMaxDay,
            this.mDayPageUpDownInterval
          );
          this.addEventListenersToField(this.mDayField);
          break;
        case "hour":
          this.mHourField = this.createEditFieldAndAppend(
            "datetime-hour",
            "datetime-time-placeholder",
            true,
            this.mMaxLength,
            this.mMaxLength,
            this.mMinHour,
            this.mMaxHour,
            this.mHourPageUpDownInterval
          );
          this.addEventListenersToField(this.mHourField);
          break;
        case "minute":
          this.mMinuteField = this.createEditFieldAndAppend(
            "datetime-minute",
            "datetime-time-placeholder",
            true,
            this.mMaxLength,
            this.mMaxLength,
            this.mMinMinute,
            this.mMaxMinute,
            this.mMinSecPageUpDownInterval
          );
          this.addEventListenersToField(this.mMinuteField);
          break;
        case "second":
          this.mSecondField = this.createEditFieldAndAppend(
            "datetime-second",
            "datetime-time-placeholder",
            true,
            this.mMaxLength,
            this.mMaxLength,
            this.mMinSecond,
            this.mMaxSecond,
            this.mMinSecPageUpDownInterval
          );
          this.addEventListenersToField(this.mSecondField);
          if (this.shouldShowMillisecField()) {
            // Intl.DateTimeFormat does not support millisecond, so we
            // need to handle this on our own.
            let span = this.shadowRoot.createElementAndAppendChildAt(
              root,
              "span"
            );
            span.textContent = this.mMillisecSeparatorText;
            this.mMillisecField = this.createEditFieldAndAppend(
              "datetime-millisecond",
              "datetime-time-placeholder",
              true,
              this.mMillisecMaxLength,
              this.mMillisecMaxLength,
              this.mMinMillisecond,
              this.mMaxMillisecond,
              this.mMinSecPageUpDownInterval
            );
            this.addEventListenersToField(this.mMillisecField);
          }
          break;
        case "dayPeriod":
          this.mDayPeriodField = this.createEditFieldAndAppend(
            "datetime-dayperiod",
            "datetime-time-placeholder",
            false
          );
          this.addEventListenersToField(this.mDayPeriodField);

          // Give aria autocomplete hint for am/pm
          this.mDayPeriodField.setAttribute("aria-autocomplete", "inline");
          break;
        default: {
          let span = this.shadowRoot.createElementAndAppendChildAt(
            root,
            "span"
          );
          span.textContent = part.value;
          break;
        }
      }
    });
  }

  buildPickerBtn() {
    this.addEventListenersToField(this.mPickerButton);
    // This is to open the picker when the picker button is clicked (this
    // includes padding area).
    this.mPickerButton.addEventListener(
      "click",
      this,
      { mozSystemGroup: true },
      false
    );
  }

  clearInputFields(aFromInputElement) {
    this.log("clearInputFields");

    if (this.mMonthField) {
      this.clearFieldValue(this.mMonthField);
    }

    if (this.mDayField) {
      this.clearFieldValue(this.mDayField);
    }

    if (this.mYearField) {
      this.clearFieldValue(this.mYearField);
    }

    if (this.mHourField) {
      this.clearFieldValue(this.mHourField);
    }

    if (this.mMinuteField) {
      this.clearFieldValue(this.mMinuteField);
    }

    if (this.mSecondField) {
      this.clearFieldValue(this.mSecondField);
    }

    if (this.mMillisecField) {
      this.clearFieldValue(this.mMillisecField);
    }

    if (this.mDayPeriodField) {
      this.clearFieldValue(this.mDayPeriodField);
    }

    if (!aFromInputElement) {
      if (this.mInputElement.value) {
        this.mInputElement.setUserInput("");
      } else {
        this.mInputElement.updateValidityState();
      }
    }
  }

  setFieldsFromInputValue() {
    // Second and millisecond part are optional, rebuild edit fields if
    // needed.
    this.rebuildEditFieldsIfNeeded();

    let value = this.mInputElement.value;
    if (!value) {
      this.clearInputFields(true);
      return;
    }

    let { year, month, day, hour, minute, second, millisecond } =
      this.getInputElementValues();
    if (this.shouldShowDate()) {
      this.log("setFieldsFromInputValue: " + value);
      this.setFieldValue(this.mYearField, year);
      this.setFieldValue(this.mMonthField, month);
      this.setFieldValue(this.mDayField, day);
    }

    if (this.shouldShowTime()) {
      if (this.isEmpty(hour) && this.isEmpty(minute)) {
        this.clearInputFields(true);
        return;
      }

      this.setFieldValue(this.mHourField, hour);
      this.setFieldValue(this.mMinuteField, minute);
      if (this.mHour12) {
        this.setDayPeriodValue(
          hour >= this.mMaxHour ? this.mPMIndicator : this.mAMIndicator
        );
      }

      if (this.mSecondField) {
        this.setFieldValue(this.mSecondField, second || 0);
      }

      if (this.mMillisecField) {
        this.setFieldValue(this.mMillisecField, millisecond || 0);
      }
    }
  }

  setInputValueFromFields() {
    if (this.isAnyFieldEmpty()) {
      // Clear input element's value if any of the field has been cleared,
      // otherwise update the validity state, since it may become "not"
      // invalid if fields are not complete.
      if (this.mInputElement.value) {
        this.mInputElement.setUserInput("");
      } else {
        this.mInputElement.updateValidityState();
      }
      return;
    }

    let { year, month, day, hour, minute, second, millisecond, dayPeriod } =
      this.getCurrentValue();

    let time = "";
    let date = "";

    // Convert to a valid time string according to:
    // https://html.spec.whatwg.org/multipage/infrastructure.html#valid-time-string
    if (this.shouldShowTime()) {
      if (this.mHour12) {
        if (dayPeriod == this.mPMIndicator && hour < this.mMaxHour) {
          hour += this.mMaxHour;
        } else if (dayPeriod == this.mAMIndicator && hour == this.mMaxHour) {
          hour = 0;
        }
      }

      hour = hour < 10 ? "0" + hour : hour;
      minute = minute < 10 ? "0" + minute : minute;

      time = hour + ":" + minute;
      if (second != undefined) {
        second = second < 10 ? "0" + second : second;
        time += ":" + second;
      }

      if (millisecond != undefined) {
        // Convert milliseconds to fraction of second.
        millisecond = millisecond
          .toString()
          .padStart(this.mMillisecMaxLength, "0");
        time += "." + millisecond;
      }
    }

    if (this.shouldShowDate()) {
      // Convert to a valid date string according to:
      // https://html.spec.whatwg.org/multipage/infrastructure.html#valid-date-string
      year = year.toString().padStart(this.mYearLength, "0");
      month = month < 10 ? "0" + month : month;
      day = day < 10 ? "0" + day : day;
      date = [year, month, day].join("-");
    }

    let value;
    if (date) {
      value = date;
    }
    if (time) {
      // https://html.spec.whatwg.org/#valid-normalised-local-date-and-time-string
      value = value ? value + "T" + time : time;
    }

    if (value == this.mInputElement.value) {
      return;
    }
    this.log("setInputValueFromFields: " + value);
    this.mInputElement.setUserInput(value);
  }

  setFieldsFromPicker({ year, month, day, hour, minute, second, millisecond }) {
    if (!this.isEmpty(hour)) {
      this.setFieldValue(this.mHourField, hour);
      if (this.mHour12) {
        this.setDayPeriodValue(
          hour >= this.mMaxHour ? this.mPMIndicator : this.mAMIndicator
        );
      }
    }

    if (!this.isEmpty(minute)) {
      this.setFieldValue(this.mMinuteField, minute);
    }

    if (!this.isEmpty(second)) {
      this.setFieldValue(this.mSecondField, second);
    }

    if (!this.isEmpty(millisecond)) {
      this.setFieldValue(this.mMillisecField, millisecond);
    }

    if (!this.isEmpty(year)) {
      this.setFieldValue(this.mYearField, year);
    }

    if (!this.isEmpty(month)) {
      this.setFieldValue(this.mMonthField, month);
    }

    if (!this.isEmpty(day)) {
      this.setFieldValue(this.mDayField, day);
    }

    // Update input element's .value if needed.
    this.setInputValueFromFields();
  }

  handleKeydown(aEvent) {
    if (!this.isEditable()) {
      return;
    }

    let targetField = aEvent.originalTarget;
    let key = aEvent.key;

    if (targetField == this.mDayPeriodField) {
      if (key == "a" || key == "A") {
        this.setDayPeriodValue(this.mAMIndicator);
      } else if (key == "p" || key == "P") {
        this.setDayPeriodValue(this.mPMIndicator);
      }
      if (!this.isAnyFieldEmpty()) {
        this.setInputValueFromFields();
      }
      return;
    }

    if (targetField.classList.contains("numeric") && key.match(/[0-9]/)) {
      let buffer = targetField.getAttribute("typeBuffer") || "";

      buffer = buffer.concat(key);
      this.setFieldValue(targetField, buffer);

      let n = Number(buffer);
      let max = targetField.getAttribute("max");
      let maxLength = targetField.getAttribute("maxlength");
      if (targetField == this.mHourField) {
        if (n * 10 > 23 || buffer.length === 2) {
          buffer = "";
          this.advanceToNextField();
        }
      } else if (buffer.length >= maxLength || n * 10 > max) {
        buffer = "";
        this.advanceToNextField();
      }
      targetField.setAttribute("typeBuffer", buffer);
    }
  }

  getCurrentValue() {
    let value = {};
    if (this.shouldShowDate()) {
      value.year = this.getFieldValue(this.mYearField);
      value.month = this.getFieldValue(this.mMonthField);
      value.day = this.getFieldValue(this.mDayField);
    }

    if (this.shouldShowTime()) {
      let dayPeriod = this.getDayPeriodValue();
      let hour = this.getFieldValue(this.mHourField);
      if (!this.isEmpty(hour)) {
        if (this.mHour12) {
          if (dayPeriod == this.mPMIndicator && hour < this.mMaxHour) {
            hour += this.mMaxHour;
          } else if (dayPeriod == this.mAMIndicator && hour == this.mMaxHour) {
            hour = 0;
          }
        }
      }
      value.hour = hour;
      value.dayPeriod = dayPeriod;
      value.minute = this.getFieldValue(this.mMinuteField);
      value.second = this.getFieldValue(this.mSecondField);
      value.millisecond = this.getFieldValue(this.mMillisecField);
    }

    this.log("getCurrentValue: " + JSON.stringify(value));
    return value;
  }

  setFieldValue(aField, aValue) {
    if (!aField || !aField.classList.contains("numeric")) {
      return;
    }

    let value = Number(aValue);
    if (isNaN(value)) {
      this.log("NaN on setFieldValue!");
      return;
    }

    if (aField == this.mHourField) {
      if (this.mHour12) {
        // Try to change to 12hr format if user input is 0 or greater
        // than 12.
        switch (true) {
          case value == 0 && aValue.length == 2:
            value = this.mMaxHour;
            this.setDayPeriodValue(this.mAMIndicator);
            break;

          case value == this.mMaxHour:
            this.setDayPeriodValue(this.mPMIndicator);
            break;

          case value < 12:
            if (!this.getDayPeriodValue()) {
              this.setDayPeriodValue(this.mAMIndicator);
            }
            break;

          case value > 12 && value < 24:
            value = value % this.mMaxHour;
            this.setDayPeriodValue(this.mPMIndicator);
            break;

          default:
            value = Math.floor(value / 10);
            break;
        }
      } else if (value > this.mMaxHour) {
        value = this.mMaxHour;
      }
    }

    let maxLength = aField.getAttribute("maxlength");
    if (aValue.length == maxLength) {
      let min = Number(aField.getAttribute("min"));
      let max = Number(aField.getAttribute("max"));

      if (value < min) {
        value = min;
      } else if (value > max) {
        value = max;
      }
    }

    aField.setAttribute("value", value);

    let minDigits = aField.getAttribute("mindigits");
    let formatted = value.toLocaleString(this.mLocales, {
      minimumIntegerDigits: minDigits,
      useGrouping: false,
    });

    aField.textContent = formatted;
    aField.setAttribute("aria-valuetext", formatted);
  }

  isAnyFieldAvailable(aForPicker = false) {
    let { year, month, day, hour, minute, second, millisecond } =
      this.getCurrentValue();
    if (
      !this.isEmpty(year) ||
      !this.isEmpty(month) ||
      !this.isEmpty(day) ||
      !this.isEmpty(hour) ||
      !this.isEmpty(minute)
    ) {
      return true;
    }

    // Picker doesn't care about seconds / milliseconds / day period.
    if (aForPicker) {
      return false;
    }

    let dayPeriod = this.getDayPeriodValue();
    return (
      (this.mDayPeriodField && !this.isEmpty(dayPeriod)) ||
      (this.mSecondField && !this.isEmpty(second)) ||
      (this.mMillisecField && !this.isEmpty(millisecond))
    );
  }

  isAnyFieldEmpty() {
    let { year, month, day, hour, minute, second, millisecond } =
      this.getCurrentValue();
    return (
      (this.mYearField && this.isEmpty(year)) ||
      (this.mMonthField && this.isEmpty(month)) ||
      (this.mDayField && this.isEmpty(day)) ||
      (this.mHourField && this.isEmpty(hour)) ||
      (this.mMinuteField && this.isEmpty(minute)) ||
      (this.mDayPeriodField && this.isEmpty(this.getDayPeriodValue())) ||
      (this.mSecondField && this.isEmpty(second)) ||
      (this.mMillisecField && this.isEmpty(millisecond))
    );
  }

  get kMsPerSecond() {
    return 1000;
  }

  get kMsPerMinute() {
    return 60 * 1000;
  }

  getInputElementValues() {
    let value = this.mInputElement.value;
    if (value.length === 0) {
      return {};
    }

    let date, time;

    let year, month, day, hour, minute, second, millisecond;
    if (this.type == "date") {
      date = value;
    }
    if (this.type == "time") {
      time = value;
    }
    if (this.type == "datetime-local") {
      // https://html.spec.whatwg.org/#valid-normalised-local-date-and-time-string
      [date, time] = value.split("T");
    }
    if (date) {
      [year, month, day] = date.split("-");
    }
    if (time) {
      [hour, minute, second] = time.split(":");
      if (second) {
        [second, millisecond] = second.split(".");

        // Convert fraction of second to milliseconds.
        if (millisecond && millisecond.length === 1) {
          millisecond *= 100;
        } else if (millisecond && millisecond.length === 2) {
          millisecond *= 10;
        }
      }
    }
    return { year, month, day, hour, minute, second, millisecond };
  }

  shouldShowSecondField() {
    if (!this.shouldShowTime()) {
      return false;
    }
    let { second } = this.getInputElementValues();
    if (second != undefined) {
      return true;
    }

    let stepBase = this.mInputElement.getStepBase();
    if (stepBase % this.kMsPerMinute != 0) {
      return true;
    }

    let step = this.mInputElement.getStep();
    if (step % this.kMsPerMinute != 0) {
      return true;
    }

    return false;
  }

  shouldShowMillisecField() {
    if (!this.shouldShowTime()) {
      return false;
    }

    let { millisecond } = this.getInputElementValues();
    if (millisecond != undefined) {
      return true;
    }

    let stepBase = this.mInputElement.getStepBase();
    if (stepBase % this.kMsPerSecond != 0) {
      return true;
    }

    let step = this.mInputElement.getStep();
    if (step % this.kMsPerSecond != 0) {
      return true;
    }

    return false;
  }

  getStringsForLocale(aLocales) {
    this.log("getStringsForLocale: " + aLocales);

    let intlUtils = this.window.intlUtils;
    if (!intlUtils) {
      return {};
    }

    let result = intlUtils.getDisplayNames(this.mLocales, {
      type: "dayPeriod",
      style: "short",
      calendar: "gregory",
      keys: ["am", "pm"],
    });

    let [amString, pmString] = result.values;

    return { amString, pmString };
  }

  is12HourTime(aLocales) {
    let options = new Intl.DateTimeFormat(aLocales, {
      hour: "numeric",
    }).resolvedOptions();

    return options.hour12;
  }

  incrementFieldValue(aTargetField, aTimes) {
    let value = this.getFieldValue(aTargetField);

    // Use current time if field is empty.
    if (this.isEmpty(value)) {
      let now = new Date();

      if (aTargetField == this.mYearField) {
        value = now.getFullYear();
      } else if (aTargetField == this.mMonthField) {
        value = now.getMonth() + 1;
      } else if (aTargetField == this.mDayField) {
        value = now.getDate();
      } else if (aTargetField == this.mHourField) {
        value = now.getHours();
        if (this.mHour12) {
          value = value % this.mMaxHour || this.mMaxHour;
        }
      } else if (aTargetField == this.mMinuteField) {
        value = now.getMinutes();
      } else if (aTargetField == this.mSecondField) {
        value = now.getSeconds();
      } else if (aTargetField == this.mMillisecField) {
        value = now.getMilliseconds();
      } else {
        this.log("Field not supported in incrementFieldValue.");
        return;
      }
    }

    let min = +aTargetField.getAttribute("min");
    let max = +aTargetField.getAttribute("max");

    value += Number(aTimes);
    if (value > max) {
      value -= max - min + 1;
    } else if (value < min) {
      value += max - min + 1;
    }

    this.setFieldValue(aTargetField, value);
  }

  handleKeyboardNav(aEvent) {
    if (!this.isEditable()) {
      return;
    }

    let targetField = aEvent.originalTarget;
    let key = aEvent.key;

    if (targetField == this.mYearField && (key == "Home" || key == "End")) {
      // Home/End key does nothing on year field.
      return;
    }

    if (targetField == this.mDayPeriodField) {
      // Home/End key does nothing on AM/PM field.
      if (key == "Home" || key == "End") {
        return;
      }

      this.setDayPeriodValue(
        this.getDayPeriodValue() == this.mAMIndicator
          ? this.mPMIndicator
          : this.mAMIndicator
      );
      this.setInputValueFromFields();
      return;
    }

    switch (key) {
      case "ArrowUp":
        this.incrementFieldValue(targetField, 1);
        break;
      case "ArrowDown":
        this.incrementFieldValue(targetField, -1);
        break;
      case "PageUp": {
        let interval = targetField.getAttribute("pginterval");
        this.incrementFieldValue(targetField, interval);
        break;
      }
      case "PageDown": {
        let interval = targetField.getAttribute("pginterval");
        this.incrementFieldValue(targetField, 0 - interval);
        break;
      }
      case "Home": {
        let min = targetField.getAttribute("min");
        this.setFieldValue(targetField, min);
        break;
      }
      case "End": {
        let max = targetField.getAttribute("max");
        this.setFieldValue(targetField, max);
        break;
      }
    }
    this.setInputValueFromFields();
  }

  getDayPeriodValue() {
    if (!this.mDayPeriodField) {
      return "";
    }

    let placeholder = this.mDayPeriodField.placeholder;
    let value = this.mDayPeriodField.textContent;
    return value == placeholder ? "" : value;
  }

  setDayPeriodValue(aValue) {
    if (!this.mDayPeriodField) {
      return;
    }

    this.mDayPeriodField.textContent = aValue;
    this.mDayPeriodField.setAttribute("value", aValue);
  }
};
