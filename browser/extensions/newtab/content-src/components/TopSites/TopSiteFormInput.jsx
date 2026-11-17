/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useState, useEffect, useRef } from "react";

export function TopSiteFormInput({
  shouldFocus,
  validationError: validationErrorProp = false,
  value = "",
  onClear,
  onChange,
  loading,
  typeUrl,
  titleId,
  placeholderId,
  errorMessageId,
  autoFocusOnOpen,
}) {
  const [validationError, setValidationError] = useState(validationErrorProp);
  const inputRef = useRef(null);
  const prevShouldFocusRef = useRef(false);

  useEffect(() => {
    if (shouldFocus && !prevShouldFocusRef.current && inputRef.current) {
      inputRef.current.focus();
    }
    prevShouldFocusRef.current = shouldFocus;
  }, [shouldFocus]);

  useEffect(() => {
    setValidationError(validationErrorProp);
  }, [validationErrorProp]);

  const handleChange = ev => {
    if (validationError) {
      setValidationError(false);
    }
    onChange(ev);
  };

  const renderLoadingOrCloseButton = () => {
    const showClearButton = value && onClear;

    if (loading) {
      return (
        <div className="loading-container">
          <div className="loading-animation" />
        </div>
      );
    } else if (showClearButton) {
      return (
        <moz-button
          className="icon-clear-input"
          type="icon ghost"
          size="small"
          iconSrc="chrome://global/skin/icons/close.svg"
          onClick={onClear}
          data-l10n-id="newtab-topsites-clear-input"
        />
      );
    }
    return null;
  };

  return (
    <label>
      <span data-l10n-id={titleId} />
      <div
        className={`field ${typeUrl ? "url" : ""}${
          validationError ? " invalid" : ""
        }`}
      >
        <input
          type="text"
          value={value}
          ref={inputRef}
          onChange={handleChange}
          data-l10n-id={placeholderId}
          // Set focus on error if the url field is valid or when the input is first rendered and is empty
          // eslint-disable-next-line jsx-a11y/no-autofocus
          autoFocus={autoFocusOnOpen}
          disabled={loading}
        />
        {renderLoadingOrCloseButton()}
        {validationError && (
          <aside className="error-tooltip" data-l10n-id={errorMessageId} />
        )}
      </div>
    </label>
  );
}
