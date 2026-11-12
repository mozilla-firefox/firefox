/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useCallback } from "react";
import { useSelector } from "react-redux";
import { actionTypes as at } from "common/Actions.mjs";
import {
  CUSTOMIZE_PANEL_SOURCES,
  WALLPAPER_CATEGORIES,
} from "content-src/lib/constants.mjs";
import { FeatureHighlight } from "./FeatureHighlight";

export function WallpaperFeatureHighlight({
  position,
  dispatch,
  handleDismiss,
  handleClick,
  handleBlock,
}) {
  // @nova-cleanup(remove-pref): Remove the nova.enabled pref check and keep the Nova copy and image path as the default once Nova ships.
  const isNova = useSelector(state => state.Prefs.values["nova.enabled"]);
  // Message content from OMC; drives the copy, feature id, and deep-link category.
  const { messageData } = useSelector(state => state.Messages);
  const onDismiss = useCallback(() => {
    handleDismiss();
    handleBlock();
  }, [handleDismiss, handleBlock]);

  const onToggleClick = useCallback(
    elementId => {
      // Deep-link the customize panel into a wallpaper category. OMC can
      // target any known category per-message (not just Firefox): an unset
      // value defaults to Firefox to keep existing messages working, a known
      // category is forwarded as-is, and an explicit empty or unrecognized
      // value opens the top-level panel (no deep-link).
      const { wallpaperCategory } = messageData?.content || {};
      let category = WALLPAPER_CATEGORIES.Firefox;
      if (wallpaperCategory !== undefined) {
        category = Object.values(WALLPAPER_CATEGORIES).includes(
          wallpaperCategory
        )
          ? wallpaperCategory
          : null;
      }
      // The custom-wallpaper category has no dedicated subsection yet, so
      // deep-linking into it opens a blank panel. Fall back to the top-level
      // panel for now; remove this guard once a user's uploaded custom
      // wallpapers get their own subsection.
      if (category === WALLPAPER_CATEGORIES.CustomWallpaper) {
        category = null;
      }
      dispatch({
        type: at.SHOW_PERSONALIZE,
        data: {
          source: CUSTOMIZE_PANEL_SOURCES.WALLPAPER_FEATURE_HIGHLIGHT,
          wallpaperCategory: category,
        },
      });
      handleClick(elementId);
      onDismiss();
    },
    [dispatch, onDismiss, handleClick, messageData]
  );

  return (
    <div
      className={`wallpaper-feature-highlight ${messageData.content?.darkModeDismiss ? "is-inverted-dark-dismiss-button" : ""}`}
    >
      <FeatureHighlight
        position={position}
        data-l10n-id="feature-highlight-wallpaper"
        feature={messageData.content.feature}
        dispatch={dispatch}
        modalClassName="wallpaper-feature-highlight-modal"
        message={
          <div className="wallpaper-feature-highlight-content">
            <picture
              className={
                isNova
                  ? "wallpaper-feature-highlight-image"
                  : "follow-section-button-highlight-image"
              }
            >
              <source
                srcSet={
                  messageData.content?.darkModeImageURL ||
                  (isNova
                    ? "chrome://newtab/content/data/content/assets/highlights/firefox-mascot-prop-paintbucket-rgb.svg"
                    : "chrome://newtab/content/data/content/assets/highlights/omc-newtab-wallpapers.svg")
                }
                media="(prefers-color-scheme: dark)"
              />
              <source
                srcSet={
                  messageData.content?.imageURL ||
                  (isNova
                    ? "chrome://newtab/content/data/content/assets/highlights/firefox-mascot-prop-paintbucket-rgb.svg"
                    : "chrome://newtab/content/data/content/assets/highlights/omc-newtab-wallpapers.svg")
                }
                media="(prefers-color-scheme: light)"
              />
              <img
                width={isNova ? "207" : "320"}
                height={isNova ? "156" : "195"}
                alt=""
              />
            </picture>
            <div className="wallpaper-feature-highlight-copy">
              {messageData.content?.cardTitle ? (
                <p className="title">{messageData.content.cardTitle}</p>
              ) : (
                <p
                  className="title"
                  data-l10n-id={
                    messageData.content.title ||
                    (isNova
                      ? "newtab-wallpaper-feature-highlight-title"
                      : "newtab-new-user-custom-wallpaper-title")
                  }
                />
              )}
              {messageData.content?.cardMessage ? (
                <p className="subtitle">{messageData.content.cardMessage}</p>
              ) : (
                <p
                  className="subtitle"
                  data-l10n-id={
                    messageData.content.subtitle ||
                    (isNova
                      ? "newtab-wallpaper-feature-highlight-subtitle"
                      : "newtab-new-user-custom-wallpaper-subtitle")
                  }
                />
              )}
            </div>
            <span className="button-wrapper">
              {messageData.content?.cardCta ? (
                <moz-button
                  type={isNova ? "primary" : "default"}
                  onClick={() => onToggleClick("open-customize-menu")}
                  label={messageData.content.cardCta}
                />
              ) : (
                <moz-button
                  type={isNova ? "primary" : "default"}
                  onClick={() => onToggleClick("open-customize-menu")}
                  data-l10n-id={
                    messageData.content.cta ||
                    (isNova
                      ? "newtab-wallpaper-feature-highlight-cta"
                      : "newtab-new-user-custom-wallpaper-cta")
                  }
                />
              )}
            </span>
          </div>
        }
        toggle={<div className="icon icon-help"></div>}
        openedOverride={true}
        showButtonIcon={false}
        dismissCallback={onDismiss}
        outsideClickCallback={handleDismiss}
      />
    </div>
  );
}
