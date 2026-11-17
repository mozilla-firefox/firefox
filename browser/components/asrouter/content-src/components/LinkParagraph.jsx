/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useCallback } from "react";
import {
  Localized,
  pickConfigurableStyles,
  resolveImageSrc,
} from "./MSLocalized";

function renderSegment(segment, index, handleAction) {
  if (typeof segment === "string") {
    return segment;
  }

  if (segment?.imageURL) {
    return (
      <img
        key={index}
        className="inline-icon"
        src={resolveImageSrc(segment)}
        alt={segment.alt ?? ""}
        style={pickConfigurableStyles(segment)}
      />
    );
  }
  if (segment?.action) {
    return (
      <a
        key={index}
        href={segment.href}
        value={segment.id}
        role={segment.href ? null : "link"}
        className="text-link"
        tabIndex="0"
        onClick={event => {
          event.preventDefault();
          handleAction(event, segment.action);
        }}
        onKeyPress={event => {
          if (event.key === "Enter" && !event.repeat) {
            event.preventDefault();
            handleAction(event, segment.action);
          }
        }}
      >
        <Localized text={segment}>
          <span />
        </Localized>
      </a>
    );
  }
  if (segment?.href) {
    const action = {
      type: "OPEN_URL",
      data: { args: segment.href, where: segment.where || "tab" },
    };
    return (
      <a
        key={index}
        href={segment.href}
        className="text-link"
        onClick={event => {
          event.preventDefault();
          handleAction(event, action);
        }}
      >
        <Localized text={segment}>
          <span />
        </Localized>
      </a>
    );
  }
  if (segment?.link_key) {
    return (
      // eslint-disable-next-line jsx-a11y/anchor-is-valid
      <a
        key={index}
        value={segment.link_key}
        role="link"
        className="text-link"
        tabIndex="0"
        onClick={handleAction}
        onKeyPress={event => {
          if (event.key === "Enter" && !event.repeat) {
            handleAction(event);
          }
        }}
      >
        <Localized text={segment}>
          <span />
        </Localized>
      </a>
    );
  }
  return (
    <Localized key={index} text={segment}>
      <span />
    </Localized>
  );
}

export const LinkParagraph = props => {
  const { text_content, handleAction } = props;
  const text = text_content?.text;
  const handleParagraphAction = useCallback(
    event => {
      const anchor = event.target.closest("a");
      if (anchor) {
        handleAction({ ...event, currentTarget: anchor });
      }
    },
    [handleAction]
  );

  const onKeyPress = useCallback(
    event => {
      if (event.key === "Enter" && !event.repeat) {
        handleParagraphAction(event);
      }
    },
    [handleParagraphAction]
  );

  const paragraphClassName =
    text_content?.font_styles === "legal"
      ? "legal-paragraph"
      : "link-paragraph";

  if (Array.isArray(text)) {
    return (
      <p
        className={paragraphClassName}
        style={pickConfigurableStyles(text_content)}
      >
        {text.map((segment, index) =>
          renderSegment(segment, index, handleAction)
        )}
      </p>
    );
  }

  return (
    <Localized text={text}>
      {/* eslint-disable jsx-a11y/no-noninteractive-element-interactions */}
      <p
        className={paragraphClassName}
        onClick={handleParagraphAction}
        value="link_paragraph"
        onKeyPress={onKeyPress}
      >
        {/* eslint-disable jsx-a11y/anchor-is-valid */}
        {text_content.link_keys?.map(link => (
          <a
            key={link}
            value={link}
            role="link"
            className="text-link"
            data-l10n-name={link}
            // must pass in tabIndex when no href is provided
            tabIndex="0"
          >
            {" "}
          </a>
        ))}
      </p>
    </Localized>
  );
};
