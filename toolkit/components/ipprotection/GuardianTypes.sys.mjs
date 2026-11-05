/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(
  lazy,
  "JsonSchemaValidator",
  () =>
    ChromeUtils.importESModule(
      "resource://gre/modules/components-utils/JsonSchemaValidator.sys.mjs"
    ).JsonSchemaValidator
);
ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    logModule: "IPP_GuardianTypes",
    maxLogLevel: Services.prefs.getBoolPref("browser.ipProtection.log", false)
      ? "Debug"
      : "Warn",
  })
);

/**
 * A ProxyPass contains a JWT token that can be used to authenticate the proxy service.
 * It also contains the timestamp until which the token is valid.
 * The Proxy will reject new connections if the token is not valid anymore.
 *
 * Immutable after creation.
 */
export class ProxyPass extends EventTarget {
  #body = {
    /** Not Before */
    nbf: 0,
    /** Expiration */
    exp: 0,
  };
  /**
   * The instant from which the pass is valid. Defaults to the token's nbf,
   * unless one is supplied.
   *
   * @type {Temporal.Instant}
   */
  #from;
  /** @type {Temporal.Duration} - The token's lifetime (exp - nbf). */
  #duration;
  /** @type {Temporal.Instant} - The instant at which the pass expires. */
  #until;
  /**
   * @param {string} token - The JWT to use for authentication.
   * @param {Temporal.Instant} [from] - Instant from which the pass is valid.
   *   Defaults to the token's nbf.
   */
  constructor(token, from) {
    super();
    if (typeof token !== "string") {
      throw new TypeError(
        "Invalid arguments for ProxyPass constructor, token is not a string"
      );
    }
    this.token = token;
    // Contains [header.body.signature]
    const parts = this.token.split(".");
    if (parts.length !== 3) {
      throw new TypeError("Invalid token format");
    }
    try {
      const body = JSON.parse(atob(parts[1]));
      if (
        !lazy.JsonSchemaValidator.validate(body, ProxyPass.bodySchema).valid
      ) {
        throw new TypeError("Token body does not match schema");
      }
      this.#body = body;
    } catch (error) {
      throw new TypeError("Invalid token format: " + error.message);
    }
    this.#from =
      from ?? Temporal.Instant.fromEpochMilliseconds(this.#body.nbf * 1000);
    this.#duration = Temporal.Duration.from({
      seconds: this.#body.exp - this.#body.nbf,
    });
    this.#until = this.#from.add(this.#duration);
  }

  isValid(now = Temporal.Now.instant()) {
    // If the remaining duration is zero or positive, the pass is still valid.
    return (
      Temporal.Instant.compare(now, this.from) >= 0 &&
      Temporal.Instant.compare(now, this.until) < 0
    );
  }

  shouldRotate(now = Temporal.Now.instant()) {
    if (!this.isValid(now)) {
      return true;
    }
    return Temporal.Instant.compare(now, this.rotationTimePoint) >= 0;
  }

  get duration() {
    return this.#duration;
  }

  get from() {
    return this.#from;
  }

  get until() {
    return this.#until;
  }

  /**
   * Parses a ProxyPass from a Response object.
   *
   * @param {Response} response
   * @returns {Promise<ProxyPass|null>} A promise that resolves to a ProxyPass instance or null if the response is invalid.
   */
  static async fromResponse(response) {
    if (!response.ok) {
      lazy.logConsole.error(
        `Failed to fetch proxy pass: ${response.status} ${response.statusText}`
      );
      return null;
    }

    try {
      const responseData = await response.json();
      const token = responseData?.token;

      if (!token || typeof token !== "string") {
        lazy.logConsole.error("Missing or invalid token in response");
        return null;
      }
      // Anchor the pass to our clock at receipt: it is valid from now for the
      // token's lifetime. This makes rotation scheduling immune to any drift
      // between Guardian's clock (nbf/exp) and ours.
      return new ProxyPass(token, Temporal.Now.instant());
    } catch (error) {
      lazy.logConsole.error("Error parsing proxy pass response:", error);
      return null;
    }
  }

  /**
   * @type {Temporal.Instant} - The Point in time when the token should be rotated.
   */
  get rotationTimePoint() {
    return this.until.subtract(ProxyPass.ROTATION_TIME);
  }

  asBearerToken() {
    return `Bearer ${this.token}`;
  }

  // Rotate 2 Minutes from the End Time
  static ROTATION_TIME = Temporal.Duration.from({ minutes: 2 });

  static get bodySchema() {
    return {
      $schema: "http://json-schema.org/draft-07/schema#",
      title: "JWT Claims",
      type: "object",
      properties: {
        sub: {
          type: "string",
          description: "Subject identifier",
        },
        aud: {
          type: "string",
          format: "uri",
          description: "Audience for which the token is intended",
        },
        iat: {
          type: "integer",
          description: "Issued-at time (seconds since Unix epoch)",
        },
        nbf: {
          type: "integer",
          description: "Not-before time (seconds since Unix epoch)",
        },
        exp: {
          type: "integer",
          description: "Expiration time (seconds since Unix epoch)",
        },
        iss: {
          type: "string",
          description: "Issuer identifier",
        },
      },
      required: ["sub", "aud", "iat", "nbf", "exp", "iss"],
      additionalProperties: true,
    };
  }
}

