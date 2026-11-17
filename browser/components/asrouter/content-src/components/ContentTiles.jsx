/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useState, useEffect } from "react";
import { Localized } from "./MSLocalized";
import { AddonsPicker } from "./AddonsPicker";
import { SingleSelect } from "./SingleSelect";
import { MobileDownloads } from "./MobileDownloads";
import { MultiSelect } from "./MultiSelect";
import { TextAreaTile } from "./TextAreaTile";
import { EmbeddedMigrationWizard } from "./EmbeddedMigrationWizard";
import { EmbeddedThemePicker } from "./EmbeddedThemePicker";
import { EmbeddedFxBackupOptIn } from "./EmbeddedFxBackupOptIn";
import { ActionChecklist } from "./ActionChecklist";
import { EmbeddedBrowser } from "./EmbeddedBrowser";
import { ConfirmationChecklist } from "./ConfirmationChecklist";
import { MultiStageUtils } from "../lib/multistage-utils.mjs";
import { EmbeddedBackupRestore } from "./EmbeddedBackupRestore";
import { PinnableSitesList } from "./PinnableSitesList";
import { ContentToggle } from "./ContentToggle";
import { TextBoxTile } from "./TextBoxTile";
import { LinkParagraph } from "./LinkParagraph";

const HEADER_STYLES = [
  "backgroundColor",
  "border",
  "padding",
  "margin",
  "width",
  "height",
];

const TILE_STYLES = [
  "border",
  "borderRadius",
  "marginBlock",
  "marginInline",
  "paddingBlock",
  "paddingInline",
];

const CONTAINER_STYLES = [
  "padding",
  "margin",
  "marginBlock",
  "marginInline",
  "paddingBlock",
  "paddingInline",
  "flexDirection",
  "flexWrap",
  "flexFlow",
  "flexGrow",
  "flexShrink",
  "justifyContent",
  "alignItems",
  "gap",
];

/**
 * @param {object|object[]} tiles - The tiles of the screen being shown.
 * @returns {object} Impression context, if any.
 */
export function getTileImpressionContext(tiles) {
  const pinnableSites = (Array.isArray(tiles) ? tiles : [tiles]).find(
    tile => tile?.type === "pinnable_sites" && Array.isArray(tile.data)
  );
  if (!pinnableSites) {
    return {};
  }
  return {
    total_sites: pinnableSites.data.length,
    personalized_sites: pinnableSites.data.filter(item => item?.personalized)
      .length,
  };
}

