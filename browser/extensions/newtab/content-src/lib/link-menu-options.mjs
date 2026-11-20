/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  actionCreators as ac,
  actionTypes as at,
} from "../../common/Actions.mjs";
import { nextDwellId } from "./dwell-id.mjs";

const _OpenInPrivateWindow = site => ({
  id: "newtab-menu-open-new-private-window",
  icon: "new-window-private",
  action: ac.OnlyToMain({
    type: at.OPEN_PRIVATE_WINDOW,
    data: {
      card_type: site.card_type,
      url: site.url,
      referrer: site.referrer,
      event_source: "CONTEXT_MENU",
      topic: site.topic,
      tile_id: site.tile_id,
      scheduled_corpus_item_id: site.scheduled_corpus_item_id,
      corpus_item_id: site.corpus_item_id,
      received_rank: site.received_rank,
      recommended_at: site.recommended_at,
      format: site.format,
      is_pocket_card: site.type === "CardGrid",
      ...(site.section
        ? {
            section: site.section,
            section_position: site.section_position,
            is_section_followed: site.is_section_followed,
          }
        : {}),
    },
  }),
  userEvent: "OPEN_PRIVATE_WINDOW",
});

/**
 * List of functions that return items that can be included as menu options in a
 * LinkMenu. All functions take the site as the first parameter, and optionally
 * the index of the site.
 */
