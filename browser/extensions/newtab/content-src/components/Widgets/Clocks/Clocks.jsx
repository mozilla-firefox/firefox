/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  PREF_CLOCKS_SIZE,
  PREF_WIDGETS_CLOCKS_ENABLED,
  WIDGET_REGISTRY,
  resolveWidgetSize,
  WIDGET_USER_ACTIONS,
  WIDGET_SOURCES,
} from "common/WidgetsRegistry.mjs";
import { useWidgetTelemetry } from "../useWidgetTelemetry";
import { AddClockForm } from "./AddClockForm";
import { ClocksRow } from "./ClocksRow";
import { EditClocksPanel } from "./EditClocksPanel";
import { SizeSubmenu } from "../SizeSubmenu";
import { WidgetMenuButton } from "../WidgetMenuButton";
import { WidgetMenuFooter } from "../WidgetMenuFooter";
import { useCuratedCityNames } from "./useCuratedCityNames";
import {
  backfillClockLabelColors,
  buildNextClockZones,
  MAX_CLOCK_COUNT,
  buildDefaultZones,
  getSupportedTimeZones,
  parseClockZonesPref,
  removeClockZoneAtIndex,
  shouldUse12HourTimeFormat,
} from "./ClocksHelpers";

const USER_ACTION_TYPES = {
  ...WIDGET_USER_ACTIONS,
  ADD_CLOCK: "add_clock",
  ADD_NICKNAME: "add_nickname",
  CHANGE_HOUR_FORMAT: "change_hour_format",
  COLLAPSE: "collapse",
  EDIT_CLOCK: "edit_clock",
  EXPAND: "expand",
  REMOVE_CLOCK: "remove_clock",
};

const PREF_CLOCKS_HOUR_FORMAT = "widgets.clocks.hourFormat";
const PREF_CLOCKS_ZONES = "widgets.clocks.zones";
const CLOCKS_PANEL = {
  FORM: "form",
  EDIT: "edit",
};
const CLOCK_WIDGET_SOURCE = {
  ...WIDGET_SOURCES,
  MANAGE: "manage",
  ROW: "row",
  TOOLBAR: "toolbar",
};

const CLOCKS_WIDGET = WIDGET_REGISTRY.find(widget => widget.id === "clocks");

function getClockWidgetDisplayState({ activePanel, hourFormatPref, size }) {
  const currentSize = size || "medium";
  const locale =
    typeof navigator !== "undefined" ? navigator.language : undefined;
  return {
    currentSize,
    locale,
    panelDisplaySize: activePanel ? "large" : currentSize,
    use12HourFormat: shouldUse12HourTimeFormat({
      prefValue: hourFormatPref,
      locale,
    }),
  };
}

/**
 * Nova-only World Clocks widget. Up to four clocks with a minute-aligned
 * tick, hover toolbar, and context menu.
 *
 * @param {object} props
 * @param {Function} props.dispatch
 * @param {Function} props.handleUserInteraction
 */
