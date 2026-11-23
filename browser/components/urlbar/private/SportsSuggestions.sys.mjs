/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { UrlbarUtils } from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";

import { RealtimeSuggestProvider } from "moz-src:///browser/components/urlbar/private/RealtimeSuggestProvider.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

/**
 * A feature that manages sports realtime suggestions.
 */
export class SportsSuggestions extends RealtimeSuggestProvider {
  get realtimeType() {
    return "sports";
  }

  get isSponsored() {
    return false;
  }

  get merinoProvider() {
    return "sports";
  }

  get notInterestedCommandL10n() {
    return {
      id: "urlbar-result-menu-dont-show-sports",
    };
  }

  get ariaGroupL10n() {
    return {
      id: "urlbar-result-aria-group-sports",
      attribute: "aria-label",
    };
  }

  getViewTemplateForImageContainer(item, index) {
    // Create two image containers, one for the home team and one for the away
    // team. Each container has an `img` and date chiclet components. Depending
    // on the item, there are three possible "modes" for each container:
    //
    // (1) When the team has an icon: `img[src]` will be the icon URL (which
    //     will be a remote URL)
    // (2) When the sport is recognized: The container's `background-image` will
    //     be a fallback icon set via CSS
    // (3) Otherwise: The date chiclet will be visible
    //
    // Note that only one team may have an icon, depending on Merino, even
    // though that shouldn't typically happen.
    //
    // See the view-update logic in `#viewUpdateImageAndBottom()`.
    return ["home", "away"].map(team => ({
      name: `${team}-team-image-container-${index}`,
      tag: "span",
      classList: ["urlbarView-realtime-image-container"],
      children: [
        {
          name: `${team}-team-image-${index}`,
          tag: "img",
          classList: ["urlbarView-realtime-image"],
        },
        {
          name: `${team}-team-date-chiclet-day-${index}`,
          tag: "span",
          classList: ["urlbarView-sports-date-chiclet-day"],
        },
        {
          name: `${team}-team-date-chiclet-month-${index}`,
          tag: "span",
          classList: ["urlbarView-sports-date-chiclet-month"],
        },
      ],
    }));
  }

  getViewTemplateForDescriptionTop(item, index) {
    return stringifiedScore(item.home_team.score) &&
      stringifiedScore(item.away_team.score)
      ? this.#viewTemplateTopWithScores(index)
      : this.#viewTemplateTopWithoutScores(index);
  }

