/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, { useState } from "react";
import { actionCreators as ac } from "common/Actions.mjs";
import { LinkMenu } from "../../LinkMenu/LinkMenu";

/**
 * A context menu for IAB banners (e.g. billboard, leaderboard).
 *
 * Note: MREC ad formats and sponsored stories share the context menu with
 * other cards: make sure you also look at DSLinkMenu component
 * to keep any updates to ad-related context menu items in sync.
 *
 * @param dispatch
 * @param spoc
 * @param position
 * @param type
 * @param showAdReporting
 * @returns {Element}
 * @constructor
 */
export function AdBannerContextMenu({
  dispatch,
  spoc,
  position,
  type,
  showAdReporting,
}) {
  const ADBANNER_CONTEXT_MENU_OPTIONS = [
    "BlockAdUrl",
    ...(showAdReporting ? ["ReportAd"] : []),
    "ManageSponsoredContent",
    "OurSponsorsAndYourPrivacy",
  ];

  const [showContextMenu, setShowContextMenu] = useState(false);
  const [contextMenuClassNames, setContextMenuClassNames] =
    useState("ads-context-menu");

  // The keyboard access parameter is passed down to LinkMenu component
  // that uses it to focus on the first context menu option for accessibility.
  const [isKeyboardAccess, setIsKeyboardAccess] = useState(false);

  /**
   * Toggles the style fix for context menu hover/active styles.
   * This allows us to have unobtrusive, transparent button background by default,
   * yet flip it over to semi-transparent grey when the menu is visible.
   *
   * @param contextMenuOpen
   */
  const toggleContextMenuStyleSwitch = contextMenuOpen => {
    if (contextMenuOpen) {
      setContextMenuClassNames("ads-context-menu context-menu-open");
    } else {
      setContextMenuClassNames("ads-context-menu");
    }
  };

  /**
   * Toggles the context menu to open or close. Sets state depending on whether
   * the context menu is accessed by mouse or keyboard.
   *
   * @param isKeyBoard
   */
  const toggleContextMenu = isKeyBoard => {
    toggleContextMenuStyleSwitch(!showContextMenu);
    setShowContextMenu(!showContextMenu);
    setIsKeyboardAccess(isKeyBoard);
  };

  const onClick = e => {
    e.preventDefault();
    toggleContextMenu(false);
  };

  const onKeyDown = e => {
    if (e.key === "Enter" || e.key === " ") {
      e.preventDefault();
      toggleContextMenu(true);
    }
  };

  const onUpdate = () => {
    toggleContextMenuStyleSwitch(!showContextMenu);
    setShowContextMenu(!showContextMenu);
  };

  return (
    <div className="ads-context-menu-wrapper">
      <div className={contextMenuClassNames}>
        <moz-button
          type="icon"
          size="default"
          iconsrc="chrome://global/skin/icons/more.svg"
          onClick={onClick}
          onKeyDown={onKeyDown}
        />
        {showContextMenu && (
          <LinkMenu
            onUpdate={onUpdate}
            dispatch={dispatch}
            keyboardAccess={isKeyboardAccess}
            options={ADBANNER_CONTEXT_MENU_OPTIONS}
            shouldSendImpressionStats={true}
            userEvent={ac.DiscoveryStreamUserEvent}
            site={{
              // Props we want to pass on for new ad types that come from Unified Ads API
              block_key: spoc.block_key,
              fetchTimestamp: spoc.fetchTimestamp,
              flight_id: spoc.flight_id,
              format: spoc.format,
              id: spoc.id,
              guid: spoc.guid,
              card_type: "spoc",
              // required to record telemetry for an action, see handleBlockUrl in TelemetryFeed.sys.mjs
              is_pocket_card: true,
              position,
              sponsor: spoc.sponsor,
              title: spoc.title,
              url: spoc.url || spoc.shim.url,
              personalization_models: spoc.personalization_models,
              priority: spoc.priority,
              score: spoc.score,
              alt_text: spoc.alt_text,
              shim: spoc.shim,
            }}
            index={position}
            source={type.toUpperCase()}
          />
        )}
      </div>
    </div>
  );
}