export const LinkMenuOptions = {
  Separator: () => ({ type: "separator" }),
  EmptyItem: () => ({ type: "empty" }),
  ShowPrivacyInfo: () => ({
    id: "newtab-menu-show-privacy-info",
    icon: "info",
    action: {
      type: at.SHOW_PRIVACY_INFO,
    },
    userEvent: "SHOW_PRIVACY_INFO",
  }),
  AboutSponsored: site => ({
    id: "newtab-menu-show-privacy-info",
    icon: "info",
    action: ac.AlsoToMain({
      type: at.ABOUT_SPONSORED_TOP_SITES,
      data: {
        advertiser_name: (site.label || site.hostname).toLocaleLowerCase(),
        position: site.sponsored_position,
        tile_id: site.sponsored_tile_id,
        block_key: site.block_key,
      },
    }),
    userEvent: "TOPSITE_SPONSOR_INFO",
  }),
  RemoveBookmark: site => ({
    id: "newtab-menu-remove-bookmark",
    icon: "bookmark-added",
    action: ac.AlsoToMain({
      type: at.DELETE_BOOKMARK_BY_ID,
      data: site.bookmarkGuid,
    }),
    userEvent: "BOOKMARK_DELETE",
  }),
  AddBookmark: site => ({
    id: "newtab-menu-bookmark",
    icon: "bookmark-hollow",
    action: ac.AlsoToMain({
      type: at.BOOKMARK_URL,
      data: { url: site.url, title: site.title, type: site.type },
    }),
    userEvent: "BOOKMARK_ADD",
  }),
  OpenInNewWindow: site => ({
    id: "newtab-menu-open-new-window",
    icon: "new-window",
    action: ac.AlsoToMain({
      type: at.OPEN_NEW_WINDOW,
      data: {
        card_type: site.card_type,
        referrer: site.referrer,
        typedBonus: site.typedBonus,
        url: site.url,
        is_sponsored: !!site.sponsored_tile_id,
        event_source: "CONTEXT_MENU",
        topic: site.topic,
        tile_id: site.tile_id,
        scheduled_corpus_item_id: site.scheduled_corpus_item_id,
        corpus_item_id: site.corpus_item_id,
        received_rank: site.received_rank,
        recommended_at: site.recommended_at,
        format: site.format,
        ...(site.flight_id ? { flight_id: site.flight_id } : {}),
        // Opts the window this opens into newtab.opened_page_dwell_time.
        ...(site.dwell_label ? { dwell_label: site.dwell_label } : {}),
        is_pocket_card: site.type === "CardGrid",
        ...(site.section
          ? {
              section: site.section,
              section_position: site.section_position,
              is_section_followed: site.is_section_followed,
            }
          : {}),
      },
    }),
    userEvent: "OPEN_NEW_WINDOW",
  }),

  // This blocks the url for regular stories,
  // but also sends a message to DiscoveryStream with flight_id.
  // If DiscoveryStream sees this message for a flight_id
  // it also blocks it on the flight_id.
  BlockUrl: (site, index, eventSource) => {
    return LinkMenuOptions.BlockUrls([site], index, eventSource);
  },
  // Same as BlockUrl, except can work on an array of sites.
  BlockUrls: (tiles, pos, eventSource) => ({
    id: "newtab-menu-dismiss",
    icon: "dismiss",
    action: ac.AlsoToMain({
      type: at.BLOCK_URL,
      source: eventSource,
      data: tiles.map(site => ({
        url: site.original_url || site.open_url || site.url,
        // pocket_id is only for pocket stories being in highlights, and then dismissed.
        pocket_id: site.pocket_id,
        tile_id: site.tile_id,
        ...(site.block_key ? { block_key: site.block_key } : {}),
        scheduled_corpus_item_id: site.scheduled_corpus_item_id,
        corpus_item_id: site.corpus_item_id,
        received_rank: site.received_rank,
        recommended_at: site.recommended_at,
        // used by PlacesFeed and TopSitesFeed for sponsored top sites blocking.
        isSponsoredTopSite: site.sponsored_position,
        type: site.type,
        card_type: site.card_type,
        ...(site.shim && site.shim.delete ? { shim: site.shim.delete } : {}),
        ...(site.flight_id ? { flight_id: site.flight_id } : {}),
        // If not sponsored, hostname could be anything (Cat3 Data!).
        // So only put in advertiser_name for sponsored topsites.
        ...(site.sponsored_position
          ? {
              advertiser_name: (
                site.label || site.hostname
              )?.toLocaleLowerCase(),
            }
          : {}),
        position: pos,
        ...(site.sponsored_tile_id ? { tile_id: site.sponsored_tile_id } : {}),
        is_pocket_card: site.type === "CardGrid",
        ...(site.format ? { format: site.format } : {}),
        ...(site.section
          ? {
              section: site.section,
              section_position: site.section_position,
              is_section_followed: site.is_section_followed,
            }
          : {}),
      })),
    }),
    userEvent: "BLOCK",
  }),

  // This is the "Dismiss" action for leaderboard/billboard ads.
  BlockAdUrl: site => ({
    id: "newtab-menu-dismiss",
    icon: "dismiss",
    action: ac.AlsoToMain({
      type: at.BLOCK_URL,
      data: [site],
    }),
    userEvent: "BLOCK",
  }),

  // This is an option for web extentions which will result in remove items from
  // memory and notify the web extenion, rather than using the built-in block list.
  WebExtDismiss: (site, index, eventSource) => ({
    id: "menu_action_webext_dismiss",
    string_id: "newtab-menu-dismiss",
    icon: "dismiss",
    action: ac.WebExtEvent(at.WEBEXT_DISMISS, {
      source: eventSource,
      url: site.url,
      action_position: index,
    }),
  }),
  DeleteUrl: (site, index, eventSource, isEnabled, siteInfo) => ({
    id: "newtab-menu-delete-history",
    icon: "delete",
    ariaHasPopup: "dialog",
    action: {
      type: at.DIALOG_OPEN,
      data: {
        onConfirm: [
          ac.AlsoToMain({
            type: at.DELETE_HISTORY_URL,
            data: {
              url: site.url,
              pocket_id: site.pocket_id,
              forceBlock: site.bookmarkGuid,
              original_url: site.original_url,
            },
          }),
          ac.UserEvent(
            Object.assign(
              { event: "DELETE", source: eventSource, action_position: index },
              siteInfo
            )
          ),
          // Also broadcast that this url has been deleted so that
          // the confirmation dialog knows it needs to disappear now.
          ac.AlsoToMain({
            type: at.DIALOG_CLOSE,
          }),
        ],
        eventSource,
        body_string_id: [
          "newtab-confirm-delete-history-p1",
          "newtab-confirm-delete-history-p2",
        ],
        confirm_button_string_id: "newtab-topsites-delete-history-button",
        cancel_button_string_id: "newtab-topsites-cancel-button",
        icon: "modal-delete",
      },
    },
    userEvent: "DIALOG_OPEN",
  }),
  ShowFile: site => ({
    id: "newtab-menu-show-file",
    icon: "search",
    action: ac.OnlyToMain({
      type: at.SHOW_DOWNLOAD_FILE,
      data: { url: site.url },
    }),
  }),
  OpenFile: site => ({
    id: "newtab-menu-open-file",
    icon: "open-file",
    action: ac.OnlyToMain({
      type: at.OPEN_DOWNLOAD_FILE,
      data: { url: site.url },
    }),
  }),
  CopyDownloadLink: site => ({
    id: "newtab-menu-copy-download-link",
    icon: "copy",
    action: ac.OnlyToMain({
      type: at.COPY_DOWNLOAD_LINK,
      data: { url: site.url },
    }),
  }),
  GoToDownloadPage: site => ({
    id: "newtab-menu-go-to-download-page",
    icon: "download",
    action: ac.OnlyToMain({
      type: at.OPEN_LINK,
      data: { url: site.referrer },
    }),
    disabled: !site.referrer,
  }),
  RemoveDownload: site => ({
    id: "newtab-menu-remove-download",
    icon: "delete",
    action: ac.OnlyToMain({
      type: at.REMOVE_DOWNLOAD_FILE,
      data: { url: site.url },
    }),
  }),
  PinTopSite: (site, index) => ({
    id: "newtab-menu-pin",
    icon: "pin",
    action: ac.AlsoToMain({
      type: at.TOP_SITES_PIN,
      data: {
        site,
        index,
      },
    }),
    userEvent: "PIN",
  }),
  UnpinTopSite: site => ({
    id: "newtab-menu-unpin",
    icon: "unpin",
    action: ac.AlsoToMain({
      type: at.TOP_SITES_UNPIN,
      data: { site: { url: site.url } },
    }),
    userEvent: "UNPIN",
  }),
  EditTopSite: (site, index) => ({
    id: "newtab-menu-edit-topsites",
    icon: "edit",
    ariaHasPopup: "dialog",
    action: {
      type: at.TOP_SITES_EDIT,
      data: { index },
    },
  }),
  // Opens the "New Shortcut" dialog. index -1 routes through the feed's insert
  // path (append to the pinned group / first free slot), like the add button.
  AddTopSite: () => ({
    id: "newtab-menu-add-topsite",
    ariaHasPopup: "dialog",
    action: {
      type: at.TOP_SITES_EDIT,
      data: { index: -1 },
    },
  }),
  CheckBookmark: site =>
    site.bookmarkGuid
      ? LinkMenuOptions.RemoveBookmark(site)
      : LinkMenuOptions.AddBookmark(site),
  CheckPinTopSite: (site, index) =>
    site.isPinned
      ? LinkMenuOptions.UnpinTopSite(site)
      : LinkMenuOptions.PinTopSite(site, index),
  OpenInPrivateWindow: (site, index, eventSource, isEnabled) =>
    isEnabled ? _OpenInPrivateWindow(site) : LinkMenuOptions.EmptyItem(),
  SectionBlock: ({
    sectionPersonalization,
    sectionKey,
    sectionPosition,
    title,
  }) => ({
    id: "newtab-menu-section-block",
    icon: "delete",
    ariaHasPopup: "dialog",
    action: {
      // Open the confirmation dialog to block a section.
      type: at.DIALOG_OPEN,
      data: {
        onConfirm: [
          // Once the user confirmed their intention to block this section,
          // update their preferences.
          ac.AlsoToMain({
            type: at.SECTION_PERSONALIZATION_SET,
            data: {
              ...sectionPersonalization,
              [sectionKey]: {
                isBlocked: true,
                isFollowed: false,
                title,
              },
            },
          }),
          // Telemetry
          ac.OnlyToMain({
            type: at.BLOCK_SECTION,
            data: {
              section: sectionKey,
              section_position: sectionPosition,
              event_source: "CONTEXT_MENU",
            },
          }),
          // Also broadcast that this section has been blocked so that
          // the confirmation dialog knows it needs to disappear now.
          ac.AlsoToMain({
            type: at.DIALOG_CLOSE,
          }),
          ac.OnlyToOneContent(
            {
              type: at.SHOW_TOAST_MESSAGE,
              data: {
                toastId: "blockSectionToast",
                showNotifications: true,
                toastData: {
                  l10nId: "newtab-section-toast-block",
                  topic: title,
                },
              },
            },
            "ActivityStream:Content"
          ),
        ],
        // Pass Fluent strings to ConfirmDialog component for the copy
        // of the prompt to block sections.
        body_string_id: [
          "newtab-section-confirm-block-topic-p1",
          "newtab-section-confirm-block-topic-p2",
        ],
        confirm_button_string_id: "newtab-section-block-topic-button",
        confirm_button_string_args: { topic: title },
        cancel_button_string_id: "newtab-section-block-cancel-button",
      },
    },
    userEvent: "DIALOG_OPEN",
  }),
  SectionUnfollow: ({
    sectionPersonalization,
    sectionKey,
    sectionPosition,
    title,
  }) => ({
    id: "newtab-menu-section-unfollow-topic",
    action: ac.AlsoToMain({
      type: at.SECTION_PERSONALIZATION_SET,
      data: (({ [sectionKey]: _sectionKey, ...remaining }) => remaining)(
        sectionPersonalization
      ),
    }),
    impression: ac.OnlyToMain({
      type: at.UNFOLLOW_SECTION,
      data: {
        section: sectionKey,
        section_position: sectionPosition,
        event_source: "CONTEXT_MENU",
      },
    }),
    toast: ac.OnlyToOneContent(
      {
        type: at.SHOW_TOAST_MESSAGE,
        data: {
          toastId: "unfollowSectionToast",
          showNotifications: true,
          toastData: { l10nId: "newtab-section-toast-unfollow", topic: title },
        },
      },
      "ActivityStream:Content"
    ),
    userEvent: "SECTION_UNFOLLOW",
  }),
  ManageSponsoredContent: () => ({
    id: "newtab-menu-manage-sponsored-content",
    action: ac.OnlyToMain({ type: at.SETTINGS_OPEN }),
    userEvent: "OPEN_NEWTAB_PREFS",
  }),
  SectionLearnMore: ({ learnMoreUrl }) => ({
    id: "newtab-menu-section-learn-more",
    action: ac.OnlyToMain({
      type: at.OPEN_LINK,
      data: { url: learnMoreUrl },
    }),
    impression: ac.OnlyToMain({
      type: at.CLICK_SECTION_LEARN_MORE,
      data: {},
    }),
    userEvent: "CLICK_SECTION_LEARN_MORE",
  }),
  // eslint-disable-next-line max-params
  OurSponsorsAndYourPrivacy: (
    site,
    index,
    source,
    isPrivateBrowsingEnabled,
    siteInfo,
    platform,
    privacyInfoUrl
  ) => ({
    id: "newtab-menu-our-sponsors-and-your-privacy",
    action: ac.OnlyToMain({
      type: at.OPEN_LINK,
      data: {
        url: privacyInfoUrl,
      },
    }),
    userEvent: "CLICK_PRIVACY_INFO",
  }),
  ReportAd: site => {
    return {
      id: "newtab-menu-report-this-ad",
      ariaHasPopup: "dialog",
      action: ac.AlsoToMain({
        type: at.REPORT_AD_OPEN,
        data: {
          card_type: site.card_type,
          position: site.position,
          reporting_url: site.shim?.report,
          url: site.url,
        },
      }),
    };
  },

  ReportContent: site => {
    return {
      id: "newtab-menu-report",
      action: ac.AlsoToMain({
        type: at.REPORT_CONTENT_OPEN,
        data: {
          card_type: site.card_type,
          corpus_item_id: site.corpus_item_id,
          scheduled_corpus_item_id: site.scheduled_corpus_item_id,
          section_position: site.section_position,
          section: site.section,
          title: site.title,
          topic: site.topic,
          url: site.url,
        },
      }),
    };
  },
};

