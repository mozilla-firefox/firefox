/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  DownloadIntegration:
    "resource://gre/modules/DownloadIntegration.sys.mjs",
  DownloadLastDir: "resource://gre/modules/DownloadLastDir.sys.mjs",
  DownloadPaths: "resource://gre/modules/DownloadPaths.sys.mjs",
  Downloads: "resource://gre/modules/Downloads.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
});

var { EventEmitter } = ExtensionCommon;
var { ExtensionError } = ExtensionUtils;

const DOWNLOAD_ITEM_FIELDS = [
  "id",
  "url",
  "referrer",
  "filename",
  "incognito",
  "cookieStoreId",
  "danger",
  "mime",
  "startTime",
  "endTime",
  "estimatedEndTime",
  "state",
  "paused",
  "canResume",
  "error",
  "bytesReceived",
  "totalBytes",
  "fileSize",
  "exists",
  "byExtensionId",
  "byExtensionName",
];

const DOWNLOAD_DATE_FIELDS = ["startTime", "endTime", "estimatedEndTime"];

// Fields that we generate onChanged events for.
const DOWNLOAD_ITEM_CHANGE_FIELDS = [
  "endTime",
  "state",
  "paused",
  "canResume",
  "error",
  "exists",
];

// From https://fetch.spec.whatwg.org/#forbidden-header-name
// Since bug 1367626 we allow extensions to set REFERER.
const FORBIDDEN_HEADERS = [
  "ACCEPT-CHARSET",
  "ACCEPT-ENCODING",
  "ACCESS-CONTROL-REQUEST-HEADERS",
  "ACCESS-CONTROL-REQUEST-METHOD",
  "CONNECTION",
  "CONTENT-LENGTH",
  "COOKIE",
  "COOKIE2",
  "DATE",
  "DNT",
  "EXPECT",
  "HOST",
  "KEEP-ALIVE",
  "ORIGIN",
  "TE",
  "TRAILER",
  "TRANSFER-ENCODING",
  "UPGRADE",
  "VIA",
];

const FORBIDDEN_PREFIXES = /^PROXY-|^SEC-/i;

const PROMPTLESS_DOWNLOAD_PREF = "browser.download.useDownloadDir";

// Lists of file extensions for each file picker filter taken from filepicker.properties
const FILTER_HTML_EXTENSIONS = ["html", "htm", "shtml", "xhtml"];

const FILTER_TEXT_EXTENSIONS = ["txt", "text"];

const FILTER_IMAGES_EXTENSIONS = [
  "jpe",
  "jpg",
  "jpeg",
  "gif",
  "png",
  "bmp",
  "ico",
  "svg",
  "svgz",
  "tif",
  "tiff",
  "ai",
  "drw",
  "pct",
  "psp",
  "xcf",
  "psd",
  "raw",
  "webp",
  "heic",
  "avif",
  "jxl",
];

const FILTER_XML_EXTENSIONS = ["xml"];

const FILTER_AUDIO_EXTENSIONS = [
  "aac",
  "aif",
  "flac",
  "iff",
  "m4a",
  "m4b",
  "mid",
  "midi",
  "mp3",
  "mpa",
  "mpc",
  "oga",
  "ogg",
  "ra",
  "ram",
  "snd",
  "wav",
  "wma",
];

const FILTER_VIDEO_EXTENSIONS = [
  "avi",
  "divx",
  "flv",
  "m4v",
  "mkv",
  "mov",
  "mp4",
  "mpeg",
  "mpg",
  "ogm",
  "ogv",
  "ogx",
  "rm",
  "rmvb",
  "smil",
  "webm",
  "wmv",
  "xvid",
];

class DownloadItem {
  constructor(id, download, extension) {
    this.id = id;
    this.download = download;
    this.extension = extension;
    this.prechange = {};
    this._error = null;
  }

  get url() {
    return this.download.source.url;
  }

  get referrer() {
    const uri = this.download.source.referrerInfo?.originalReferrer;

    return uri?.spec;
  }

  get filename() {
    return this.download.target.path;
  }

  get incognito() {
    return this.download.source.isPrivate;
  }

  get cookieStoreId() {
    if (this.download.source.isPrivate) {
      return PRIVATE_STORE;
    }
    if (this.download.source.userContextId) {
      return getCookieStoreIdForContainer(this.download.source.userContextId);
    }
    return DEFAULT_STORE;
  }

  get danger() {
    // TODO
    return "safe";
  }

