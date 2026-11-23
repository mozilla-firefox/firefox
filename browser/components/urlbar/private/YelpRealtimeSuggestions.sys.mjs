/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { UrlbarUtils } from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";

import { RealtimeSuggestProvider } from "moz-src:///browser/components/urlbar/private/RealtimeSuggestProvider.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

/**
 * A feature that supports Yelp realtime suggestions.
 */
export class YelpRealtimeSuggestions extends RealtimeSuggestProvider {
  get realtimeType() {
    // Ideally this would simply be "yelp" but we want to avoid confusion with
    // offline Yelp suggestions.
    return "yelpRealtime";
  }

  get isSponsored() {
    return true;
  }

  get merinoProvider() {
    return "yelp";
  }

  get optInIcon() {
    return "chrome://browser/skin/illustrations/yelpRealtime-opt-in.svg";
  }

  get optInTitleL10n() {
    return {
      id: "urlbar-result-yelp-realtime-opt-in-title",
    };
  }

  get optInDescriptionL10n() {
    return {
      id: "urlbar-result-yelp-realtime-opt-in-description",
      parseMarkup: true,
    };
  }

  get notInterestedCommandL10n() {
    return {
      id: "urlbar-result-menu-dont-show-yelp-realtime",
    };
  }

  get ariaGroupL10n() {
    return {
      id: "urlbar-result-aria-group-yelp-realtime",
      attribute: "aria-label",
    };
  }

  getViewTemplateForDescriptionTop(_item, index) {
    return [
      {
        name: `title_${index}`,
        tag: "span",
        classList: ["urlbarView-yelpRealtime-title"],
      },
    ];
  }

  getViewTemplateForDescriptionBottom(_item, index) {
    return [
      {
        name: `address_${index}`,
        tag: "span",
        classList: ["urlbarView-yelpRealtime-description-address"],
      },
      {
        tag: "span",
        classList: ["urlbarView-realtime-description-separator-dot"],
      },
      {
        name: `pricing_${index}`,
        tag: "span",
        classList: ["urlbarView-yelpRealtime-description-pricing"],
      },
      {
        tag: "span",
        classList: ["urlbarView-realtime-description-separator-dot"],
      },
      {
        name: `business_hours_${index}`,
        tag: "span",
        classList: ["urlbarView-yelpRealtime-description-business-hours"],
      },
      {
        tag: "span",
        classList: ["urlbarView-realtime-description-separator-dot"],
      },
      {
        tag: "span",
        classList: ["urlbarView-yelpRealtime-description-popularity-star"],
      },
      {
        name: `popularity_${index}`,
        tag: "span",
        classList: ["urlbarView-yelpRealtime-description-popularity"],
      },
    ];
  }

  getViewUpdateForPayloadItem(item, index, controller) {
    return {
      [`item_${index}`]: {
        attributes: {
          state: item.business_hours[0].is_open_now ? "open" : "closed",
        },
      },
      [`image_${index}`]: {
        attributes: {
          src: UrlbarUtils.getRemoteImageUrl({
            controller,
            url: item.image_url,
            size: lazy.UrlbarShared.TOP_PICK_ICON_SIZE,
          }),
        },
      },
      [`title_${index}`]: {
        textContent: item.name,
      },
      [`address_${index}`]: {
        textContent: item.address,
      },
      [`pricing_${index}`]: {
        textContent: item.pricing,
      },
      [`business_hours_${index}`]: {
        l10n: {
          id: item.business_hours[0].is_open_now
            ? "urlbar-result-yelp-realtime-business-hours-open"
            : "urlbar-result-yelp-realtime-business-hours-closed",
          args: {
            // TODO: Need to pass "time until keeping the status" after resolving
            // the timezone issue.
            timeUntil: new Intl.DateTimeFormat(undefined, {
              hour: "numeric",
            }).format(new Date()),
          },
          parseMarkup: true,
        },
      },
      [`popularity_${index}`]: {
        l10n: {
          id: "urlbar-result-yelp-realtime-popularity",
          args: {
            rating: item.rating,
            review_count: item.review_count,
          },
        },
      },
    };
  }
}