/**
 * A short lived bearer token used to authenticate against Guardian, as returned
 * by an auth provider's `getToken()`.
 *
 * A handle is built for a single Guardian request. Providers whose tokens are
 * owned outside of Gecko omit `onTokenRejected`.
 *
 * @typedef {object} TokenHandle
 * @property {string} token - The bearer token to authenticate with.
 * @property {() => void|Promise<void>} [onTokenRejected] - Invalidates the
 *   token after Guardian refused it, so that the caller can retry with a fresh
 *   one.
 */

/**
 * Represents a user's Entitlement for the Proxy Service of Guardian.
 * If a user has an entitlement, they may access the proxy service.
 *
 * Immutable after creation.
 */
export class Entitlement {
  /** True if the User has any valid subscription plan to the Mozilla VPN (not firefox VPN) */
  subscribed = false;
  /** The Guardian User ID */
  uid = 0;
  /** The maximum number of bytes allowed for the user */
  maxBytes = BigInt(0);
  /** True if the user has a bandwidth cap; false if the user has unlimited bandwidth */
  limitedBandwidth = true;

  constructor(
    args = {
      subscribed: false,
      uid: 0,
      maxBytes: "0",
      limited_bandwidth: true,
    }
  ) {
    this.subscribed = args.subscribed;
    this.uid = args.uid;
    this.maxBytes = BigInt(args.maxBytes);
    this.limitedBandwidth = args.limited_bandwidth ?? true;
    Object.freeze(this);
  }

  static fromResponse(response) {
    if (!response.ok) {
      return null;
    }
    return response.json().then(data => {
      const result = lazy.JsonSchemaValidator.validate(
        data,
        Entitlement.schema
      );
      if (!result.valid) {
        return null;
      }
      return new Entitlement(data);
    });
  }

  static get schema() {
    return {
      $schema: "http://json-schema.org/draft-07/schema#",
      title: "Entitlement",
      type: "object",
      properties: {
        subscribed: {
          type: "boolean",
        },
        uid: {
          type: "integer",
        },
        maxBytes: {
          type: "string",
          description:
            "A BigInt string representing the maximum number of bytes allowed for the user",
        },
        limited_bandwidth: {
          type: "boolean",
          description:
            "True if the user has a bandwidth cap; false if the user has unlimited bandwidth",
        },
      },
      required: ["subscribed", "uid", "maxBytes"],
      additionalProperties: true,
    };
  }

  toString() {
    return JSON.stringify({
      subscribed: this.subscribed,
      uid: this.uid,
      maxBytes: this.maxBytes.toString(),
      limited_bandwidth: this.limitedBandwidth,
    });
  }
}

/**
 * Represents usage tracking information for the Proxy Service.
 * Contains data about quota limits, remaining quota, and reset time.
 *
 * Immutable after creation.
 */
export class ProxyUsage {
  /** @type {bigint | null} - Maximum bytes allowed */
  max = null;
  /** @type {bigint | null} - Remaining bytes available */
  remaining = null;
  /** @type {Temporal.Instant | null} - When the usage quota resets */
  reset = null;
  /** @type {boolean} - True if the user has unlimited bandwidth; false if the user has a bandwidth cap */
  unlimited = false;

  /**
   * @param {string | null} max - Maximum bytes allowed (as string for BigInt parsing)
   * @param {string | null} remaining - Remaining bytes available (as string for BigInt parsing)
   * @param {string | null} reset - ISO 8601 timestamp when quota resets
   * @param {boolean} [unlimited] - Whether the user has unlimited bandwidth
   */
  constructor(max, remaining, reset, unlimited) {
    this.unlimited = !!unlimited;

    if (this.unlimited) {
      Object.freeze(this);
      return;
    }

    if (max === null || remaining === null || reset === null) {
      throw new TypeError("invalid usage");
    }

    this.max = BigInt(max);
    if (this.max < BigInt(0)) {
      throw new TypeError("max must be non-negative");
    }

    this.remaining = BigInt(remaining);
    if (this.remaining < BigInt(0)) {
      throw new TypeError("remaining must be non-negative");
    }

    if (this.remaining > this.max) {
      throw new TypeError("remaining cannot exceed max");
    }

    this.reset = Temporal.Instant.from(reset);

    Object.freeze(this);
  }

  /**
   * Compares this usage against another for value equality.
   *
   * @param {ProxyUsage | null} other - The usage to compare against.
   * @returns {boolean} True if both represent the same usage state.
   */
  equals(other) {
    if (!(other instanceof ProxyUsage)) {
      return false;
    }
    if (
      this.unlimited !== other.unlimited ||
      this.max !== other.max ||
      this.remaining !== other.remaining
    ) {
      return false;
    }
    if (this.reset === null || other.reset === null) {
      return this.reset === other.reset;
    }
    return Temporal.Instant.compare(this.reset, other.reset) === 0;
  }

  static fromResponse(response) {
    const unlimited = response.headers.get("X-Quota-Unlimited") === "true";

    if (unlimited) {
      return new ProxyUsage(null, null, null, true);
    }

    const getOrThrow = headerName => {
      const value = response.headers.get(headerName);
      if (!value) {
        throw new TypeError(`Missing required header: ${headerName}`);
      }
      return value;
    };

    const quotaLimit = getOrThrow("X-Quota-Limit");
    const quotaRemaining = getOrThrow("X-Quota-Remaining");
    const quotaReset = getOrThrow("X-Quota-Reset");

    return new ProxyUsage(quotaLimit, quotaRemaining, quotaReset);
  }

  get quotaExhausted() {
    return !this.unlimited && this.remaining <= 0;
  }
}