function Clocks({ dispatch, handleUserInteraction, widgetEnabledMap }) {
  const size = useSelector(state =>
    resolveWidgetSize(CLOCKS_WIDGET, state.Prefs.values)
  );
  const clocksZonesPref = useSelector(
    state => state.Prefs.values[PREF_CLOCKS_ZONES]
  );
  const hourFormatPref = useSelector(
    state => state.Prefs.values[PREF_CLOCKS_HOUR_FORMAT]
  );

  const [now, setNow] = useState(null);
  const contextMenuRef = useRef(null);
  const contextMenuButtonRef = useRef(null);
  // Suppress hover-reveal after a menu action; cleared on mouseleave.
  const [isDismissed, setIsDismissed] = useState(false);
  const [activePanel, setActivePanel] = useState(null);
  const [formSource, setFormSource] = useState(CLOCK_WIDGET_SOURCE.TOOLBAR);
  const [panelOpenSource, setPanelOpenSource] = useState(null);
  const [editingClockIndex, setEditingClockIndex] = useState(null);
  const addButtonRef = useRef(null);

  // Blur the trigger after hide() returns focus there; otherwise
  // :focus-within keeps the overlay open.
  const closeContextMenu = useCallback(() => {
    contextMenuRef.current?.hide?.();
    setIsDismissed(true);
    // Defer a frame so we don't race hide()'s synchronous focus return.
    requestAnimationFrame(() => {
      if (document.activeElement instanceof HTMLElement) {
        document.activeElement.blur();
      }
    });
  }, []);

  const { currentSize, locale, panelDisplaySize, use12HourFormat } =
    getClockWidgetDisplayState({
      activePanel,
      hourFormatPref,
      size,
    });

  const { impressionRef, recordUserAction } = useWidgetTelemetry({
    dispatch,
    widget: CLOCKS_WIDGET,
    widgetSize: currentSize,
  });

  // Each tick realigns to the next minute, so paused tabs or device sleep
  // can't compound drift. `now` starts null so the first render stays
  // stable for prerender/hydration.
  useEffect(() => {
    let timeoutId;
    const tick = () => {
      setNow(new Date());
      timeoutId = setTimeout(tick, 60_000 - (Date.now() % 60_000));
    };
    tick();
    return () => clearTimeout(timeoutId);
  }, []);

  const handleClocksInteraction = useCallback(
    () => handleUserInteraction("clocks"),
    [handleUserInteraction]
  );

  const handleChangeSize = useCallback(
    newSize => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: PREF_CLOCKS_SIZE, value: newSize },
        })
      );
      recordUserAction(USER_ACTION_TYPES.CHANGE_SIZE, {
        source: CLOCK_WIDGET_SOURCE.CONTEXT_MENU,
        value: newSize,
        size: newSize,
      });
      handleClocksInteraction();
      closeContextMenu();
    },
    [dispatch, recordUserAction, handleClocksInteraction, closeContextMenu]
  );

  const handleToggleHourFormat = useCallback(() => {
    const nextFormat = use12HourFormat ? "24" : "12";
    dispatch(
      ac.OnlyToMain({
        type: at.SET_PREF,
        data: { name: PREF_CLOCKS_HOUR_FORMAT, value: nextFormat },
      })
    );
    recordUserAction(USER_ACTION_TYPES.CHANGE_HOUR_FORMAT, {
      source: CLOCK_WIDGET_SOURCE.CONTEXT_MENU,
      value: nextFormat,
    });
    handleClocksInteraction();
    closeContextMenu();
  }, [
    use12HourFormat,
    dispatch,
    recordUserAction,
    closeContextMenu,
    handleClocksInteraction,
  ]);

  const handleLearnMore = useCallback(() => {
    recordUserAction(USER_ACTION_TYPES.LEARN_MORE, {
      source: CLOCK_WIDGET_SOURCE.CONTEXT_MENU,
    });
    handleClocksInteraction();
    closeContextMenu();
  }, [recordUserAction, closeContextMenu, handleClocksInteraction]);

  const clockZones = useMemo(
    () => parseClockZonesPref(clocksZonesPref) || buildDefaultZones(),
    [clocksZonesPref]
  );

  useEffect(() => {
    if (!clockZones.some(clock => clock.label && !clock.labelColor)) {
      return;
    }
    dispatch(
      ac.OnlyToMain({
        type: at.SET_PREF,
        data: {
          name: PREF_CLOCKS_ZONES,
          value: JSON.stringify(backfillClockLabelColors(clockZones)),
        },
      })
    );
  }, [clockZones, dispatch]);

  const canAddClock = clockZones.length < MAX_CLOCK_COUNT;
  const supportedTimeZones = useMemo(() => getSupportedTimeZones(), []);

  // Localized names for the shown clocks only (<= MAX_CLOCK_COUNT); the add
  // form resolves the full curated list on demand when it opens.
  const curatedNames = useCuratedCityNames(
    clockZones.map(clock => clock.cityId).filter(Boolean)
  );
  const resetAddClockForm = useCallback(() => {
    setEditingClockIndex(null);
  }, []);

  const handleShowAddClock = useCallback(
    (source = CLOCK_WIDGET_SOURCE.TOOLBAR) => {
      setActivePanel(CLOCKS_PANEL.FORM);
      setFormSource(source);
      setEditingClockIndex(null);
      setIsDismissed(false);
      handleClocksInteraction();
    },
    [handleClocksInteraction]
  );

  const handleShowEditClocks = useCallback(
    source => {
      setActivePanel(CLOCKS_PANEL.EDIT);
      setPanelOpenSource(source);
      setIsDismissed(false);
      recordUserAction(USER_ACTION_TYPES.EXPAND, { source });
      handleClocksInteraction();
    },
    [recordUserAction, handleClocksInteraction]
  );

  const handleCloseDisplayPanel = useCallback(() => {
    if (activePanel === CLOCKS_PANEL.EDIT) {
      recordUserAction(USER_ACTION_TYPES.COLLAPSE, {
        source: panelOpenSource,
      });
    }
    setActivePanel(null);
    resetAddClockForm();
    requestAnimationFrame(() => {
      (addButtonRef.current ?? contextMenuButtonRef.current)?.focus();
    });
  }, [activePanel, panelOpenSource, recordUserAction, resetAddClockForm]);

  const handleCloseClockForm = useCallback(() => {
    if (formSource === CLOCK_WIDGET_SOURCE.MANAGE) {
      setActivePanel(CLOCKS_PANEL.EDIT);
      resetAddClockForm();
      return;
    }
    handleCloseDisplayPanel();
  }, [formSource, handleCloseDisplayPanel, resetAddClockForm]);

  const handleShowEditClockForm = useCallback(
    (index, source = CLOCK_WIDGET_SOURCE.ROW) => {
      setActivePanel(CLOCKS_PANEL.FORM);
      setFormSource(source);
      setEditingClockIndex(index);
      setIsDismissed(false);
    },
    []
  );

  const handleSaveClock = useCallback(
    zone => {
      const existingClock =
        editingClockIndex !== null ? clockZones[editingClockIndex] : null;
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: {
            name: PREF_CLOCKS_ZONES,
            value: JSON.stringify(
              buildNextClockZones(clockZones, editingClockIndex, zone)
            ),
          },
        })
      );
      recordUserAction(
        editingClockIndex !== null
          ? USER_ACTION_TYPES.EDIT_CLOCK
          : USER_ACTION_TYPES.ADD_CLOCK,
        { source: formSource }
      );
      if (zone.label && !existingClock?.label) {
        recordUserAction(USER_ACTION_TYPES.ADD_NICKNAME, {
          source: formSource,
        });
      }
      if (formSource === CLOCK_WIDGET_SOURCE.MANAGE) {
        setActivePanel(CLOCKS_PANEL.EDIT);
        resetAddClockForm();
        return;
      }
      handleClocksInteraction();
      handleCloseDisplayPanel();
    },
    [
      clockZones,
      formSource,
      editingClockIndex,
      handleCloseDisplayPanel,
      resetAddClockForm,
      dispatch,
      recordUserAction,
      handleClocksInteraction,
    ]
  );

  const handleRemoveClock = useCallback(
    (index, source = CLOCK_WIDGET_SOURCE.ROW) => {
      if (clockZones.length <= 1) {
        return;
      }
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: {
            name: PREF_CLOCKS_ZONES,
            value: JSON.stringify(removeClockZoneAtIndex(clockZones, index)),
          },
        })
      );
      recordUserAction(USER_ACTION_TYPES.REMOVE_CLOCK, { source });
      handleClocksInteraction();
    },
    [clockZones, dispatch, recordUserAction, handleClocksInteraction]
  );

  const isClockFormOpen = activePanel === CLOCKS_PANEL.FORM;
  const isEditingClocks = activePanel === CLOCKS_PANEL.EDIT;
  const hasAnyLabel = clockZones.some(c => !!c.label);

  return (
    <article
      className={`clocks-widget col-4 ${panelDisplaySize}-widget${
        clockZones.length === 1 ? " is-hero" : ""
      }${isDismissed ? " is-dismissed" : ""}${
        isClockFormOpen ? " is-clock-form-open" : ""
      }${isEditingClocks ? " is-editing-clocks" : ""}${
        activePanel ? " is-panel-open" : ""
      }${hasAnyLabel ? "" : " has-no-labels"}`}
      data-clock-count={clockZones.length}
      onMouseLeave={() => setIsDismissed(false)}
      ref={impressionRef}
    >
      <div className="widget-toolbar" inert={!!activePanel}>
        {canAddClock && (
          <moz-button
            className="clocks-add-button"
            type="icon primary"
            size="small"
            iconSrc="chrome://global/skin/icons/plus.svg"
            data-l10n-id="newtab-clock-widget-button-add"
            onClick={() => handleShowAddClock()}
            ref={addButtonRef}
          />
        )}
        <WidgetMenuButton
          className="clocks-context-menu-button"
          menuId="clocks-widget-context-menu"
          l10nId="newtab-clock-widget-open-menu-button"
          ref={contextMenuButtonRef}
        />
        <panel-list
          className="panel-list-no-icons"
          ref={contextMenuRef}
          id="clocks-widget-context-menu"
        >
          <panel-item
            data-l10n-id="newtab-clock-widget-menu-edit"
            onClick={() => {
              handleShowEditClocks(CLOCK_WIDGET_SOURCE.CONTEXT_MENU);
              closeContextMenu();
            }}
          />
          <panel-item
            data-l10n-id={
              use12HourFormat
                ? "newtab-clock-widget-menu-switch-to-24h"
                : "newtab-clock-widget-menu-switch-to-12h"
            }
            onClick={handleToggleHourFormat}
          />
          <WidgetMenuFooter
            dispatch={dispatch}
            widgetId="clocks"
            widgetEnabledMap={widgetEnabledMap}
            widgetName="clocks"
            enabledPref={PREF_WIDGETS_CLOCKS_ENABLED}
            widgetSize={currentSize}
            learnMoreL10nId="newtab-clock-widget-menu-learn-more"
            onAfterHide={closeContextMenu}
            onLearnMore={handleLearnMore}
            sizeSubmenu={
              <SizeSubmenu
                submenuId="clocks-size-submenu"
                sizes={["small", "medium", "large"]}
                checkedSize={currentSize}
                onChangeSize={handleChangeSize}
              />
            }
          />
        </panel-list>
      </div>
      {isClockFormOpen && (
        <AddClockForm
          key={editingClockIndex ?? "add"}
          isEditing={editingClockIndex !== null}
          initialClock={
            editingClockIndex !== null ? clockZones[editingClockIndex] : null
          }
          canAddClock={canAddClock}
          supportedTimeZones={supportedTimeZones}
          locale={locale}
          onSave={handleSaveClock}
          onCancel={handleCloseClockForm}
        />
      )}
      {isEditingClocks && (
        <EditClocksPanel
          clockZones={clockZones}
          curatedNames={curatedNames}
          canAddClock={canAddClock}
          onShowAddClock={() => handleShowAddClock(CLOCK_WIDGET_SOURCE.MANAGE)}
          onEditClock={index =>
            handleShowEditClockForm(index, CLOCK_WIDGET_SOURCE.MANAGE)
          }
          onRemoveClock={index =>
            handleRemoveClock(index, CLOCK_WIDGET_SOURCE.MANAGE)
          }
          onClose={handleCloseDisplayPanel}
        />
      )}
      <ul className="clocks-list" inert={!!activePanel}>
        {clockZones.map((c, i) => {
          const showLabel = panelDisplaySize === "large" && !!c.label;
          // Medium columns too narrow at 3+ clocks; Small always abbreviates.
          const shouldAbbreviate =
            panelDisplaySize === "small" ||
            (panelDisplaySize === "medium" && clockZones.length >= 3);
          const showInlineActions = !activePanel && currentSize !== "small";
          const hideTimeOnInlineActions =
            showInlineActions && clockZones.length > 1;
          return (
            <ClocksRow
              key={`${c.timeZone}-${i}`}
              clock={c}
              curatedNames={curatedNames}
              locale={locale}
              now={now}
              onEdit={
                showInlineActions ? () => handleShowEditClockForm(i) : null
              }
              onRemove={
                showInlineActions && clockZones.length > 1
                  ? () => handleRemoveClock(i)
                  : null
              }
              shouldAbbreviate={shouldAbbreviate}
              showLabel={showLabel}
              hideTimeOnInlineActions={hideTimeOnInlineActions}
              showInlineActions={showInlineActions}
              use12HourFormat={use12HourFormat}
            />
          );
        })}
      </ul>
    </article>
  );
}

export { Clocks };
