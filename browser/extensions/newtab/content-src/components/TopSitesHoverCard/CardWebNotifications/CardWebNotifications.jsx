/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { useDispatch, useSelector } from "react-redux";
import { formatRelativeTime } from "content-src/lib/utils";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  getNotificationIdsForUrl,
  originFromUrl,
} from "content-src/lib/web-notification-match.mjs";
import { proxiedIconUrl } from "content-src/lib/web-notification-icon.mjs";
import { CUSTOMIZE_PANEL_SOURCES } from "content-src/lib/constants.mjs";
import React from "react";

// Origins whose notification icon just repeats the site's own shortcut icon, so
// listing it is visual noise. Hand-curated; grown as needed.
const ICON_SUPPRESS_ORIGINS = new Set(["https://apnews.com"]);

function NotificationTime({ timestamp, locale, now }) {
  if (!timestamp) {
    return null;
  }
  const { text, l10nId } = formatRelativeTime(
    timestamp,
    locale,
    now,
    "newtab-topsites-hover-card-just-now"
  );
  return (
    <time
      className="top-sites-hover-card-notification-time"
      dateTime={new Date(timestamp).toISOString()}
      data-l10n-id={l10nId}
    >
      {text}
    </time>
  );
}

/**
 * A notification's icon, proxied. Renders nothing when the icon cannot be
 * proxied or the proxy fails to serve it — there is deliberately no fallback to
 * the origin URL, which is the load the proxy exists to avoid.
 */
function NotificationIcon({ notification }) {
  const [failed, setFailed] = React.useState(false);

  if (ICON_SUPPRESS_ORIGINS.has(notification.origin)) {
    return null;
  }
  const src = proxiedIconUrl(notification.icon);
  if (!src || failed) {
    return null;
  }
  return (
    <img
      src={src}
      alt=""
      className="top-sites-hover-card-notification-icon"
      onError={() => setFailed(true)}
    />
  );
}

function NotificationList({
  notifications,
  locale,
  now,
  onActivate,
  onDismiss,
}) {
  return (
    <ul className="top-sites-hover-card-notifications">
      {notifications.map(notification => {
        return (
          <li
            className="top-sites-hover-card-notification"
            key={notification.id}
            dir={notification.dir || "auto"}
          >
            <button
              type="button"
              className="top-sites-hover-card-notification-activate"
              onClick={() => onActivate(notification)}
            >
              <NotificationIcon notification={notification} />
              <div className="top-sites-hover-card-notification-text">
                <span className="top-sites-hover-card-notification-title">
                  {notification.title}
                </span>
                {notification.body ? (
                  <span className="top-sites-hover-card-notification-body">
                    {notification.body}
                  </span>
                ) : null}
                <NotificationTime
                  timestamp={notification.timestamp}
                  locale={locale}
                  now={now}
                />
              </div>
            </button>
            <button
              type="button"
              className="top-sites-hover-card-notification-dismiss"
              data-l10n-id="newtab-topsites-hover-card-dismiss"
              onClick={() => onDismiss(notification)}
            />
          </li>
        );
      })}
    </ul>
  );
}

/**
 * Web notifications variant of the top-sites hover card. Lists the hovered
 * site's web notifications, most recent first, and renders nothing when the
 * site has no notifications. The list scrolls in place past roughly three
 * entries rather than spilling into a separate surface.
 *
 * This is one discrete content card behind the TopSitesHoverCard shell, which
 * renders exactly one variant per tile (see hover-card-content.jsx). Clicking a
 * service-worker notification fires its origin and dismisses it; the dismiss and
 * mark-all controls remove entries durably via WebNotificationsFeed.
 *
 * @param link The top site link object for the hovered tile.
 */
function CardWebNotifications({ link }) {
  const dispatch = useDispatch();
  const locale = useSelector(state => state.App.locale);
  const byId = useSelector(state => state.WebNotifications.notifications);
  const ids = useSelector(state => getNotificationIdsForUrl(state, link?.url));

  const notifications = ids
    .map(id => byId[id])
    .filter(Boolean)
    .sort((a, b) => (b.timestamp || 0) - (a.timestamp || 0));

  if (!notifications.length) {
    return null;
  }

  const site = link?.label || link?.hostname || originFromUrl(link?.url) || "";
  const now = Date.now();

  const openSettings = () => {
    dispatch({
      type: at.SHOW_PERSONALIZE,
      data: { source: CUSTOMIZE_PANEL_SOURCES.TOP_SITES_WEB_NOTIFICATIONS },
    });
  };

  const activate = notification =>
    dispatch(
      ac.AlsoToMain({
        type: at.WEB_NOTIFICATIONS_CLICK,
        data: { origin: notification.origin, id: notification.id },
      })
    );

  const dismiss = notification =>
    dispatch(
      ac.AlsoToMain({
        type: at.WEB_NOTIFICATIONS_DISMISS,
        data: { origin: notification.origin, id: notification.id },
      })
    );

  const dismissAll = () =>
    dispatch(
      ac.AlsoToMain({
        type: at.WEB_NOTIFICATIONS_DISMISS_ALL,
        data: { origin: notifications[0].origin },
      })
    );

  return (
    <div className="top-sites-hover-card" role="group">
      <div className="top-sites-hover-card-inner">
        <div className="top-sites-hover-card-header">
          <span
            className="top-sites-hover-card-header-title"
            data-l10n-id="newtab-topsites-hover-card-header"
            data-l10n-args={JSON.stringify({ site })}
          />
          <div className="top-sites-hover-card-header-actions">
            <button
              type="button"
              className="top-sites-hover-card-mark-read"
              data-l10n-id="newtab-topsites-hover-card-mark-all-read"
              onClick={dismissAll}
            />
            <button
              type="button"
              className="top-sites-hover-card-settings"
              data-l10n-id="newtab-topsites-hover-card-settings"
              onClick={openSettings}
            />
          </div>
        </div>
        <NotificationList
          notifications={notifications}
          locale={locale}
          now={now}
          onActivate={activate}
          onDismiss={dismiss}
        />
      </div>
    </div>
  );
}

export { CardWebNotifications };
