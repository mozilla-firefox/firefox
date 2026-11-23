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
 * A feature that supports flight status suggestions.
 */
export class FlightStatusSuggestions extends RealtimeSuggestProvider {
  get realtimeType() {
    return "flightStatus";
  }

  get isSponsored() {
    return false;
  }

  get merinoProvider() {
    return "flightaware";
  }

  get baseTelemetryType() {
    return "flights";
  }

  get notInterestedCommandL10n() {
    return {
      id: "urlbar-result-menu-dont-show-flight-status",
    };
  }

  get ariaGroupL10n() {
    return {
      id: "urlbar-result-aria-group-flight-status",
      attribute: "aria-label",
    };
  }

  getViewTemplateForDescriptionTop(_item, index) {
    return [
      {
        name: `departure_time_${index}`,
        tag: "span",
        classList: ["urlbarView-flightStatus-time"],
      },
      {
        name: `origin_airport_${index}`,
        tag: "span",
        classList: ["urlbarView-flightStatus-airport"],
      },
      {
        tag: "span",
        classList: ["urlbarView-realtime-description-separator-dash"],
      },
      {
        name: `arrival_time_${index}`,
        tag: "span",
        classList: ["urlbarView-flightStatus-time"],
      },
      {
        name: `destination_airport_${index}`,
        tag: "span",
        classList: ["urlbarView-flightStatus-airport"],
      },
    ];
  }

  getViewTemplateForDescriptionBottom(_item, index) {
    return [
      {
        name: `departure_date_${index}`,
        tag: "span",
        classList: ["urlbarView-flightStatus-departure-date"],
      },
      {
        tag: "span",
        classList: ["urlbarView-realtime-description-separator-dot"],
      },
      {
        name: `flight_number_${index}`,
        tag: "span",
        classList: ["urlbarView-flightStatus-flight-number"],
      },
      {
        tag: "span",
        classList: ["urlbarView-realtime-description-separator-dot"],
      },
      {
        name: `status_${index}`,
        tag: "span",
        classList: ["urlbarView-flightStatus-status"],
      },
      {
        tag: "span",
        classList: ["urlbarView-realtime-description-separator-dot"],
      },
      {
        name: `time_left_${index}`,
        tag: "span",
        classList: ["urlbarView-flightStatus-time-left"],
      },
    ];
  }

