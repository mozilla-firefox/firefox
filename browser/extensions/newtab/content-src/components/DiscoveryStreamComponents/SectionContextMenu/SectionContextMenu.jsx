/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, { useState } from "react";
import { LinkMenu } from "../../LinkMenu/LinkMenu";

/**
 * A context menu for blocking, following and unfollowing sections.
 *
 * @param props
 * @returns {React.FunctionComponent}
 */
export function SectionContextMenu({
  type = "DISCOVERY_STREAM",
  title,
  source,
  index,
  dispatch,
  sectionKey,
  following,
  sectionPersonalization,
  sectionPosition,
}) {
  // Initial context menu options: block this section only.
  const SECTIONS_CONTEXT_MENU_OPTIONS = ["SectionBlock"];
  const [showContextMenu, setShowContextMenu] = useState(false);

  if (following) {
    SECTIONS_CONTEXT_MENU_OPTIONS.push("SectionUnfollow");
  }

  const onClick = e => {
    e.preventDefault();
    setShowContextMenu(!showContextMenu);
  };

  const onUpdate = () => {
    setShowContextMenu(!showContextMenu);
  };

  return (
    <div className="section-context-menu">
      <moz-button
        type="icon"
        size="default"
        iconsrc="chrome://global/skin/icons/more.svg"
        title={title || source}
        onClick={onClick}
      />
      {showContextMenu && (
        <LinkMenu
          onUpdate={onUpdate}
          dispatch={dispatch}
          index={index}
          source={type.toUpperCase()}
          options={SECTIONS_CONTEXT_MENU_OPTIONS}
          shouldSendImpressionStats={true}
          site={{
            sectionPersonalization,
            sectionKey,
            sectionPosition,
            title,
          }}
        />
      )}
    </div>
  );
}