  get mime() {
    return this.download.contentType;
  }

  get startTime() {
    return this.download.startTime;
  }

  get endTime() {
    // TODO bug 1256269: implement endTime.
    return null;
  }

  get estimatedEndTime() {
    // Based on the code in summarizeDownloads() in DownloadsCommon.sys.mjs
    if (this.download.hasProgress && this.download.speed > 0) {
      let sizeLeft = this.download.totalBytes - this.download.currentBytes;
      let timeLeftInSeconds = sizeLeft / this.download.speed;
      return new Date(Date.now() + timeLeftInSeconds * 1000);
    }
    return undefined;
  }

  get state() {
    if (this.download.succeeded) {
      return "complete";
    }
    if (this.download.canceled || this.error) {
      return "interrupted";
    }
    return "in_progress";
  }

  get paused() {
    return (
      this.download.canceled &&
      this.download.hasPartialData &&
      !this.download.error
    );
  }

  get canResume() {
    return (
      (this.download.stopped || this.download.canceled) &&
      this.download.hasPartialData &&
      (!this.download.error || this.download.error.becauseSourceFailed)
    );
  }

  get error() {
    if (this._error) {
      return this._error;
    }
    if (
      !this.download.startTime ||
      !this.download.stopped ||
      this.download.succeeded
    ) {
      return null;
    }

    // TODO store this instead of calculating it
    if (this.download.error) {
      if (this.download.error.becauseSourceFailed) {
        return "NETWORK_FAILED"; // TODO
      }
      if (this.download.error.becauseTargetFailed) {
        return "FILE_FAILED"; // TODO
      }
      return "CRASH";
    }
    return "USER_CANCELED";
  }

  set error(value) {
    this._error = value && value.toString();
  }

  get bytesReceived() {
    return this.download.currentBytes;
  }

  get totalBytes() {
    return this.download.hasProgress ? this.download.totalBytes : -1;
  }

  get fileSize() {
    // todo: this is supposed to be post-compression
    return this.download.succeeded ? this.download.target.size : -1;
  }

  get exists() {
    return this.download.target.exists;
  }

  get byExtensionId() {
    return this.extension?.id;
  }

  get byExtensionName() {
    return this.extension?.name;
  }

  /**
   * Create a cloneable version of this object by pulling all the
   * fields into simple properties (instead of getters).
   *
   * @returns {object} A DownloadItem with flat properties,
   *                   suitable for cloning.
   */
  serialize() {
    let obj = {};
    for (let field of DOWNLOAD_ITEM_FIELDS) {
      obj[field] = this[field];
    }
    for (let field of DOWNLOAD_DATE_FIELDS) {
      if (obj[field]) {
        obj[field] = obj[field].toISOString();
      }
    }
    return obj;
  }

  // When a change event fires, handlers can look at how an individual
  // field changed by comparing item.fieldname with item.prechange.fieldname.
  // After all handlers have been invoked, this gets called to store the
  // current values of all fields ahead of the next event.
  _storePrechange() {
    for (let field of DOWNLOAD_ITEM_CHANGE_FIELDS) {
      this.prechange[field] = this[field];
    }
  }
}

// DownloadMap maps back and forth between the numeric identifiers used in
// the downloads WebExtension API and a Download object from the Downloads sys.mjs.
// TODO Bug 1247794: make id and extension info persistent
const DownloadMap = new (class extends EventEmitter {
  constructor() {
    super();

    this.currentId = 0;
    this.loadPromise = null;

    // Maps numeric id -> DownloadItem
    this.byId = new Map();

    // Maps Download object -> DownloadItem
    this.byDownload = new WeakMap();
  }

  lazyInit() {
    if (!this.loadPromise) {
      this.loadPromise = (async () => {
        const list = await Downloads.getList(Downloads.ALL);

        list.addView({
          onDownloadAdded: download => {
            const item = this.newFromDownload(download, null);
            this.emit("create", item);
            item._storePrechange();
          },
          onDownloadRemoved: download => {
            const item = this.byDownload.get(download);
            if (item) {
              this.emit("erase", item);
              this.byDownload.delete(download);
              this.byId.delete(item.id);
            }
          },
          onDownloadChanged: download => {
            const item = this.byDownload.get(download);
            if (item) {
              this.emit("change", item);
              item._storePrechange();
            } else {
              Cu.reportError(
                "Got onDownloadChanged for unknown download object"
              );
            }
          },
        });

        const downloads = await list.getAll();

        for (let download of downloads) {
          this.newFromDownload(download, null);
        }

        return list;
      })();
    }

    return this.loadPromise;
  }

  getDownloadList() {
    return this.lazyInit();
  }

  async getAll() {
    await this.lazyInit();
    return this.byId.values();
  }

  fromId(id, privateAllowed = true) {
    const download = this.byId.get(id);
    if (!download || (!privateAllowed && download.incognito)) {
      throw new ExtensionError(`Invalid download id ${id}`);
    }
    return download;
  }

  newFromDownload(download, extension) {
    if (this.byDownload.has(download)) {
      return this.byDownload.get(download);
    }

    const id = ++this.currentId;
    let item = new DownloadItem(id, download, extension);
    this.byId.set(id, item);
    this.byDownload.set(download, item);
    return item;
  }

  async erase(item) {
    // TODO Bug 1255507: for now we only work with downloads in the DownloadList
    // from getAll()
    const list = await this.getDownloadList();
    list.remove(item.download);
  }
})();