export const ContentTiles = props => {
  const { content } = props;
  const [expandedTileIndex, setExpandedTileIndex] = useState(null);
  // State for header that toggles showing and hiding all tiles, if applicable
  const [tilesHeaderExpanded, setTilesHeaderExpanded] = useState(false);
  const { tiles } = content;
  if (!tiles) {
    return null;
  }

  // eslint-disable-next-line react-hooks/rules-of-hooks
  useEffect(() => {
    // Run once when ContentTiles mounts to prefill activeMultiSelect
    if (!props.activeMultiSelect) {
      const tilesArray = Array.isArray(tiles) ? tiles : [tiles];

      tilesArray.forEach((tile, index) => {
        if (tile.type !== "multiselect" || !tile.data) {
          return;
        }

        const multiSelectId = `tile-${index}`;
        const newActiveMultiSelect = [];

        tile.data.forEach(({ id, defaultValue }) => {
          if (defaultValue && id) {
            newActiveMultiSelect.push(id);
          }
        });

        if (newActiveMultiSelect.length) {
          props.setActiveMultiSelect(newActiveMultiSelect, multiSelectId);
        }
      });
    }
  }, [tiles]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    /**
     * In Spotlight dialogs, the VO cursor can move without changing DOM focus.
     * When a user lands on content tiles, or a tile re-renders, DOM focus often
     * stays on or "snaps back" to the dialog’s first tabbable control by
     * SubDialog’s focus enforcement. Pressing Space/Enter then activates that
     * outside control instead of the VO target.
     *
     * To address this, we remember the last real DOM-focused element inside
     * #content-tiles-container. If focus jumps outside tiles without a recent
     * tab, such as with a VO focus move, restore focus to that element on the
     * next rAF. Tab navigation is unaffected.
     */
    const page =
      document.querySelector(
        "#multi-stage-message-root.onboardingContainer[data-page]"
      )?.dataset.page || document.location.href;

    if (page !== "spotlight") {
      return () => {};
    }

    const tilesEl = document.getElementById("content-tiles-container");
    const dialog = tilesEl?.closest('main[role="alertdialog"]') || null;
    if (!tilesEl || !dialog) {
      return () => {};
    }

    // We use 250ms to tell “intentional tab navigation” from a programmatic
    // snap. It’s long enough to cover a human Tab press (and a quick double-tab
    // / key repeat), but short enough that we don’t delay correcting unintended
    // focus jumps.
    const TAB_GRACE_WINDOW_MS = 250;
    let lastTilesEl = null;
    let lastTabAt = 0;
    let restoring = false;
    let tabFromTiles = false;

    function onKeyDown(e) {
      if (e.key === "Tab") {
        lastTabAt = performance.now();

        if (tilesEl.contains(document.activeElement)) {
          tabFromTiles = true;
        }
      }
    }

    function onFocusIn(event) {
      const { target } = event;

      // Track true DOM focus inside tiles.
      if (tilesEl.contains(target)) {
        lastTilesEl = target;
        // Reset when focus enters tiles
        tabFromTiles = false;
        return;
      }

      // If focus left tiles without a recent tab, treat as a programmatic snap.
      const tabRecently = performance.now() - lastTabAt < TAB_GRACE_WINDOW_MS;
      if (
        tabRecently ||
        !lastTilesEl ||
        !document.contains(lastTilesEl) ||
        restoring
      ) {
        tabFromTiles = false;
        return;
      }

      // If tab was pressed while in tiles and focus moved to action buttons, don't restore focus for the intended keyboard navigation
      const actionButtons = dialog.querySelector(".action-buttons");
      if (actionButtons?.contains(target) && tabFromTiles) {
        tabFromTiles = false;
        return;
      }

      // Restore immediately (before paint) to avoid visible flicker.
      restoring = true;
      try {
        lastTilesEl.focus({ preventScroll: true });
      } finally {
        restoring = false;
      }
    }

    // Preempt other dialog handlers.
    dialog.addEventListener("keydown", onKeyDown, true);
    dialog.addEventListener("focusin", onFocusIn, true);

    return () => {
      dialog.removeEventListener("keydown", onKeyDown, true);
      dialog.removeEventListener("focusin", onFocusIn, true);
    };
  }, []);

  const toggleTile = (index, tile) => {
    const tileId = `${tile.type}${tile.id ? "_" : ""}${tile.id ?? ""}_header`;
    MultiStageUtils.sendActionTelemetry(
      props.messageId,
      tileId,
      "CLICK_BUTTON"
    );
    if (tile.type === "link" && tile.action) {
      props.handleAction(
        {
          currentTarget: {
            value: tileId,
          },
        },
        tile.action
      );
    } else {
      setExpandedTileIndex(prevIndex => (prevIndex === index ? null : index));
    }
  };

  const toggleTiles = () => {
    setTilesHeaderExpanded(prev => !prev);
    MultiStageUtils.sendActionTelemetry(
      props.messageId,
      "content_tiles_header",
      "CLICK_BUTTON"
    );
  };

  function getTileMultiSelects(screenMultiSelects, index) {
    return screenMultiSelects?.[`tile-${index}`];
  }

  function getTileActiveMultiSelect(activeMultiSelect, index) {
    return activeMultiSelect?.[`tile-${index}`];
  }

  const renderContentTile = (tile, index = 0) => {
    const isExpanded = expandedTileIndex === index;
    const { header, title, subtitle } = tile;

    const tileHeaderProps =
      tile.type === "link"
        ? { role: "link" }
        : {
            "aria-expanded": isExpanded,
            "aria-controls": `tile-content-${index}`,
          };

    const headerTitle =
      tile.type === "textbox" && props.contentToggleChecked === false
        ? (header?.alternateTitle ?? header?.title)
        : header?.title;

    return (
      <div
        key={index}
        className={`content-tile ${header ? "has-header" : ""}`}
        style={MultiStageUtils.getTileStyle(tile, TILE_STYLES)}
      >
        {header?.title && (
          <button
            className={`tile-header secondary${header.linkStyle ? " link-style" : ""}`}
            onClick={() => toggleTile(index, tile)}
            {...tileHeaderProps}
            style={MultiStageUtils.getValidStyle(header.style, HEADER_STYLES)}
          >
            <div className="header-text-container">
              <Localized text={headerTitle}>
                <span className="header-title" />
              </Localized>
              {header.subtitle && (
                <Localized text={header.subtitle}>
                  <span className="header-subtitle" />
                </Localized>
              )}
            </div>
            <div
              className={
                tile.type === "link" ? "external-link-icon" : "arrow-icon"
              }
            ></div>
          </button>
        )}
        {(title || subtitle) && (
          <div
            className="tile-title-container"
            id={`tile-title-container-${index}`}
          >
            {title && (
              <Localized text={title}>
                {/* H1 content is provided by Localized */}
                {/* eslint-disable-next-line jsx-a11y/heading-has-content */}
                <h1 className="tile-title" id={`content-tile-title-${index}`} />
              </Localized>
            )}

            {subtitle && (
              <Localized text={subtitle}>
                <p className="tile-subtitle" />
              </Localized>
            )}
          </div>
        )}
        {tile.type !== "link" && (isExpanded || !header) ? (
          <div className="tile-content" id={`tile-content-${index}`}>
            {tile.type === "addons-picker" && tile.data && (
              <AddonsPicker
                content={{ tiles: tile }}
                installedAddons={props.installedAddons}
                message_id={props.messageId}
                handleAction={props.handleAction}
                layout={content.position}
              />
            )}
            {["theme", "single-select"].includes(tile.type) && tile.data && (
              <SingleSelect
                content={{ tiles: tile }}
                activeTheme={props.activeTheme}
                handleAction={props.handleAction}
                activeSingleSelectSelections={
                  props.activeSingleSelectSelections
                }
                setActiveSingleSelectSelection={
                  props.setActiveSingleSelectSelection
                }
                singleSelectId={`single-select-${index}`}
                messageId={props.messageId}
              />
            )}
            {tile.type === "mobile_downloads" && tile.data && (
              <MobileDownloads
                data={tile.data}
                handleAction={props.handleAction}
              />
            )}
            {tile.type === "multiselect" && tile.data && (
              <MultiSelect
                content={{ tiles: tile }}
                screenMultiSelects={getTileMultiSelects(
                  props.screenMultiSelects,
                  index
                )}
                setScreenMultiSelects={props.setScreenMultiSelects}
                activeMultiSelect={getTileActiveMultiSelect(
                  props.activeMultiSelect,
                  index
                )}
                setActiveMultiSelect={props.setActiveMultiSelect}
                multiSelectId={`tile-${index}`}
              />
            )}
            {tile.type === "textarea" && tile.data && (
              <TextAreaTile
                content={{ tiles: tile }}
                textInputs={props.textInputs}
                setTextInput={props.setTextInput}
                tileIndex={index}
              />
            )}
            {tile.type === "migration-wizard" && (
              <EmbeddedMigrationWizard
                handleAction={props.handleAction}
                content={{ tiles: tile }}
              />
            )}
            {tile.type === "theme-picker" && (
              <EmbeddedThemePicker
                handleAction={props.handleAction}
                installSource={tile.data?.installSource}
              />
            )}
            {tile.type === "action_checklist" && tile.data && (
              <ActionChecklist
                content={content}
                message_id={props.messageId}
                handleAction={props.handleAction}
              />
            )}
            {tile.type === "embedded_browser" && tile.data?.url && (
              <EmbeddedBrowser url={tile.data.url} style={tile.data.style} />
            )}
            {tile.type === "backup_restore" && (
              <EmbeddedBackupRestore
                handleAction={props.handleAction}
                content={{ tiles: tile }}
                skipButton={props.content.skip_button}
                layout={content.position}
              />
            )}
            {tile.type === "fx_backup_file_path" && (
              <EmbeddedFxBackupOptIn
                handleAction={props.handleAction}
                isEncryptedBackup={content.isEncryptedBackup}
                options={tile.options}
                messageId={props.messageId}
              />
            )}
            {tile.type === "fx_backup_password" && (
              <EmbeddedFxBackupOptIn
                handleAction={props.handleAction}
                isEncryptedBackup={content.isEncryptedBackup}
                options={tile.options}
                messageId={props.messageId}
              />
            )}
            {tile.type === "confirmation-checklist" && tile.data && (
              <ConfirmationChecklist
                content={tile.data}
                handleAction={props.handleAction}
              />
            )}
            {tile.type === "pinnable_sites" && tile.data && (
              <PinnableSitesList
                tile={tile}
                messageId={props.messageId}
                handleAction={props.handleAction}
                setPinnedSite={props.setPinnedSite}
              />
            )}
            {tile.type === "content-toggle" && tile.data && (
              <ContentToggle
                content={{ tiles: tile }}
                toggled={props.contentToggleChecked}
                onToggle={props.setContentToggleChecked}
              />
            )}
            {tile.type === "textbox" && tile.data && (
              <TextBoxTile
                content={{ tiles: tile }}
                contentToggled={props.contentToggleChecked}
              />
            )}
            {tile.type === "text" && tile.text && (
              <LinkParagraph
                text_content={tile}
                handleAction={props.handleAction}
              />
            )}
          </div>
        ) : null}
      </div>
    );
  };

  const renderContentTiles = () => {
    if (Array.isArray(tiles)) {
      const containerStyle = content?.tiles_container?.style;

      return (
        <div
          id="content-tiles-container"
          style={MultiStageUtils.getValidStyle(
            containerStyle,
            CONTAINER_STYLES
          )}
        >
          {tiles.map((tile, index) => renderContentTile(tile, index))}
        </div>
      );
    }
    // If tiles is not an array render the tile alone without a container
    return renderContentTile(tiles, 0);
  };

  if (content.tiles_header) {
    return (
      <React.Fragment>
        <button
          className="content-tiles-header secondary"
          onClick={toggleTiles}
          aria-expanded={tilesHeaderExpanded}
          aria-controls={`content-tiles-container`}
        >
          <Localized text={content.tiles_header.title}>
            <span className="header-title" />
          </Localized>
          <div className="arrow-icon"></div>
        </button>
        {tilesHeaderExpanded && renderContentTiles()}
      </React.Fragment>
    );
  }
  return renderContentTiles(tiles);
};
