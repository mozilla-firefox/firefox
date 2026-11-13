/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React, { useCallback } from "react";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  WIDGET_REGISTRY,
  resolveWidgetSize,
  WIDGET_USER_ACTIONS,
  WIDGET_SOURCES,
} from "common/WidgetsRegistry.mjs";
import { WidgetMenuButton } from "../WidgetMenuButton";
import { WidgetMenuFooter } from "../WidgetMenuFooter";
import { SizeSubmenu } from "../SizeSubmenu";
import { useWidgetTelemetry } from "../useWidgetTelemetry";
import { formatRelativeTime } from "content-src/lib/utils";

const USER_ACTION_TYPES = {
  ...WIDGET_USER_ACTIONS,
  CHANGE_TAB: "change_tab",
  OPEN_LINK: "open_link",
  REMOVE_SEARCH: "remove_search",
};

const TABS = {
  RECENT: "recent",
  TRENDING: "trending",
};

// Store the last tab the user switched to in case the user wants to hide
// their recent searches briefly.
const TAB_PREF = "widgets.recentSearches.tab";

const TAB_ICONS = {
  [TABS.RECENT]: "chrome://browser/skin/history.svg",
  [TABS.TRENDING]: "chrome://global/skin/icons/trending.svg",
};

const RECENT_SEARCHES_ENTRY = WIDGET_REGISTRY.find(
  w => w.id === "recentSearches"
);

function RowTime({ lastUsed, locale, now }) {
  const { text, l10nId } = formatRelativeTime(
    lastUsed,
    locale,
    now,
    "newtab-recent-searches-just-now"
  );
  return (
    <time
      className="recent-searches-row-time"
      dateTime={new Date(lastUsed).toISOString()}
      data-l10n-id={l10nId}
    >
      {text}
    </time>
  );
}

/**
 * Recent Searches widget.
 *
 * @param {object} props
 * @param {Function} props.dispatch - Redux dispatch.
 * @param {Function} props.handleUserInteraction - Marks the widget as
 *   interacted with, which removes the "New" badge.
 * @param {boolean} props.widgetsMayBeMaximized - Whether the current layout
 *   allows resizing, which gates the Change size submenu.
 * @param {object} props.widgetEnabledMap - Map of widget id to whether it is
 *   currently active, used by the Move submenu.
 */
