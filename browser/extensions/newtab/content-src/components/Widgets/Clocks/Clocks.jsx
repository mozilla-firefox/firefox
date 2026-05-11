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
import { batch, useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  PREF_CLOCKS_SIZE,
  PREF_WIDGETS_CLOCKS_ENABLED,
} from "../WidgetsRegistry.mjs";
import { useIntersectionObserver } from "../../../lib/utils";
import { AddClockForm } from "./AddClockForm";
import { ClocksRow } from "./ClocksRow";
import { EditClocksPanel } from "./EditClocksPanel";
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
  ADD_CLOCK: "add_clock",
  ADD_NICKNAME: "add_nickname",
  CHANGE_HOUR_FORMAT: "change_hour_format",
  CHANGE_SIZE: "change_size",
  COLLAPSE: "collapse",
  EDIT_CLOCK: "edit_clock",
  EXPAND: "expand",
  LEARN_MORE: "learn_more",
  REMOVE_CLOCK: "remove_clock",
};

const PREF_CLOCKS_HOUR_FORMAT = "widgets.clocks.hourFormat";
const PREF_CLOCKS_ZONES = "widgets.clocks.zones";
const CLOCKS_PANEL = {
  FORM: "form",
  EDIT: "edit",
};
const CLOCK_WIDGET_SOURCE = {
  CONTEXT_MENU: "context_menu",
  MANAGE: "manage",
  ROW: "row",
  TOOLBAR: "toolbar",
};

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
 * @param {"small"|"medium"|"large"} [props.size] Defaults to "medium".
 */