// Create a callable function that filters a DownloadItem based on a
// query object of the type passed to search() or erase().
const downloadQuery = query => {
  let queryTerms = [];
  let queryNegativeTerms = [];
  if (query.query != null) {
    for (let term of query.query) {
      if (term[0] == "-") {
        queryNegativeTerms.push(term.slice(1).toLowerCase());
      } else {
        queryTerms.push(term.toLowerCase());
      }
    }
  }

  function normalizeDownloadTime(arg, before) {
    if (arg == null) {
      return before ? Number.MAX_VALUE : 0;
    }
    return ExtensionCommon.normalizeTime(arg).getTime();
  }

  const startedBefore = normalizeDownloadTime(query.startedBefore, true);
  const startedAfter = normalizeDownloadTime(query.startedAfter, false);

  // TODO bug 1727510: Implement endedBefore/endedAfter
  // const endedBefore = normalizeDownloadTime(query.endedBefore, true);
  // const endedAfter = normalizeDownloadTime(query.endedAfter, false);

  const totalBytesGreater = query.totalBytesGreater ?? -1;
  const totalBytesLess = query.totalBytesLess ?? Number.MAX_VALUE;

  // Handle options for which we can have a regular expression and/or
  // an explicit value to match.
  function makeMatch(regex, value, field) {
    if (value == null && regex == null) {
      return () => true;
    }

    let re;
    try {
      re = new RegExp(regex || "", "i");
    } catch (err) {
      throw new ExtensionError(`Invalid ${field}Regex: ${err.message}`);
    }
    if (value == null) {
      return input => re.test(input);
    }

    value = value.toLowerCase();
    if (re.test(value)) {
      return input => value == input;
    }
    return () => false;
  }

  const matchFilename = makeMatch(
    query.filenameRegex,
    query.filename,
    "filename"
  );
  const matchUrl = makeMatch(query.urlRegex, query.url, "url");

  return function (item) {
    const url = item.url.toLowerCase();
    const filename = item.filename.toLowerCase();

    if (
      !queryTerms.every(term => url.includes(term) || filename.includes(term))
    ) {
      return false;
    }

    if (
      queryNegativeTerms.some(
        term => url.includes(term) || filename.includes(term)
      )
    ) {
      return false;
    }

    if (!matchFilename(filename) || !matchUrl(url)) {
      return false;
    }

    if (!item.startTime) {
      if (query.startedBefore != null || query.startedAfter != null) {
        return false;
      }
    } else if (
      item.startTime > startedBefore ||
      item.startTime < startedAfter
    ) {
      return false;
    }

    // todo endedBefore, endedAfter

    if (item.totalBytes == -1) {
      if (query.totalBytesGreater !== null || query.totalBytesLess !== null) {
        return false;
      }
    } else if (
      item.totalBytes <= totalBytesGreater ||
      item.totalBytes >= totalBytesLess
    ) {
      return false;
    }

    // todo: include danger
    const SIMPLE_ITEMS = [
      "id",
      "mime",
      "startTime",
      "endTime",
      "state",
      "paused",
      "error",
      "incognito",
      "cookieStoreId",
      "bytesReceived",
      "totalBytes",
      "fileSize",
      "exists",
    ];
    for (let field of SIMPLE_ITEMS) {
      if (query[field] != null && item[field] != query[field]) {
        return false;
      }
    }

    return true;
  };
};