function RecentSearches({
  dispatch,
  handleUserInteraction,
  widgetsMayBeMaximized,
  widgetEnabledMap,
}) {
  const prefs = useSelector(state => state.Prefs.values);
  const locale = useSelector(state => state.App.locale);
  const { initialized, searches, trending, engineName } = useSelector(
    state => state.RecentSearches
  );

  const isTrending = prefs[TAB_PREF] === TABS.TRENDING;
  const activeTab = isTrending ? TABS.TRENDING : TABS.RECENT;

  const now = Date.now();
  const widgetSize = resolveWidgetSize(RECENT_SEARCHES_ENTRY, prefs);
  const rows = isTrending
    ? (trending ?? []).map(value => ({ value }))
    : searches;
  const isEmpty =
    !rows.length && (isTrending ? Array.isArray(trending) : initialized);

  // Show the "New" badge until the user first interacts with the widget.
  const hasInteracted = prefs["widgets.recentSearches.interaction"];

  const { impressionRef, recordUserAction } = useWidgetTelemetry({
    dispatch,
    widget: RECENT_SEARCHES_ENTRY,
    widgetSize,
  });

  const handleInteraction = useCallback(
    () => handleUserInteraction("recentSearches"),
    [handleUserInteraction]
  );

  const handleChangeSize = useCallback(
    size => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: RECENT_SEARCHES_ENTRY.sizePref, value: size },
        })
      );
      // `value` is action_value; `size` overrides the reported widget_size,
      // which would otherwise still be the pre-change size.
      recordUserAction(USER_ACTION_TYPES.CHANGE_SIZE, {
        source: WIDGET_SOURCES.CONTEXT_MENU,
        value: size,
        size,
      });
      handleInteraction();
    },
    [dispatch, recordUserAction, handleInteraction]
  );

  // The shared footer opens the support link; here we only record the click.
  function handleLearnMore() {
    recordUserAction(USER_ACTION_TYPES.LEARN_MORE, {
      source: WIDGET_SOURCES.CONTEXT_MENU,
    });
    handleInteraction();
  }

  const handleTabChange = useCallback(
    tab => {
      if (tab === activeTab) {
        return;
      }
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: TAB_PREF, value: tab },
        })
      );
      recordUserAction(USER_ACTION_TYPES.CHANGE_TAB, {
        source: WIDGET_SOURCES.WIDGET,
        value: tab,
      });
      handleInteraction();
    },
    [activeTab, dispatch, recordUserAction, handleInteraction]
  );

  const handleRemoveClick = useCallback(
    search => {
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_RECENT_SEARCHES_REMOVE_SEARCH,
          data: { search },
        })
      );
      recordUserAction(USER_ACTION_TYPES.REMOVE_SEARCH, {
        source: WIDGET_SOURCES.WIDGET,
      });
      handleInteraction();
    },
    [dispatch, recordUserAction, handleInteraction]
  );

  const handleSearchClick = useCallback(
    (search, event) => {
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_RECENT_SEARCHES_OPEN_LINK,
          data: {
            search,
            // The main process needs the modifiers to decide where to open.
            eventInfo: {
              button: event.button,
              shiftKey: event.shiftKey,
              ctrlKey: event.ctrlKey,
              metaKey: event.metaKey,
              altKey: event.altKey,
            },
          },
        })
      );
      recordUserAction(USER_ACTION_TYPES.OPEN_LINK, {
        source: WIDGET_SOURCES.WIDGET,
      });
      handleInteraction();
    },
    [dispatch, recordUserAction, handleInteraction]
  );

  return (
    <article
      className={`recent-searches widget col-4 ${widgetSize}-widget`}
      ref={impressionRef}
      aria-labelledby="recent-searches-widget-label"
    >
      <div className="recent-searches-title-wrapper">
        <div className="recent-searches-badge-title-wrapper">
          {!hasInteracted && (
            <moz-badge
              className="recent-searches-new-badge"
              data-l10n-id="newtab-widget-lists-label-new"
            ></moz-badge>
          )}
          <h2
            id="recent-searches-widget-label"
            className="newtab-widget-title recent-searches-title"
            data-l10n-id="newtab-search-widget-title"
            hidden={true}
          />
          <div className="recent-searches-tabs" role="tablist">
            {Object.values(TABS).map(tab => {
              const label = {
                "data-l10n-id": `newtab-recent-searches-tab-${tab}`,
              };
              return (
                <button
                  key={tab}
                  id={`recent-searches-${tab}-tab`}
                  role="tab"
                  aria-selected={activeTab === tab}
                  className={`recent-searches-tab${
                    activeTab === tab ? " is-active" : ""
                  }`}
                  onClick={() => handleTabChange(tab)}
                >
                  <span className="recent-searches-tab-label" {...label} />
                  <span
                    className="recent-searches-tab-reserve"
                    aria-hidden="true"
                    {...label}
                  />
                </button>
              );
            })}
          </div>
        </div>
        <div className="recent-searches-context-menu-wrapper">
          <WidgetMenuButton
            className="recent-searches-context-menu-button"
            menuId="recent-searches-context-menu"
            l10nId="newtab-search-widget-open-menu-button"
          />
          <panel-list
            className="panel-list-no-icons"
            id="recent-searches-context-menu"
          >
            {/* No items above the footer yet, so its leading divider is off.
                Add widget items here and set showDivider back to true. */}
            <WidgetMenuFooter
              dispatch={dispatch}
              widgetId={RECENT_SEARCHES_ENTRY.id}
              widgetEnabledMap={widgetEnabledMap}
              widgetName={RECENT_SEARCHES_ENTRY.telemetryName}
              enabledPref={RECENT_SEARCHES_ENTRY.enabledPref}
              widgetSize={widgetSize}
              learnMoreL10nId="newtab-recent-searches-menu-learn-more"
              onLearnMore={handleLearnMore}
              showDivider={false}
              sizeSubmenu={
                widgetsMayBeMaximized ? (
                  <SizeSubmenu
                    submenuId="recent-searches-size-submenu"
                    sizes={RECENT_SEARCHES_ENTRY.validSizes}
                    checkedSize={widgetSize}
                    onChangeSize={handleChangeSize}
                  />
                ) : null
              }
            />
          </panel-list>
        </div>
      </div>

      {/* tabindex=-1 because we don't want the container to be its own separate focus
          target, the child buttons should receive the first focus when tabbed into. */}
      <div
        className="recent-searches-body"
        role="tabpanel"
        tabIndex={-1}
        aria-labelledby={`recent-searches-${activeTab}-tab`}
      >
        {isEmpty && (
          <div className="recent-searches-empty">
            <p
              className="recent-searches-empty-message"
              data-l10n-id={`newtab-recent-searches-empty-${activeTab}`}
            />
          </div>
        )}
        {rows.map(({ value, lastUsed }) => (
          // The row wraps two controls, so the search cannot be a button
          // containing the remove button.
          <div key={value} className="recent-searches-row">
            <moz-button
              className="recent-searches-row-search"
              type="ghost"
              size="small"
              iconSrc={TAB_ICONS[activeTab]}
              onClick={event => handleSearchClick(value, event)}
            >
              <span className="recent-searches-row-label">{value}</span>
              {lastUsed ? (
                <RowTime lastUsed={lastUsed} locale={locale} now={now} />
              ) : null}
            </moz-button>
            {activeTab === TABS.RECENT && (
              <moz-button
                className="recent-searches-row-remove"
                type="icon ghost"
                size="small"
                iconSrc="chrome://global/skin/icons/delete.svg"
                onClick={() => handleRemoveClick(value)}
                data-l10n-id="newtab-recent-searches-row-remove"
                data-l10n-args={JSON.stringify({ search: value })}
              />
            )}
          </div>
        ))}
      </div>

      {isTrending && !!rows.length && !!engineName && (
        <p
          className="recent-searches-trending-attribution"
          data-l10n-id="newtab-recent-searches-trending-attribution"
          data-l10n-args={JSON.stringify({ engine: engineName })}
        />
      )}
    </article>
  );
}

export { RecentSearches };
