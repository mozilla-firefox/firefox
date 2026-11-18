/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://html.spec.whatwg.org/multipage/input.html#the-input-element
 * https://html.spec.whatwg.org/multipage/obsolete.html#other-elements%2C-attributes-and-apis
 * https://wicg.github.io/entries-api/#idl-index
 *
 * © Copyright 2004-2011 Apple Computer, Inc., Mozilla Foundation, and
 * Opera Software ASA. You are granted a license to use, reproduce
 * and create derivative works of this document.
 */

enum SelectionMode {
  "select",
  "start",
  "end",
  "preserve",
};

interface XULControllers;

[Exposed=Window]
interface HTMLInputElement : HTMLElement {
  [HTMLConstructor] constructor();

  [CEReactions, Pure, SetterThrows]
           attribute DOMString accept;
  [CEReactions, Pure, SetterThrows, Pref="dom.forms.alpha.enabled"]
           attribute boolean alpha;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString alt;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString autocomplete;
  [CEReactions, Pure, SetterThrows, Pref="dom.capture.enabled"]
           attribute DOMString capture;
  [CEReactions, Pure, SetterThrows]
           attribute boolean defaultChecked;
  [Pure]
           attribute boolean checked;
  [CEReactions, Pure, SetterThrows, Pref="dom.forms.colorspace.enabled"]
           attribute DOMString colorSpace;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString dirName;
  [CEReactions, Pure, SetterThrows]
           attribute boolean disabled;
  [BinaryName=formForBindings]
  readonly attribute Element? form;
  [Pure]
           attribute FileList? files;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString formAction;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString formEnctype;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString formMethod;
  [CEReactions, Pure, SetterThrows]
           attribute boolean formNoValidate;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString formTarget;
  [CEReactions, SetterThrows]
           attribute unsigned long height;
  [Pure]
           attribute boolean indeterminate;
  [Pure, BinaryName=listForBindings]
  readonly attribute Element? list;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString max;
  [CEReactions, Pure, SetterThrows]
           attribute long maxLength;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString min;
  [CEReactions, Pure, SetterThrows]
           attribute long minLength;
  [CEReactions, Pure, SetterThrows]
           attribute boolean multiple;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString name;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString pattern;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString placeholder;
  [CEReactions, Pure, SetterThrows]
           attribute boolean readOnly;
  [CEReactions, Pure, SetterThrows]
           attribute boolean required;
  [CEReactions, Pure, SetterThrows]
           attribute unsigned long size;
  [CEReactions, Pure, SetterNeedsSubjectPrincipal=NonSystem, SetterThrows]
           attribute DOMString src;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString step;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString type;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString defaultValue;
  [CEReactions, Pure, SetterThrows, NeedsCallerType]
           attribute [LegacyNullToEmptyString] DOMString value;
  [Throws]
           attribute object? valueAsDate;
  [Pure, SetterThrows]
           attribute unrestricted double valueAsNumber;
  [CEReactions, SetterThrows]
           attribute unsigned long width;

  [Throws]
  undefined stepUp(optional long n = 1);
  [Throws]
  undefined stepDown(optional long n = 1);

  [Pure]
  readonly attribute boolean willValidate;
  [Pure]
  readonly attribute ValidityState validity;
  [Throws]
  readonly attribute DOMString validationMessage;
  boolean checkValidity();
  boolean reportValidity();
  undefined setCustomValidity(DOMString error);

  [BinaryName=labelsForBindings]
  readonly attribute NodeList? labels;

  undefined select();

  [Throws]
           attribute unsigned long? selectionStart;
  [Throws]
           attribute unsigned long? selectionEnd;
  [Throws]
           attribute DOMString? selectionDirection;
  [Throws]
  undefined setRangeText(DOMString replacement);
  [Throws]
  undefined setRangeText(DOMString replacement, unsigned long start,
    unsigned long end, optional SelectionMode selectionMode = "preserve");
  [Throws]
  undefined setSelectionRange(unsigned long start, unsigned long end, optional DOMString direction);

  [Throws]
  undefined showPicker();

  // also has obsolete members
};

partial interface HTMLInputElement {
  [CEReactions, Pure, SetterThrows]
           attribute DOMString align;
  [CEReactions, Pure, SetterThrows]
           attribute DOMString useMap;
};

// Mozilla extensions

partial interface HTMLInputElement {
  [GetterThrows, ChromeOnly]
  readonly attribute XULControllers?       controllers;
  // Binaryname because we have a FragmentOrElement function named "TextLength()".
  [NeedsCallerType, BinaryName="inputTextLength"]
  readonly attribute long                  textLength;

  [Throws, ChromeOnly]
  sequence<DOMString> mozGetFileNameArray();

  [ChromeOnly, Throws]
  undefined mozSetFileNameArray(sequence<DOMString> fileNames);

