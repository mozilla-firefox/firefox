/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  GeckoViewPrompter:
    "moz-src:///mobile/shared/components/geckoview/GeckoViewPrompter.sys.mjs",
});

const { debug, warn } = GeckoViewUtils.initLogging("FilePickerDelegate");

export class FilePickerDelegate {
  _filesInWebKitDirectory = [];

  /* ----------  nsIFilePicker  ---------- */
  init(aBrowsingContext, aTitle, aMode) {
    let mode;
    switch (aMode) {
      case Ci.nsIFilePicker.modeOpen:
        mode = "single";
        break;
      case Ci.nsIFilePicker.modeGetFolder:
        mode = "folder";
        break;
      case Ci.nsIFilePicker.modeOpenMultiple:
        mode = "multiple";
        break;
      default:
        throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
    }
    this._browsingContext = aBrowsingContext;
    this._prompt = new lazy.GeckoViewPrompter(aBrowsingContext);
    this._msg = {
      type: "file",
      title: aTitle,
      mode,
    };
    this._mode = aMode;
    this._mimeTypes = [];
    this._capture = 0;
  }

  get mode() {
    return this._mode;
  }

  appendRawFilter(aFilter) {
    this._mimeTypes.push(aFilter);
  }

  open(aFilePickerShownCallback) {
    if (!this._browsingContext.canOpenModalPicker) {
      // File pickers are not allowed to open, so we respond to the callback
      // with returnCancel.
      aFilePickerShownCallback.done(Ci.nsIFilePicker.returnCancel);
      return;
    }

    this._msg.mimeTypes = this._mimeTypes;
    this._msg.capture = this._capture;
    this._prompt.asyncShowPrompt(this._msg, result => {
      // OK: result
      // Cancel: !result
      if (!result || !result.files || !result.files.length) {
        aFilePickerShownCallback.done(Ci.nsIFilePicker.returnCancel);
      } else {
        this._resolveFilesInWebKitDirectory(result.filesInWebKitDirectory).then(
          () => {
            this._resolveFiles(result.files, aFilePickerShownCallback);
          }
        );
      }
    });
  }

  async _resolveFiles(aFiles, aCallback) {
    const fileData = [];

    try {
      for (const file of aFiles) {
        const domFileOrDir = await this._getDOMFileOrDir(file);
        fileData.push({
          file,
          domFileOrDir,
        });
      }
    } catch (ex) {
      warn`Error resolving files from file picker: ${ex}`;
      aCallback.done(Ci.nsIFilePicker.returnCancel);
      return;
    }

    this._fileData = fileData;
    aCallback.done(Ci.nsIFilePicker.returnOK);
  }

  async _resolveFilesInWebKitDirectory(files) {
    if (!files) {
      return;
    }

    const filesInWebKitDirectory = [];

    for (const info of files) {
      const { filePath, relativePath, type, lastModified } = info;
      if (filePath) {
        const file = await (() => {
          if (this._prompt.domWin) {
            return this._prompt.domWin.File.createFromFileName(filePath, {
              type,
              lastModified,
            });
          }
          return File.createFromFileName(filePath, {
            type,
            lastModified,
          });
        })();

        file.setMozRelativePath(relativePath);
        filesInWebKitDirectory.push(file);
        continue;
      }

      // File path cannot be resolved, but we know content URI.
      const file = this._getDOMFileFromContentUri(info);
      file.setMozRelativePath(relativePath);
      filesInWebKitDirectory.push(file);
    }

    this._filesInWebKitDirectory = filesInWebKitDirectory;
  }

  _getDOMFileFromContentUri(aInfo) {
    const { uri, name, type, lastModified, size } = aInfo;
    if (size < 0) {
      throw Components.Exception(
        "File size is unknown",
        Cr.NS_ERROR_NOT_AVAILABLE
      );
    }

    const input = Cc[
      "@mozilla.org/network/android-content-input-stream;1"
    ].createInstance(Ci.nsIAndroidContentInputStream);
    input.init(Services.io.newURI(uri));
    if (this._prompt.domWin) {
      return this._prompt.domWin.File.createFromNsIInputStream(input, size, {
        name,
        type,
        lastModified,
      });
    }
    return File.createFromNsIInputStream(input, size, {
      name,
      type,
      lastModified,
    });
  }

  get file() {
    if (!this._fileData) {
      throw Components.Exception("", Cr.NS_ERROR_NOT_AVAILABLE);
    }
    const fileData = this._fileData[0];
    if (!fileData) {
      return null;
    }
    return new lazy.FileUtils.File(fileData.file);
  }

  get fileURL() {
    return Services.io.newFileURI(this.file);
  }

  *_getEnumerator(aDOMFile) {
    if (!this._fileData) {
      throw Components.Exception("", Cr.NS_ERROR_NOT_AVAILABLE);
    }

    for (const fileData of this._fileData) {
      if (aDOMFile) {
        yield fileData.domFileOrDir;
      }
      yield new lazy.FileUtils.File(fileData.file);
    }
  }

  get files() {
    return this._getEnumerator(/* aDOMFile */ false);
  }

  _getDOMFileOrDir(aPath) {
    if (this.mode == Ci.nsIFilePicker.modeGetFolder) {
      return this._getDOMDir(aPath);
    }
    return this._getDOMFile(aPath);
  }

  _getDOMDir(aPath) {
    if (this._prompt.domWin) {
      return new this._prompt.domWin.Directory(aPath);
    }
    return new Directory(aPath);
  }

  _getDOMFile(aPath) {
    if (this._prompt.domWin) {
      return this._prompt.domWin.File.createFromFileName(aPath);
    }
    return File.createFromFileName(aPath);
  }

  get domFileOrDirectory() {
    if (!this._fileData) {
      throw Components.Exception("", Cr.NS_ERROR_NOT_AVAILABLE);
    }
    return this._fileData[0]?.domFileOrDir;
  }

  get domFileOrDirectoryEnumerator() {
    return this._getEnumerator(/* aDOMFile */ true);
  }

  get defaultString() {
    return "";
  }

  set defaultString(aValue) {}

  get defaultExtension() {
    return "";
  }

  set defaultExtension(aValue) {}

  get filterIndex() {
    return 0;
  }

  set filterIndex(aValue) {}

  get displayDirectory() {
    return null;
  }

  set displayDirectory(aValue) {}

  get displaySpecialDirectory() {
    return "";
  }

  set displaySpecialDirectory(aValue) {}

  get addToRecentDocs() {
    return false;
  }

  set addToRecentDocs(aValue) {}

  get okButtonLabel() {
    return "";
  }

  set okButtonLabel(aValue) {}

  get capture() {
    return this._capture;
  }

  set capture(aValue) {
    this._capture = aValue;
  }

  *_getDOMFilesInWebKitDirectory() {
    if (
      this._mode != Ci.nsIFilePicker.modeGetFolder ||
      AppConstants.platform != "android"
    ) {
      throw Components.Exception("", Cr.NS_ERROR_NOT_AVAILABLE);
    }

    for (const file of this._filesInWebKitDirectory) {
      yield file;
    }
  }

  get domFilesInWebKitDirectory() {
    return this._getDOMFilesInWebKitDirectory();
  }
}

FilePickerDelegate.prototype.classID = Components.ID(
  "{e4565e36-f101-4bf5-950b-4be0887785a9}"
);
FilePickerDelegate.prototype.QueryInterface = ChromeUtils.generateQI([
  "nsIFilePicker",
]);
