/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import React, { useState, useEffect, useRef, useCallback } from "react";
import { useSelector } from "react-redux";
import { WIDGET_REGISTRY, resolveWidgetSize } from "common/WidgetsRegistry.mjs";
import { WidgetCelebration } from "../WidgetCelebration";
import { useWidgetCelebration } from "../useWidgetCelebration";
import { SizeSubmenu } from "../SizeSubmenu";
import { WidgetMenuButton } from "../WidgetMenuButton";
import { WidgetMenuFooter } from "../WidgetMenuFooter";
import { useWidgetTelemetry } from "../useWidgetTelemetry";

const FOCUS_TIMER_CELEBRATION_GRADIENT_STOPS = [
  { offset: "0%", color: "var(--timer-celebration-leading)" },
  { offset: "100%", color: "var(--timer-celebration-trailing)" },
];

const USER_ACTION_TYPES = {
  CHANGE_SIZE: "change_size",
  TIMER_SET: "timer_set",
  TIMER_PLAY: "timer_play",
  TIMER_PAUSE: "timer_pause",
  TIMER_RESET: "timer_reset",
  TIMER_END: "timer_end",
  TIMER_TOGGLE_FOCUS: "timer_toggle_focus",
  TIMER_TOGGLE_BREAK: "timer_toggle_break",
};

const PREF_NOVA_ENABLED = "nova.enabled";
const PREF_FOCUS_TIMER_SIZE = "widgets.focusTimer.size";

/**
 * Calculates the remaining time (in seconds) by subtracting elapsed time from the original duration
 *
 * @param duration
 * @param start
 * @returns int
 */
export const calculateTimeRemaining = (duration, start) => {
  const currentTime = Math.floor(Date.now() / 1000);

  // Subtract the elapsed time from initial duration to get time remaining in the timer
  return Math.max(duration - (currentTime - start), 0);
};

/**
 * Converts a number of seconds into a zero-padded MM:SS time string
 *
 * @param seconds
 * @returns string
 */
export const formatTime = seconds => {
  const minutes = Math.floor(seconds / 60)
    .toString()
    .padStart(2, "0");
  const secs = (seconds % 60).toString().padStart(2, "0");
  return `${minutes}:${secs}`;
};

/**
 * Validates that the inputs in the timer only allow numerical digits (0-9)
 *
 * @param input - The character being input
 * @returns boolean - true if valid numeric input, false otherwise
 */
export const isNumericValue = input => {
  // Check for null/undefined input or non-numeric characters
  return input && /^\d+$/.test(input);
};

/**
 * Validates if adding a new digit would exceed the 2-character limit
 *
 * @param currentValue - The current value in the field
 * @returns boolean - true if at 2-character limit, false otherwise
 */
export const isAtMaxLength = currentValue => {
  return currentValue.length >= 2;
};

// Drop this if the spinbutton is ever replaced with a native control.
/**
 * Validates whether the next state of the Nova spinbutton is acceptable.
 * Allows up to 2 digits, an optional single colon, and up to 2 more digits.
 *
 * @param current - The element's current text content
 * @param input - The string the user is about to insert
 * @param start - The selection start (insertion point) within `current`
 * @param end - The selection end within `current`
 * @returns boolean - true if the resulting string matches the MM:SS pattern
 */
export const isValidSpinbuttonInput = (current, input, start, end) => {
  if (input === null || input === undefined) {
    return true;
  }
  const next = current.slice(0, start) + input + current.slice(end);
  return /^(\d{1,2})?(:\d{0,2})?$/.test(next);
};

/**
 * Converts a polar coordinate (angle on circle) into a percentage-based [x,y] position for clip-path
 *
 * @param cx
 * @param cy
 * @param radius
 * @param angle
 * @returns string
 */
export const polarToPercent = (cx, cy, radius, angle) => {
  const rad = ((angle - 90) * Math.PI) / 180;
  const x = cx + radius * Math.cos(rad);
  const y = cy + radius * Math.sin(rad);
  return `${x}% ${y}%`;
};

/**
 * Generates a clip-path polygon string that represents a pie slice from 0 degrees
 * to the current progress angle
 *
 * @returns string
 * @param progress
 */
export const getClipPath = progress => {
  const cx = 50;
  const cy = 50;
  const radius = 50;
  // Show some progress right at the start - 6 degrees is just enough to paint a dot once the timer is ticking
  const angle = progress > 0 ? Math.max(progress * 360, 6) : 0;
  const points = [`50% 50%`];

  for (let a = 0; a <= angle; a += 2) {
    points.push(polarToPercent(cx, cy, radius, a));
  }

  return `polygon(${points.join(", ")})`;
};

