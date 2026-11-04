/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

const COLLECTION = "newtab-location-interest-grid";

export class LocationInterestGrid {
  #fetch;
  #recordsPromise = null;
  #metadataPromise = null;
  #bandPromises = new Map();

  /** Creates a grid loader with the given resource fetcher. */
  constructor({
    fetch: fetchResource = this.#fetchFromRemoteSettings.bind(this),
  } = {}) {
    this.#fetch = fetchResource;
  }

  /** Downloads files from one snapshot of the collection to keep metadata and bands consistent. */
  async #fetchFromRemoteSettings(file) {
    const client = lazy.RemoteSettings(COLLECTION);
    this.#recordsPromise ??= client.get().catch(error => {
      this.#recordsPromise = null;
      throw error;
    });
    const record = (await this.#recordsPromise).find(
      entry => entry.filename === file
    );
    if (!record) {
      return new Response(null, { status: 404 });
    }
    const { buffer } = await client.attachments.download(record);
    return new Response(buffer);
  }

  /** Fetches a file of the location interest grid. */
  async #fetchResource(file) {
    const response = await this.#fetch(file);
    if (!response.ok) {
      throw new Error(`Unable to load location interest grid: ${file}`);
    }
    return response;
  }

  /** Loads and validates the grid metadata. */
  async #loadMetadata() {
    const response = await this.#fetchResource("topic_grid_bands.json");
    const metadata = /** @type {any} */ (await response.json());
    if (
      metadata.format_version !== 1 ||
      metadata.encoding !== "packed16" ||
      metadata.dtype !== "uint16" ||
      metadata.byte_order !== "little" ||
      metadata.layout !== "row-major" ||
      metadata.bits_per_topic !== 2 ||
      metadata.topic_order !== "most-significant-first" ||
      metadata.levels_max !== 3 ||
      !metadata.topics?.length ||
      metadata.topics.length > 7 ||
      !metadata.bands?.length
    ) {
      throw new Error("Unsupported location interest grid format");
    }
    return metadata;
  }

  /** Loads and validates a binary grid band. */
  async #loadBand(band) {
    const response = await this.#fetchResource(band.file);
    const buffer = await response.arrayBuffer();
    if (
      buffer.byteLength !== band.byte_length ||
      buffer.byteLength !== 2 * band.n_lat * band.n_lon
    ) {
      throw new Error(`Invalid location interest grid size: ${band.file}`);
    }
    return new DataView(buffer);
  }

  /**
   * Returns discrete topic levels or null when no location prior is available.
   * Radius is the location accuracy radius in kilometers; omitted values use 25 km.
   * Resource loading errors reject the promise so callers can fall back.
   *
   * @param {object} location
   * @returns {Promise<object|null>}
   */
  async lookup({ latitude, longitude, radius } = {}) {
    // check for valid input
    if (
      !Number.isFinite(latitude) ||
      !Number.isFinite(longitude) ||
      Math.abs(latitude) > 90 ||
      Math.abs(longitude) > 180 ||
      (radius !== null &&
        radius !== undefined &&
        (!Number.isFinite(radius) || radius <= 0))
    ) {
      return null;
    }

    // load grid metadata
    this.#metadataPromise ??= this.#loadMetadata().catch(error => {
      this.#metadataPromise = null;
      this.#recordsPromise = null;
      throw error;
    });
    const metadata = await this.#metadataPromise;
    const radiusKm = radius ?? metadata.default_radius_km;
    // if location radius is above our grid, bounce
    if (radiusKm > metadata.no_prior_above_km) {
      return null;
    }

    // find closest radius
    const logRadius = Math.log(radiusKm);
    const band = metadata.bands.reduce((closest, candidate) =>
      Math.abs(logRadius - Math.log(candidate.radius_km)) <
      Math.abs(logRadius - Math.log(closest.radius_km))
        ? candidate
        : closest
    );
    // compute index into grid based on (lat,lon)
    const row = Math.floor((latitude - band.lat0) / band.step_deg);
    const col = Math.floor((longitude - band.lon0) / band.step_deg);
    // make sure we are in the grid
    if (row < 0 || row >= band.n_lat || col < 0 || col >= band.n_lon) {
      return null;
    }

    // cache each grid/band as a promise
    if (!this.#bandPromises.has(band.file)) {
      this.#bandPromises.set(
        band.file,
        this.#loadBand(band).catch(error => {
          this.#bandPromises.delete(band.file);
          throw error;
        })
      );
    }
    // load the grid we need
    const raster = await this.#bandPromises.get(band.file);
    // index into the grid
    const code = raster.getUint16(2 * (row * band.n_lon + col), true);
    if (code === metadata.no_data) {
      return null;
    }
    if (code >= 4 ** metadata.topics.length) {
      throw new Error(`Invalid location interest grid value: ${band.file}`);
    }
    // decode and return the value at the grid location
    return Object.fromEntries(
      metadata.topics.map((topic, index) => [
        topic,
        (code >>> (2 * (metadata.topics.length - 1 - index))) & 3,
      ])
    );
  }
}
