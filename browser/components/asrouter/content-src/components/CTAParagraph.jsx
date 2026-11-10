/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { Localized, CONFIGURABLE_STYLES } from "./MSLocalized";
import { MultiStageUtils } from "../lib/multistage-utils.mjs";

export const CTAParagraph = props => {
  const { content, handleAction } = props;

  if (!content?.text) {
    return null;
  }

  const onClick = React.useCallback(
    event => {
      handleAction(event);
      event.preventDefault();
    },
    [handleAction]
  );

  const keyPressedRef = React.useRef(false);

  return (
    <h2
      className={`cta-paragraph ${content?.info_tile ? "info-tile" : ""}`}
      style={{
        ...MultiStageUtils.getValidStyle(content?.style, CONFIGURABLE_STYLES),
      }}
    >
      <div className="cta-paragraph-icon-wrapper">
        <img
          className="cta-paragraph-icon"
          src={content?.icon?.iconURL}
          style={MultiStageUtils.getValidStyle(
            content?.icon,
            CONFIGURABLE_STYLES
          )}
        ></img>
      </div>
      <Localized text={content.text}>
        {content.text.string_name && typeof handleAction === "function" ? (
          <span
            data-l10n-id={content.text.string_id}
            onClick={onClick}
            onKeyDown={event => {
              keyPressedRef.current = ["Enter", " "].includes(event.key);
            }}
            onKeyUp={event => {
              if (keyPressedRef.current && ["Enter", " "].includes(event.key)) {
                onClick(event);
              }
              keyPressedRef.current = false;
            }}
            value="cta_paragraph"
          >
            {" "}
            {/* <a> is valid here because of click and keyup handling. */}
            {/* <button> cannot be used due to fluent integration. <a> content is provided by fluent */}
            <a
              data-l10n-name={content.text.string_name}
              tabIndex="0"
              role="link"
            ></a>
          </span>
        ) : null}
      </Localized>
    </h2>
  );
};
