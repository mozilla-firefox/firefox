/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { MerinoClient } from "moz-src:///browser/components/urlbar/MerinoClient.sys.mjs";

/**
 * GETs a Merino JSON endpoint.
 *
 * @param {string} label
 *   Names the request in error messages.
 * @param {object} options
 * @param {string} [options.source]
 *   Sent as the `source` search param.
 * @param {string} options.endpointUrl
 * @param {string} [options.acceptLanguage]
 *   Sent as the Accept-Language header.
 * @param {{[key: string]: string}} [options.params]
 *   Extra search params.
 * @returns {Promise<{data: object|null, error: null | "invalid_url" | "load_error"}>}
 *   `error: null` with `data: null` means the endpoint is not configured.
 */
const fetchJSON = async (
  label,
  { source, endpointUrl, acceptLanguage, params = {} }
) => {
  if (!endpointUrl) {
    return { data: null, error: null };
  }
  let url = URL.parse(endpointUrl);
  if (!url) {
    console.error(
      `NewTabMerinoWrapper: invalid ${label} endpoint`,
      endpointUrl
    );
    return { data: null, error: "invalid_url" };
  }
  if (source) {
    url.searchParams.set("source", source);
  }
  for (const [name, value] of Object.entries(params)) {
    url.searchParams.set(name, value);
  }
  const fetchOptions = acceptLanguage
    ? { headers: { "Accept-Language": acceptLanguage } }
    : undefined;
  try {
    const response = await fetch(url, fetchOptions);
    if (!response.ok) {
      console.error(
        `NewTabMerinoWrapper: ${label} fetch failed with status ${response.status}`
      );
      return { data: null, error: "load_error" };
    }
    const data = /** @type {any} */ (await response.json());
    return { data, error: null };
  } catch (e) {
    console.error(`NewTabMerinoWrapper: ${label} fetch error`, e);
    return { data: null, error: "load_error" };
  }
};

/**
 * MerinoClient plus the New Tab requests it doesn't have yet. Each added
 * method uses MerinoClient's own version once it has one.
 */
export class NewTabMerinoWrapper extends MerinoClient {
  /**
   * MerinoClient has no fetchPictureOfTheDay yet. Remove this copy once it
   * does and that version reaches Release.
   *
   * @param {object} options
   * @param {string} [options.source]
   *   Sent as the `source` search param.
   * @param {string} options.endpointUrl
   *   The Picture of the Day endpoint.
   * @param {string} [options.acceptLanguage]
   *   Sent as the Accept-Language header so Merino can localize the
   *   description.
   * @returns {Promise<{data: object|null, error: null | "invalid_url" | "load_error"}>}
   *   `error: null` with `data: null` means the endpoint is not configured.
   */
  async fetchPictureOfTheDay(options) {
    // @ts-expect-error MerinoClient has no fetchPictureOfTheDay yet.
    const upstream = super.fetchPictureOfTheDay;
    if (upstream) {
      return upstream.call(this, options);
    }
    const { source, endpointUrl, acceptLanguage } = options;
    return fetchJSON("Picture of the Day", {
      source,
      endpointUrl,
      acceptLanguage,
    });
  }

  /**
   * MerinoClient has no fetchFramedWidgets yet. Remove this copy once it
   * does and that version reaches Release.
   *
   * @param {object} options
   * @param {string} [options.source]
   *   Sent as the `source` search param.
   * @param {string} options.endpointUrl
   *   The framed widgets manifest endpoint.
   * @param {string} [options.locale]
   *   Sent as the `locale` search param and the Accept-Language header.
   *   Merino defaults to en-US without it.
   * @returns {Promise<{data: object|null, error: null | "invalid_url" | "load_error"}>}
   *   Same contract as fetchPictureOfTheDay.
   */
  async fetchFramedWidgets(options) {
    // @ts-expect-error MerinoClient has no fetchFramedWidgets yet.
    const upstream = super.fetchFramedWidgets;
    if (upstream) {
      return upstream.call(this, options);
    }
    const { source, endpointUrl, locale } = options;
    return fetchJSON("framed widgets", {
      source,
      endpointUrl,
      acceptLanguage: locale,
      params: locale ? { locale } : {},
    });
  }
}
