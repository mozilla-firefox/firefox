/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useCallback } from "react";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";

export function ActivationWindowMessage({
  dispatch,
  handleBlock,
  handleClick,
  handleDismiss,
  messageData,
}) {
  const { content } = messageData;
  const hasButtons = content.primaryButton || content.secondaryButton;
  const hasResponsiveImage =
    !!content.imageSrcResponsive ||
    !!content.imageSrcDarkResponsive ||
    !!content.imageSrcNarrow ||
    !!content.imageSrcDarkNarrow;

  const onDismiss = useCallback(() => {
    handleDismiss();
    handleBlock();
  }, [handleDismiss, handleBlock]);

  const onPrimaryClick = useCallback(() => {
    handleClick("primary-button");
    if (content.primaryButton?.action?.dismiss) {
      handleDismiss();
      handleBlock();
    }

    if (content.primaryButton?.action?.type === "SHOW_PERSONALIZE") {
      dispatch({ type: at.SHOW_PERSONALIZE });
      dispatch(ac.UserEvent({ event: "SHOW_PERSONALIZE" }));
    }
  }, [dispatch, handleClick, handleDismiss, handleBlock, content]);

  const onSecondaryClick = useCallback(() => {
    handleClick("secondary-button");
    if (content.secondaryButton?.action?.dismiss) {
      handleDismiss();
      handleBlock();
    }
  }, [handleClick, handleDismiss, handleBlock, content]);

  return (
    <aside
      className={[
        "activation-window-message",
        !hasButtons ? "no-buttons" : "",
        hasResponsiveImage ? "bleed-image" : "",
      ]
        .filter(Boolean)
        .join(" ")}
    >
      <div className="activation-window-message-dismiss">
        <moz-button
          type="icon ghost"
          iconSrc="chrome://global/skin/icons/close.svg"
          onClick={onDismiss}
          data-l10n-id="newtab-activation-window-message-dismiss-button"
        ></moz-button>
      </div>
      <div className="activation-window-message-inner">
        <picture className="activation-window-message-image">
          {content.imageSrcDark && (
            <source
              srcSet={content.imageSrcDark}
              media="(min-width: 1072px) and (prefers-color-scheme: dark)"
            />
          )}
          {content.imageSrcDarkNarrow && (
            <source
              srcSet={content.imageSrcDarkNarrow}
              media="(min-width: 866px) and (max-width: 1071.98px) and (prefers-color-scheme: dark)"
            />
          )}
          {content.imageSrcDarkResponsive && (
            <source
              srcSet={content.imageSrcDarkResponsive}
              media="(max-width: 865.98px) and (prefers-color-scheme: dark)"
            />
          )}
          {content.imageSrcNarrow && (
            <source
              srcSet={content.imageSrcNarrow}
              media="(min-width: 866px) and (max-width: 1071.98px)"
            />
          )}
          {content.imageSrcResponsive && (
            <source
              srcSet={content.imageSrcResponsive}
              media="(max-width: 865.98px)"
            />
          )}
          <img
            src={
              content.imageSrc ||
              "chrome://newtab/content/data/content/assets/kit-in-circle.svg"
            }
            alt=""
            role="presentation"
          />
        </picture>
        <div>
          {content.heading &&
            (typeof content.heading === "string" ? (
              <h2>{content.heading}</h2>
            ) : (
              <h2 data-l10n-id={content.heading.string_id}></h2>
            ))}
          {content.message &&
            (typeof content.message === "string" ? (
              <p>{content.message}</p>
            ) : (
              <p data-l10n-id={content.message.string_id}></p>
            ))}
          {(content.primaryButton || content.secondaryButton) && (
            <moz-button-group>
              {content.primaryButton &&
                (typeof content.primaryButton.label === "string" ? (
                  <moz-button type="primary" onClick={onPrimaryClick}>
                    {content.primaryButton.label}
                  </moz-button>
                ) : (
                  <moz-button
                    type="primary"
                    onClick={onPrimaryClick}
                    data-l10n-id={content.primaryButton.label.string_id}
                  />
                ))}
              {content.secondaryButton &&
                (typeof content.secondaryButton.label === "string" ? (
                  <moz-button type="default" onClick={onSecondaryClick}>
                    {content.secondaryButton.label}
                  </moz-button>
                ) : (
                  <moz-button
                    type="default"
                    onClick={onSecondaryClick}
                    data-l10n-id={content.secondaryButton.label.string_id}
                  />
                ))}
            </moz-button-group>
          )}
        </div>
      </div>
    </aside>
  );
}