const DEFAULT_SITE_MENU_OPTIONS = [
  "CheckPinTopSite",
  "EditTopSite",
  "AddTopSite",
  "Separator",
  "OpenInNewWindow",
  "OpenInPrivateWindow",
  "Separator",
  "BlockUrl",
];

/**
 * Turns a list of option keys (e.g. "CheckPinTopSite") into concrete
 * LinkMenuOptions entries with onClick handlers wired up to dispatch/
 * telemetry. Shared by any menu renderer (ContextMenu- or panel-list-based)
 * that needs the same option-building behavior.
 */
export function getLinkMenuOptions(props) {
  const {
    site,
    index,
    source,
    isPrivateBrowsingEnabled,
    siteInfo,
    platform,
    privacyInfoUrl,
    dispatch,
    options,
    shouldSendImpressionStats,
    userEvent = ac.UserEvent,
  } = props;

  // Handle special case of default site
  const propOptions =
    site.isDefault && !site.searchTopSite && !site.sponsored_position
      ? DEFAULT_SITE_MENU_OPTIONS
      : options;

  const linkMenuOptions = propOptions
    .map(o =>
      LinkMenuOptions[o](
        site,
        index,
        source,
        isPrivateBrowsingEnabled,
        siteInfo,
        platform,
        privacyInfoUrl
      )
    )
    .map(option => {
      const {
        action,
        impression,
        toast,
        id,
        type,
        userEvent: eventName,
      } = option;
      if (!type && id) {
        option.onClick = (event = {}) => {
          const { ctrlKey, metaKey, shiftKey, button } = event;
          // Only send along event info if there's something non-default to send
          if (ctrlKey || metaKey || shiftKey || button === 1) {
            action.data = Object.assign(
              {
                event: { ctrlKey, metaKey, shiftKey, button },
              },
              action.data
            );
          }
          // A fresh id per open, shared with the user event below, so the
          // dwell time on a story can find the click it belongs to.
          const dwellId = action.data?.dwell_label?.startsWith("story_")
            ? nextDwellId()
            : undefined;
          if (dwellId) {
            action.data = { ...action.data, dwell_id: dwellId };
          }
          dispatch(action);
          if (toast) {
            dispatch(toast);
          }
          if (eventName) {
            let value;
            // Bug 1958135: Pass additional info to ac.OPEN_NEW_WINDOW event
            if (
              action.type === "OPEN_NEW_WINDOW" ||
              action.type === "OPEN_PRIVATE_WINDOW"
            ) {
              const {
                card_type,
                corpus_item_id,
                event_source,
                format,
                is_section_followed,
                received_rank,
                recommended_at,
                scheduled_corpus_item_id,
                section_position,
                section,
                selected_topics,
                tile_id,
                topic,
              } = action.data;

              value = {
                card_type,
                corpus_item_id,
                event_source,
                format,
                received_rank,
                recommended_at,
                scheduled_corpus_item_id,
                ...(section
                  ? { is_section_followed, section_position, section }
                  : {}),
                selected_topics: selected_topics ? selected_topics : "",
                tile_id,
                topic,
                ...(dwellId ? { dwell_id: dwellId } : {}),
              };
            } else {
              value = { card_type: site.flight_id ? "spoc" : "organic" };
            }
            const userEventData = Object.assign(
              {
                event: eventName,
                source,
                action_position: index,
                value,
              },
              siteInfo
            );
            dispatch(userEvent(userEventData));
            if (impression && shouldSendImpressionStats) {
              dispatch(impression);
            }
          }
        };
      }
      return option;
    });

  // This is for accessibility to support making each item tabbable.
  // We want to know which item is the first and which item
  // is the last, so we can close the context menu accordingly.
  linkMenuOptions[0].first = true;
  linkMenuOptions[linkMenuOptions.length - 1].last = true;
  return linkMenuOptions;
}
