/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useEffect } from "react";

import { Localized } from "./MSLocalized";
import { TileButton } from "./TileButton";
import { TileList } from "./TileList";
import { MultiStageUtils } from "../lib/multistage-utils.mjs";
import { CarouselNav, useCarouselScroll } from "./CarouselNav";

// This component was formerly "Themes" and continues to support theme
export const SingleSelect = ({
  activeSingleSelectSelections = {}, // This now holds all active selections keyed by `singleSelectId`
  activeTheme,
  content,
  handleAction,
  messageId,
  setActiveSingleSelectSelection,
  singleSelectId,
}) => {
  const category = content.tiles?.category?.type || content.tiles?.type;
  const isSingleSelect = category === "single-select";
  const isCarousel = isSingleSelect && content.tiles?.layout === "carousel";

  const { scrollerRef, registerCard, isSmallLayout } = useCarouselScroll(
    activeSingleSelectSelections[singleSelectId],
    isCarousel
  );

  const handlePillSelect = id => {
    setActiveSingleSelectSelection(id, singleSelectId);
    MultiStageUtils.sendActionTelemetry(
      messageId,
      `carousel-pill-${id}`,
      "CLICK_BUTTON"
    );
  };

  const autoTriggerAllowed = itemAction => {
    // Currently only enabled for sidebar experiment prefs
    const allowedActions = ["SET_PREF"];
    const allowedPrefs = [
      "sidebar.revamp",
      "sidebar.verticalTabs",
      "sidebar.visibility",
    ];
    const checkAction = action => {
      if (!allowedActions.includes(action.type)) {
        return false;
      }
      if (
        action.type === "SET_PREF" &&
        !allowedPrefs.includes(action.data?.pref.name)
      ) {
        return false;
      }
      return true;
    };
    if (itemAction.type === "MULTI_ACTION") {
      // Only allow autoTrigger if all actions are allowed
      return !itemAction.data.actions.some(action => !checkAction(action));
    }
    return checkAction(itemAction);
  };

  // When screen renders for first time or user navigates back, update state to
  // check default option.
  useEffect(() => {
    if (isSingleSelect && !activeSingleSelectSelections[singleSelectId]) {
      let newActiveSingleSelect =
        content.tiles?.selected || content.tiles?.data[0].id;

      setActiveSingleSelectSelection(newActiveSingleSelect, singleSelectId);

      let selectedTile = content.tiles?.data.find(
        opt => opt.id === newActiveSingleSelect
      );
      // If applicable, automatically trigger the action for the default
      // selected tile.
      if (
        isSingleSelect &&
        content.tiles?.autoTrigger &&
        autoTriggerAllowed(selectedTile?.action)
      ) {
        handleAction({ currentTarget: { value: selectedTile.id } });
      }
    }
  }, [activeSingleSelectSelections]); // eslint-disable-line react-hooks/exhaustive-deps

  const CONFIGURABLE_STYLES = [
    "background",
    "border",
    "borderRadius",
    "height",
    "marginBlock",
    "marginBlockStart",
    "marginBlockEnd",
    "marginInline",
    "paddingBlock",
    "paddingBlockStart",
    "paddingBlockEnd",
    "paddingInline",
    "paddingInlineStart",
    "paddingInlineEnd",
    "width",
  ];

  return (
    <div
      className={`tiles-single-select-container ${isCarousel ? "carousel" : ""}`}
    >
      {isCarousel ? (
        <CarouselNav
          items={content.tiles?.data}
          activeId={activeSingleSelectSelections[singleSelectId]}
          onSelect={handlePillSelect}
          navLabel={content.tiles?.pill_nav_label}
          compact={isSmallLayout}
        />
      ) : null}
      <div className="tiles-single-select-wrapper">
        <fieldset
          className={`tiles-single-select-section ${category} ${isCarousel ? "carousel" : ""}`}
          ref={scrollerRef}
        >
          <Localized text={content.tiles?.subtitle || content.subtitle}>
            <legend className="sr-only" />
          </Localized>
          {content.tiles.data.map(
            ({
              description,
              inert,
              icon,
              id,
              label = "",
              body = "",
              subtitle = "",
              theme,
              tooltip,
              type = "",
              flair,
              style,
              tilebutton,
            }) => {
              const value = id || theme;
              let inputName = `select-item-${id}`;
              if (!isSingleSelect) {
                inputName = category === "theme" ? "theme" : id; // unique names per item are currently used in the wallpaper picker
              }
              const selected =
                (theme && theme === activeTheme) ||
                (isSingleSelect &&
                  activeSingleSelectSelections[singleSelectId] === value);
              const valOrObj = val => (typeof val === "object" ? val : {});

              const iconStyle = MultiStageUtils.getValidStyle(
                icon,
                CONFIGURABLE_STYLES
              );
              if (icon?.darkModeBackground) {
                iconStyle["--single-select-icon-background"] = icon.background;
                iconStyle["--single-select-icon-background-dark"] =
                  icon.darkModeBackground;
                delete iconStyle.background;
              }
              if (isCarousel && iconStyle) {
                iconStyle.backgroundSize = "100% 100%";
              }

              const handleClick = evt => {
                if (isSingleSelect) {
                  setActiveSingleSelectSelection(value, singleSelectId); // Update selection for the specific component
                }
                handleAction(evt);
              };

              const handleKeyDown = evt => {
                if (evt.key === "Enter" || evt.keyCode === 13) {
                  // Set target value to the input inside of the selected label
                  evt.currentTarget.value = value;
                  handleClick(evt);
                }
              };

              return (
                <Localized
                  key={value + (isSingleSelect ? "" : label)}
                  text={valOrObj(tooltip)}
                >
                  {/* eslint-disable jsx-a11y/label-has-associated-control */}
                  {/* eslint-disable jsx-a11y/no-noninteractive-element-interactions */}
                  <label
                    className={`select-item ${type} ${selected ? " selected" : ""}`}
                    ref={el => registerCard(value, el)}
                    onKeyDown={e => handleKeyDown(e)}
                    style={{
                      ...MultiStageUtils.getValidStyle(
                        style,
                        CONFIGURABLE_STYLES
                      ),
                      ...(icon?.width ? { minWidth: icon.width } : {}),
                    }}
                  >
                    {flair ? (
                      <Localized text={valOrObj(flair.text)}>
                        <span
                          className={`flair ${flair.centered ? "centered" : ""} ${flair.spacer ? "spacer" : ""} ${type}`}
                        ></span>
                      </Localized>
                    ) : (
                      ""
                    )}
                    <Localized text={valOrObj(description)}>
                      <input
                        type="radio"
                        value={value}
                        name={inputName}
                        checked={selected}
                        className="sr-only input"
                        disabled={inert}
                        onClick={e => handleClick(e)}
                      />
                    </Localized>
                    <div
                      className={`icon ${icon?.darkModeBackground ? " has-dark-background" : ""} ${selected ? " selected" : ""} ${value}`}
                      style={iconStyle}
                    />
                    <Localized text={label}>
                      <div className="text label-text" />
                    </Localized>
                    {body.items ? (
                      <TileList content={body} />
                    ) : (
                      <Localized text={body}>
                        <div className="text body-text" />
                      </Localized>
                    )}
                    {subtitle && selected ? (
                      <Localized text={subtitle}>
                        <div className="text subtitle-text" />
                      </Localized>
                    ) : (
                      ""
                    )}
                    {tilebutton ? (
                      <TileButton
                        content={tilebutton}
                        handleAction={handleAction}
                        inputName={inputName}
                        inert={isCarousel && !selected}
                      />
                    ) : (
                      ""
                    )}
                  </label>
                </Localized>
              );
            }
          )}
        </fieldset>
      </div>
    </div>
  );
};