  getViewUpdateForPayloadItem(item, index, controller) {
    let status;
    switch (item.status) {
      case "Scheduled": {
        status = "ontime";
        break;
      }
      case "En Route": {
        status = "inflight";
        break;
      }
      case "Arrived": {
        status = "arrived";
        break;
      }
      case "Cancelled": {
        status = "cancelled";
        break;
      }
      case "Delayed": {
        status = "delayed";
        break;
      }
    }

    let departureTime;
    let departureTimeZone;
    let arrivalTime;
    let arrivalTimeZone;
    if (status == "delayed" || !item.delayed) {
      departureTime = new Date(item.departure.scheduled_time);
      departureTimeZone = getTimeZone(item.departure.scheduled_time);
      arrivalTime = new Date(item.arrival.scheduled_time);
      arrivalTimeZone = getTimeZone(item.arrival.scheduled_time);
    } else {
      departureTime = new Date(item.departure.estimated_time);
      departureTimeZone = getTimeZone(item.departure.estimated_time);
      arrivalTime = new Date(item.arrival.estimated_time);
      arrivalTimeZone = getTimeZone(item.arrival.estimated_time);
    }

    let statusL10nId = `urlbar-result-flight-status-status-${status}`;
    let statusL10nArgs;
    if (status == "delayed") {
      statusL10nArgs = {
        departureEstimatedTime: new Intl.DateTimeFormat(
          lazy.UrlbarShared.locale(),
          {
            hour: "numeric",
            minute: "numeric",
            timeZone: getTimeZone(item.departure.estimated_time),
          }
        ).format(new Date(item.departure.estimated_time)),
      };
    }

    let foregroundImage;
    let backgroundImage;

    let iconUrl;
    if (item.airline.icon) {
      iconUrl = UrlbarUtils.getRemoteImageUrl({
        controller,
        url: item.airline.icon,
        size: lazy.UrlbarShared.TOP_PICK_ICON_SIZE,
      });
    }

    if (status == "inflight") {
      let backgroundImageId =
        item.progress_percent == 100
          ? 4
          : Math.floor(item.progress_percent / 20);
      backgroundImage = {
        style: {
          "--airline-color": item.airline.color,
        },
        attributes: {
          backgroundImageId,
          hasForegroundImage: !!item.airline.icon,
        },
      };
      foregroundImage = {
        attributes: {
          src: iconUrl,
        },
      };
    } else {
      foregroundImage = {
        attributes: {
          src: iconUrl ?? "chrome://browser/skin/urlbar/flight-airline.svg",
          fallback: !iconUrl,
        },
      };
    }

    let timeLeft;
    if (typeof item.time_left_minutes == "number") {
      let hours = Math.floor(item.time_left_minutes / 60);
      let minutes = item.time_left_minutes % 60;
      // TODO Bug 1997547: TypeScript support for `Intl.DurationFormat`
      // @ts-ignore
      timeLeft = new Intl.DurationFormat(lazy.UrlbarShared.locale(), {
        style: "short",
      }).format({
        // If hours is zero, pass `undefined` to not show it at all.
        hours: hours || undefined,
        minutes,
      });
    }

    return {
      [`item_${index}`]: {
        attributes: {
          status,
        },
      },
      [`image_container_${index}`]: backgroundImage,
      [`image_${index}`]: foregroundImage,
      [`departure_time_${index}`]: {
        textContent: new Intl.DateTimeFormat(lazy.UrlbarShared.locale(), {
          hour: "numeric",
          minute: "numeric",
          timeZone: departureTimeZone,
        }).format(departureTime),
      },
      [`departure_date_${index}`]: {
        textContent: new Intl.DateTimeFormat(lazy.UrlbarShared.locale(), {
          month: "short",
          day: "numeric",
          weekday: "short",
          timeZone: departureTimeZone,
        }).format(departureTime),
      },
      [`arrival_time_${index}`]: {
        textContent: new Intl.DateTimeFormat(lazy.UrlbarShared.locale(), {
          hour: "numeric",
          minute: "numeric",
          timeZone: arrivalTimeZone,
        }).format(arrivalTime),
      },
      [`origin_airport_${index}`]: {
        l10n: {
          id: "urlbar-result-flight-status-airport",
          args: {
            city: item.origin.city,
            code: item.origin.code,
          },
        },
      },
      [`destination_airport_${index}`]: {
        l10n: {
          id: "urlbar-result-flight-status-airport",
          args: {
            city: item.destination.city,
            code: item.destination.code,
          },
        },
      },
      [`flight_number_${index}`]: item.airline.name
        ? {
            l10n: {
              id: "urlbar-result-flight-status-flight-number-with-airline",
              args: {
                flightNumber: item.flight_number,
                airlineName: item.airline.name,
              },
            },
          }
        : {
            textContent: item.flight_number,
          },
      [`status_${index}`]: {
        l10n: {
          id: statusL10nId,
          args: statusL10nArgs,
        },
      },
      [`time_left_${index}`]: timeLeft
        ? {
            l10n: {
              id: "urlbar-result-flight-status-time-left",
              args: {
                timeLeft,
              },
            },
          }
        : null,
    };
  }
}

function getTimeZone(isoTimeString) {
  let match = isoTimeString.match(/([+-]\d{2}:?\d{2}|Z)$/);
  if (!match) {
    return undefined;
  }

  let timeZone = match[1];
  return timeZone == "Z" ? "UTC" : timeZone;
}