  [ChromeOnly]
  undefined mozSetFileArray(sequence<File> files);

  // This method is meant to use for testing only.
  [ChromeOnly, Throws]
  undefined mozSetDirectory(DOMString directoryPath);

  // This method is meant to use for testing only.
  [ChromeOnly]
  undefined mozSetDndFilesAndDirectories(sequence<(File or Directory)> list);

  // This method is meant to use for testing only.
  [ChromeOnly, NewObject]
  Promise<sequence<(File or Directory)>> getFilesAndDirectories();

  [ChromeOnly]
  boolean mozIsTextField(boolean aExcludePassword);

  [ChromeOnly]
  readonly attribute boolean hasBeenTypePassword;

  [ChromeOnly]
  attribute DOMString previewValue;

  // A string indicating that the value of the element has been autofilled:
  // either "filled", "preview" or "".
  [ChromeOnly]
  attribute DOMString autofillState;

  // Last value entered by the user, not by a script.
  // NOTE(emilio): As of right now some execCommand triggered changes might be
  // considered interactive.
  [ChromeOnly]
  readonly attribute DOMString lastInteractiveValue;

  [ChromeOnly]
  // This function will return null if @autocomplete is not defined for the
  // current @type
  AutocompleteInfo? getAutocompleteInfo();

  [ChromeOnly]
  // The reveal password state for a type=password control.
  attribute boolean revealPassword;
};

interface mixin MozEditableElement {
  // Returns an nsIEditor instance which is associated with the element.
  // If the element can be associated with an editor but not yet created,
  // this creates new one automatically.
  [ChromeOnly, BinaryName="editorForBindings"]
  readonly attribute nsIEditor? editor;

  // Returns true if an nsIEditor instance has already been associated with
  // the element.
  [Pure, ChromeOnly]
  readonly attribute boolean hasEditor;

  // This is set to true if "input" event should be fired with InputEvent on
  // the element.  Otherwise, i.e., if "input" event should be fired with
  // Event, set to false.
  [ChromeOnly]
  readonly attribute boolean isInputEventTarget;

  // This is similar to set .value on nsIDOMInput/TextAreaElements, but handling
  // of the value change is closer to the normal user input, so 'change' event
  // for example will be dispatched when focusing out the element.
  [Func="IsChromeOrUAWidget", NeedsSubjectPrincipal]
  undefined setUserInput(DOMString input);
};

HTMLInputElement includes MozEditableElement;

HTMLInputElement includes MozImageLoadingContent;

HTMLInputElement includes PopoverInvokerElement;

// https://wicg.github.io/entries-api/#idl-index
partial interface HTMLInputElement {
  [Pref="dom.webkitBlink.filesystem.enabled", Frozen, Cached, Pure]
  readonly attribute sequence<FileSystemEntry> webkitEntries;

  [Pref="dom.webkitBlink.dirPicker.enabled", BinaryName="WebkitDirectoryAttr", SetterThrows]
          attribute boolean webkitdirectory;
};

// Chrome-only functions for datetime picker

dictionary DateTimeValue {
  long hour;
  long minute;
  long second;
  long millisecond;
  long year;
  long month;
  long day;
};

partial interface HTMLInputElement {
  [ChromeOnly]
  DateTimeValue getDateTimeInputBoxValue();

  [ChromeOnly]
  readonly attribute Element? dateTimeBoxElement;

  [ChromeOnly, BinaryName="getMinimumAsDouble"]
  double getMinimum();

  [ChromeOnly, BinaryName="getMaximumAsDouble"]
  double getMaximum();

  [Func="IsChromeOrUAWidget"]
  undefined openDateTimePicker(optional DateTimeValue initialValue = {});

  [Func="IsChromeOrUAWidget"]
  undefined closeDateTimePicker();

  [Func="IsChromeOrUAWidget"]
  undefined setOpenState(boolean aIsOpen);

  [Func="IsChromeOrUAWidget"]
  undefined setFocusState(boolean aIsFocused);

  [Func="IsChromeOrUAWidget"]
  undefined updateValidityState();

  [Func="IsChromeOrUAWidget", BinaryName="getStepAsDouble"]
  double getStep();

  [Func="IsChromeOrUAWidget", BinaryName="getStepBaseAsDouble"]
  double getStepBase();
};

// Chrome-only functions for color picker

dictionary InputPickerColor {
  required float component1;
  required float component2;
  required float component3;

  required unrestricted float alpha;
  // bug 2009748
  // required InputColorSpace colorSpace;
};

partial interface HTMLInputElement {
  [Func="IsChromeOrUAWidget"]
  InputPickerColor getColor();

  [Func="IsChromeOrUAWidget"]
  undefined setUserInputColor(InputPickerColor aColor);
};