function Clocks({ dispatch, size }) {
  const clocksZonesPref = useSelector(
    state => state.Prefs.values[PREF_CLOCKS_ZONES]
  );
  const hourFormatPref = useSelector(
    state => state.Prefs.values[PREF_CLOCKS_HOUR_FORMAT]
  );

  const [now, setNow] = useState(null);
  const impressionFired = useRef(false);
  const sizeSubmenuRef = useRef(null);
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
  const currentSizeRef = useRef(currentSize);

  useEffect(() => {
    currentSizeRef.current = currentSize;
  }, [currentSize]);

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

  const handleIntersection = useCallback(() => {
    if (impressionFired.current) {
      return;
    }
    impressionFired.current = true;
    dispatch(
      ac.AlsoToMain({
        type: at.WIDGETS_IMPRESSION,
        data: {
          widget_name: "clocks",
          widget_size: currentSizeRef.current,
        },
      })
    );
  }, [dispatch]);

  const clocksRef = useIntersectionObserver(handleIntersection);

  const handleChangeSize = useCallback(
    newSize => {
      batch(() => {
        dispatch(
          ac.OnlyToMain({
            type: at.SET_PREF,
            data: { name: PREF_CLOCKS_SIZE, value: newSize },
          })
        );
        dispatch(
          ac.OnlyToMain({
            type: at.WIDGETS_USER_EVENT,
            data: {
              widget_name: "clocks",
              widget_source: CLOCK_WIDGET_SOURCE.CONTEXT_MENU,
              user_action: USER_ACTION_TYPES.CHANGE_SIZE,
              action_value: newSize,
              widget_size: newSize,
            },
          })
        );
      });
      closeContextMenu();
    },
    [dispatch, closeContextMenu]
  );

  // moz-panel-list moves the submenu into shadow DOM, so React synthetic
  // events don't reach inner items. Listen directly and use composedPath.
  useEffect(() => {
    const el = sizeSubmenuRef.current;
    if (!el) {
      return undefined;
    }
    const listener = e => {
      const item = e.composedPath().find(node => node.dataset?.size);
      if (item) {
        handleChangeSize(item.dataset.size);
      }
    };
    el.addEventListener("click", listener);
    return () => el.removeEventListener("click", listener);
  }, [handleChangeSize]);

  const handleToggleHourFormat = useCallback(() => {
    const nextFormat = use12HourFormat ? "24" : "12";
    batch(() => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: PREF_CLOCKS_HOUR_FORMAT, value: nextFormat },
        })
      );
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_USER_EVENT,
          data: {
            widget_name: "clocks",
            widget_source: CLOCK_WIDGET_SOURCE.CONTEXT_MENU,
            user_action: USER_ACTION_TYPES.CHANGE_HOUR_FORMAT,
            action_value: nextFormat,
            widget_size: currentSize,
          },
        })
      );
    });
    closeContextMenu();
  }, [use12HourFormat, dispatch, currentSize, closeContextMenu]);

  const handleHide = useCallback(() => {
    batch(() => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: PREF_WIDGETS_CLOCKS_ENABLED, value: false },
        })
      );
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_ENABLED,
          data: {
            widget_name: "clocks",
            widget_source: CLOCK_WIDGET_SOURCE.CONTEXT_MENU,
            enabled: false,
            widget_size: currentSize,
          },
        })
      );
    });
    closeContextMenu();
  }, [dispatch, currentSize, closeContextMenu]);

  const handleLearnMore = useCallback(() => {
    batch(() => {
      dispatch(
        ac.OnlyToMain({
          type: at.OPEN_LINK,
          data: {
            url: "https://support.mozilla.org/kb/firefox-new-tab-widgets",
          },
        })
      );
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_USER_EVENT,
          data: {
            widget_name: "clocks",
            widget_source: CLOCK_WIDGET_SOURCE.CONTEXT_MENU,
            user_action: USER_ACTION_TYPES.LEARN_MORE,
            widget_size: currentSize,
          },
        })
      );
    });
    closeContextMenu();
  }, [dispatch, currentSize, closeContextMenu]);

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
  const resetAddClockForm = useCallback(() => {
    setEditingClockIndex(null);
  }, []);

  const handleShowAddClock = useCallback(
    (source = CLOCK_WIDGET_SOURCE.TOOLBAR) => {
      setActivePanel(CLOCKS_PANEL.FORM);
      setFormSource(source);
      setEditingClockIndex(null);
      setIsDismissed(false);
    },
    []
  );

  const handleShowEditClocks = useCallback(
    source => {
      setActivePanel(CLOCKS_PANEL.EDIT);
      setPanelOpenSource(source);
      setIsDismissed(false);
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_USER_EVENT,
          data: {
            widget_name: "clocks",
            widget_source: source,
            user_action: USER_ACTION_TYPES.EXPAND,
            widget_size: currentSize,
          },
        })
      );
    },
    [currentSize, dispatch]
  );

  const handleCloseDisplayPanel = useCallback(() => {
    if (activePanel === CLOCKS_PANEL.EDIT) {
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_USER_EVENT,
          data: {
            widget_name: "clocks",
            widget_source: panelOpenSource,
            user_action: USER_ACTION_TYPES.COLLAPSE,
            widget_size: currentSize,
          },
        })
      );
    }
    setActivePanel(null);
    resetAddClockForm();
    requestAnimationFrame(() => {
      (addButtonRef.current ?? contextMenuButtonRef.current)?.focus();
    });
  }, [activePanel, panelOpenSource, currentSize, dispatch, resetAddClockForm]);

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
      batch(() => {
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
        dispatch(
          ac.OnlyToMain({
            type: at.WIDGETS_USER_EVENT,
            data: {
              widget_name: "clocks",
              widget_source: formSource,
              user_action:
                editingClockIndex !== null
                  ? USER_ACTION_TYPES.EDIT_CLOCK
                  : USER_ACTION_TYPES.ADD_CLOCK,
              widget_size: currentSize,
            },
          })
        );
        if (zone.label && !existingClock?.label) {
          dispatch(
            ac.OnlyToMain({
              type: at.WIDGETS_USER_EVENT,
              data: {
                widget_name: "clocks",
                widget_source: formSource,
                user_action: USER_ACTION_TYPES.ADD_NICKNAME,
                widget_size: currentSize,
              },
            })
          );
        }
      });
      if (formSource === CLOCK_WIDGET_SOURCE.MANAGE) {
        setActivePanel(CLOCKS_PANEL.EDIT);
        resetAddClockForm();
        return;
      }
      handleCloseDisplayPanel();
    },
    [
      clockZones,
      formSource,
      currentSize,
      editingClockIndex,
      handleCloseDisplayPanel,
      resetAddClockForm,
      dispatch,
    ]
  );

  const handleRemoveClock = useCallback(
    (index, source = CLOCK_WIDGET_SOURCE.ROW) => {
      if (clockZones.length <= 1) {
        return;
      }
      batch(() => {
        dispatch(
          ac.OnlyToMain({
            type: at.SET_PREF,
            data: {
              name: PREF_CLOCKS_ZONES,
              value: JSON.stringify(removeClockZoneAtIndex(clockZones, index)),
            },
          })
        );
        dispatch(
          ac.OnlyToMain({
            type: at.WIDGETS_USER_EVENT,
            data: {
              widget_name: "clocks",
              widget_source: source,
              user_action: USER_ACTION_TYPES.REMOVE_CLOCK,
              widget_size: currentSize,
            },
          })
        );
      });
    },
    [clockZones, currentSize, dispatch]
  );

  const isClockFormOpen = activePanel === CLOCKS_PANEL.FORM;
  const isEditingClocks = activePanel === CLOCKS_PANEL.EDIT;

  return (
    <article
      className={`clocks-widget col-4 ${panelDisplaySize}-widget${
        clockZones.length === 1 ? " is-hero" : ""
      }${isDismissed ? " is-dismissed" : ""}${
        isClockFormOpen ? " is-clock-form-open" : ""
      }${isEditingClocks ? " is-editing-clocks" : ""}${
        activePanel ? " is-panel-open" : ""
      }`}
      data-clock-count={clockZones.length}
      onMouseLeave={() => setIsDismissed(false)}
      ref={el => {
        // useIntersectionObserver expects ref.current to be an array of targets.
        clocksRef.current = [el];
      }}
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
        <moz-button
          className="clocks-context-menu-button"
          data-l10n-id="newtab-clock-widget-menu-button"
          iconSrc="chrome://global/skin/icons/more.svg"
          menuId="clocks-widget-context-menu"
          type="icon ghost"
          size="small"
          ref={contextMenuButtonRef}
        />
        <panel-list ref={contextMenuRef} id="clocks-widget-context-menu">
          <panel-item submenu="clocks-size-submenu">
            <span data-l10n-id="newtab-widget-menu-change-size"></span>
            <panel-list
              ref={sizeSubmenuRef}
              slot="submenu"
              id="clocks-size-submenu"
            >
              {["small", "medium", "large"].map(s => (
                <panel-item
                  key={s}
                  type="checkbox"
                  checked={currentSize === s}
                  data-size={s}
                  data-l10n-id={`newtab-widget-size-${s}`}
                />
              ))}
            </panel-list>
          </panel-item>
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
          <panel-item
            data-l10n-id="newtab-clock-widget-menu-hide"
            onClick={handleHide}
          />
          <panel-item
            data-l10n-id="newtab-clock-widget-menu-learn-more"
            onClick={handleLearnMore}
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
          onSave={handleSaveClock}
          onCancel={handleCloseClockForm}
        />
      )}
      {isEditingClocks && (
        <EditClocksPanel
          clockZones={clockZones}
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
