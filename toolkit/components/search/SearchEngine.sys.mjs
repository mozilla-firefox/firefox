/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint no-shadow: error, mozilla/no-aArgs: error */

/**
 * @typedef {import("./AddonSearchEngine.sys.mjs").AddonSearchEngine} AddonSearchEngine
 * @typedef {import("./OpenSearchEngine.sys.mjs").OpenSearchEngine} OpenSearchEngine
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  SearchSettings: "resource://gre/modules/SearchSettings.sys.mjs",
  SearchUtils: "resource://gre/modules/SearchUtils.sys.mjs",
  OpenSearchEngine: "resource://gre/modules/OpenSearchEngine.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    prefix: "SearchEngine",
    maxLogLevel: lazy.SearchUtils.loggingEnabled ? "Debug" : "Warn",
  });
});

// Supported OpenSearch parameters
// See https://web.archive.org/web/20060203040832/http://opensearch.a9.com/spec/1.1/querysyntax/#core
const OS_PARAM_INPUT_ENCODING = "inputEncoding";
const OS_PARAM_LANGUAGE = "language";
const OS_PARAM_OUTPUT_ENCODING = "outputEncoding";

// Default values
const OS_PARAM_LANGUAGE_DEF = "*";
const OS_PARAM_OUTPUT_ENCODING_DEF = "UTF-8";

// "Unsupported" OpenSearch parameters. For example, we don't support
// page-based results, so if the engine requires that we send the "page index"
// parameter, we'll always send "1".
const OS_PARAM_COUNT = "count";
const OS_PARAM_START_INDEX = "startIndex";
const OS_PARAM_START_PAGE = "startPage";

// Default values
const OS_PARAM_COUNT_DEF = "20"; // 20 results
const OS_PARAM_START_INDEX_DEF = "1"; // start at 1st result
const OS_PARAM_START_PAGE_DEF = "1"; // 1st page

// A array of arrays containing parameters that we don't fully support, and
// their default values. We will only send values for these parameters if
// required, since our values are just really arbitrary "guesses" that should
// give us the output we want.
var OS_UNSUPPORTED_PARAMS = [
  [OS_PARAM_COUNT, OS_PARAM_COUNT_DEF],
  [OS_PARAM_START_INDEX, OS_PARAM_START_INDEX_DEF],
  [OS_PARAM_START_PAGE, OS_PARAM_START_PAGE_DEF],
];

// An array of attributes that are saved in the engines `_metaData` object.
// Attributes not in this array are considered as system attributes.
const USER_ATTRIBUTES = ["alias", "order", "hideOneOffButton"];

/**
 * Truncates big blobs of (data-)URIs to console-friendly sizes
 *
 * @param {string} str
 *   String to tone down
 * @param {number} len
 *   Maximum length of the string to return. Defaults to the length of a tweet.
 * @returns {string}
 *   The shortend string.
 */
function limitURILength(str, len = 140) {
  if (str.length > len) {
    return str.slice(0, len) + "...";
  }
  return str;
}

/**
 * Represents a name/value pair for a parameter
 */