const queryHelper = async query => {
  let matchFn = downloadQuery(query);
  let compareFn;

  if (query.orderBy) {
    const fields = query.orderBy.map(field =>
      field[0] == "-"
        ? { reverse: true, name: field.slice(1) }
        : { reverse: false, name: field }
    );

    for (let field of fields) {
      if (!DOWNLOAD_ITEM_FIELDS.includes(field.name)) {
        throw new ExtensionError(`Invalid orderBy field ${field.name}`);
      }
    }

    compareFn = (dl1, dl2) => {
      for (let field of fields) {
        const val1 = dl1[field.name];
        const val2 = dl2[field.name];

        if (val1 < val2) {
          return field.reverse ? 1 : -1;
        } else if (val1 > val2) {
          return field.reverse ? -1 : 1;
        }
      }
      return 0;
    };
  }

  let downloads = await DownloadMap.getAll();

  if (compareFn) {
    downloads = Array.from(downloads);
    downloads.sort(compareFn);
  }

  let results = [];
  for (let download of downloads) {
    if (query.limit && results.length >= query.limit) {
      break;
    }
    if (matchFn(download)) {
      results.push(download);
    }
  }
  return results;
};

async function applyFilenameSuggestion(suggestion, currentPath) {
  let { filename: suggestedFilename, conflictAction } = suggestion;
  if (!suggestedFilename) {
    return null;
  }

  if (PathUtils.isAbsolute(suggestedFilename)) {
    Cu.reportError(
      "downloads.onDeterminingFilename: suggested filename must not be an absolute path"
    );
    return null;
  }

  if (AppConstants.platform === "win") {
    suggestedFilename = suggestedFilename.replace(/\//g, "\\");
  }

  let pathComponents;
  try {
    pathComponents = PathUtils.splitRelative(suggestedFilename, {
      allowEmpty: true,
      allowCurrentDir: true,
      allowParentDir: true,
    });
  } catch (e) {
    Cu.reportError(
      `downloads.onDeterminingFilename: invalid suggested filename: ${e}`
    );
    return null;
  }

  if (pathComponents.some(component => component == "..")) {
    Cu.reportError(
      "downloads.onDeterminingFilename: suggested filename must not contain back-references (..)"
    );
    return null;
  }

  const dir = PathUtils.parent(currentPath);
  const newPath = PathUtils.joinRelative(dir, suggestedFilename);
  await IOUtils.makeDirectory(PathUtils.parent(newPath));

  const resolvedConflictAction = conflictAction || "uniquify";

  if (await IOUtils.exists(newPath)) {
    if (resolvedConflictAction === "uniquify") {
      return DownloadPaths.createNiceUniqueFile(new FileUtils.File(newPath))
        .path;
    }
    // "overwrite" — use the path as-is
  }

  return newPath;
}

this.downloads = class extends ExtensionAPIPersistent {
  downloadEventRegistrar(event, listener) {
    let { extension } = this;
    return ({ fire }) => {
      const handler = (what, item) => {
        if (extension.privateBrowsingAllowed || !item.incognito) {
          listener(fire, what, item);
        }
      };
      let registerPromise = DownloadMap.getDownloadList().then(() => {
        DownloadMap.on(event, handler);
      });
      return {
        unregister() {
          registerPromise.then(() => {
            DownloadMap.off(event, handler);
          });
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    };
  }

  PERSISTENT_EVENTS = {
    onChanged: this.downloadEventRegistrar("change", (fire, what, item) => {
      let changes = {};
      const noundef = val => (val === undefined ? null : val);
      DOWNLOAD_ITEM_CHANGE_FIELDS.forEach(fld => {
        if (item[fld] != item.prechange[fld]) {
          changes[fld] = {
            previous: noundef(item.prechange[fld]),
            current: noundef(item[fld]),
          };
        }
      });
      if (Object.keys(changes).length) {
        changes.id = item.id;
        fire.async(changes);
      }
    }),

    onCreated: this.downloadEventRegistrar("create", (fire, what, item) => {
      fire.async(item.serialize());
    }),

    onErased: this.downloadEventRegistrar("erase", (fire, what, item) => {
      fire.async(item.id);
    }),
  };

  getAPI(context) {
    let { extension } = context;
    return {
      downloads: {
        async download(options) {
          const isHandlingUserInput =
            context.callContextData?.isHandlingUserInput;
          let { filename } = options;
          if (filename && AppConstants.platform === "win") {
            // cross platform javascript code uses "/"
            filename = filename.replace(/\//g, "\\");
          }

          if (filename != null) {
            if (!filename.length) {
              throw new ExtensionError("filename must not be empty");
            }

            if (PathUtils.isAbsolute(filename)) {
              throw new ExtensionError("filename must not be an absolute path");
            }

            // % is not permitted but relatively common.
            filename = filename.replaceAll("%", "_");

            const pathComponents = PathUtils.splitRelative(filename, {
              allowEmpty: true,
              allowCurrentDir: true,
              allowParentDir: true,
            });

            if (pathComponents.some(component => component == "..")) {
              throw new ExtensionError(
                "filename must not contain back-references (..)"
              );
            }

            if (
              pathComponents.some((component, i) => {
                let sanitized = DownloadPaths.sanitize(component, {
                  compressWhitespaces: false,
                  allowDirectoryNames: i < pathComponents.length - 1,
                });
                return component != sanitized;
              })
            ) {
              throw new ExtensionError(
                "filename must not contain illegal characters"
              );
            }
          }

          if (options.incognito && !context.privateBrowsingAllowed) {
            throw new ExtensionError("private browsing access not allowed");
          }

          if (options.conflictAction == "prompt") {
            // TODO
            throw new ExtensionError(
              "conflictAction prompt not yet implemented"
            );
          }

          if (options.headers) {
            for (let { name } of options.headers) {
              if (
                FORBIDDEN_HEADERS.includes(name.toUpperCase()) ||
                name.match(FORBIDDEN_PREFIXES)
              ) {
                throw new ExtensionError("Forbidden request header name");
              }
            }
          }

          let userContextId = null;
          if (options.cookieStoreId != null) {
            userContextId = getUserContextIdForCookieStoreId(
              extension,
              options.cookieStoreId,
              options.incognito
            );
          }

          // Handle method, headers and body options.
          function adjustChannel(channel) {
            if (channel instanceof Ci.nsIHttpChannel) {
              const method = options.method || "GET";
              channel.requestMethod = method;

              if (options.headers) {
                for (let { name, value } of options.headers) {
                  if (name.toLowerCase() == "referer") {
                    // The referer header and referrerInfo object should always
                    // match. So if we want to set the header from privileged
                    // context, we should set referrerInfo. The referrer header
                    // will get set internally.
                    channel.setNewReferrerInfo(
                      value,
                      Ci.nsIReferrerInfo.UNSAFE_URL,
                      true
                    );
                  } else {
                    channel.setRequestHeader(name, value, false);
                  }
                }
              }

              if (options.body != null) {
                const stream = Cc[
                  "@mozilla.org/io/string-input-stream;1"
                ].createInstance(Ci.nsIStringInputStream);
                stream.setByteStringData(options.body);

                channel.QueryInterface(Ci.nsIUploadChannel2);
                channel.explicitSetUploadStream(
                  stream,
                  null,
                  -1,
                  method,
                  false
                );
              }
            }
            return Promise.resolve();
          }

          function allowHttpStatus(download, status) {
            const item = DownloadMap.byDownload.get(download);
            if (item === null) {
              return true;
            }

            let error = null;
            switch (status) {
              case 204: // No Content
              case 205: // Reset Content
              case 404: // Not Found
                error = "SERVER_BAD_CONTENT";
                break;

              case 403: // Forbidden
                error = "SERVER_FORBIDDEN";
                break;

              case 402: // Unauthorized
              case 407: // Proxy authentication required
                error = "SERVER_UNAUTHORIZED";
                break;

              default:
                if (status >= 400) {
                  error = "SERVER_FAILED";
                }
                break;
            }

            if (error) {
              item.error = error;
              return false;
            }

            // No error, ergo allow the request.
            return true;
          }

          async function createTarget() {
            if (!filename) {
              let uri = Services.io.newURI(options.url);
              if (uri instanceof Ci.nsIURL) {
                filename = DownloadPaths.sanitize(
                  Services.textToSubURI.unEscapeURIForUI(
                    uri.fileName,
                    /* dontEscape = */ true
                  )
                );
              }
            }

            let target = PathUtils.joinRelative(
              await Downloads.getPreferredDownloadsDirectory(),
              filename || "download"
            );

            let saveAs;
            if (options.saveAs !== null) {
              saveAs = options.saveAs;
            } else {
              // If options.saveAs was not specified, only show the file chooser
              // if |browser.download.useDownloadDir == false|. That is to say,
              // only show the file chooser if Firefox normally shows it when
              // a file is downloaded.
              saveAs = !Services.prefs.getBoolPref(
                PROMPTLESS_DOWNLOAD_PREF,
                true
              );
            }

            // Create any needed subdirectories if required by filename.
            const dir = PathUtils.parent(target);
            await IOUtils.makeDirectory(dir);

            if (await IOUtils.exists(target)) {
              // This has a race, something else could come along and create
              // the file between this test and them time the download code
              // creates the target file.  But we can't easily fix it without
              // modifying DownloadCore so we live with it for now.
              switch (options.conflictAction) {
                case "uniquify":
                default:
                  target = DownloadPaths.createNiceUniqueFile(
                    new FileUtils.File(target)
                  ).path;
                  if (saveAs) {
                    // createNiceUniqueFile actually creates the file, which
                    // is premature if we need to show a SaveAs dialog.
                    await IOUtils.remove(target);
                  }
                  break;

                case "overwrite":
                  break;
              }
            }

            if (!saveAs || AppConstants.platform === "android") {
              return target;
            }

            if (!("windowTracker" in global)) {
              return target;
            }

            // At this point we are committed to displaying the file picker.
            const downloadLastDir = new DownloadLastDir(
              null,
              options.incognito
            );

            async function getLastDirectory() {
              return downloadLastDir.getFileAsync(extension.baseURI);
            }

            function appendFilterForFileExtension(picker, ext) {
              if (FILTER_HTML_EXTENSIONS.includes(ext)) {
                picker.appendFilters(Ci.nsIFilePicker.filterHTML);
              } else if (FILTER_TEXT_EXTENSIONS.includes(ext)) {
                picker.appendFilters(Ci.nsIFilePicker.filterText);
              } else if (FILTER_IMAGES_EXTENSIONS.includes(ext)) {
                picker.appendFilters(Ci.nsIFilePicker.filterImages);
              } else if (FILTER_XML_EXTENSIONS.includes(ext)) {
                picker.appendFilters(Ci.nsIFilePicker.filterXML);
              } else if (FILTER_AUDIO_EXTENSIONS.includes(ext)) {
                picker.appendFilters(Ci.nsIFilePicker.filterAudio);
              } else if (FILTER_VIDEO_EXTENSIONS.includes(ext)) {
                picker.appendFilters(Ci.nsIFilePicker.filterVideo);
              }
            }

            function saveLastDirectory(lastDir) {
              downloadLastDir.setFile(extension.baseURI, lastDir);
            }

            const basename = PathUtils.filename(target);
            const ext = basename.match(/\.([^.]+)$/)?.[1];

            // If the filename passed in by the extension is a simple name
            // and not a path, we open the file picker so it displays the
            // last directory that was chosen by the user.
            const pathSep = AppConstants.platform === "win" ? "\\" : "/";
            const lastFilePickerDirectory =
              !filename || !filename.includes(pathSep)
                ? await getLastDirectory()
                : undefined;

            // Setup the file picker Save As dialog.
            const picker = Cc["@mozilla.org/filepicker;1"].createInstance(
              Ci.nsIFilePicker
            );
            // The picker requires a BrowsingContext to associate DOM File
            // objects. Since this caller is a system principal, and the file
            // saving operation should outlast the extension context, we use an
            // arbitrary chrome window's browsingContext.
            // We cannot use the extension context's browsingContext, because
            // its canOpenModalPicker may be false for non-foreground tabs,
            // which would cause the modal to be blocked.
            picker.init(
              context.browsingContext.topChromeWindow.browsingContext,
              null,
              Ci.nsIFilePicker.modeSave
            );
            if (lastFilePickerDirectory) {
              picker.displayDirectory = lastFilePickerDirectory;
            } else {
              picker.displayDirectory = new FileUtils.File(dir);
            }
            picker.defaultString = basename;
            if (ext) {
              // Configure a default file extension, used as fallback on Windows.
              picker.defaultExtension = ext;
              appendFilterForFileExtension(picker, ext);
            }
            picker.appendFilters(Ci.nsIFilePicker.filterAll);

            // Open the dialog and resolve/reject with the result.
            return new Promise((resolve, reject) => {
              picker.open(result => {
                if (result === Ci.nsIFilePicker.returnCancel) {
                  reject({ message: "Download canceled by the user" });
                } else {
                  saveLastDirectory(picker.file.parent);
                  resolve(picker.file.path);
                }
              });
            });
          }

          const targetPromise = createTarget();
          if (context.isBackgroundContext) {
            // Prevent event page suspension, so extensions can be notified of
            // the download ID for the download they just initiated.
            // This work-around is also needed for blob:-URLs, at least until
            // blob:-URLs can outlive an extension context (bug 2005952).
            //
            // Note: Although there may be other async code before and after
            // this point, they are not captured by this waitUntil logic,
            // because these async calls are not using an unbounded amount of
            // time. The resolution of targetPromise can take an unbounded
            // amount of time if we wait for user input in the file picker.
            extension.emit("background-script-idle-waituntil", {
              promise: targetPromise,
              reason: "downloads_saveAs",
            });
          }
          const target = await targetPromise;
          const uri = Services.io.newURI(options.url);
          const cookieJarSettings = Cc[
            "@mozilla.org/cookieJarSettings;1"
          ].createInstance(Ci.nsICookieJarSettings);
          cookieJarSettings.initWithURI(uri, options.incognito);

          const source = {
            url: options.url,
            isPrivate: options.incognito,
            // Use the extension's principal to allow extensions to observe
            // their own downloads via the webRequest API.
            loadingPrincipal: context.principal,
            cookieJarSettings,
          };

          if (userContextId) {
            source.userContextId = userContextId;
          }

          // blob:-URLs can only be loaded by the principal with which they
          // are associated. This principal may have origin attributes.
          // `context.principal` does sometimes not have these attributes
          // due to bug 1653681. If `context.principal` were to be passed,
          // the download request would be rejected because of mismatching
          // principals (origin attributes).
          // TODO bug 1653681: fix context.principal and remove this.
          if (options.url.startsWith("blob:")) {
            // To make sure that the blob:-URL can be loaded, fall back to
            // the default (system) principal instead.
            delete source.loadingPrincipal;
          }

          // Unless the API user explicitly wants errors ignored,
          // set the allowHttpStatus callback, which will instruct
          // DownloadCore to cancel downloads on HTTP errors.
          if (!options.allowHttpErrors) {
            source.allowHttpStatus = allowHttpStatus;
          }

          if (options.method || options.headers || options.body) {
            source.adjustChannel = adjustChannel;
          }

          const download = await Downloads.createDownload({
            // Only open the download panel if the method has been called
            // while handling user input (See Bug 1759231).
            openDownloadsListOnStart: isHandlingUserInput,
            source,
            target: {
              path: target,
              partFilePath: `${target}.part`,
            },
          });

          const list = await DownloadMap.getDownloadList();
          const item = DownloadMap.newFromDownload(download, extension);
          list.add(download);

          // This is necessary to make pause/resume work.
          download.tryToKeepPartialData = true;

          // Do not handle errors.
          // Extensions will use listeners to be informed about errors.
          // Just ignore any errors from |start()| to avoid spamming the
          // error console.
          download.start().catch(err => {
            if (err.name !== "DownloadError") {
              Cu.reportError(err);
            }
          });

          return item.id;
        },

        async removeFile(id) {
          await DownloadMap.lazyInit();

          let item = DownloadMap.fromId(id, context.privateBrowsingAllowed);

          if (item.state !== "complete") {
            throw new ExtensionError(
              `Cannot remove incomplete download id ${id}`
            );
          }

          try {
            await IOUtils.remove(item.filename, { ignoreAbsent: false });
          } catch (err) {
            if (DOMException.isInstance(err) && err.name === "NotFoundError") {
              throw new ExtensionError(
                `Could not remove download id ${item.id} because the file doesn't exist`
              );
            }

            // Unexpected other error. Throw the original error, so that it
            // can bubble up to the global browser console, but keep it
            // sanitized (i.e. not wrapped in ExtensionError) to avoid
            // inadvertent disclosure of potentially sensitive information.
            throw err;
          }
        },

        async search(query) {
          if (!context.privateBrowsingAllowed) {
            query.incognito = false;
          }

          const items = await queryHelper(query);
          return items.map(item => item.serialize());
        },

        async pause(id) {
          await DownloadMap.lazyInit();

          let item = DownloadMap.fromId(id, context.privateBrowsingAllowed);

          if (item.state !== "in_progress") {
            throw new ExtensionError(
              `Download ${id} cannot be paused since it is in state ${item.state}`
            );
          }

          return item.download.cancel();
        },

        async resume(id) {
          await DownloadMap.lazyInit();

          let item = DownloadMap.fromId(id, context.privateBrowsingAllowed);

          if (!item.canResume) {
            throw new ExtensionError(`Download ${id} cannot be resumed`);
          }

          item.error = null;
          return item.download.start();
        },

        async cancel(id) {
          await DownloadMap.lazyInit();

          let item = DownloadMap.fromId(id, context.privateBrowsingAllowed);

          if (item.download.succeeded) {
            throw new ExtensionError(`Download ${id} is already complete`);
          }

          return item.download.finalize(true);
        },

        showDefaultFolder() {
          Downloads.getPreferredDownloadsDirectory()
            .then(dir => {
              let dirobj = new FileUtils.File(dir);
              if (dirobj.isDirectory()) {
                dirobj.launch();
              } else {
                throw new Error(
                  `Download directory ${dirobj.path} is not actually a directory`
                );
              }
            })
            .catch(Cu.reportError);
        },

        async erase(query) {
          if (!context.privateBrowsingAllowed) {
            query.incognito = false;
          }

          const items = await queryHelper(query);
          let results = [];
          let promises = [];

          for (let item of items) {
            promises.push(DownloadMap.erase(item));
            results.push(item.id);
          }

          await Promise.all(promises);
          return results;
        },

        async open(downloadId) {
          await DownloadMap.lazyInit();

          let { download } = DownloadMap.fromId(
            downloadId,
            context.privateBrowsingAllowed
          );

          if (!download.succeeded) {
            throw new ExtensionError("Download has not completed.");
          }

          return download.launch();
        },

        async show(downloadId) {
          await DownloadMap.lazyInit();

          const { download } = DownloadMap.fromId(
            downloadId,
            context.privateBrowsingAllowed
          );

          await download.showContainingDirectory();

          return true;
        },

        async getFileIcon(downloadId, options) {
          await DownloadMap.lazyInit();

          const size = options?.size || 32;
          const { download } = DownloadMap.fromId(
            downloadId,
            context.privateBrowsingAllowed
          );

          let pathPrefix = "";
          let path;

          if (download.succeeded) {
            let file = FileUtils.File(download.target.path);
            path = Services.io.newFileURI(file).spec;
          } else {
            path = PathUtils.filename(download.target.path);
            pathPrefix = "//";
          }

          let windowlessBrowser =
            Services.appShell.createWindowlessBrowser(true);

          let canvas = windowlessBrowser.document.createElement("canvas");
          let img = new windowlessBrowser.docShell.domWindow.Image(size, size);

          canvas.width = size;
          canvas.height = size;

          img.src = `moz-icon:${pathPrefix}${path}?size=${size}`;

          try {
            await img.decode();

            canvas.getContext("2d").drawImage(img, 0, 0, size, size);

            let dataURL = canvas.toDataURL("image/png");

            return dataURL;
          } finally {
            windowlessBrowser.close();
          }
        },

        onChanged: new EventManager({
          context,
          module: "downloads",
          event: "onChanged",
          extensionApi: this,
        }).api(),

        onCreated: new EventManager({
          context,
          module: "downloads",
          event: "onCreated",
          extensionApi: this,
        }).api(),

        onErased: new EventManager({
          context,
          module: "downloads",
          event: "onErased",
          extensionApi: this,
        }).api(),

        onDeterminingFilename: new EventManager({
          context,
          name: "downloads.onDeterminingFilename",
          register: fire => {
            DownloadMap.lazyInit();
            const callback = async download => {
              if (!extension.privateBrowsingAllowed && download.source.isPrivate) {
                return;
              }
              // Ensure the download has an entry in DownloadMap even if it has
              // not been added to the DownloadList yet (e.g. legacy downloads
              // call start() before list.add()).
              const item =
                DownloadMap.byDownload.get(download) ||
                DownloadMap.newFromDownload(download, null);
              let suggestion;
              try {
                suggestion = await fire.async(item.serialize());
              } catch (e) {
                Cu.reportError(e);
                return;
              }
              if (suggestion) {
                const newPath = await applyFilenameSuggestion(
                  suggestion,
                  download.target.path
                );
                if (newPath) {
                  download.target.path = newPath;
                  download.target.partFilePath = `${newPath}.part`;
                }
              }
            };
            DownloadIntegration._determineFilenameCallbacks.add(callback);
            return () => {
              DownloadIntegration._determineFilenameCallbacks.delete(callback);
            };
          },
        }).api(),
      },
    };
  }
};
