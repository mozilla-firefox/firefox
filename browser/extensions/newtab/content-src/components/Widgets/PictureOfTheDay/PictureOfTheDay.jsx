/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React, { useCallback, useEffect, useState } from "react";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { useSizeSubmenu } from "../../../lib/utils";
import { WIDGET_REGISTRY, resolveWidgetSize } from "common/WidgetsRegistry.mjs";
import { MoveSubmenu } from "../MoveSubmenu";
import { WidgetHideMenuItem } from "../WidgetPlacement";
import { useWidgetTelemetry } from "../useWidgetTelemetry";
import { WidgetMenuButton } from "../WidgetMenuButton";

const PICTURE_OF_THE_DAY_ENTRY = WIDGET_REGISTRY.find(
  w => w.id === "pictureOfTheDay"
);

// Whether the "Set as wallpaper" feature is enabled. The dedicated
// widgetPictureOfTheDay trainhop object wins, then the legacy widgets.* key, then
// the pref (each checked with !== undefined so a trainhop `false` can turn the
// feature off even when the pref default is `true`).
function resolveSetAsWallpaperEnabled(prefs) {
  const dedicated =
    prefs.trainhopConfig?.widgetPictureOfTheDay?.setAsWallpaperEnabled;
  if (dedicated !== undefined) {
    return dedicated;
  }
  const shared =
    prefs.trainhopConfig?.widgets?.pictureOfTheDaySetAsWallpaperEnabled;
  if (shared !== undefined) {
    return shared;
  }
  return Boolean(prefs["widgets.pictureOfTheDay.setAsWallpaper.enabled"]);
}

// How long the confirmation checkmark shows after setting the wallpaper.
const JUST_SET_CHECKMARK_MS = 2000;

// "Set wallpaper" button icons: the canvas icon by default, swapped for a
// checkmark during the brief post-set confirmation.
const SET_WALLPAPER_ICON = "chrome://browser/skin/canvas.svg";
const SET_WALLPAPER_CHECK_ICON = "chrome://global/skin/icons/check.svg";

// Local midnight for a "YYYY-MM-DD" date, as Fluent's DATETIME takes a timestamp
// and has no time zone option. Parsing the string directly would read it as UTC.
const toLocalTimestamp = isoDate => {
  const [year, month, day] = isoDate.split("-").map(Number);
  const timestamp = new Date(year, month - 1, day).getTime();
  return Number.isFinite(timestamp) ? timestamp : null;
};