export class QueryParameter {
  /**
   * @param {string} name
   *   The parameter's name. Must not be null.
   * @param {string} value
   *   The value of the parameter. May be an empty string, must not be null or
   *   undefined.
   */
  constructor(name, value) {
    if (!name || value == null) {
      throw Components.Exception(
        "missing name or value for QueryParameter!",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    this.name = name;
    this._value = value;
  }

  get value() {
    return this._value;
  }

  /**
   * Creates a JavaScript object that represents this parameter.
   *
   * @returns {object}
   *   An object suitable for serialization as JSON.
   */
  toJSON() {
    return {
      name: this.name,
      value: this.value,
    };
  }
}

/**
 * Perform OpenSearch parameter substitution on a parameter value.
 *
 * @see https://web.archive.org/web/20060203040832/http://opensearch.a9.com/spec/1.1/querysyntax/#core
 *
 * @param {string} paramValue
 *   The OpenSearch search parameters.
 * @param {string} searchTerms
 *   The user-provided search terms. This string will inserted into
 *   paramValue as the value of the searchTerms parameter.
 *   This value must already be escaped appropriately - it is inserted
 *   as-is.
 * @param {string} queryCharset
 *   The character set of the search engine to use for query encoding.
 * @returns {string}
 *   An updated parameter string.
 */
function paramSubstitution(paramValue, searchTerms, queryCharset) {
  const PARAM_REGEXP = /\{(\w+)(\??)\}/g;
  return paramValue.replace(PARAM_REGEXP, function (match, name, optional) {
    // {searchTerms} is by far the most common param so handle it first.
    if (name == "searchTerms") {
      return searchTerms;
    }

    // {inputEncoding} is the second most common param.
    if (name == OS_PARAM_INPUT_ENCODING) {
      return queryCharset;
    }

    // Handle the less common OpenSearch parameters we're confident about.
    if (name == OS_PARAM_LANGUAGE) {
      return Services.locale.requestedLocale || OS_PARAM_LANGUAGE_DEF;
    }
    if (name == OS_PARAM_OUTPUT_ENCODING) {
      return OS_PARAM_OUTPUT_ENCODING_DEF;
    }

    // At this point, if a parameter is optional, just omit it.
    if (optional) {
      return "";
    }

    // Replace unsupported parameters that only have hardcoded default values.
    for (let param of OS_UNSUPPORTED_PARAMS) {
      if (name == param[0]) {
        return param[1];
      }
    }

    // Don't replace unknown non-optional parameters.
    return match;
  });
}

/**
 * EngineURL holds a query URL and all associated parameters.
 */
export class EngineURL {
  /** @type {QueryParameter[]} */
  params = [];
  /** @type {string[]} */
  rels = [];
  /** @type {string} */
  template;

  /**
   * The name of the parameter used for the search term.
   *
   * @type {?string}
   */
  #searchTermParam = null;

  /**
   * Creates an EngineURL.
   *
   * @param {string} mimeType
   *   The name of the MIME type of the search results returned by this URL.
   * @param {string} requestMethod
   *   The HTTP request method. Must be a case insensitive value of either
   *   "GET" or "POST".
   * @param {string} template
   *   The URL to which search queries should be sent. For GET requests,
   *   must contain the string "{searchTerms}", to indicate where the user
   *   entered search terms should be inserted.
   *
   * @see https://web.archive.org/web/20060203040832/http://opensearch.a9.com/spec/1.1/querysyntax/#urltag
   *
   * @throws NS_ERROR_NOT_IMPLEMENTED if aType is unsupported.
   */
  constructor(mimeType, requestMethod, template) {
    if (!mimeType || !requestMethod || !template) {
      throw Components.Exception(
        "missing mimeType, method or template for EngineURL!",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    var method = requestMethod.toUpperCase();
    var type = mimeType.toLowerCase();

    if (method != "GET" && method != "POST") {
      throw Components.Exception(
        'method passed to EngineURL must be "GET" or "POST"',
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    this.type = type;
    this.method = method;

    var templateURI = lazy.SearchUtils.makeURI(template);
    if (!templateURI) {
      throw Components.Exception(
        "new EngineURL: template is not a valid URI!",
        Cr.NS_ERROR_FAILURE
      );
    }

    switch (templateURI.scheme) {
      case "http":
      case "https":
        this.template = template;
        break;
      default:
        throw Components.Exception(
          "new EngineURL: template uses invalid scheme!",
          Cr.NS_ERROR_FAILURE
        );
    }

    this.templateHost = templateURI.host;

    // It's possible that the search term parameter
    // is part of the template.
    let urlParms = new URLSearchParams(templateURI.query);
    for (let [name, value] of urlParms.entries()) {
      if (value == "{searchTerms}") {
        this.#searchTermParam = name;
      }
    }
  }

  /**
   * Adds a QueryParameter object to the list of params.
   *
   * @param {QueryParameter} param
   *   The QueryParameter to add.
   */
  addQueryParameter(param) {
    if (param.value == "{searchTerms}") {
      this.setSearchTermParamName(param.name);
      return;
    }
    this.params.push(param);
  }

  /**
   * Adds a QueryParameter by name and value.
   * This exists because it's a frequent operation and it allows
   * other files to add QueryParameters without importing QueryParameter.
   *
   * @param {string} name name of the parameter
   * @param {string} value value of the parameter
   */
  addParam(name, value) {
    this.addQueryParameter(new QueryParameter(name, value));
  }

  /**
   * Sets the name of the search term parameter and
   * adds it to the list of query parameters.
   *
   * @param {string} name
   *   The name of the parameter.
   */
  setSearchTermParamName(name) {
    if (this.#searchTermParam) {
      lazy.logConsole.warn(
        "set searchTermParamName: searchTermParamName was set twice."
      );
    }
    this.params.push(new QueryParameter(name, "{searchTerms}"));
    this.#searchTermParam = name;
  }

  /**
   * Returns the name of the parameter used for the search term.
   *
   * @returns {?string}
   *   A string which is the name of the parameter, or null if no parameter
   *   can be found (e.g. if search terms are contained within the URL).
   */
  get searchTermParamName() {
    return this.#searchTermParam;
  }

  /**
   * Returns a complete URL with parameter data that can be used for submitting
   * a suggestion query or loading a search page.
   *
   * @param {string} searchTerms
   *   The user's search terms.
   * @param {string} queryCharset
   *   The character set that is being used for the query.
   * @returns {Submission}
   *   The submission data containing the URL and post data for the URL.
   */
  getSubmission(searchTerms, queryCharset) {
    let escapedSearchTerms;
    try {
      escapedSearchTerms = Services.textToSubURI.ConvertAndEscape(
        queryCharset,
        searchTerms
      );
    } catch (ex) {
      lazy.logConsole.warn(
        "getSubmission: Falling back to default queryCharset!"
      );
      escapedSearchTerms = Services.textToSubURI.ConvertAndEscape(
        lazy.SearchUtils.DEFAULT_QUERY_CHARSET,
        searchTerms
      );
    }

    let templateURI = new URL(this.template);
    let paramString = this.#encodeParams(escapedSearchTerms, queryCharset);

    let postData = null;
    let query = paramSubstitution(
      templateURI.search,
      escapedSearchTerms,
      queryCharset
    );
    if (this.method == "GET" && paramString) {
      // Query parameters may be specified in the template url AND in `this.params`.
      // Thus, we need to supply both with the search terms and join them.
      if (query) {
        query += "&" + paramString;
      } else {
        query = paramString;
      }
    } else if (this.method == "POST") {
      // POST method requests must wrap the encoded text in a MIME
      // stream and supply that as POSTDATA.
      let stringStream = Cc[
        "@mozilla.org/io/string-input-stream;1"
      ].createInstance(Ci.nsIStringInputStream);
      stringStream.setByteStringData(paramString);

      postData = Cc["@mozilla.org/network/mime-input-stream;1"].createInstance(
        Ci.nsIMIMEInputStream
      );
      postData.addHeader("Content-Type", "application/x-www-form-urlencoded");
      postData.setData(stringStream);
    }

    templateURI.search = query;

    // textToSubURI encodes spaces with '+', but we want to use '%20' if the
    // search terms are part of the file path or ref. We only use '+' if they
    // are part of a query parameter.
    let urlSearchTerms = escapedSearchTerms.replaceAll("+", "%20");
    templateURI.pathname = paramSubstitution(
      // The braces in filePath are percent-encoded, so we
      // decode them to ensure paramSubstitution finds them.
      decodeURIComponent(templateURI.pathname),
      urlSearchTerms,
      queryCharset
    );
    templateURI.hash = paramSubstitution(
      templateURI.hash,
      urlSearchTerms,
      queryCharset
    );

    return new Submission(templateURI.URI, postData);
  }

  /**
   * Returns a application/x-www-form-urlencoded representation of the params
   * using the specified search term (name=value&name=value&name=value).
   * Can be used for GET and POST.
   *
   * @param {string} escapedSearchTerms
   *   The user's search terms escaped with the correct charset.
   * @param {string} queryCharset
   *   The character set that is being used for the query.
   * @returns {string}
   *   Parameter string containing the search terms.
   */
  #encodeParams(escapedSearchTerms, queryCharset) {
    let dataArray = [];
    for (let param of this.params) {
      // QueryPreferenceParameters might not have a preferenced saved, or a valid value.
      if (param.value != null) {
        let value = paramSubstitution(
          param.value,
          escapedSearchTerms,
          queryCharset
        );
        dataArray.push(param.name + "=" + value);
      }
    }
    return dataArray.join("&");
  }

  _hasRelation(rel) {
    return this.rels.some(e => e == rel.toLowerCase());
  }

  _initWithJSON(json) {
    if (!json.params) {
      return;
    }

    this.rels = json.rels;

    for (let param of json.params) {
      // mozparam and purpose were only supported for app-provided engines.
      // Always ignore them for engines loaded from JSON.
      if (!param.mozparam && !param.purpose) {
        this.addParam(param.name, param.value);
      }
    }
  }

  /**
   * Creates a JavaScript object that represents this URL.
   *
   * @returns {object}
   *   An object suitable for serialization as JSON.
   */
  toJSON() {
    var json = {
      params: this.params,
      rels: this.rels,
      template: this.template,
    };

    if (this.type != lazy.SearchUtils.URL_TYPE.SEARCH) {
      json.type = this.type;
    }
    if (this.method != "GET") {
      json.method = this.method;
    }

    return json;
  }
}

/**
 * SearchEngine is the base class that all search engine classes inherit from.
 *
 * @implements {nsISearchEngine}
 */
export class SearchEngine {
  QueryInterface = ChromeUtils.generateQI(["nsISearchEngine"]);
  // Data set by the user.
  _metaData = {};
  // Anonymized path of where we initially loaded the engine from.
  // This will stay null for engines installed in the profile before we moved
  // to a JSON storage.
  _loadPath = null;
  // The engine's name.
  _name = null;
  // The name of the charset used to submit the search terms.
  _queryCharset = null;
  // The order hint from the configuration (if any).
  _orderHint = null;
  // The telemetry id from the configuration (if any).
  _telemetryId = null;
  // Set to true once the engine has been added to the store, and the initial
  // notification sent. This allows to skip sending notifications during
  // initialization.
  _engineAddedToStore = false;
  // The aliases coming from the engine definition (via webextension
  // keyword field for example).
  _definedAliases = [];
  // The urls associated with this engine.
  _urls = [];
  // The known public suffix of the search url, cached in memory to avoid
  // repeated look-ups.
  _searchUrlPublicSuffix = null;
  /**
   * The unique id of the Search Engine.
   *
   * @type {string}
   */
  #id;
  /**
   * The URL to report the search to.
   *
   * @type {?string}
   */
  clickUrl = null;

  /**
   *  Creates a Search Engine.
   *
   * @param {object} options
   *   The options for this search engine.
   * @param {string} [options.id]
   *   The identifier to use for this engine, if none is specified a random
   *   uuid is created.
   * @param {string} options.loadPath
   *   The path of the engine was originally loaded from. Should be anonymized.
   */
  constructor(options) {
    this.#id = options.id ?? this.#uuid();
    if (!("loadPath" in options)) {
      throw new Error("loadPath missing from options.");
    }
    this._loadPath = options.loadPath;
  }

  /**
   * Attempts to find an EngineURL object in the set of EngineURLs for
   * this Engine that has the given type string.  (This corresponds to the
   * "type" attribute in the "Url" node in the OpenSearch spec.)
   *
   * @param {string} type
   *   The type to match the EngineURL's type attribute.
   * @param {string} [rel]
   *   Only return URLs that with this rel value.
   * @returns {EngineURL|null}
   *   Returns the first matching URL found, null otherwise.
   */
  _getURLOfType(type, rel) {
    for (let url of this._urls) {
      if (url.type == type && (!rel || url._hasRelation(rel))) {
        return url;
      }
    }

    return null;
  }

  /**
   * Add an icon to the icon map used by getIconURL().
   * Icon must be square.
   *
   * @param {string} iconURL
   *   String with the icon's URI.
   * @param {number} size
   *   Width and height of the icon.
   * @param {boolean} override
   *   Whether the new URI should override an existing one.
   */
  _addIconToMap(iconURL, size, override = true) {
    // Use an object instead of a Map() because it needs to be serializable.
    this._iconMapObj = this._iconMapObj || {};
    if (!(size in this._iconMapObj) || override) {
      this._iconMapObj[size] = iconURL;
    }
  }

  /**
   * Sets the .iconURI property of the engine. If size is provided
   * an entry will be added to _iconMapObj that will enable accessing
   * icon's data through getIconURL() APIs.
   *
   * @param {string} iconURL
   *   A URI string pointing to the engine's icon. Must have a http[s]
   *   or data scheme. Icons with HTTP[S] schemes will be
   *   downloaded and converted to data URIs for storage in the engine
   *   XML files, if the engine is not built-in.
   * @param {number} [size]
   *   Width and height of the icon.
   * @param {boolean} [override]
   *   Whether the new URI should override an existing one.
   */
  async _setIcon(iconURL, size, override = true) {
    let uri = lazy.SearchUtils.makeURI(iconURL);

    // Ignore bad URIs
    if (!uri) {
      return;
    }

    lazy.logConsole.debug(
      "_setIcon: Setting icon url for",
      this.name,
      "to",
      limitURILength(uri.spec)
    );
    // Only accept remote icons from http[s]
    switch (uri.scheme) {
      case "moz-extension": {
        if (!size) {
          let byteArray, contentType;
          try {
            [byteArray, contentType] = await lazy.SearchUtils.fetchIcon(uri);
          } catch {
            lazy.logConsole.warn(
              `Failed to load icon of search engine ${this.name}.`
            );
            return;
          }
          size = lazy.SearchUtils.decodeSize(byteArray, contentType, 16);
        }

        this._addIconToMap(iconURL, size, override);
        break;
      }
      // We also fetch data URLs to ensure the size doesn't exceed MAX_ICON_SIZE.
      case "data":
      case "http":
      case "https": {
        let byteArray, contentType;
        try {
          [byteArray, contentType] = await lazy.SearchUtils.fetchIcon(uri);
        } catch {
          lazy.logConsole.warn(
            `Failed to load icon of search engine ${this.name}.`
          );
          return;
        }

        if (byteArray.length > lazy.SearchUtils.MAX_ICON_SIZE) {
          try {
            lazy.logConsole.debug(
              `Rescaling icon for search engine ${this.name}.`
            );
            [byteArray, contentType] = lazy.SearchUtils.rescaleIcon(
              byteArray,
              contentType,
              32
            );
            size = 32;
          } catch (ex) {
            lazy.logConsole.error(
              `Unable to rescale  icon for search engine ${this.name}:`,
              ex
            );
            return;
          }
        }

        if (!size) {
          size = lazy.SearchUtils.decodeSize(byteArray, contentType, 16);
        }

        let dataURL = "data:" + contentType + ";base64," + byteArray.toBase64();
        this._addIconToMap(dataURL, size, override);
        break;
      }
    }

    if (this._engineAddedToStore) {
      lazy.SearchUtils.notifyAction(
        this,
        lazy.SearchUtils.MODIFIED_TYPE.ICON_CHANGED
      );
    }
  }

  /**
   * Initialize an EngineURL object from metadata.
   *
   * @param {string} type
   *   The url type.
   * @param {object} params
   *   The URL parameters.
   * @param {string | Array} [params.getParams]
   *   Any parameters for a GET method. This is either a query string, or
   *   an array of objects which have name/value pairs.
   * @param {string} [params.method]
   *   The type of method, defaults to GET.
   * @param {string | Array} [params.postParams]
   *   Any parameters for a POST method. This is either a query string, or
   *   an array of objects which have name/value pairs.
   * @param {string} params.template
   *   The url template.
   * @returns {EngineURL}
   *   The newly created EngineURL.
   */
  _getEngineURLFromMetaData(type, params) {
    let url = new EngineURL(type, params.method || "GET", params.template);

    if (params.postParams) {
      if (Array.isArray(params.postParams)) {
        for (let { name, value } of params.postParams) {
          url.addParam(name, value);
        }
      } else {
        for (let [name, value] of new URLSearchParams(params.postParams)) {
          url.addParam(name, value);
        }
      }
    }

    if (params.getParams) {
      if (Array.isArray(params.getParams)) {
        for (let { name, value } of params.getParams) {
          url.addParam(name, value);
        }
      } else {
        for (let [name, value] of new URLSearchParams(params.getParams)) {
          url.addParam(name, value);
        }
      }
    }

    return url;
  }

  /**
   * Initialize this engine object using a WebExtension style object.
   *
   * @param {object} details
   *   The details of the engine.
   * @param {string} details.name
   *   The name of the engine.
   * @param {string} details.keyword
   *   The keyword for the engine.
   * @param {string} [details.iconURL]
   *   The url to use for the icon of the engine.
   * @param {string} details.search_url
   *   The search url template for the engine.
   * @param {string} [details.search_url_get_params]
   *   The search url parameters for use with the GET method.
   * @param {string} [details.search_url_post_params]
   *   The search url parameters for use with the POST method.
   * @param {string} [details.suggest_url]
   *   The suggestion url template for the engine.
   * @param {string} [details.suggest_url_get_params]
   *   The suggestion url parameters for use with the GET method.
   * @param {string} [details.suggest_url_post_params]
   *   The suggestion url parameters for use with the POST method.
   * @param {string} [details.encoding]
   *   The encoding to use for the engine.
   */
  _initWithDetails(details) {
    this._name = details.name.trim();

    this._definedAliases = [];
    if (Array.isArray(details.keyword)) {
      this._definedAliases = details.keyword.map(k => k.trim());
    } else if (details.keyword?.trim()) {
      this._definedAliases = [details.keyword?.trim()];
    }

    if (details.iconURL) {
      this._setIcon(details.iconURL).catch(e =>
        lazy.logConsole.log("Error while setting search engine icon:", e)
      );
    }
    this._setUrls(details);
  }

  /**
   * This sets the urls for the search engine based on the supplied parameters.
   * If you add anything here, please consider if it needs to be handled in the
   * overrideWithEngine / removeExtensionOverride functions as well.
   *
   * @param {object} details
   *   The details of the engine.
   * @param {string} details.search_url
   *   The search url template for the engine.
   * @param {string} [details.search_url_get_params]
   *   The search url parameters for use with the GET method.
   * @param {string} [details.search_url_post_params]
   *   The search url parameters for use with the POST method.
   * @param {string} [details.suggest_url]
   *   The suggestion url template for the engine.
   * @param {string} [details.suggest_url_get_params]
   *   The suggestion url parameters for use with the GET method.
   * @param {string} [details.suggest_url_post_params]
   *   The suggestion url parameters for use with the POST method.
   * @param {string} [details.encoding]
   *   The encoding to use for the engine.
   */
  _setUrls(details) {
    let postParams = details.search_url_post_params || "";
    let url = this._getEngineURLFromMetaData(lazy.SearchUtils.URL_TYPE.SEARCH, {
      method: (postParams && "POST") || "GET",
      // AddonManager will sometimes encode the URL via `new URL()`. We want
      // to ensure we're always dealing with decoded urls.
      template: decodeURI(details.search_url),
      getParams: details.search_url_get_params || "",
      postParams,
    });

    this._urls.push(url);

    if (details.suggest_url) {
      let suggestPostParams = details.suggest_url_post_params || "";
      url = this._getEngineURLFromMetaData(
        lazy.SearchUtils.URL_TYPE.SUGGEST_JSON,
        {
          method: (suggestPostParams && "POST") || "GET",
          // suggest_url doesn't currently get encoded.
          template: details.suggest_url,
          getParams: details.suggest_url_get_params || "",
          postParams: suggestPostParams,
        }
      );

      this._urls.push(url);
    }

    if (details.encoding) {
      this._queryCharset = details.encoding;
    }
  }

  checkSearchUrlMatchesManifest(details) {
    let existingUrl = this._getURLOfType(lazy.SearchUtils.URL_TYPE.SEARCH);

    let newUrl = this._getEngineURLFromMetaData(
      lazy.SearchUtils.URL_TYPE.SEARCH,
      {
        method: (details.search_url_post_params && "POST") || "GET",
        // AddonManager will sometimes encode the URL via `new URL()`. We want
        // to ensure we're always dealing with decoded urls.
        template: decodeURI(details.search_url),
        getParams: details.search_url_get_params || "",
        postParams: details.search_url_post_params || "",
      }
    );

    let existingSubmission = existingUrl.getSubmission("", this.queryCharset);
    let newSubmission = newUrl.getSubmission("", this.queryCharset);

    return (
      existingSubmission.uri.equals(newSubmission.uri) &&
      existingSubmission.postData?.data.data ==
        newSubmission.postData?.data.data
    );
  }

  /**
   * Overrides the urls/parameters with those of the provided engine or extension.
   * The url parameters are not saved to the search settings - the code handling
   * the extension should set these on every restart, this avoids potential
   * third party modifications and means that we can verify the WebExtension is
   * still in the allow list.
   *
   * @param {object} options
   *   The options for this function.
   * @param {AddonSearchEngine|OpenSearchEngine} [options.engine]
   *   The search engine to override with this engine. If not specified, `manifest`
   *   must be provided.
   * @param {object} [options.extension]
   *   An object representing the WebExtensions. If not specified,
   *   `engine` must be provided
   */
  overrideWithEngine({ engine, extension }) {
    this._overriddenData = {
      urls: this._urls,
      queryCharset: this._queryCharset,
    };
    if (engine) {
      // Copy any saved user data (alias, order etc).
      this.copyUserSettingsFrom(engine);

      this._urls = engine._urls;
      this.setAttr("overriddenBy", engine._extensionID ?? engine.id);
      if (engine instanceof lazy.OpenSearchEngine) {
        this.setAttr("overriddenByOpenSearch", engine.toJSON());
      }
    } else {
      this._urls = [];
      this.setAttr("overriddenBy", extension.id);
      this._setUrls(
        extension.manifest.chrome_settings_overrides.search_provider
      );
    }

    if (this.searchURLWithNoTerms.spec != this.getAttr("overriddenURL")) {
      this.setAttr("overriddenURL", this.searchURLWithNoTerms.spec, true);
    }
  }

  /**
   * Resets the overrides for the engine if it has been overridden.
   */
  removeExtensionOverride() {
    if (this.getAttr("overriddenBy")) {
      // If the attribute is set, but there is no data, skip it. Worst case,
      // the urls will be reset on a restart.
      if (this._overriddenData) {
        this._urls = this._overriddenData.urls;
        this._queryCharset = this._overriddenData.queryCharset;
        delete this._overriddenData;
      } else {
        lazy.logConsole.error(
          `${this._name} had overriddenBy set, but no _overriddenData`
        );
      }
      this.clearAttr("overriddenBy");
      this.clearAttr("overriddenURL");
      lazy.SearchUtils.notifyAction(
        this,
        lazy.SearchUtils.MODIFIED_TYPE.CHANGED
      );
    }
  }

  /**
   * Copies settings from the supplied search engine. Typically used for
   * restoring settings when removing an override.
   *
   * @param {SearchEngine|object} engine
   *   The engine to copy the settings from, or the engine settings from
   *   the user's saved settings.
   */
  copyUserSettingsFrom(engine) {
    for (let attribute of USER_ATTRIBUTES) {
      if (attribute in engine._metaData) {
        this._metaData[attribute] = engine._metaData[attribute];
      }
    }
  }

  /**
   * Init from a JSON record.
   *
   * @param {object} json
   *   The json record to use.
   */
  _initWithJSON(json) {
    this.#id = json.id ?? this.#id;
    this._name = json._name;
    this._queryCharset =
      json.queryCharset || lazy.SearchUtils.DEFAULT_QUERY_CHARSET;
    this._iconMapObj = json._iconMapObj || null;
    this._metaData = json._metaData || {};
    this._orderHint = json._orderHint || null;
    this._definedAliases = json._definedAliases || [];
    // These changed keys in Firefox 80, maintain the old keys
    // for backwards compatibility.
    if (json._definedAlias) {
      this._definedAliases.push(json._definedAlias);
    }
    this._filePath = json.filePath || json._filePath || null;

    for (let i = 0; i < json._urls.length; ++i) {
      let url = json._urls[i];
      let engineURL = new EngineURL(
        url.type || lazy.SearchUtils.URL_TYPE.SEARCH,
        url.method || "GET",
        url.template
      );
      engineURL._initWithJSON(url);
      this._urls.push(engineURL);
    }
  }

  /**
   * Creates a JavaScript object that represents this engine.
   *
   * @returns {object}
   *   An object suitable for serialization as JSON.
   */
  toJSON() {
    const fieldsToCopy = [
      "id",
      "_name",
      "_loadPath",
      "_iconMapObj",
      "_metaData",
      "_urls",
      "_orderHint",
      "_telemetryId",
      "_filePath",
      "_definedAliases",
    ];

    let json = {};
    for (const field of fieldsToCopy) {
      if (field in this) {
        json[field] = this[field];
      }
    }

    if (this.queryCharset != lazy.SearchUtils.DEFAULT_QUERY_CHARSET) {
      json.queryCharset = this.queryCharset;
    }

    return json;
  }

  setAttr(name, val, sendNotification = false) {
    // Cache whether the attribute actually changes so we don't lose that info
    // when updating `_metaData`.
    let hasChangedAttr = val != this[name];
    this._metaData[name] = val;
    if (hasChangedAttr && sendNotification) {
      lazy.SearchUtils.notifyAction(
        this,
        lazy.SearchUtils.MODIFIED_TYPE.CHANGED
      );
    }
  }

  getAttr(name) {
    return this._metaData[name] || undefined;
  }

  clearAttr(name) {
    delete this._metaData[name];
  }

  /**
   * @type {string}
   *   The partner code being used by this search engine in the Search URL.
   */
  get partnerCode() {
    return "";
  }

  /**
   * Loads engine settings (_metaData) from the list of settings, finding
   * the appropriate details for this engine.
   *
   * @param {object} [settings]
   *   The saved settings for the user.
   */
  _loadSettings(settings) {
    if (!settings) {
      return;
    }

    let engineSettings = lazy.SearchSettings.findSettingsForEngine(
      settings,
      this.id,
      this.name
    );
    if (engineSettings?._metaData) {
      this._metaData = structuredClone(engineSettings._metaData);
    }
  }

  /**
   * Gets the order hint for this engine. This is determined from the search
   * configuration when the engine is initialized.
   *
   * @type {number}
   */
  get orderHint() {
    return this._orderHint;
  }

  /**
   * Get the user-defined alias.
   *
   * @type {string}
   */
  get alias() {
    return this.getAttr("alias") || "";
  }

  set alias(val) {
    var value = val ? val.trim() : "";
    this.setAttr("alias", value, true);
  }

  /**
   * Returns a list of aliases, including a user defined alias and
   * a list defined by webextension keywords.
   *
   * @returns {string[]}
   */
  get aliases() {
    return [
      ...(this.getAttr("alias") ? [this.getAttr("alias")] : []),
      ...this._definedAliases,
    ];
  }

  /**
   * Returns the appropriate identifier to use for telemetry. It is based on
   * the following order:
   *
   * - telemetryId: The telemetry id from the configuration, or derived from
   *                the WebExtension name.
   * - other-<name>: The engine name prefixed by `other-` for non-app-provided
   *                 engines.
   *
   * @returns {string}
   */
  get telemetryId() {
    let telemetryId = this._telemetryId || `other-${this.name}`;
    if (this.getAttr("overriddenBy")) {
      return telemetryId + "-addon";
    }
    return telemetryId;
  }

  /**
   * Return the built-in identifier of app-provided engines.
   *
   * @returns {string|null}
   *   Returns a valid if this is a built-in engine, null otherwise.
   */
  get identifier() {
    // No identifier if If the engine isn't app-provided
    return this.isAppProvided ? this._telemetryId : null;
  }

  get hidden() {
    return this.getAttr("hidden") || false;
  }

  set hidden(val) {
    var value = !!val;
    this.setAttr("hidden", value, true);
  }

  get hideOneOffButton() {
    return this.getAttr("hideOneOffButton") || false;
  }
  set hideOneOffButton(val) {
    const value = !!val;
    this.setAttr("hideOneOffButton", value, true);
  }

  /**
   * Whether or not this engine is provided by the application, e.g. it is
   * in the list of configured search engines.
   *
   * @returns {boolean}
   *   This returns false for most engines, but may be overridden by particular
   *   engine types, such as add-on engines which are used by the application.
   */
  get isAppProvided() {
    return false;
  }

  /**
   * Whether or not this engine is an in-memory only search engine.
   * These engines are typically application provided or policy engines,
   * where they are loaded every time on SearchService initialization
   * using the policy JSON or the extension manifest. Minimal details of the
   * in-memory engines are saved to disk, but they are never loaded
   * from the user's saved settings file.
   *
   * @returns {boolean}
   *   This results false for most engines, but may be overridden by particular
   *   engine types, such as add-on engines and policy engines.
   */
  get inMemory() {
    return false;
  }

  /**
   * If this engine has been overridden by a third-party engine, the id returned
   * will be the engine it was overriden by. Otherwise this will return null.
   *
   * @returns {?string}
   */
  get overriddenById() {
    return this.getAttr("overriddenBy");
  }

  get isGeneralPurposeEngine() {
    return false;
  }

  get _hasUpdates() {
    return false;
  }

  get name() {
    return this._name;
  }

  /**
   * Anonymized path of where we initially loaded the engine from.
   */
  get loadPath() {
    return this._loadPath;
  }

  get queryCharset() {
    return this._queryCharset || lazy.SearchUtils.DEFAULT_QUERY_CHARSET;
  }

  /**
   * Gets an object that contains information about what to send to the search
   * engine, for a request. This will be a URI and may also include data for POST
   * requests.
   *
   * @param {string} searchTerms
   *   The search term(s) for the submission.
   * @param {lazy.SearchUtils.URL_TYPE} [responseType]
   *   The MIME type that we'd like to receive in response
   *   to this submission.  If null, will default to "text/html".
   * @returns {nsISearchSubmission|null}
   *   The submission data. If no appropriate submission can be determined for
   *   the request type, this may be null.
   */
  getSubmission(searchTerms, responseType) {
    // We can't use a default parameter as that doesn't work correctly with
    // the idl interfaces.
    if (!responseType) {
      responseType = lazy.SearchUtils.URL_TYPE.SEARCH;
    }

    var url = this._getURLOfType(responseType);

    if (!url) {
      return null;
    }

    if (
      !searchTerms &&
      (responseType == lazy.SearchUtils.URL_TYPE.SEARCH ||
        responseType == lazy.SearchUtils.URL_TYPE.SUGGEST_JSON)
    ) {
      lazy.logConsole.warn("getSubmission: searchTerms is empty!");
    }

    return url.getSubmission(searchTerms, this.queryCharset);
  }

  /**
   * Returns a search URL with no search terms. This is typically used for
   * purposes where we want to check something on the URL, but not use it for
   * an actual submission to the search engine.
   *
   * @returns {nsIURI}
   */
  get searchURLWithNoTerms() {
    return this._getURLOfType(lazy.SearchUtils.URL_TYPE.SEARCH).getSubmission(
      "",
      this.queryCharset
    ).uri;
  }

  /**
   * Returns the search term of a possible search result URI if and only if:
   * - The URI has the same scheme, host, and path as the engine.
   * - All query parameters of the URI have a matching name and value in the engine.
   * - An exception to the equality check is the engine's termsParameterName
   *   value, which contains a placeholder, i.e. {searchTerms}.
   * - If an engine has query parameters with "null" values, they will be ignored.
   *
   * @param {nsIURI} uri
   *   A URI that may or may not be from a search result matching the engine.
   *
   * @returns {string}
   *   A string representing the termsParameterName value of the URI,
   *   or an empty string if the URI isn't matched to the engine.
   */
  searchTermFromResult(uri) {
    let url = this._getURLOfType(lazy.SearchUtils.URL_TYPE.SEARCH);
    if (!url) {
      return "";
    }

    // To avoid unnecessarily comparing search parameters, start by ensuring
    // that the origin and path of both URLs are identical.
    // Note that URIs encode the path as percent encoded characters, while the
    // path of the URL from search config is not percent encoded. Thus, we
    // convert both strings into URL objects to ensure consistent comparisons.
    let url1 = new URL(url.template);
    let url2 = URL.fromURI(uri);
    if (url1.origin != url2.origin || url1.pathname != url2.pathname) {
      return "";
    }

    let engineParams;
    if (url.params.length) {
      engineParams = new URLSearchParams();
      for (let { name, value } of url.params) {
        // Some values might be null, so avoid adding
        // them since the input is unlikely to have it too.
        if (value) {
          // Use append() rather than set() so multiple
          // values of the same name can be stored.
          engineParams.append(name, value);
        }
      }
    } else {
      // Try checking the template for the presence of query params.
      engineParams = url1.searchParams;
    }

    let uriParams = url2.searchParams;
    if (
      new Set([...uriParams.keys()]).size !=
      new Set([...engineParams.keys()]).size
    ) {
      return "";
    }

    let termsParameterName = this.searchUrlQueryParamName;

    for (let [name, value] of uriParams.entries()) {
      // Don't check the name matching the search
      // query because its value will differ.
      if (name == termsParameterName) {
        continue;
      }
      // All params of an input must have a matching
      // key and value in the list of engine parameters.
      if (!engineParams.getAll(name).includes(value)) {
        return "";
      }
    }

    // An engine can use a non UTF-8 charset, which URLSearchParams
    // might not parse properly. Convert the terms parameter value
    // from the original input using the appropriate charset.
    if (this.queryCharset.toLowerCase() != "utf-8") {
      let name = `${termsParameterName}=`;
      let queryString = uri.query
        .split("&")
        .filter(str => str.startsWith(name))
        .pop();
      return Services.textToSubURI.UnEscapeAndConvert(
        this.queryCharset,
        queryString.substring(queryString.indexOf("=") + 1).replace(/\+/g, " ")
      );
    }

    return uriParams.get(termsParameterName) ?? "";
  }

  get searchUrlQueryParamName() {
    return (
      this._getURLOfType(lazy.SearchUtils.URL_TYPE.SEARCH)
        .searchTermParamName || ""
    );
  }

  get searchUrlPublicSuffix() {
    if (this._searchUrlPublicSuffix != null) {
      return this._searchUrlPublicSuffix;
    }
    let searchURLPublicSuffix = Services.eTLD.getKnownPublicSuffix(
      this.searchURLWithNoTerms
    );
    return (this._searchUrlPublicSuffix = searchURLPublicSuffix);
  }

  // from nsISearchEngine
  supportsResponseType(type) {
    return this._getURLOfType(type) != null;
  }

  // from nsISearchEngine
  get searchUrlDomain() {
    let url = this._getURLOfType(lazy.SearchUtils.URL_TYPE.SEARCH);
    if (url) {
      return url.templateHost;
    }
    return "";
  }

  /**
   * @returns {string}
   *   URL to the main page of the search engine.
   *   Uses the first URL of type SEARCH_FORM or the pre path
   *   of the search URL as a fallback if no such URL exists.
   */
  get searchForm() {
    let url = this._getURLOfType(lazy.SearchUtils.URL_TYPE.SEARCH_FORM);
    if (url) {
      return url.getSubmission("", this.queryCharset).uri.spec;
    }
    return this.searchURLWithNoTerms.prePath;
  }

  /**
   * @returns {object}
   *   URL parsing properties used by _buildParseSubmissionMap.
   */
  getURLParsingInfo() {
    let url = this._getURLOfType(lazy.SearchUtils.URL_TYPE.SEARCH);
    if (!url || url.method != "GET") {
      return null;
    }

    let termsParameterName = url.searchTermParamName;
    if (!termsParameterName) {
      return null;
    }

    let templateUrl = Services.io.newURI(url.template);
    return {
      mainDomain: templateUrl.host,
      path: templateUrl.filePath.toLowerCase(),
      termsParameterName,
    };
  }

  get wrappedJSObject() {
    return this;
  }

  /**
   * Returns the icon URL for the search engine closest to the preferred width
   * or undefined if the engine has no icons.
   *
   * @param {number} preferredWidth
   *   Width of the requested icon. If not specified, it is assumed that
   *   16x16 is desired.
   * @returns {Promise<string|undefined>}
   */
  async getIconURL(preferredWidth) {
    // XPCOM interfaces pass optional number parameters as 0.
    preferredWidth ||= 16;

    if (!this._iconMapObj) {
      return undefined;
    }

    let availableWidths = Object.keys(this._iconMapObj).map(k => parseInt(k));
    if (!availableWidths.length) {
      return undefined;
    }

    let bestWidth = lazy.SearchUtils.chooseIconSize(
      preferredWidth,
      availableWidths
    );
    return this._iconMapObj[bestWidth];
  }

  /**
   * Opens a speculative connection to the engine's search URI
   * (and suggest URI, if different) to reduce request latency
   *
   * @param {object} options
   *   The options object
   * @param {Window} options.window
   *   The content window for the window performing the search.
   * @param {object} options.originAttributes
   *   The originAttributes for performing the search
   * @throws NS_ERROR_INVALID_ARG if options is omitted or lacks required
   *         elements
   */
  speculativeConnect(options) {
    if (!options || !options.window) {
      console.error(
        "invalid options arg passed to nsISearchEngine.speculativeConnect"
      );
      throw Components.Exception("", Cr.NS_ERROR_INVALID_ARG);
    }
    let connector = Services.io.QueryInterface(Ci.nsISpeculativeConnect);

    let searchURI = this.searchURLWithNoTerms;

    let callbacks = options.window.docShell.QueryInterface(Ci.nsILoadContext);

    // Using the content principal which is constructed by the search URI
    // and given originAttributes. If originAttributes are not given, we
    // fallback to use the docShell's originAttributes.
    let attrs = options.originAttributes;

    if (!attrs) {
      attrs = options.window.docShell.getOriginAttributes();
    }

    let principal = Services.scriptSecurityManager.createContentPrincipal(
      searchURI,
      attrs
    );

    try {
      connector.speculativeConnect(searchURI, principal, callbacks, false);
    } catch (e) {
      // Can't setup speculative connection for this url, just ignore it.
      console.error(e);
    }

    if (this.supportsResponseType(lazy.SearchUtils.URL_TYPE.SUGGEST_JSON)) {
      let suggestURI = this.getSubmission(
        "dummy",
        lazy.SearchUtils.URL_TYPE.SUGGEST_JSON
      ).uri;
      if (suggestURI.prePath != searchURI.prePath) {
        try {
          connector.speculativeConnect(suggestURI, principal, callbacks, false);
        } catch (e) {
          // Can't setup speculative connection for this url, just ignore it.
          console.error(e);
        }
      }
    }
  }

  get id() {
    return this.#id;
  }

  /**
   * Generates an UUID.
   *
   * @returns {string}
   *   An UUID string, without leading or trailing braces.
   */
  #uuid() {
    let uuid = Services.uuid.generateUUID().toString();
    return uuid.slice(1, uuid.length - 1);
  }
}

/**
 * @implements {nsISearchSubmission}.
 */
class Submission {
  QueryInterface = ChromeUtils.generateQI(["nsISearchSubmission"]);

  constructor(uri, postData = null) {
    this._uri = uri;
    this._postData = postData;
  }

  get uri() {
    return this._uri;
  }
  get postData() {
    return this._postData;
  }
}