/* eslint-disable complexity, max-statements */
export const FocusTimer = ({
  dispatch,
  handleUserInteraction,
  isMaximized,
  widgetsMayBeMaximized,
  widgetEnabledMap,
}) => {
  const [timeLeft, setTimeLeft] = useState(0);
  // calculated value for the progress circle; 1 = 100%
  const [progress, setProgress] = useState(0);

  const activeMinutesRef = useRef(null);
  const activeSecondsRef = useRef(null);
  const arcRef = useRef(null);

  const timerType = useSelector(state => state.TimerWidget.timerType);
  const timerData = useSelector(state => state.TimerWidget);
  const { duration, initialDuration, startTime, isRunning } =
    timerData[timerType];
  const initialTimerDuration = timerData[timerType].initialDuration;

  const prefs = useSelector(state => state.Prefs.values);
  // @nova-cleanup(remove-pref): Remove novaEnabled and this check; always use resolveWidgetSize directly after Nova ships
  const novaEnabled = prefs[PREF_NOVA_ENABLED];
  const isSmallSize = novaEnabled
    ? false
    : !isMaximized && widgetsMayBeMaximized;
  const timerWidget = WIDGET_REGISTRY.find(w => w.id === "focusTimer");
  let widgetSize;
  if (novaEnabled) {
    widgetSize = resolveWidgetSize(timerWidget, prefs);
  } else {
    widgetSize = isSmallSize ? "small" : "medium";
  }

  // @nova-cleanup(remove-conditional): Inline these for Nova-only after Nova ships
  // Nova spinbutton works in whole minutes; ceil to the next minute so a 4:38
  // remainder reads as "5 minutes" via aria-valuenow / accessible name.
  const minutesValue = Math.max(1, Math.ceil((timeLeft || duration) / 60));
  // For +/- and arrow-key adjustments, treat the integer-minutes part of the
  // current duration as the base so e.g. 0:01 + 1 -> 1:00 (not 2:00).
  const minutesFloor = Math.floor((timeLeft || duration) / 60);
  const hasProgressed = duration < initialDuration || isRunning;
  const isComplete = progress === 1;

  const handleTimerInteraction = useCallback(
    () => handleUserInteraction("focusTimer"),
    [handleUserInteraction]
  );

  const { impressionRef, recordUserAction } = useWidgetTelemetry({
    dispatch,
    widget: timerWidget,
    widgetSize,
    legacyImpressionTypes: [at.WIDGETS_TIMER_USER_IMPRESSION],
    legacyUserEventType: at.WIDGETS_TIMER_USER_EVENT,
  });

  const widgetCelebrationRef = useRef(null);
  const {
    celebrationFrame,
    celebrationId,
    completeCelebration,
    isCelebrating,
    triggerCelebration,
  } = useWidgetCelebration(widgetCelebrationRef);
  // Guards against a double-fire that would re-toggle SET_TYPE.
  const celebrationCompletedRef = useRef(false);
  // The timer is shared across open newtab/home pages, but each page fires its
  // own end-of-session switch. Set the next type directly rather than toggling
  // the shared value, so two pages don't toggle it and cancel out.
  const completingTypeRef = useRef(timerType);

  useEffect(() => {
    if (isCelebrating) {
      celebrationCompletedRef.current = false;
    }
  }, [isCelebrating]);

  const resetProgressCircle = useCallback(() => {
    if (arcRef?.current) {
      arcRef.current.style.clipPath = "polygon(50% 50%)";
      arcRef.current.style.webkitClipPath = "polygon(50% 50%)";
    }
    setProgress(0);
    handleTimerInteraction();
  }, [arcRef, handleTimerInteraction]);

  const handleCelebrationComplete = useCallback(() => {
    if (celebrationCompletedRef.current) {
      return;
    }
    celebrationCompletedRef.current = true;
    resetProgressCircle();

    const completedType = completingTypeRef.current;
    const nextType = completedType === "focus" ? "break" : "focus";
    const userAction =
      completedType === "focus"
        ? USER_ACTION_TYPES.TIMER_TOGGLE_BREAK
        : USER_ACTION_TYPES.TIMER_TOGGLE_FOCUS;

    dispatch(
      ac.AlsoToMain({
        type: at.WIDGETS_TIMER_SET_TYPE,
        data: { timerType: nextType },
      })
    );

    recordUserAction(userAction, { source: "widget", legacy: true });

    completeCelebration();
  }, [completeCelebration, dispatch, resetProgressCircle, recordUserAction]);

  const showSystemNotifications =
    prefs["widgets.focusTimer.showSystemNotifications"];

  // Held in a ref so the ticker effect below doesn't re-arm whenever
  // timerType / widgetSize / handleCelebrationComplete change. Reassigned
  // each render so the closure captures the latest values at fire time.
  const handleTimerEndRef = useRef(null);
  handleTimerEndRef.current = () => {
    dispatch(
      ac.AlsoToMain({
        type: at.WIDGETS_TIMER_END,
        data: {
          timerType,
          duration: initialTimerDuration,
          initialDuration: initialTimerDuration,
        },
      })
    );

    recordUserAction(USER_ACTION_TYPES.TIMER_END, {
      source: "widget",
      legacy: true,
    });

    completingTypeRef.current = timerType;
    celebrationCompletedRef.current = false;

    // animate the progress circle to turn solid green
    setProgress(1);

    // Classic mode and reduced-motion users skip the animation, so
    // run the completion handler inline so the auto-toggle still fires.
    // @nova-cleanup(remove-conditional): replace with `if (!triggerCelebration())`.
    if (!(novaEnabled && triggerCelebration())) {
      handleCelebrationComplete();
    }
  };

  // Ticker: re-arms only when run-state changes, not on every timerType flip.
  useEffect(() => {
    if (!isRunning || duration <= 0) {
      return undefined;
    }
    let hasReachedZero = false;
    const interval = setInterval(() => {
      const currentTime = Math.floor(Date.now() / 1000);
      const elapsed = currentTime - startTime;
      const remaining = calculateTimeRemaining(duration, startTime);

      // using setTimeLeft to trigger a re-render of the component to show live countdown each second
      setTimeLeft(remaining);
      setProgress((initialDuration - remaining) / initialDuration);

      if (elapsed >= duration && hasReachedZero) {
        clearInterval(interval);
        handleTimerEndRef.current?.();
      } else if (elapsed >= duration) {
        hasReachedZero = true;
      }
    }, 1000);
    return () => clearInterval(interval);
  }, [isRunning, startTime, duration, initialDuration]);

  // Paused-UI sync: shows the correct live time and progress whenever timer
  // state changes (page load, type toggle, pause/resume).
  useEffect(() => {
    setTimeLeft(
      isRunning ? calculateTimeRemaining(duration, startTime) : duration
    );

    if (!isRunning && duration < initialDuration) {
      // Show previously elapsed time
      setProgress((initialDuration - duration) / initialDuration);
    } else if (!isRunning && !isCelebrating) {
      // Don't reset while celebrating — would clear progress=1 mid-animation.
      setProgress(0);
    }
  }, [isRunning, startTime, duration, initialDuration, isCelebrating]);

  // Update the clip-path of the gradient circle to match the current progress value
  useEffect(() => {
    if (arcRef?.current) {
      // Only set clip-path if current timer has been started or is running
      if (progress > 0 || isRunning) {
        arcRef.current.style.clipPath = getClipPath(progress);
      } else {
        arcRef.current.style.clipPath = "";
      }
    }
  }, [progress, isRunning]);

  // set timer function
  const setTimerDuration = () => {
    const minutesEl = activeMinutesRef.current;
    const secondsEl = activeSecondsRef.current;

    const minutesText = minutesEl.innerText.trim() || "0";
    const secondsText = secondsEl.innerText.trim() || "0";

    let minutes = parseInt(minutesText || "0", 10);
    let seconds = parseInt(secondsText || "0", 10);

    // Set a limit of 99 minutes
    minutes = Math.min(minutes, 99);
    // Set a limit of 59 seconds
    seconds = Math.min(seconds, 59);

    const totalSeconds = minutes * 60 + seconds;

    if (
      !Number.isNaN(totalSeconds) &&
      totalSeconds > 0 &&
      totalSeconds !== duration
    ) {
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_TIMER_SET_DURATION,
          data: { timerType, duration: totalSeconds },
        })
      );

      recordUserAction(USER_ACTION_TYPES.TIMER_SET, {
        source: "widget",
        legacy: true,
      });
    }
    handleTimerInteraction();
  };

  // Pause timer function
  const toggleTimer = () => {
    // Ignore activations during the celebration window so the just-finished
    // timer can't be restarted before Focus<->Break flips.
    if (isCelebrating) {
      return;
    }
    if (!isRunning && duration > 0) {
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_TIMER_PLAY,
          data: { timerType },
        })
      );

      recordUserAction(USER_ACTION_TYPES.TIMER_PLAY, {
        source: "widget",
        legacy: true,
      });
    } else if (isRunning) {
      // calculated to get the new baseline of the timer when it starts or resumes
      const remaining = calculateTimeRemaining(duration, startTime);
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_TIMER_PAUSE,
          data: {
            timerType,
            duration: remaining,
          },
        })
      );

      recordUserAction(USER_ACTION_TYPES.TIMER_PAUSE, {
        source: "widget",
        legacy: true,
      });
    }
    handleTimerInteraction();
  };

  // reset timer function
  const resetTimer = () => {
    // Same rationale as toggleTimer: don't let the keyboard-reachable
    // reset button restart the cycle while the celebration is running.
    if (isCelebrating) {
      return;
    }
    dispatch(
      ac.AlsoToMain({
        type: at.WIDGETS_TIMER_RESET,
        data: {
          timerType,
          duration: initialTimerDuration,
          initialDuration: initialTimerDuration,
        },
      })
    );

    recordUserAction(USER_ACTION_TYPES.TIMER_RESET, {
      source: "widget",
      legacy: true,
    });

    // Reset progress value and gradient arc on the progress circle
    resetProgressCircle();

    handleTimerInteraction();
  };

  // Toggles between "focus" and "break" timer types
  const toggleType = useCallback(
    type => {
      const oldTypeRemaining = calculateTimeRemaining(duration, startTime);

      // The type we are toggling away from automatically pauses
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_TIMER_PAUSE,
          data: {
            timerType,
            duration: oldTypeRemaining,
          },
        })
      );

      recordUserAction(USER_ACTION_TYPES.TIMER_PAUSE, {
        source: "widget",
        legacy: true,
      });

      // Sets the current timer type so it persists when opening a new tab
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_TIMER_SET_TYPE,
          data: {
            timerType: type,
          },
        })
      );

      const toggleUserAction =
        type === "focus"
          ? USER_ACTION_TYPES.TIMER_TOGGLE_FOCUS
          : USER_ACTION_TYPES.TIMER_TOGGLE_BREAK;

      recordUserAction(toggleUserAction, {
        source: "widget",
        legacy: true,
      });
      handleTimerInteraction();
    },
    [
      duration,
      startTime,
      timerType,
      dispatch,
      recordUserAction,
      handleTimerInteraction,
    ]
  );

  const handleKeyDown = e => {
    if (e.key === "Enter") {
      e.preventDefault();
      setTimerDuration(e);
      handleTimerInteraction();
    }

    if (e.key === "Tab") {
      setTimerDuration(e);
      handleTimerInteraction();
    }
  };

  const handleBeforeInput = e => {
    const input = e.data;
    const values = e.target.innerText.trim();

    // only allow numerical digits 0–9 for time input
    if (!isNumericValue(input)) {
      e.preventDefault();
      return;
    }

    const selection = window.getSelection();
    const selectedText = selection.toString();

    // if entire value is selected, replace it with the new input
    if (selectedText === values) {
      e.preventDefault(); // prevent default typing
      e.target.innerText = input;

      // Places the caret at the end of the content-editable text
      // This is a known problem with content-editable where the caret
      const range = document.createRange();
      range.selectNodeContents(e.target);
      range.collapse(false);
      const sel = window.getSelection();
      sel.removeAllRanges();
      sel.addRange(range);
      return;
    }

    // only allow 2 values each for minutes and seconds
    if (isAtMaxLength(values)) {
      e.preventDefault();
    }
  };

  const handleFocus = e => {
    if (isRunning) {
      // calculated to get the new baseline of the timer when it starts or resumes
      const remaining = calculateTimeRemaining(duration, startTime);

      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_TIMER_PAUSE,
          data: {
            timerType,
            duration: remaining,
          },
        })
      );

      recordUserAction(USER_ACTION_TYPES.TIMER_PAUSE, {
        source: "widget",
        legacy: true,
      });
    }

    // highlight entire text when focused on the time.
    // this makes it easier to input the new time instead of backspacing
    const el = e.target;
    if (document.createRange && window.getSelection) {
      const range = document.createRange();
      range.selectNodeContents(el);
      const sel = window.getSelection();
      sel.removeAllRanges();
      sel.addRange(range);
    }
  };

  function handlePrefUpdate(prefName, prefValue) {
    dispatch(
      ac.OnlyToMain({
        type: at.SET_PREF,
        data: {
          name: prefName,
          value: prefValue,
        },
      })
    );
    handleTimerInteraction();
  }

  const handleChangeSize = useCallback(
    size => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: PREF_FOCUS_TIMER_SIZE, value: size },
        })
      );
      recordUserAction(USER_ACTION_TYPES.CHANGE_SIZE, {
        source: "context_menu",
        value: size,
        size,
      });
    },
    [dispatch, recordUserAction]
  );

  // @nova-cleanup(remove-conditional): Keep this function. Its only classic
  // caller is the legacy body removed below; no change needed here.
  const setTimerMinutes = useCallback(
    nextMinutes => {
      const clamped = Math.max(1, Math.min(99, nextMinutes));
      const totalSeconds = clamped * 60;
      if (totalSeconds === duration) {
        return;
      }
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_TIMER_SET_DURATION,
          data: { timerType, duration: totalSeconds },
        })
      );

      recordUserAction(USER_ACTION_TYPES.TIMER_SET, {
        source: "widget",
        legacy: true,
      });
      handleTimerInteraction();
    },
    [dispatch, duration, timerType, recordUserAction, handleTimerInteraction]
  );

  // @nova-cleanup(remove-conditional): Keep this function; it drives the Nova
  // spinbutton. No change needed here.
  const commitSpinbuttonDuration = useCallback(() => {
    const el = activeMinutesRef.current;
    if (!el) {
      return;
    }
    const text = el.innerText.replace(/\s+/g, "");
    const [mmRaw, ssRaw = "0"] = text.split(":");
    const mm = parseInt(mmRaw, 10);
    const ss = parseInt(ssRaw, 10);
    if (Number.isNaN(mm)) {
      // Invalid input; restore visual to current state by re-rendering
      el.innerText = formatTime(timeLeft);
      return;
    }
    const minutes = Math.min(99, Math.max(0, mm));
    const seconds = Math.min(59, Math.max(0, Number.isNaN(ss) ? 0 : ss));
    const totalSeconds = Math.max(1, minutes * 60 + seconds);
    if (totalSeconds === duration) {
      // No change; rewrite text to clamp display to valid range
      el.innerText = formatTime(totalSeconds);
      return;
    }
    dispatch(
      ac.AlsoToMain({
        type: at.WIDGETS_TIMER_SET_DURATION,
        data: { timerType, duration: totalSeconds },
      })
    );

    recordUserAction(USER_ACTION_TYPES.TIMER_SET, {
      source: "widget",
      legacy: true,
    });
    handleTimerInteraction();
  }, [
    dispatch,
    duration,
    timerType,
    recordUserAction,
    handleTimerInteraction,
    timeLeft,
  ]);

  // Drop this if the spinbutton is ever replaced with a native control.
  const handleSpinBeforeInput = e => {
    const input = e.data;
    if (input === null || input === undefined) {
      return;
    }
    const current = e.target.innerText;
    const selection = window.getSelection();
    const start = selection
      ? Math.min(selection.anchorOffset, selection.focusOffset)
      : current.length;
    const end = selection
      ? Math.max(selection.anchorOffset, selection.focusOffset)
      : current.length;
    if (!isValidSpinbuttonInput(current, input, start, end)) {
      e.preventDefault();
    }
  };

  // Drop this if the spinbutton is ever replaced with a native control.
  const handleSpinKeyDown = e => {
    let next = minutesValue;
    switch (e.key) {
      case "Enter":
        e.preventDefault();
        commitSpinbuttonDuration();
        e.target.blur();
        return;
      case "ArrowUp":
        next = minutesFloor + 1;
        break;
      case "ArrowDown":
        next = minutesFloor - 1;
        break;
      case "PageUp":
        next = minutesFloor + 5;
        break;
      case "PageDown":
        next = minutesFloor - 5;
        break;
      case "Home":
        next = 1;
        break;
      case "End":
        next = 99;
        break;
      default:
        return;
    }
    e.preventDefault();
    setTimerMinutes(next);
  };

  // The item assigns the group's value before firing change, so reading it
  // here is safe. Change fires on re-selecting the active mode too, hence the
  // comparison below.
  const modeGroupRef = useCallback(
    modeGroup => {
      if (!modeGroup) {
        return undefined;
      }
      const handleModeChange = () => {
        const selectedTimerType = modeGroup.value;
        if (selectedTimerType && selectedTimerType !== timerType) {
          toggleType(selectedTimerType);
        }
      };
      modeGroup.addEventListener("change", handleModeChange);
      return () => modeGroup.removeEventListener("change", handleModeChange);
    },
    [timerType, toggleType]
  );

  // Keep the running-state body layout through the celebration so the ring
  // doesn't shift to a third position during the animation.
  const bodyShowsRunningLayout = hasProgressed || isCelebrating || isComplete;

  // Small has no manual Focus/Break toggle; medium and large show it when idle.
  const showModeGroup =
    !bodyShowsRunningLayout &&
    (widgetSize === "medium" || widgetSize === "large");

  // Small shows the celebration headline only; other sizes add a subhead.
  let celebrationSubheadL10nId;
  if (widgetSize !== "small") {
    celebrationSubheadL10nId =
      timerType === "focus"
        ? "newtab-widget-timer-celebration-message-focus"
        : "newtab-widget-timer-celebration-message-break";
  }

  return timerData ? (
    <article
      // @nova-cleanup(remove-conditional): Remove novaEnabled check; always apply col-4 and size class after Nova ships
      className={`focus-timer widget ${novaEnabled ? `col-4 ${widgetSize}-widget` : ""} ${isSmallSize ? "is-small" : ""} ${isMaximized ? "is-maximized" : ""}${isComplete ? " is-complete" : ""}${isCelebrating ? " is-celebrating" : ""}${hasProgressed && !isComplete ? " is-active" : ""}`}
      ref={el => {
        impressionRef(el);
        widgetCelebrationRef.current = el;
      }}
    >
      {
        // @nova-cleanup(remove-conditional): drop the `novaEnabled &&` guard.
        novaEnabled && isCelebrating && celebrationFrame ? (
          <WidgetCelebration
            classNamePrefix="focus-timer-celebration"
            celebrationFrame={celebrationFrame}
            celebrationId={celebrationId}
            gradientStops={FOCUS_TIMER_CELEBRATION_GRADIENT_STOPS}
            headlineL10nId={
              timerType === "focus"
                ? "newtab-widget-timer-celebration-heading-focus"
                : "newtab-widget-timer-celebration-heading-break"
            }
            illustrationSrc={null}
            onComplete={handleCelebrationComplete}
            subheadL10nId={celebrationSubheadL10nId}
          />
        ) : null
      }
      <div className="newtab-widget-timer-notification-title-wrapper">
        <h2 data-l10n-id="newtab-widget-timer-notification-title"></h2>
        <div className="focus-timer-context-menu-wrapper">
          <WidgetMenuButton
            className="focus-timer-context-menu-button"
            menuId="focus-timer-context-menu"
            l10nId="newtab-widget-timer-open-menu-button"
          />
          <panel-list
            className="panel-list-no-icons"
            id="focus-timer-context-menu"
          >
            <panel-item
              data-l10n-id={
                showSystemNotifications
                  ? "newtab-widget-timer-menu-notifications"
                  : "newtab-widget-timer-menu-notifications-on"
              }
              onClick={() => {
                handlePrefUpdate(
                  "widgets.focusTimer.showSystemNotifications",
                  !showSystemNotifications
                );
              }}
            />
            <WidgetMenuFooter
              dispatch={dispatch}
              widgetId="focusTimer"
              widgetEnabledMap={widgetEnabledMap}
              widgetName="focus_timer"
              enabledPref="widgets.focusTimer.enabled"
              widgetSize={widgetSize}
              learnMoreL10nId="newtab-widget-timer-menu-learn-more"
              onLearnMore={handleTimerInteraction}
              sizeSubmenu={
                // @nova-cleanup(remove-conditional): Remove the `novaEnabled &&` check; keep widgetsMayBeMaximized
                novaEnabled &&
                widgetsMayBeMaximized && (
                  <SizeSubmenu
                    submenuId="focus-timer-size-submenu"
                    sizes={["small", "medium", "large"]}
                    checkedSize={widgetSize}
                    onChangeSize={handleChangeSize}
                  />
                )
              }
            />
          </panel-list>
        </div>
      </div>
      {
        // @nova-cleanup(remove-conditional): Remove this branch and the legacy block below; keep only the Nova body
        novaEnabled ? (
          <>
            {/*
             * Clicking anywhere inside the circle (including the ring) toggles
             * the timer. The moz-button inside still owns focus and the
             * accessible name; its click handler stops propagation so the
             * wrapper handler doesn't double-fire.
             */}
            <div
              role="progress"
              className={`progress-circle-wrapper${isComplete ? " is-complete" : ""}${hasProgressed ? " is-active" : ""}`}
              onClick={toggleTimer}
            >
              <div
                className={`progress-circle-background${timerType === "break" ? "-break" : ""}`}
              />
              <div
                className={`progress-circle ${timerType === "focus" ? "focus-visible" : "focus-hidden"}`}
                ref={timerType === "focus" ? arcRef : null}
              />
              <div
                className={`progress-circle ${timerType === "break" ? "break-visible" : "break-hidden"}`}
                ref={timerType === "break" ? arcRef : null}
              />
              <div
                className={`progress-circle-complete${isComplete ? " visible" : ""}`}
              />
              {progress > 0 && progress < 1 && (
                <div
                  className={`progress-circle-cap-rotator is-${timerType}`}
                  style={{ "--progress-angle": `${progress * 360}deg` }}
                  aria-hidden="true"
                >
                  <div className="progress-circle-cap" />
                </div>
              )}
              <moz-button
                className="focus-timer-play-button"
                type="icon ghost"
                iconsrc={`resource://content-accessible/media/${isRunning ? "pause" : "play"}-fill.svg`}
                data-l10n-id={
                  isRunning
                    ? "newtab-widget-timer-pause-aria"
                    : "newtab-widget-timer-start-aria"
                }
                data-l10n-args={JSON.stringify({ minutes: minutesValue })}
                onClick={e => {
                  e.stopPropagation();
                  toggleTimer();
                }}
              />
            </div>

            <div className="focus-timer-body">
              <div className="focus-timer-time-slot">
                {bodyShowsRunningLayout && (
                  <div className="focus-timer-time-display">
                    <span className="focus-timer-time-text">
                      {formatTime(timeLeft)}
                    </span>
                    <span
                      className="focus-timer-time-mode"
                      data-l10n-id={
                        timerType === "focus"
                          ? "newtab-widget-timer-running-focus"
                          : "newtab-widget-timer-running-break"
                      }
                    />
                  </div>
                )}
                {!bodyShowsRunningLayout && (
                  <div className="focus-timer-time-row">
                    <moz-button
                      className="focus-timer-minute-decrement"
                      type="icon ghost"
                      iconsrc="chrome://global/skin/icons/minus.svg"
                      data-l10n-id="newtab-widget-timer-decrease-min"
                      aria-controls="focus-timer-spinbutton"
                      tabindex="-1"
                      onClick={() => setTimerMinutes(minutesFloor - 1)}
                    />
                    <span
                      id="focus-timer-spinbutton"
                      className="focus-timer-spinbutton"
                      role="spinbutton"
                      aria-valuemin={1}
                      aria-valuemax={99}
                      aria-valuenow={minutesValue}
                      data-l10n-id="newtab-widget-timer-spinbutton-name"
                      data-l10n-args={JSON.stringify({
                        minutes: minutesValue,
                      })}
                      contentEditable="true"
                      suppressContentEditableWarning={true}
                      tabIndex={0}
                      onKeyDown={handleSpinKeyDown}
                      onBeforeInput={handleSpinBeforeInput}
                      onFocus={handleFocus}
                      onBlur={commitSpinbuttonDuration}
                      ref={activeMinutesRef}
                    >
                      {formatTime(timeLeft)}
                    </span>
                    <moz-button
                      className="focus-timer-minute-increment"
                      type="icon ghost"
                      iconsrc="chrome://global/skin/icons/plus.svg"
                      data-l10n-id="newtab-widget-timer-increase-min"
                      aria-controls="focus-timer-spinbutton"
                      tabindex="-1"
                      onClick={() => setTimerMinutes(minutesFloor + 1)}
                    />
                  </div>
                )}
              </div>

              <div className="focus-timer-bottom-slot">
                {bodyShowsRunningLayout && widgetSize === "large" && (
                  <moz-button
                    className="focus-timer-reset-button"
                    type="icon"
                    iconsrc="chrome://newtab/content/data/content/assets/arrow-clockwise-16.svg"
                    data-l10n-id="newtab-widget-timer-reset"
                    onClick={resetTimer}
                  />
                )}
                {showModeGroup && (
                  <moz-segmented-control
                    data-l10n-id="newtab-widget-timer-mode-group"
                    value={timerType}
                    ref={modeGroupRef}
                  >
                    <moz-segmented-control-item
                      value="focus"
                      data-l10n-id="newtab-widget-timer-mode-focus"
                    />
                    <moz-segmented-control-item
                      value="break"
                      data-l10n-id="newtab-widget-timer-mode-break"
                    />
                  </moz-segmented-control>
                )}
              </div>
            </div>
          </>
        ) : (
          <>
            <div className="focus-timer-tabs">
              <div className="focus-timer-tabs-buttons">
                <moz-button
                  type={timerType === "focus" ? "default" : "ghost"}
                  data-l10n-id="newtab-widget-timer-mode-focus"
                  size="small"
                  onClick={() => toggleType("focus")}
                />
                <moz-button
                  type={timerType === "break" ? "default" : "ghost"}
                  data-l10n-id="newtab-widget-timer-mode-break"
                  size="small"
                  onClick={() => toggleType("break")}
                />
              </div>
            </div>
            <div
              role="progress"
              className={`progress-circle-wrapper ${
                !showSystemNotifications && !timerData[timerType].isRunning
                  ? "is-small"
                  : ""
              }`}
            >
              <div
                className={`progress-circle-background${timerType === "break" ? "-break" : ""}`}
              />

              <div
                className={`progress-circle ${timerType === "focus" ? "focus-visible" : "focus-hidden"}`}
                ref={timerType === "focus" ? arcRef : null}
              />

              <div
                className={`progress-circle ${timerType === "break" ? "break-visible" : "break-hidden"}`}
                ref={timerType === "break" ? arcRef : null}
              />

              <div
                className={`progress-circle-complete${progress === 1 ? " visible" : ""}`}
              />
              <div role="timer" className="progress-circle-label">
                <EditableTimerFields
                  minutesRef={activeMinutesRef}
                  secondsRef={activeSecondsRef}
                  onKeyDown={handleKeyDown}
                  onBeforeInput={handleBeforeInput}
                  onFocus={handleFocus}
                  timeLeft={timeLeft}
                  onBlur={() => setTimerDuration()}
                />
              </div>
            </div>

            <div className="set-timer-controls-wrapper">
              <div className={`focus-timer-controls timer-running`}>
                <moz-button
                  {...(!isRunning ? { type: "primary" } : {})}
                  iconsrc={`resource://content-accessible/media/${isRunning ? "pause" : "play"}-fill.svg`}
                  data-l10n-id={
                    isRunning
                      ? "newtab-widget-timer-label-pause"
                      : "newtab-widget-timer-label-play"
                  }
                  onClick={toggleTimer}
                />
                {isRunning && (
                  <moz-button
                    type="icon ghost"
                    iconsrc="chrome://newtab/content/data/content/assets/arrow-clockwise-16.svg"
                    data-l10n-id="newtab-widget-timer-reset"
                    onClick={resetTimer}
                  />
                )}
              </div>
            </div>
            {!showSystemNotifications && !timerData[timerType].isRunning && (
              <p
                className="timer-notification-status"
                data-l10n-id="newtab-widget-timer-notification-warning"
              ></p>
            )}
          </>
        )
      }
    </article>
  ) : null;
};
/* eslint-enable complexity, max-statements */

function EditableTimerFields({
  minutesRef,
  secondsRef,
  tabIndex = 0,
  ...props
}) {
  return (
    <>
      <span
        contentEditable="true"
        suppressContentEditableWarning={true}
        ref={minutesRef}
        className="timer-set-minutes"
        onKeyDown={props.onKeyDown}
        onBeforeInput={props.onBeforeInput}
        onFocus={props.onFocus}
        onBlur={props.onBlur}
        tabIndex={tabIndex}
      >
        {formatTime(props.timeLeft).split(":")[0]}
      </span>
      :
      <span
        contentEditable="true"
        suppressContentEditableWarning={true}
        ref={secondsRef}
        className="timer-set-seconds"
        onKeyDown={props.onKeyDown}
        onBeforeInput={props.onBeforeInput}
        onFocus={props.onFocus}
        onBlur={props.onBlur}
        tabIndex={tabIndex}
      >
        {formatTime(props.timeLeft).split(":")[1]}
      </span>
    </>
  );
}