// The daily Merino picture (image, attribution, description, "Set wallpaper"),
// or a sunrise-gradient empty state with an eye button when it's hidden/absent.
const PictureOfTheDay = ({
  dispatch,
  handleUserInteraction,
  widgetsMayBeMaximized,
  widgetEnabledMap,
}) => {
  const prefs = useSelector(state => state.Prefs.values);
  const todaysPicture = useSelector(state => state.PictureOfTheDay);
  // While today's picture is the wallpaper, show the previous one so the same
  // image isn't on the page twice (bug 2065871).
  const pictureData =
    todaysPicture.previous?.thumbnailUrl &&
    todaysPicture.publishedDate &&
    prefs["newtabWallpapers.user.enabled"] &&
    prefs["widgets.pictureOfTheDay.wallpaperActive"] ===
      todaysPicture.publishedDate
      ? todaysPicture.previous
      : todaysPicture;
  const widgetSize = resolveWidgetSize(PICTURE_OF_THE_DAY_ENTRY, prefs);

  // Only offer the "Set wallpaper" CTA when the feature is enabled and wallpapers
  // are on and custom wallpapers are allowed, since this action sets a custom
  // wallpaper.
  const canSetWallpaper = Boolean(
    resolveSetAsWallpaperEnabled(prefs) &&
    prefs["newtabWallpapers.enabled"] &&
    prefs["newtabWallpapers.customWallpaper.enabled"]
  );

  // Fall back to the empty state when the picture fails to load (e.g. a cached
  // URL opened offline, or a broken/404 image) instead of showing a broken
  // image. Reset when a new picture arrives so the next day's image is tried.
  const [imageFailed, setImageFailed] = useState(false);
  // Keep the picture hidden until it loads so its alt text never flashes.
  const [imageLoaded, setImageLoaded] = useState(false);
  useEffect(() => {
    setImageFailed(false);
  }, [pictureData.thumbnailUrl]);

  // Dismissal is keyed to the picture's date so a new day's picture shows again
  // automatically (Bug 2050972). Edge case: Merino may omit published_date, and
  // without a key "Hide this picture" would silently no-op, so fall back to
  // the local day. Tradeoff: hide then tracks the user's local clock, so an
  // undated picture restores at local midnight even if Merino hasn't rotated it.
  const pictureDate = pictureData.publishedDate || new Date().toDateString();
  const dismissed =
    pictureDate === prefs["widgets.pictureOfTheDay.dismissedDate"];
  // The picture is the active wallpaper only while the stored published date
  // matches the currently-shown picture (mirrors the dismissed check above, so a
  // new day's picture automatically re-offers the CTA) AND wallpapers are toggled
  // on. Toggling wallpapers off in the Content section keeps the picture selected
  // but hidden, so the checkmark hides while off and returns when toggled back on.
  const isSetAsWallpaper =
    Boolean(prefs["newtabWallpapers.user.enabled"]) &&
    pictureDate === prefs["widgets.pictureOfTheDay.wallpaperActive"];
  const hasPicture =
    Boolean(pictureData.thumbnailUrl) && !dismissed && !imageFailed;
  // Reset only when the image leaves the page. On a new URL, Gecko keeps the
  // old picture painted until the new one is ready.
  useEffect(() => {
    if (!hasPicture) {
      setImageLoaded(false);
    }
  }, [hasPicture]);

  // Show the "New" badge until the user first interacts with the widget;
  // handleInteraction flips widgets.pictureOfTheDay.interaction on any action,
  // which removes it.
  const hasInteracted = prefs["widgets.pictureOfTheDay.interaction"];

  // Show a brief checkmark right after the user sets the wallpaper, then settle
  // into the collapsed "already set" state.
  const [justSet, setJustSet] = useState(false);
  useEffect(() => {
    if (!justSet) {
      return undefined;
    }
    const timer = setTimeout(() => setJustSet(false), JUST_SET_CHECKMARK_MS);
    return () => clearTimeout(timer);
  }, [justSet]);

  // After setting, keep the button collapsed for the current hover/focus
  // session so it doesn't pop open to the pill the moment the checkmark clears;
  // a fresh hover (after leaving the widget) expands it again.
  const [suppressExpand, setSuppressExpand] = useState(false);

  // Setting today's picture swaps the previous one in, so drop the checkmark
  // rather than show it on a picture that isn't the wallpaper.
  useEffect(() => {
    setJustSet(false);
  }, [pictureData.publishedDate]);

  const { impressionRef, recordUserAction, recordEnabled } = useWidgetTelemetry(
    { dispatch, widget: PICTURE_OF_THE_DAY_ENTRY, widgetSize }
  );

  // Flip widgets.pictureOfTheDay.interaction on the first user action; every
  // handler calls it so no action is missed (the flip is idempotent).
  const handleInteraction = useCallback(
    () => handleUserInteraction("pictureOfTheDay"),
    [handleUserInteraction]
  );

  // Alt text uses the (localized) description when present, else a localized
  // generic fallback (a11y decision, Bug 2050975; the raw image title was
  // rejected as unreliable). Resolved via the l10n value API (computed string).
  const [fallbackAlt, setFallbackAlt] = useState("");
  useEffect(() => {
    document.l10n
      ?.formatValues?.([{ id: "newtab-picture-image-alt" }])
      ?.then(([value]) => value && setFallbackAlt(value));
  }, []);
  const imageAlt = pictureData.description || fallbackAlt;

  const handleHide = () => {
    dispatch(
      ac.OnlyToMain({
        type: at.SET_PREF,
        data: { name: PICTURE_OF_THE_DAY_ENTRY.enabledPref, value: false },
      })
    );
    // Disabling the widget is not an interaction, so it does not flip the
    // interaction pref.
    recordEnabled(false, { source: "context_menu" });
  };

  const handleChangeSize = useCallback(
    size => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: PICTURE_OF_THE_DAY_ENTRY.sizePref, value: size },
        })
      );
      recordUserAction("change_size", {
        source: "context_menu",
        value: size,
        size,
      });
      handleInteraction();
    },
    [dispatch, recordUserAction, handleInteraction]
  );

  const sizeSubmenuRef = useSizeSubmenu(handleChangeSize);

  const handleLearnMore = () => {
    dispatch(
      ac.OnlyToMain({
        type: at.OPEN_LINK,
        data: {
          url: "https://support.mozilla.org/kb/firefox-new-tab-widgets",
          where: "tab",
        },
      })
    );
    recordUserAction("learn_more", { source: "context_menu" });
    handleInteraction();
  };

  const handleManageWallpaper = () => {
    dispatch({
      type: at.SHOW_PERSONALIZE,
      data: { source: PICTURE_OF_THE_DAY_ENTRY.telemetryName },
    });
    recordUserAction("manage_wallpaper", { source: "context_menu" });
    handleInteraction();
  };

  const setDismissedDate = value =>
    dispatch(
      ac.OnlyToMain({
        type: at.SET_PREF,
        data: { name: "widgets.pictureOfTheDay.dismissedDate", value },
      })
    );

  const handleHidePhoto = () => {
    setDismissedDate(pictureDate);
    recordUserAction("hide_photo", { source: "context_menu" });
    handleInteraction();
  };

  const handleShow = (source = "widget") => {
    setDismissedDate("");
    recordUserAction("show_picture", { source });
    handleInteraction();
  };

  // The Merino image host doesn't send CORS headers, so the picture bytes can
  // only be read in the privileged main process. The feed does the fetch,
  // derives the theme, and applies it as the custom wallpaper; show a checkmark
  // confirmation immediately.
  const handleSetWallpaper = () => {
    // Once the picture is the active wallpaper the button is just a status
    // checkmark, so clicking it is a no-op.
    if (isSetAsWallpaper) {
      return;
    }
    dispatch(
      ac.OnlyToMain({
        type: at.WIDGETS_PICTURE_SET_WALLPAPER,
        data: { publishedDate: pictureData.publishedDate },
      })
    );
    recordUserAction("set_wallpaper", { source: "widget" });
    handleInteraction();
    setJustSet(true);
    setSuppressExpand(true);
  };

  // The image, source line, and description are anchors whose href opens the
  // picture's source page (Wikimedia Commons) in the current tab via native
  // navigation; the handler only records telemetry (mirrors the Weather widget).
  const canOpenSource = Boolean(pictureData.sourceUrl);
  const handleOpenSource = () => {
    recordUserAction("open_source", { source: "widget" });
    handleInteraction();
  };

  // The license name is an anchor whose href opens the license terms (Creative
  // Commons) in the current tab, separate from the source page link above.
  const canOpenLicense = Boolean(pictureData.licenseUrl);
  const handleOpenLicense = () => {
    recordUserAction("open_license", { source: "widget" });
    handleInteraction();
  };

  // Attribution line under the title: "© {author} / {source} / {license}".
  // Each part renders only when its field is present; the source and license
  // are links (when their URLs are supplied), the author is plain text.
  const renderAttribution = () => {
    const parts = [];
    if (pictureData.author) {
      parts.push(
        <span
          key="author"
          className="picture-of-the-day-attribution-author"
          data-l10n-id="newtab-picture-attribution-author"
          data-l10n-args={JSON.stringify({ author: pictureData.author })}
        ></span>
      );
    }
    if (canOpenSource) {
      parts.push(
        <a
          key="source"
          href={pictureData.sourceUrl}
          className="picture-of-the-day-attribution-link picture-of-the-day-source-link"
          data-l10n-id="newtab-picture-attribution-source-link"
          onClick={handleOpenSource}
        ></a>
      );
    }
    if (pictureData.licenseLabel) {
      parts.push(
        canOpenLicense ? (
          <a
            key="license"
            href={pictureData.licenseUrl}
            className="picture-of-the-day-attribution-link picture-of-the-day-source-link"
            data-l10n-id="newtab-picture-attribution-license"
            data-l10n-args={JSON.stringify({
              license: pictureData.licenseLabel,
            })}
            onClick={handleOpenLicense}
          >
            {pictureData.licenseLabel}
          </a>
        ) : (
          <span key="license" className="picture-of-the-day-attribution-item">
            {pictureData.licenseLabel}
          </span>
        )
      );
    }
    if (!parts.length) {
      return null;
    }
    return (
      <p className="picture-of-the-day-attribution">
        {parts.map((part, i) => (
          <React.Fragment key={part.key}>
            {i > 0 ? (
              <span
                className="picture-of-the-day-attribution-sep"
                aria-hidden="true"
              >
                {" / "}
              </span>
            ) : null}
            {part}
          </React.Fragment>
        ))}
      </p>
    );
  };

  const headerTimestamp = pictureData.publishedDate
    ? toLocalTimestamp(pictureData.publishedDate)
    : null;
  const headerL10nId = headerTimestamp
    ? "newtab-picture-header-date"
    : "newtab-picture-header-main";

  const pictureImage = (
    <img
      className={`picture-of-the-day-image${imageLoaded ? " loaded" : ""}`}
      src={pictureData.thumbnailUrl}
      alt={imageAlt}
      onLoad={() => setImageLoaded(true)}
      onError={() => setImageFailed(true)}
    />
  );

  return (
    <article
      className={`picture-of-the-day widget col-4 ${widgetSize}-widget${
        hasPicture ? " has-picture" : ""
      }`}
      ref={impressionRef}
      onMouseLeave={() => setSuppressExpand(false)}
      onBlur={e => {
        // Only reset when focus leaves the whole widget, not when it moves
        // between the menu and button inside it.
        if (!e.currentTarget.contains(e.relatedTarget)) {
          setSuppressExpand(false);
        }
      }}
    >
      <div className="picture-of-the-day-toolbar">
        {hasPicture ? (
          <div className="picture-of-the-day-heading">
            <div className="picture-of-the-day-title-row">
              {!hasInteracted && (
                <moz-badge
                  className="picture-of-the-day-new-badge"
                  data-l10n-id="newtab-widget-lists-label-new"
                ></moz-badge>
              )}
              <p
                key={headerL10nId}
                className="newtab-widget-title picture-of-the-day-source"
                data-l10n-id={headerL10nId}
                data-l10n-args={
                  headerTimestamp
                    ? JSON.stringify({ date: headerTimestamp })
                    : undefined
                }
              ></p>
            </div>
            {renderAttribution()}
          </div>
        ) : null}
        <div className="picture-of-the-day-context-menu-wrapper">
          <WidgetMenuButton
            className="picture-of-the-day-context-menu-button"
            menuId="picture-of-the-day-context-menu"
            l10nId="newtab-picture-widget-open-menu-button"
          />
          <panel-list
            className="panel-list-no-icons"
            id="picture-of-the-day-context-menu"
          >
            <panel-item
              data-l10n-id="newtab-picture-menu-manage-wallpaper"
              onClick={handleManageWallpaper}
            />
            {dismissed ? (
              <panel-item
                data-l10n-id="newtab-picture-menu-show-photo2"
                onClick={() => handleShow("context_menu")}
              />
            ) : (
              <panel-item
                data-l10n-id="newtab-picture-menu-hide-photo2"
                onClick={handleHidePhoto}
              />
            )}
            <hr />
            {widgetsMayBeMaximized && (
              <panel-item submenu="picture-of-the-day-size-submenu">
                <span data-l10n-id="newtab-widget-menu-change-size"></span>
                <panel-list
                  ref={sizeSubmenuRef}
                  slot="submenu"
                  id="picture-of-the-day-size-submenu"
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
              widgetId="pictureOfTheDay"
              widgetEnabledMap={widgetEnabledMap}
            />

            <WidgetHideMenuItem
              widgetId="pictureOfTheDay"
              onHide={handleHide}
            />
            <panel-item
              data-l10n-id="newtab-picture-menu-learn-more"
              onClick={handleLearnMore}
            />
          </panel-list>
        </div>
      </div>

      {hasPicture ? (
        <div className="picture-of-the-day-populated">
          {canOpenSource ? (
            <a
              href={pictureData.sourceUrl}
              className="picture-of-the-day-image-link"
              onClick={handleOpenSource}
            >
              {pictureImage}
            </a>
          ) : (
            pictureImage
          )}
          <div className="picture-of-the-day-details">
            {pictureData.description ? (
              <p className="picture-of-the-day-description">
                {pictureData.description}
              </p>
            ) : null}
            {canSetWallpaper ? (
              <moz-button
                className={`picture-of-the-day-set-wallpaper${
                  justSet || isSetAsWallpaper ? " is-collapsed" : ""
                }${suppressExpand || isSetAsWallpaper ? " no-expand" : ""}`}
                type="primary"
                iconSrc={
                  justSet || isSetAsWallpaper
                    ? SET_WALLPAPER_CHECK_ICON
                    : SET_WALLPAPER_ICON
                }
                onClick={handleSetWallpaper}
                data-l10n-id="newtab-picture-set-wallpaper2"
              ></moz-button>
            ) : null}
          </div>
        </div>
      ) : (
        <div className="picture-of-the-day-footer">
          <button
            type="button"
            className="picture-of-the-day-show-button"
            onClick={() => handleShow("widget")}
            data-l10n-id="newtab-picture-show-button2"
          ></button>
          <p
            className="picture-of-the-day-message"
            data-l10n-id="newtab-picture-check-back"
          ></p>
        </div>
      )}
    </article>
  );
};

export { PictureOfTheDay };