  #viewTemplateTopWithScores(index) {
    return [
      {
        name: `home-team-name-${index}`,
        tag: "span",
      },
      {
        name: `home-team-score-${index}`,
        tag: "span",
        classList: ["urlbarView-sports-score", "urlbarView-sports-score-home"],
      },
      {
        tag: "span",
        classList: ["urlbarView-realtime-description-separator-dot"],
      },
      {
        name: `away-team-score-${index}`,
        tag: "span",
        classList: ["urlbarView-sports-score", "urlbarView-sports-score-away"],
      },
      {
        name: `away-team-name-${index}`,
        tag: "span",
      },
    ];
  }

  #viewTemplateTopWithoutScores(index) {
    return [
      {
        name: `team-names-${index}`,
        tag: "span",
        classList: ["urlbarView-sports-team-names"],
      },
    ];
  }

  getViewTemplateForDescriptionBottom(item, index) {
    return [
      {
        name: `sport-${index}`,
        tag: "span",
      },
      {
        tag: "span",
        classList: ["urlbarView-realtime-description-separator-dot"],
      },
      {
        name: `date-${index}`,
        tag: "span",
      },
      {
        tag: "span",
        classList: ["urlbarView-realtime-description-separator-dot"],
      },
      {
        name: `status-${index}`,
        tag: "span",
        classList: ["urlbarView-sports-status"],
      },
    ];
  }

  getViewUpdateForPayloadItem(item, index, controller) {
    let topUpdate =
      stringifiedScore(item.home_team.score) &&
      stringifiedScore(item.away_team.score)
        ? this.#viewUpdateTopWithScores(item, index)
        : this.#viewUpdateTopWithoutScores(item, index);

    return {
      ...topUpdate,
      ...this.#viewUpdateImageAndBottom(item, index, controller),
      [`item_${index}`]: {
        attributes: {
          "sport-category": item.sport_category,
          status: item.status_type,
        },
      },
    };
  }

  #viewUpdateTopWithScores(item, i) {
    return {
      [`home-team-name-${i}`]: {
        textContent: item.home_team.name,
      },
      [`home-team-score-${i}`]: {
        textContent: stringifiedScore(item.home_team.score),
      },
      [`away-team-name-${i}`]: {
        textContent: item.away_team.name,
      },
      [`away-team-score-${i}`]: {
        textContent: stringifiedScore(item.away_team.score),
      },
    };
  }

  #viewUpdateTopWithoutScores(item, i) {
    return {
      [`team-names-${i}`]: {
        l10n: {
          id: "urlbar-result-sports-team-names",
          args: {
            homeTeam: item.home_team.name,
            awayTeam: item.away_team.name,
          },
        },
      },
    };
  }

  #viewUpdateImageAndBottom(item, i, controller) {
    // Format the date.
    let date = new Date(item.date);
    let {
      formattedDate,
      formattedTime,
      parseDateResult: { daysAgo, zonedNow },
    } = lazy.UrlbarShared.formatDate(date, {
      // If the item has an icon, the date chiclet won't be shown, so show the
      // absolute date in the bottom part of the row.
      forceMonthAndDayWhenAbsolute:
        !!item.home_team?.icon || !!item.away_team?.icon,
      includeTimeZone: true,
      capitalizeRelativeDate: true,
    });

    // Compute the date chiclet components.
    let partsArray = new Intl.DateTimeFormat(lazy.UrlbarShared.locale(), {
      month: "short",
      day: "numeric",
      timeZone: zonedNow.timeZoneId,
    }).formatToParts(date);
    let partsMap = Object.fromEntries(
      partsArray.map(({ type, value }) => [type, value])
    );

    // Create the image update. Start by creating each team's update.
    let imageUpdatesByTeam = ["home", "away"].reduce((memo, team) => {
      let itemKey = `${team}_team`;

      let iconUrl;
      if (item[itemKey]?.icon) {
        iconUrl = UrlbarUtils.getRemoteImageUrl({
          controller,
          url: item[itemKey].icon,
          size: lazy.UrlbarShared.TOP_PICK_ICON_SIZE,
        });
      }

      memo[team] = {
        [`image-container-${i}`]: {
          attributes: {
            "has-team-icon": iconUrl ? "" : null,
          },
        },
        [`image-${i}`]: {
          attributes: {
            src: iconUrl ?? null,
          },
        },
        [`date-chiclet-day-${i}`]: {
          textContent: partsMap.day ?? "",
        },
        [`date-chiclet-month-${i}`]: {
          textContent: partsMap.month ?? "",
        },
      };
      return memo;
    }, {});

    // If the teams' updates are the same, then they'll show the same thing, so
    // hide the away team's container (the second container) in that case.
    // @ts-ignore
    imageUpdatesByTeam.away[`image-container-${i}`].attributes.hidden =
      lazy.ObjectUtils.deepEqual(
        // @ts-ignore
        imageUpdatesByTeam.home,
        // @ts-ignore
        imageUpdatesByTeam.away
      )
        ? ""
        : null;

    // Create the final image update. Merge the two teams' updates and prefix
    // each key with the team.
    let imageUpdate = Object.entries(imageUpdatesByTeam).reduce(
      (memo, [team, teamUpdate]) => {
        for (let [key, value] of Object.entries(teamUpdate)) {
          memo[`${team}-team-${key}`] = value;
        }
        return memo;
      },
      {}
    );

    // Create the date update in the bottom part of the row.
    let dateUpdate;
    if (formattedTime) {
      dateUpdate = {
        [`date-${i}`]: {
          l10n: {
            id: "urlbar-result-sports-game-date-with-time",
            args: {
              date: formattedDate,
              time: formattedTime,
            },
          },
        },
      };
    } else {
      dateUpdate = {
        [`date-${i}`]: {
          textContent: formattedDate,
        },
      };
    }

    // Create the status update. Show the status if the game is live or if it
    // happened earlier today. Otherwise clear the status.
    let statusUpdate;
    if (item.status_type == "live") {
      statusUpdate = {
        [`status-${i}`]: {
          l10n: {
            id: "urlbar-result-sports-status-live",
          },
        },
      };
    } else if (daysAgo == 0 && item.status_type == "past") {
      statusUpdate = {
        [`status-${i}`]: {
          l10n: {
            id: "urlbar-result-sports-status-final",
          },
        },
      };
    } else {
      statusUpdate = {
        [`status-${i}`]: {
          // Clear the status by setting its text to an empty string.
          textContent: "",
        },
      };
    }

    return {
      ...imageUpdate,
      ...dateUpdate,
      ...statusUpdate,
      [`sport-${i}`]: {
        textContent: item.sport,
      },
    };
  }
}

function stringifiedScore(scoreValue) {
  let s = scoreValue;
  if (typeof s == "number") {
    s = String(s);
  }
  return typeof s == "string" ? s : "";
}
