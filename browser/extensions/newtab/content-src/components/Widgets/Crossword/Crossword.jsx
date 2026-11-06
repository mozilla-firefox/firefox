/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React, {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { useSizeSubmenu } from "../../../lib/utils";
import {
  FRAMED_WIDGET_SANDBOX,
  WIDGET_REGISTRY,
  resolveWidgetFrame,
  resolveWidgetSize,
} from "common/WidgetsRegistry.mjs";
import { MoveSubmenu } from "../MoveSubmenu";
import { WidgetHideMenuItem } from "../WidgetPlacement";
import { useWidgetTelemetry } from "../useWidgetTelemetry";
import { WidgetMenuButton } from "../WidgetMenuButton";
import { WidgetMenuPoweredBy } from "../WidgetMenuPoweredBy";

const USER_ACTION_TYPES = {
  CHANGE_SIZE: "change_size",
  POWERED_BY_PARTICLE: "powered_by_particle",
};

const PARTICLE_URL = "https://particle.news";

// postMessage contract with the Particle crossword bundle (per the widget's
// postMessage API doc). Every message travels on a single channel; the widget
// discards anything without it, and so do we.
const CROSSWORD_CHANNEL = "crossword_widget";

// Outbound: newtab -> widget host commands. Sent with an explicit targetOrigin
// (never "*"). A menu_action carries a unique requestId so the widget can reply
// with a matching command_ack.
const COMMAND_TYPES = {
  MENU_ACTION: "menu_action",
  FORCE_REFRESH: "force_refresh",
};

// Inbound: widget -> newtab events. Only accepted from the Merino bundle origin.
const EVENT_TYPES = {
  COMMAND_ACK: "command_ack",
  WIDGET_READY: "widget_ready",
  WIDGET_ERROR: "widget_error",
  PUZZLE_STATE: "puzzle_state",
  PUZZLE_COMPLETED: "puzzle_completed",
  INTERACTION: "interaction",
};

// The puzzle lifecycle states the widget reports via puzzle_state. Only
// "in_progress" drives the widget to the large layout; "intro" and "completed"
// (the compact returning-completion card) use the user's configured size.
const PUZZLE_STATES = ["intro", "in_progress", "completed"];

// Actions that force the widget size to be large when the puzzle is
// completed if "show clues", "view completed crossword", or "solve puzzle"
// are clicked.
const LARGE_LAYOUT_INTERACTIONS = new Set([
  "admire_crossword_clicked",
  "all_clues_opened",
  "reveal_grid_requested",
]);

const isNonNegativeNumber = value =>
  typeof value === "number" && Number.isFinite(value) && value >= 0;

const isWholeCount = value =>
  isNonNegativeNumber(value) && Number.isInteger(value);

// Structural validators for each inbound event payload. An event whose type is
// unknown, or whose payload fails its validator, is discarded without side
// effects so a malformed/unexpected message can't drive Redux or telemetry.
const EVENT_PAYLOAD_VALIDATORS = {
  [EVENT_TYPES.COMMAND_ACK]: payload =>
    typeof payload?.requestId === "string" &&
    typeof payload?.status === "string",
  [EVENT_TYPES.WIDGET_READY]: () => true,
  [EVENT_TYPES.WIDGET_ERROR]: payload =>
    typeof payload?.reason === "string" &&
    typeof payload?.terminal === "boolean",
  [EVENT_TYPES.PUZZLE_STATE]: payload => PUZZLE_STATES.includes(payload?.state),
  [EVENT_TYPES.PUZZLE_COMPLETED]: payload =>
    isNonNegativeNumber(payload?.elapsedTimeSeconds) &&
    isWholeCount(payload?.hintsTaken),
  [EVENT_TYPES.INTERACTION]: payload => typeof payload?.action === "string",
};

const MENU_ACTION_ITEMS = [
  {
    key: "show-all-clues",
    label: "Show clues",
    action: "show_all_clues",
  },
  {
    // "Solve puzzle" only shows when the crossword is in the intro state or
    // in-progress, so it is hidden once the puzzle is completed.
    key: "solve-puzzle",
    label: "Solve puzzle",
    action: "reveal_grid",
    hideWhenCompleted: true,
  },
];

// This allow list allows us to filter out the echos coming from context-menu interactions,
// as well as per-cell/per-keystroke actions which were creating noisy telemetry event dispatches.
// This way, only the necessary events are sent to Glean.
const INTERACTION_TELEMETRY_ALLOWLIST = new Set([
  "play_started",
  "hint_revealed",
  "related_article_clicked",
  "admire_crossword_clicked",
  "endgame_reveal_incorrect",
]);

const CROSSWORD_ENTRY = WIDGET_REGISTRY.find(w => w.id === "crossword");

// Flipped to true the first time the user interacts with the crossword. Used to
// hide the "New" badge once the widget has been used.
const PREF_CROSSWORD_INTERACTION = "widgets.crossword.interaction";

function Crossword({
  dispatch,
  handleUserInteraction,
  widgetsMayBeMaximized,
  widgetEnabledMap,
}) {
  const prefs = useSelector(state => state.Prefs.values);
  const framedWidgets = useSelector(state => state.FramedWidgets);
  const { src, state: frameState } = resolveWidgetFrame(
    CROSSWORD_ENTRY,
    prefs,
    framedWidgets
  );
  const widgetSize = resolveWidgetSize(CROSSWORD_ENTRY, prefs);
  const hasInteracted = prefs[PREF_CROSSWORD_INTERACTION];
  const iframeRef = useRef(null);

  const { impressionRef, recordUserAction, recordEnabled } = useWidgetTelemetry(
    {
      dispatch,
      widget: CROSSWORD_ENTRY,
      widgetSize,
    }
  );

  // Set once the widget reports the puzzle is finished, so menu actions that
  // only apply to an in-progress game (Solve puzzle) can be hidden.
  const [puzzleCompleted, setPuzzleCompleted] = useState(false);

  // Grow to large once a puzzle is in progress and stay large through the
  // completed screen; only the intro state (or loading directly into completed,
  // i.e. the returning-completion card) stays medium. Driven by puzzle_state,
  // plus the interactions in LARGE_LAYOUT_INTERACTIONS that open a large screen
  const [showLarge, setShowLarge] = useState(false);

  // Gated on widgetsMayBeMaximized so we never render a large-widget on a layout
  // that can't host it.
  const displaySize = widgetsMayBeMaximized && showLarge ? "large" : widgetSize;

  // Any real interaction flips the interaction pref, which hides the "New"
  // badge. The helper is a no-op once the pref is already true.
  const handleInteraction = useCallback(
    () => handleUserInteraction("crossword"),
    [handleUserInteraction]
  );

  // The single origin we accept inbound messages from and target for outbound
  // ones.
  const merinoOrigin = useMemo(() => {
    try {
      return new URL(src).origin;
    } catch {
      return null;
    }
  }, [src]);

  // requestId -> action for menu_action commands awaiting a command_ack, so an
  // incoming ack can be matched back to the action the user selected. A
  // command_ack is the widget's reply confirming it received and processed a
  // command we sent.
  const pendingCommandsRef = useRef(new Map());

  // Post a menu_action host command to the widget, always with the Merino
  // origin as targetOrigin so a replaced/compromised iframe src can never
  // receive it. Each command gets a unique requestId; without one the widget
  // replies with widget_error (host-missing-request-id) instead of a
  // command_ack.
  const postMenuAction = useCallback(
    action => {
      const frameWindow = iframeRef.current?.contentWindow;
      const requestId = `firefox-menu-${crypto.randomUUID()}`;
      if (!frameWindow || !merinoOrigin) {
        return;
      }
      pendingCommandsRef.current.set(requestId, action);
      frameWindow.postMessage(
        {
          channel: CROSSWORD_CHANNEL,
          type: COMMAND_TYPES.MENU_ACTION,
          requestId,
          action,
        },
        merinoOrigin
      );
    },
    [merinoOrigin]
  );

  const handleWidgetEvent = useCallback(
    (type, payload) => {
      switch (type) {
        case EVENT_TYPES.COMMAND_ACK:
          pendingCommandsRef.current.delete(payload.requestId);
          break;
        case EVENT_TYPES.WIDGET_READY:
          break;
        case EVENT_TYPES.WIDGET_ERROR:
          break;
        case EVENT_TYPES.PUZZLE_STATE:
          // Grow when a puzzle is in progress and stay large through the
          // completed screen; shrink only when returning to the intro state.
          if (payload.state === "in_progress") {
            setShowLarge(true);
          } else if (payload.state === "intro") {
            setShowLarge(false);
          }
          //  This keeps "Solve puzzle" hidden when the puzzle's been completed.
          if (payload.state === "completed") {
            setPuzzleCompleted(true);
          } else if (payload.state === "intro") {
            setPuzzleCompleted(false);
          }
          break;
        case EVENT_TYPES.PUZZLE_COMPLETED:
          setPuzzleCompleted(true);
          recordUserAction("puzzle_completed", {
            source: "iframe",
            value: payload.hintsTaken,
            alsoToMain: true,
          });
          break;
        case EVENT_TYPES.INTERACTION:
          // Viewing the completed grid or opening the all-clues panel both need
          // the large layout, even from the medium completed/returning card.
          if (LARGE_LAYOUT_INTERACTIONS.has(payload.action)) {
            setShowLarge(true);
          }
          // Flip the "New" badge pref for every real interaction, but only
          // forward the curated allowlist to Glean (see the allowlist comment).
          handleInteraction();
          if (INTERACTION_TELEMETRY_ALLOWLIST.has(payload.action)) {
            recordUserAction(payload.action, {
              source: "iframe",
              alsoToMain: true,
            });
          }
          break;
        default:
          break;
      }
    },
    [recordUserAction, handleInteraction]
  );

  // Listen for events from the widget, discarding anything that fails origin,
  // source, channel, or payload validation before it can touch Redux/telemetry.
  useEffect(() => {
    if (!merinoOrigin) {
      return undefined;
    }
    function handleMessage(event) {
      if (event.origin !== merinoOrigin) {
        return;
      }
      if (event.source !== iframeRef.current?.contentWindow) {
        return;
      }
      const message = event.data;
      if (
        !message ||
        message.channel !== CROSSWORD_CHANNEL ||
        typeof message.type !== "string"
      ) {
        return;
      }
      const validatePayload = EVENT_PAYLOAD_VALIDATORS[message.type];
      const payload = message.payload ?? {};
      if (!validatePayload || !validatePayload(payload)) {
        return;
      }
      handleWidgetEvent(message.type, payload);
    }
    window.addEventListener("message", handleMessage);
    return () => window.removeEventListener("message", handleMessage);
  }, [merinoOrigin, handleWidgetEvent]);

  const handleMenuAction = useCallback(
    action => {
      handleInteraction();
      postMenuAction(action);
      recordUserAction(action, { source: "context_menu" });
    },
    [handleInteraction, postMenuAction, recordUserAction]
  );

  function handleCrosswordHide() {
    dispatch(
      ac.OnlyToMain({
        type: at.SET_PREF,
        data: { name: CROSSWORD_ENTRY.enabledPref, value: false },
      })
    );
    recordEnabled(false, { source: "context_menu" });
  }

  const handleChangeSize = useCallback(
    size => {
      handleInteraction();
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: CROSSWORD_ENTRY.sizePref, value: size },
        })
      );
      recordUserAction(USER_ACTION_TYPES.CHANGE_SIZE, {
        source: "context_menu",
        value: size,
        size,
      });
    },
    [dispatch, handleInteraction, recordUserAction]
  );

  const sizeSubmenuRef = useSizeSubmenu(handleChangeSize);

  function handleLearnMore() {
    handleInteraction();
    dispatch(
      ac.OnlyToMain({
        type: at.OPEN_LINK,
        data: {
          url: "https://support.mozilla.org/kb/firefox-new-tab-widgets",
        },
      })
    );
    recordUserAction("learn_more", { source: "context_menu" });
  }

  if (frameState === "hidden") {
    return null;
  }

  return (
    <article
      className={`crossword widget col-4 ${displaySize}-widget`}
      ref={impressionRef}
    >
      <div className="crossword-title-wrapper">
        <div className="crossword-badge-title-wrapper">
          {!hasInteracted && (
            <moz-badge
              className="crossword-new-badge"
              data-l10n-id="newtab-widget-lists-label-new"
            ></moz-badge>
          )}
          <h3 className="newtab-widget-title newtab-crossword-title">
            Daily crossword
          </h3>
        </div>
        <div className="crossword-context-menu-wrapper">
          <WidgetMenuButton
            className="crossword-context-menu-button"
            menuId="crossword-context-menu"
            l10nId="newtab-crossword-widget-open-menu-button"
          />
          <panel-list
            className="panel-list-no-icons"
            id="crossword-context-menu"
          >
            {MENU_ACTION_ITEMS.filter(
              item => !(puzzleCompleted && item.hideWhenCompleted)
            ).map(item => (
              <panel-item
                key={item.key}
                className={item.key}
                onClick={() => handleMenuAction(item.action)}
              >
                {item.label}
              </panel-item>
            ))}

            <WidgetMenuPoweredBy
              dispatch={dispatch}
              url={PARTICLE_URL}
              l10nId="newtab-crossword-menu-powered-by-particle"
              recordUserAction={recordUserAction}
              userAction={USER_ACTION_TYPES.POWERED_BY_PARTICLE}
              onClick={handleInteraction}
            />

            <hr />

            {widgetsMayBeMaximized && (
              <panel-item submenu="crossword-size-submenu">
                <span data-l10n-id="newtab-widget-menu-change-size"></span>
                <panel-list
                  ref={sizeSubmenuRef}
                  slot="submenu"
                  id="crossword-size-submenu"
                >
                  {["medium", "large"].map(size => (
                    <panel-item
                      key={size}
                      type="checkbox"
                      checked={widgetSize === size || undefined}
                      data-size={size}
                      data-l10n-id={`newtab-widget-size-${size}`}
                    />
                  ))}
                </panel-list>
              </panel-item>
            )}

            <MoveSubmenu
              widgetId="crossword"
              widgetEnabledMap={widgetEnabledMap}
            />

            <WidgetHideMenuItem
              widgetId="crossword"
              onHide={handleCrosswordHide}
            />
            <panel-item className="learn-more" onClick={handleLearnMore}>
              Learn more
            </panel-item>
          </panel-list>
        </div>
      </div>

      <div className="crossword-body">
        <iframe
          ref={iframeRef}
          className="crossword-frame"
          title="Crossword"
          src={src}
          sandbox={FRAMED_WIDGET_SANDBOX}
        />
      </div>
    </article>
  );
}

export { Crossword };
