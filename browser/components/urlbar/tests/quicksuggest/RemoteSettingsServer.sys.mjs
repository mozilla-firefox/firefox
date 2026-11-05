/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/* eslint-disable jsdoc/require-param-description */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  HttpError: "resource://testing-common/httpd.sys.mjs",
  HttpServer: "resource://testing-common/httpd.sys.mjs",
  HTTP_404: "resource://testing-common/httpd.sys.mjs",
});

/**
 * @import {HttpError} from "resource://testing-common/httpd.sys.mjs"
 */

const SERVER_PREF = "services.settings.server";

/**
 * A remote settings server. Tested with the desktop and Rust remote settings
 * clients.
 */
export class RemoteSettingsServer {
  /**
   * The server must be started by calling `start()`.
   *
   * @param {object} options
   * @param {ConsoleLogLevel} [options.maxLogLevel]
   *   A log level value as defined by ConsoleInstance. `Info` logs server start
   *   and stop. `Debug` logs requests, responses, and added and removed
   *   records.
   */
  constructor({ maxLogLevel = "Info" } = {}) {
    this.#log = console.createInstance({
      logModule: "RemoteSettingsServer",
      maxLogLevel,
    });
  }

  /**
   * @returns {URL}
   *   The server's URL. Null when the server is stopped.
   */
  get url() {
    return this.#url;
  }

  /**
   * Starts the server and sets the `services.settings.server` pref to its
   * URL. The server's `url` property will be non-null on return.
   */
  async start() {
    this.#log.info("Starting");

    if (this.#url) {
      this.#log.info("Already started at " + this.#url);
      return;
    }

    if (!this.#server) {
      this.#server = new lazy.HttpServer();
      this.#server.registerPrefixHandler("/", this);
    }
    this.#server.start(-1);

    this.#url = new URL("http://localhost/v2");
    this.#url.port = this.#server.identity.primaryPort;

    this.#originalServerPrefValue = Services.prefs.getCharPref(
      SERVER_PREF,
      null
    );
    Services.prefs.setCharPref(SERVER_PREF, this.#url.toString());

    this.#log.info("Server is now started at " + this.#url);
  }

  /**
   * Stops the server and clears the `services.settings.server` pref. The
   * server's `url` property will be null on return.
   */
  async stop() {
    this.#log.info("Stopping");

    if (!this.#url) {
      this.#log.info("Already stopped");
      return;
    }

    await this.#server.stop();
    this.#url = null;

    if (this.#originalServerPrefValue === null) {
      Services.prefs.clearUserPref(SERVER_PREF);
    } else {
      Services.prefs.setCharPref(SERVER_PREF, this.#originalServerPrefValue);
    }

    this.#log.info("Server is now stopped");
  }

  /**
   * Adds remote settings records to the server. Records may have attachments;
   * see the param doc below.
   *
   * @param {object} options
   * @param {string} options.bucket
   * @param {string} options.collection
   * @param {Array} options.records
   *   Each object in this array should be a realistic remote settings record
   *   with the following exceptions:
   *
   *   - `record.id` will be generated if it's undefined.
   *   - `record.last_modified` will be set to the `#lastModified` property of
   *     the server if it's undefined.
   *   - `record.attachment`, if defined, should be the attachment itself and
   *     not its metadata. The server will automatically create some dummy
   *     metadata. Currently the only supported attachment type is plain
   *     JSON'able objects that the server will convert to JSON in responses.
   */
  async addRecords({ bucket = "main", collection = "test", records }) {
    this.#log.debug("Adding records:", { bucket, collection, records });

    this.#lastModified++;

    let key = this.#recordsKey(bucket, collection);
    let allRecords = this.#records.get(key);
    if (!allRecords) {
      allRecords = [];
      this.#records.set(key, allRecords);
    }

    for (let record of records) {
      let copy = { ...record };

      if (!copy.hasOwnProperty("id")) {
        copy.id = String(this.#nextRecordId++);
      }
      if (!copy.hasOwnProperty("last_modified")) {
        copy.last_modified = this.#lastModified;
      }
      if (copy.attachment) {
        await this.#addAttachment({ bucket, collection, record: copy });
      }
      allRecords.push(copy);
    }

    this.#log.debug("Done adding records. All records are now:", [
      ...this.#records.entries(),
    ]);
  }

  /**
   * Marks records as deleted. Deleted records will still be returned in
   * responses, but they'll have a `deleted = true` property. Their attachments
   * will be deleted immediately, however.
   *
   * @param {object} filter
   *   If null, all records will be marked as deleted. Otherwise only records
   *   that match the filter will be marked as deleted. For a given record, each
   *   value in the filter object will be compared to the value with the same
   *   key in the record. If all values are the same, the record will be
   *   removed. Examples:
   *
   *   To remove remove records whose `type` key has the value "data":
   *   `{ type: "data" }
   *
   *   To remove remove records whose `type` key has the value "data" and whose
   *   `last_modified` key has the value 1234:
   *   `{ type: "data", last_modified: 1234 }
   */
  removeRecords(filter = null) {
    this.#log.debug("Removing records", { filter });

    this.#lastModified++;

    for (let records of this.#records.values()) {
      for (let record of records) {
        if (
          !filter ||
          Object.entries(filter).every(
            ([filterKey, filterValue]) =>
              record.hasOwnProperty(filterKey) &&
              record[filterKey] == filterValue
          )
        ) {
          record.deleted = true;
          record.last_modified = this.#lastModified;

          // If the record has an attachment, leave it. Sometimes the following
          // sequence can happen: A test requests records, we send them,
          // something else deletes the records, and then the test requests
          // their attachments. The JS RS client throws an error in that case
          // since the attachment hashes don't match the hashes in the records.
        }
      }
    }

    this.#log.debug("Done removing records. All records are now:", [
      ...this.#records.entries(),
    ]);
  }

  /**
   * Removes all existing records and adds the given records to the server.
   *
   * @param {object} options
   * @param {string} options.bucket
   * @param {string} options.collection
   * @param {Array} options.records
   *   See `addRecords()`.
   */
  async setRecords({ bucket = "main", collection = "test", records }) {
    this.#log.debug("Setting records");

    this.removeRecords();
    await this.addRecords({ bucket, collection, records });

    this.#log.debug("Done setting records");
  }

  /**
   * `nsIHttpRequestHandler` callback from the backing server. Handles a
   * request.
   *
   * @param {nsIHttpRequest} request
   * @param {nsIHttpResponse} response
   */
  handle(request, response) {
    this.#logRequest(request);

    // Get the route that matches the request path.
    let { match, route } = this.#getRoute(request.path) || {};
    if (!route) {
      this.#prepareError({ request, response, error: lazy.HTTP_404 });
      return;
    }

    let respInfo = route.response(match, request, response);
    if (respInfo instanceof lazy.HttpError) {
      this.#prepareError({ request, response, error: respInfo });
    } else {
      this.#prepareResponse({ ...respInfo, request, response });
    }
  }

  /**
   * @returns {Array}
   *   The routes handled by the server. Each item in this array is an object
   *   with the following properties that describes one or more paths and the
   *   response that should be sent when a request is made on those paths:
   *
   *   {string} spec
   *     A path spec. This is required unless `specs` is defined. To determine
   *     which route should be used for a given request, the server will check
   *     each route's spec(s) until it finds the first that matches the
   *     request's path. A spec is just a path whose components can be variables
   *     that start with "$". When a spec with variables matches a request path,
   *     the `match` object passed to the route's `response` function will map
   *     from variable names to the corresponding components in the path.
   *   {Array} specs
   *     An array of path spec strings. Use this instead of `spec` if the route
   *     handles more than one.
   *   {function} response
   *     A function that will be called when the route matches a request. It is
   *     called as: `response(match, request, response)`
   *
   *     {object} match
   *       An object mapping variable names in the spec to their matched
   *       components in the path. See `#match()` for details.
   *     {nsIHttpRequest} request
   *     {nsIHttpResponse} response
   *
   *     The function must return one of the following:
   *
   *     {object}
   *       An object that describes the response with the following properties:
   *       {object} body
   *         A plain JSON'able object. The server will convert this to JSON and
   *         set it to the response body.
   *     {HttpError}
   *       An `HttpError` instance defined in `httpd.sys.mjs`.
   */
  get #routes() {
    return [
      {
        spec: "/v2",
        response: () => ({
          body: {
            capabilities: {
              attachments: {
                base_url: this.#url.toString(),
              },
            },
          },
        }),
      },

      {
        spec: "/v2/buckets/monitor/collections/changes/changeset",
        response: () => ({
          body: {
            timestamp: this.#lastModified,
            changes: [
              {
                last_modified: this.#lastModified,
              },
            ],
          },
        }),
      },

      {
        spec: "/v2/buckets/$bucket/collections/$collection/changeset",
        response: ({ bucket, collection }, request) => {
          let records = this.#getRecords(bucket, collection, request);
          return !records
            ? lazy.HTTP_404
            : {
                body: {
                  metadata: {
                    bucket,
                    signatures: [
                      {
                        signature: "",
                        x5u: "",
                        // The signature type. ECDSA P384 is the only type
                        // supported by both the Rust client (which requires
                        // this field and skips signatures with any other
                        // mode) and the JS one (RemoteSettingsClient.sys.mjs).
                        mode: "p384ecdsa",
                      },
                    ],
                  },
                  timestamp: this.#lastModified,
                  changes: records,
                },
              };
        },
      },

      {
        specs: [
          // The Rust remote settings client doesn't include "v1" in attachment
          // URLs, but the JS client does.
          "/attachments/$bucket/$collection/$filename",
          "/v2/attachments/$bucket/$collection/$filename",
        ],
        response: ({ bucket, collection, filename }) => {
          return {
            body: this.#getAttachment(bucket, collection, filename),
          };
        },
      },
    ];
  }

  /**
   * @returns {object}
   *   Default response headers.
   */
  get #responseHeaders() {
    return {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Expose-Headers":
        "Retry-After, Content-Length, Alert, Backoff",
      Server: "waitress",
      Etag: `"${this.#lastModified}"`,
    };
  }

  /**
   * Returns the route that matches a request path.
   *
   * @param {string} path
   *   A request path.
   * @returns {object}
   *   If no route matches the path, returns an empty object. Otherwise returns
   *   an object with the following properties:
   *
   *   {object} match
   *     An object describing the matched variables in the route spec. See
   *     `#match()` for details.
   *   {object} route
   *     The matched route. See `#routes` for details.
   */
  #getRoute(path) {
    for (let route of this.#routes) {
      let specs = route.specs || [route.spec];
      for (let spec of specs) {
        let match = this.#match(path, spec);
        if (match) {
          return { match, route };
        }
      }
    }
    return {};
  }

  /**
   * Matches a request path to a route spec.
   *
   * @param {string} path
   *   A request path.
   * @param {string} spec
   *   A route spec. See `#routes` for details.
   * @returns {object|null}
   *   If the spec doesn't match the path, returns null. Otherwise returns an
   *   object mapping variable names in the spec to their matched components in
   *   the path. Example:
   *
   *   path   : "/main/myfeature/foo"
   *   spec   : "/$bucket/$collection/foo"
   *   returns: `{ bucket: "main", collection: "myfeature" }`
   */
  #match(path, spec) {
    let pathParts = path.split("/");
    let specParts = spec.split("/");

    if (pathParts.length != specParts.length) {
      // If the path has only one more part than the spec and its last part is
      // empty, then the path ends in a trailing slash but the spec does not.
      // Consider that a match. Otherwise return null for no match.
      if (
        pathParts[pathParts.length - 1] ||
        pathParts.length != specParts.length + 1
      ) {
        return null;
      }
      pathParts.pop();
    }

    let match = {};
    for (let i = 0; i < pathParts.length; i++) {
      let pathPart = pathParts[i];
      let specPart = specParts[i];
      if (specPart.startsWith("$")) {
        match[specPart.substring(1)] = pathPart;
      } else if (pathPart != specPart) {
        return null;
      }
    }

    return match;
  }

  #getRecords(bucket, collection, request) {
    let records = this.#records.get(this.#recordsKey(bucket, collection));
    let params = new URLSearchParams(request.queryString);

    let type = params.get("type");
    if (type) {
      records = records.filter(r => r.type == type);
    }

    let gtLastModified = params.get("gt_last_modified");
    if (gtLastModified) {
      records = records.filter(r => r.last_modified > gtLastModified);
    }

    let since = params.get("_since");
    if (since) {
      // Example value: "%221368273600004%22"
      let match = /^"([0-9]+)"$/.exec(decodeURIComponent(since));
      if (match) {
        let sinceTime = parseInt(match[1]);
        records = records.filter(r => r.last_modified > sinceTime);
      }
    }

    let sort = params.get("_sort");
    if (sort == "last_modified") {
      records = records.toSorted((a, b) => a.last_modified - b.last_modified);
    }

    return records;
  }

  #recordsKey(bucket, collection) {
    return `${bucket}/${collection}`;
  }

  /**
   * Registers an attachment for a record.
   *
   * @param {object} options
   * @param {string} options.bucket
   * @param {string} options.collection
   * @param {object} options.record
   *   The record should have an `attachment` property as described in
   *   `addRecords()`.
   */
  async #addAttachment({ bucket, collection, record }) {
    let { attachment } = record;

    let mimetype =
      record.attachmentMimetype ?? "application/json; charset=UTF-8";
    if (!mimetype.startsWith("application/json")) {
      throw new Error(
        "Mimetype not handled, please add code for it! " + mimetype
      );
    }

    let encoder = new TextEncoder();
    let bytes = encoder.encode(JSON.stringify(attachment));

    let hashBuffer = await crypto.subtle.digest("SHA-256", bytes);
    let hashBytes = new Uint8Array(hashBuffer);
    let toHex = b => b.toString(16).padStart(2, "0");
    let hash = Array.from(hashBytes, toHex).join("");

    let filename = record.id;
    this.#attachments.set(
      this.#attachmentsKey(bucket, collection, filename),
      attachment
    );

    // Replace `record.attachment` with appropriate metadata in order to conform
    // with the remote settings API.
    record.attachment = {
      hash,
      filename,
      mimetype,
      size: bytes.length,
      location: `attachments/${bucket}/${collection}/${filename}`,
    };

    delete record.attachmentMimetype;
  }

  #attachmentsKey(bucket, collection, filename) {
    return `${bucket}/${collection}/${filename}`;
  }

  #getAttachment(bucket, collection, filename) {
    return this.#attachments.get(
      this.#attachmentsKey(bucket, collection, filename)
    );
  }

  /**
   * Prepares an HTTP response.
   *
   * @param {object} options
   * @param {nsIHttpRequest} options.request
   * @param {nsIHttpResponse} options.response
   * @param {object|null} [options.body]
   *   Currently only JSON'able objects are supported. They will be converted to
   *   JSON in the response.
   * @param {number} [options.status]
   * @param {string} [options.statusText]
   */
  #prepareResponse({
    request,
    response,
    body = null,
    status = 200,
    statusText = "OK",
  }) {
    let headers = { ...this.#responseHeaders };
    if (body) {
      headers["Content-Type"] = "application/json; charset=UTF-8";
    }

    this.#logResponse({ request, status, body });

    for (let [name, value] of Object.entries(headers)) {
      response.setHeader(name, value, false);
    }
    if (body) {
      response.write(JSON.stringify(body));
    }
    response.setStatusLine(request.httpVersion, status, statusText);
  }

  /**
   * Prepares an HTTP error response.
   *
   * @param {object} options
   * @param {nsIHttpRequest} options.request
   * @param {nsIHttpResponse} options.response
   * @param {HttpError} options.error
   *   An `HttpError` instance defined in `httpd.sys.mjs`.
   */
  #prepareError({ request, response, error }) {
    this.#prepareResponse({
      request,
      response,
      status: error.code,
      statusText: error.description,
    });
  }

  /**
   * Logs a request.
   *
   * @param {nsIHttpRequest} request
   */
  #logRequest(request) {
    let pathAndQuery = request.path;
    if (request.queryString) {
      pathAndQuery += "?" + request.queryString;
    }
    this.#log.debug(
      `< HTTP ${request.httpVersion} ${request.method} ${pathAndQuery}`
    );
  }

  /**
   * Logs a response.
   *
   * @param {object} options
   * @param {nsIHttpRequest} options.request
   *   The associated request.
   * @param {number} options.status
   *   The HTTP status code of the response.
   * @param {object} options.body
   *   The response body, if any.
   */
  #logResponse({ request, status, body }) {
    this.#log.debug(`> ${status} ${request.path}`);
    if (body) {
      this.#log.debug("Response body:", body);
    }
  }

  // records key (see `#recordsKey()`) -> array of record objects
  #records = new Map();

  // attachments key (see `#attachmentsKey()`) -> attachment object
  #attachments = new Map();

  #log;
  #server;
  #originalServerPrefValue;
  #url = null;
  #lastModified = 1368273600000;
  #nextRecordId = 1;
}
