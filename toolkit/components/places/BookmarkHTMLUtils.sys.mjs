/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file works on the old-style "bookmarks.html" file.  It includes
 * functions to import and export existing bookmarks to this file format.
 *
 * Format
 * ------
 *
 * Primary heading := h1
 *   Old version used this to set attributes on the bookmarks RDF root, such
 *   as the last modified date. We only use H1 to check for the attribute
 *   PLACES_ROOT, which tells us that this hierarchy root is the places root.
 *   For backwards compatibility, if we don't find this, we assume that the
 *   hierarchy is rooted at the bookmarks menu.
 * Heading := any heading other than h1
 *   Old version used this to set attributes on the current container. We only
 *   care about the content of the heading container, which contains the title
 *   of the bookmark container.
 * Bookmark := a
 *   HREF is the destination of the bookmark
 *   FEEDURL is the URI of the RSS feed. This is deprecated and no more
 *   supported, but some old files may still contain it.
 *   LAST_CHARSET is stored as an annotation so that the next time we go to
 *     that page we remember the user's preference.
 *   ICON will be stored in the favicon service
 *   ICON_URI is new for places bookmarks.html, it refers to the original
 *     URI of the favicon so we don't have to make up favicon URLs.
 *   Text of the <a> container is the name of the bookmark
 *   Ignored: LAST_VISIT, ID (writing out non-RDF IDs can confuse Firefox 2)
 * Bookmark comment := dd
 *   This affects the previosly added bookmark
 * Separator := hr
 *   Insert a separator into the current container
 * The folder hierarchy is defined by <dl>/<ul>/<menu> (the old importing code
 *     handles all these cases, when we write, use <dl>).
 *
 * Overall design
 * --------------
 *
 * We need to emulate a recursive parser. A "Bookmark import frame" is created
 * corresponding to each folder we encounter. These are arranged in a stack,
 * and contain all the state we need to keep track of.
 *
 * A frame is created when we find a heading, which defines a new container.
 * The frame also keeps track of the nesting of <DL>s, (in well-formed
 * bookmarks files, these will have a 1-1 correspondence with frames, but we
 * try to be a little more flexible here). When the nesting count decreases
 * to 0, then we know a frame is complete and to pop back to the previous
 * frame.
 *
 * Note that a lot of things happen when tags are CLOSED because we need to
 * get the text from the content of the tag. For example, link and heading tags
 * both require the content (= title) before actually creating it.
 */

import { NetUtil } from "resource://gre/modules/NetUtil.sys.mjs";

import { FileUtils } from "resource://gre/modules/FileUtils.sys.mjs";
import { PlacesUtils } from "resource://gre/modules/PlacesUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PlacesBackups: "resource://gre/modules/PlacesBackups.sys.mjs",
});

const Container_Normal = 0;
const Container_Toolbar = 1;
const Container_Menu = 2;
const Container_Unfiled = 3;
const Container_Places = 4;

const MICROSEC_PER_SEC = 1000000;

const EXPORT_INDENT = "    "; // four spaces

/**
 * Provides HTML escaping for use in HTML attributes and body of the bookmarks
 * file, compatible with the old bookmarks system.
 *
 * @param {string} aText
 */
function escapeHtmlEntities(aText) {
  return (aText || "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

/**
 * Provides URL escaping for use in HTML attributes of the bookmarks file,
 * compatible with the old bookmarks system.
 *
 * @param {string} aText
 */
function escapeUrl(aText) {
  return (aText || "").replace(/"/g, "%22");
}

function notifyObservers(aTopic, aInitialImport) {
  Services.obs.notifyObservers(
    null,
    aTopic,
    aInitialImport ? "html-initial" : "html"
  );
}

export var BookmarkHTMLUtils = Object.freeze({
  /**
   * Loads the current bookmarks hierarchy from a "bookmarks.html" file.
   *
   * @param {string} aSpec
   *        String containing the "file:" URI for the existing "bookmarks.html"
   *        file to be loaded.
   * @param {object} [options]
   * @param {boolean} [options.replace]
   *        Whether we should erase existing bookmarks before loading.
   *        Defaults to `false`.
   * @param {number} [options.source]
   *        The bookmark change source, used to determine the sync status for
   *        imported bookmarks. Defaults to `RESTORE` if `replace = true`, or
   *        `IMPORT` otherwise.
   *
   * @returns {Promise<number>} The number of imported bookmarks, not including
   *   folders and separators. Rejects if there is an issue.
   */
  async importFromURL(
    aSpec,
    {
      replace: aInitialImport = false,
      source: aSource = aInitialImport
        ? PlacesUtils.bookmarks.SOURCES.RESTORE
        : PlacesUtils.bookmarks.SOURCES.IMPORT,
    } = {}
  ) {
    let bookmarkCount;
    notifyObservers(PlacesUtils.TOPIC_BOOKMARKS_RESTORE_BEGIN, aInitialImport);
    try {
      let importer = new BookmarkImporter(aInitialImport, aSource);
      bookmarkCount = await importer.importFromURL(aSpec);

      notifyObservers(
        PlacesUtils.TOPIC_BOOKMARKS_RESTORE_SUCCESS,
        aInitialImport
      );
    } catch (ex) {
      console.error(`Failed to import bookmarks from ${aSpec}:`, ex);
      notifyObservers(
        PlacesUtils.TOPIC_BOOKMARKS_RESTORE_FAILED,
        aInitialImport
      );
      throw ex;
    }
    return bookmarkCount;
  },

  /**
   * Loads the current bookmarks hierarchy from a "bookmarks.html" file.
   *
   * @param {string} aFilePath
   *        OS.File path string of the "bookmarks.html" file to be loaded.
   * @param {object} options
   * @param {boolean} [options.replace]
   *        Whether we should erase existing bookmarks before loading.
   *        Defaults to `false`.
   * @param {number} [options.source]
   *        The bookmark change source, used to determine the sync status for
   *        imported bookmarks. Defaults to `RESTORE` if `replace = true`, or
   *        `IMPORT` otherwise.
   *
   * @returns {Promise<number>} The number of imported bookmarks, not including
   *   folders and separators. Rejects if there is an issue.
   */
  async importFromFile(
    aFilePath,
    {
      replace: aInitialImport = false,
      source: aSource = aInitialImport
        ? PlacesUtils.bookmarks.SOURCES.RESTORE
        : PlacesUtils.bookmarks.SOURCES.IMPORT,
    } = {}
  ) {
    let bookmarkCount;
    notifyObservers(PlacesUtils.TOPIC_BOOKMARKS_RESTORE_BEGIN, aInitialImport);
    try {
      if (!(await IOUtils.exists(aFilePath))) {
        throw new Error(
          "Cannot import from nonexisting html file: " + aFilePath
        );
      }
      let importer = new BookmarkImporter(aInitialImport, aSource);
      bookmarkCount = await importer.importFromURL(
        PathUtils.toFileURI(aFilePath)
      );

      notifyObservers(
        PlacesUtils.TOPIC_BOOKMARKS_RESTORE_SUCCESS,
        aInitialImport
      );
    } catch (ex) {
      console.error(`Failed to import bookmarks from ${aFilePath}:`, ex);
      notifyObservers(
        PlacesUtils.TOPIC_BOOKMARKS_RESTORE_FAILED,
        aInitialImport
      );
      throw ex;
    }
    return bookmarkCount;
  },

  /**
   * Saves the current bookmarks hierarchy to a "bookmarks.html" file.
   *
   * @param {string} aFilePath
   *        OS.File path string for the "bookmarks.html" file to be created.
   *
   * @returns {Promise<number>} The exported bookmarks count. Rejects if there
   *   is an issue.
   */
  async exportToFile(aFilePath) {
    let [bookmarks, count] = await lazy.PlacesBackups.getBookmarksTree();
    let timerId = Glean.places.exportTohtml.start();

    // Report the time taken to convert the tree to HTML.
    let exporter = new BookmarkExporter(bookmarks);
    await exporter.exportToFile(aFilePath);

    Glean.places.exportTohtml.stopAndAccumulate(timerId);

    return count;
  },

  get defaultPath() {
    try {
      return Services.prefs.getCharPref("browser.bookmarks.file");
    } catch (ex) {}
    return PathUtils.join(PathUtils.profileDir, "bookmarks.html");
  },
});

function Frame(aFolder) {
  this.folder = aFolder;

  /**
   * How many <dl>s have been nested. Each frame/container should start
   * with a heading, and is then followed by a <dl>, <ul>, or <menu>. When
   * that list is complete, then it is the end of this container and we need
   * to pop back up one level for new items. If we never get an open tag for
   * one of these things, we should assume that the container is empty and
   * that things we find should be siblings of it. Normally, these <dl>s won't
   * be nested so this will be 0 or 1.
   */
  this.containerNesting = 0;

  /**
   * when we find a heading tag, it actually affects the title of the NEXT
   * container in the list. This stores that heading tag and whether it was
   * special. 'consumeHeading' resets this._
   */
  this.lastContainerType = Container_Normal;

  /**
   * this contains the text from the last begin tag until now. It is reset
   * at every begin tag. We can check it when we see a </a>, or </h3>
   * to see what the text content of that node should be.
   */
  this.previousText = "";

  /**
   * true when we hit a <dd>, which contains the description for the preceding
   * <a> tag. We can't just check for </dd> like we can for </a> or </h3>
   * because if there is a sub-folder, it is actually a child of the <dd>
   * because the tag is never explicitly closed. If this is true and we see a
   * new open tag, that means to commit the description to the previous
   * bookmark.
   *
   * Additional weirdness happens when the previous <dt> tag contains a <h3>:
   * this means there is a new folder with the given description, and whose
   * children are contained in the following <dl> list.
   *
   * This is handled in openContainer(), which commits previous text if
   * necessary.
   */
  this.inDescription = false;

  /**
   * contains the URL of the previous bookmark created. This is used so that
   * when we encounter a <dd>, we know what bookmark to associate the text with.
   * This is cleared whenever we hit a <h3>, so that we know NOT to save this
   * with a bookmark, but to keep it until
   */
  this.previousLink = null;

  /**
   * Contains a reference to the last created bookmark or folder object.
   */
  this.previousItem = null;

  /**
   * Contains the date-added and last-modified-date of an imported item.
   * Used to override the values set by insertBookmark, createFolder, etc.
   */
  this.previousDateAdded = null;
  this.previousLastModifiedDate = null;
}

function BookmarkImporter(aInitialImport, aSource) {
  this._isImportDefaults = aInitialImport;
  this._source = aSource;

  // This root is where we construct the bookmarks tree into, following the format
  // of the imported file.
  // If we're doing an initial import, the non-menu roots will be created as
  // children of this root, so in _getBookmarkTrees we'll split them out.
  // If we're not doing an initial import, everything gets imported under the
  // bookmark menu folder, so there won't be any need for _getBookmarkTrees to
  // do separation.
  this._bookmarkTree = {
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    guid: PlacesUtils.bookmarks.menuGuid,
    children: [],
  };

  this._frames = [];
  this._frames.push(new Frame(this._bookmarkTree));
}

BookmarkImporter.prototype = {
  _safeTrim: function safeTrim(aStr) {
    return aStr ? aStr.trim() : aStr;
  },

  get _curFrame() {
    return this._frames[this._frames.length - 1];
  },

  get _previousFrame() {
    return this._frames[this._frames.length - 2];
  },

  /**
   * This is called when there is a new folder found. The folder takes the
   * name from the previous frame's heading.
   */
  _newFrame: function newFrame() {
    let frame = this._curFrame;
    let containerTitle = frame.previousText;
    frame.previousText = "";
    let containerType = frame.lastContainerType;

    let folder = {
      children: [],
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
    };

    switch (containerType) {
      case Container_Normal:
        // This can only be a sub-folder so no need to set a guid here.
        folder.title = containerTitle;
        break;
      case Container_Places:
        folder.guid = PlacesUtils.bookmarks.rootGuid;
        break;
      case Container_Menu:
        folder.guid = PlacesUtils.bookmarks.menuGuid;
        break;
      case Container_Unfiled:
        folder.guid = PlacesUtils.bookmarks.unfiledGuid;
        break;
      case Container_Toolbar:
        folder.guid = PlacesUtils.bookmarks.toolbarGuid;
        break;
      default:
        // NOT REACHED
        throw new Error("Unknown bookmark container type!");
    }

    frame.folder.children.push(folder);

    if (frame.previousDateAdded != null) {
      folder.dateAdded = frame.previousDateAdded;
      frame.previousDateAdded = null;
    }

    if (frame.previousLastModifiedDate != null) {
      folder.lastModified = frame.previousLastModifiedDate;
      frame.previousLastModifiedDate = null;
    }

    if (
      !folder.hasOwnProperty("dateAdded") &&
      folder.hasOwnProperty("lastModified")
    ) {
      folder.dateAdded = folder.lastModified;
    }

    frame.previousItem = folder;

    this._frames.push(new Frame(folder));
  },

  /**
   * Handles <hr> as a separator.
   *
   * Separators may have a title in old html files, though Places dropped
   * support for them.
   * We also don't import ADD_DATE or LAST_MODIFIED for separators because
   * pre-Places bookmarks did not support them.
   */
  _handleSeparator: function handleSeparator() {
    let frame = this._curFrame;

    let separator = {
      type: PlacesUtils.bookmarks.TYPE_SEPARATOR,
    };
    frame.folder.children.push(separator);
    frame.previousItem = separator;
  },

  /**
   * Called for h2,h3,h4,h5,h6. This just stores the correct information in
   * the current frame; the actual new frame corresponding to the container
   * associated with the heading will be created when the tag has been closed
   * and we know the title (we don't know to create a new folder or to merge
   * with an existing one until we have the title).
   *
   * @param {Element} aElt
   */
  _handleHeadBegin: function handleHeadBegin(aElt) {
    let frame = this._curFrame;

    // after a heading, a previous bookmark is not applicable (for example, for
    // the descriptions contained in a <dd>). Neither is any previous head type
    frame.previousLink = null;
    frame.lastContainerType = Container_Normal;

    // It is syntactically possible for a heading to appear after another heading
    // but before the <dl> that encloses that folder's contents.  This should not
    // happen in practice, as the file will contain "<dl></dl>" sequence for
    // empty containers.
    //
    // Just to be on the safe side, if we encounter
    //   <h3>FOO</h3>
    //   <h3>BAR</h3>
    //   <dl>...content 1...</dl>
    //   <dl>...content 2...</dl>
    // we'll pop the stack when we find the h3 for BAR, treating that as an
    // implicit ending of the FOO container. The output will be FOO and BAR as
    // siblings. If there's another <dl> following (as in "content 2"), those
    // items will be treated as further siblings of FOO and BAR
    // This special frame popping business, of course, only happens when our
    // frame array has more than one element so we can avoid situations where
    // we don't have a frame to parse into anymore.
    if (frame.containerNesting == 0 && this._frames.length > 1) {
      this._frames.pop();
    }

    // We have to check for some attributes to see if this is a "special"
    // folder, which will have different creation rules when the end tag is
    // processed.
    if (aElt.hasAttribute("personal_toolbar_folder")) {
      if (this._isImportDefaults) {
        frame.lastContainerType = Container_Toolbar;
      }
    } else if (aElt.hasAttribute("bookmarks_menu")) {
      if (this._isImportDefaults) {
        frame.lastContainerType = Container_Menu;
      }
    } else if (aElt.hasAttribute("unfiled_bookmarks_folder")) {
      if (this._isImportDefaults) {
        frame.lastContainerType = Container_Unfiled;
      }
    } else if (aElt.hasAttribute("places_root")) {
      if (this._isImportDefaults) {
        frame.lastContainerType = Container_Places;
      }
    } else {
      let addDate = aElt.getAttribute("add_date");
      if (addDate) {
        frame.previousDateAdded =
          this._convertImportedDateToInternalDate(addDate);
      }
      let modDate = aElt.getAttribute("last_modified");
      if (modDate) {
        frame.previousLastModifiedDate =
          this._convertImportedDateToInternalDate(modDate);
      }
    }
    this._curFrame.previousText = "";
  },

  /*
   * Handles "<a" tags by creating a new bookmark. The title of the bookmark
   * will be the text content, which will be stuffed in previousText for us
   * and which will be saved by handleLinkEnd
   */
  _handleLinkBegin: function handleLinkBegin(aElt) {
    let frame = this._curFrame;

    frame.previousItem = null;
    frame.previousText = ""; // Will hold link text, clear it.

    // Get the attributes we care about.
    let href = this._safeTrim(aElt.getAttribute("href"));
    let icon = this._safeTrim(aElt.getAttribute("icon"));
    let iconUri = this._safeTrim(aElt.getAttribute("icon_uri"));
    let lastCharset = this._safeTrim(aElt.getAttribute("last_charset"));
    let keyword = this._safeTrim(aElt.getAttribute("shortcuturl"));
    let postData = this._safeTrim(aElt.getAttribute("post_data"));
    let dateAdded = this._safeTrim(aElt.getAttribute("add_date"));
    let lastModified = this._safeTrim(aElt.getAttribute("last_modified"));
    let tags = this._safeTrim(aElt.getAttribute("tags"));

    // Ignore <a> tags that have no href.
    try {
      frame.previousLink = Services.io.newURI(href).spec;
    } catch (e) {
      frame.previousLink = null;
      return;
    }

    let bookmark = {};

    // Only set the url for bookmarks.
    if (frame.previousLink) {
      bookmark.url = frame.previousLink;
    }

    if (dateAdded) {
      bookmark.dateAdded = this._convertImportedDateToInternalDate(dateAdded);
    }
    // Save bookmark's last modified date.
    if (lastModified) {
      bookmark.lastModified =
        this._convertImportedDateToInternalDate(lastModified);
    }

    if (!dateAdded && lastModified) {
      bookmark.dateAdded = bookmark.lastModified;
    }

    if (tags) {
      bookmark.tags = tags
        .split(",")
        .filter(
          aTag =>
            !!aTag.length && aTag.length <= PlacesUtils.bookmarks.MAX_TAG_LENGTH
        );

      // If we end up with none, then delete the property completely.
      if (!bookmark.tags.length) {
        delete bookmark.tags;
      }
    }

    if (lastCharset) {
      bookmark.charset = lastCharset;
    }

    if (keyword) {
      bookmark.keyword = keyword;
    }

    if (postData) {
      bookmark.postData = postData;
    }

    if (icon) {
      bookmark.icon = icon;
    }

    if (iconUri) {
      bookmark.iconUri = iconUri;
    }

    // Add bookmark to the tree.
    frame.folder.children.push(bookmark);
    frame.previousItem = bookmark;
  },

  _handleContainerBegin: function handleContainerBegin() {
    this._curFrame.containerNesting++;
  },

  /**
   * Our "indent" count has decreased, and when we hit 0 that means that this
   * container is complete and we need to pop back to the outer frame. Never
   * pop the toplevel frame
   */
  _handleContainerEnd: function handleContainerEnd() {
    let frame = this._curFrame;
    if (frame.containerNesting > 0) {
      frame.containerNesting--;
    }
    if (this._frames.length > 1 && frame.containerNesting == 0) {
      this._frames.pop();
    }
  },

  /**
   * Creates the new frame for this heading now that we know the name of the
   * container (tokens since the heading open tag will have been placed in
   * previousText).
   */
  _handleHeadEnd: function handleHeadEnd() {
    this._newFrame();
  },

  /**
   * Saves the title for the given bookmark.
   */
  _handleLinkEnd: function handleLinkEnd() {
    let frame = this._curFrame;
    frame.previousText = frame.previousText.trim();

    if (frame.previousItem != null) {
      frame.previousItem.title = frame.previousText;
    }

    frame.previousText = "";
  },

  _openContainer: function openContainer(aElt) {
    if (aElt.namespaceURI != "http://www.w3.org/1999/xhtml") {
      return;
    }
    switch (aElt.localName) {
      case "h2":
      case "h3":
      case "h4":
      case "h5":
      case "h6":
        this._handleHeadBegin(aElt);
        break;
      case "a":
        this._handleLinkBegin(aElt);
        break;
      case "dl":
      case "ul":
      case "menu":
        this._handleContainerBegin();
        break;
      case "dd":
        this._curFrame.inDescription = true;
        break;
      case "hr":
        this._handleSeparator();
        break;
    }
  },

  _closeContainer: function closeContainer(aElt) {
    let frame = this._curFrame;

    // Although we no longer support importing descriptions, we still need to
    // clear any previous text, so that it doesn't get swallowed into other elements.
    if (frame.inDescription) {
      frame.previousText = "";
      frame.inDescription = false;
    }

    if (aElt.namespaceURI != "http://www.w3.org/1999/xhtml") {
      return;
    }
    switch (aElt.localName) {
      case "dl":
      case "ul":
      case "menu":
        this._handleContainerEnd();
        break;
      case "dt":
        break;
      case "h1":
        // ignore
        break;
      case "h2":
      case "h3":
      case "h4":
      case "h5":
      case "h6":
        this._handleHeadEnd();
        break;
      case "a":
        this._handleLinkEnd();
        break;
      default:
        break;
    }
  },

  _appendText: function appendText(str) {
    this._curFrame.previousText += str;
  },

  /**
   * Converts a string date in seconds to a date object
   *
   * @param {string} seconds
   */
  _convertImportedDateToInternalDate(seconds) {
    try {
      let parsed = parseInt(seconds);
      if (!isNaN(parsed)) {
        return new Date(parsed * 1000); // in bookmarks.html this value is in seconds
      }
    } catch (ex) {
      // Do nothing.
    }
    return new Date();
  },

  _walkTreeForImport(aDoc) {
    if (!aDoc) {
      return;
    }

    let current = aDoc;
    let next;
    for (;;) {
      switch (current.nodeType) {
        case current.ELEMENT_NODE:
          this._openContainer(current);
          break;
        case current.TEXT_NODE:
          this._appendText(current.data);
          break;
      }
      if ((next = current.firstChild)) {
        current = next;
        continue;
      }
      for (;;) {
        if (current.nodeType == current.ELEMENT_NODE) {
          this._closeContainer(current);
        }
        if (current == aDoc) {
          return;
        }
        if ((next = current.nextSibling)) {
          current = next;
          break;
        }
        current = current.parentNode;
      }
    }
  },

  /**
   * Returns the bookmark tree(s) from the importer. These are suitable for
   * passing to PlacesUtils.bookmarks.insertTree().
   *
   * @returns {Array} An array of bookmark trees.
   */
  _getBookmarkTrees() {
    // If we're not importing defaults, then everything gets imported under the
    // Bookmarks menu.
    if (!this._isImportDefaults) {
      return [this._bookmarkTree];
    }

    // If we are importing defaults, we need to separate out the top-level
    // default folders into separate items, for the caller to pass into insertTree.
    let bookmarkTrees = [this._bookmarkTree];

    // The children of this "root" element will contain normal children of the
    // bookmark menu as well as the places roots. Hence, we need to filter out
    // the separate roots, but keep the children that are relevant to the
    // bookmark menu.
    this._bookmarkTree.children = this._bookmarkTree.children.filter(child => {
      if (
        child.guid &&
        PlacesUtils.bookmarks.userContentRoots.includes(child.guid)
      ) {
        bookmarkTrees.push(child);
        return false;
      }
      return true;
    });

    return bookmarkTrees;
  },

  /**
   * Imports the bookmarks from the importer into the places database.
   *
   * @returns {Promise<number>} The number of imported bookmarks, not including
   *   folders and separators
   */
  async _importBookmarks() {
    if (this._isImportDefaults) {
      await PlacesUtils.bookmarks.eraseEverything();
    }

    let bookmarksTrees = this._getBookmarkTrees();
    let bookmarkCount = 0;
    for (let tree of bookmarksTrees) {
      if (!tree.children.length) {
        continue;
      }

      // Give the tree the source.
      tree.source = this._source;
      let bookmarks = await PlacesUtils.bookmarks.insertTree(tree, {
        fixupOrSkipInvalidEntries: true,
      });
      // We want to count only bookmarks, not folders or separators
      bookmarkCount += bookmarks.filter(
        bookmark => bookmark.type == PlacesUtils.bookmarks.TYPE_BOOKMARK
      ).length;

      insertFaviconsForTree(tree);
    }
    return bookmarkCount;
  },

  /**
   * Imports data into the places database from the supplied url.
   *
   * @param {string} href The url to import data from.
   * @returns {Promise<number>} The number of imported bookmarks, not including
   *   folders and separators.
   */
  async importFromURL(href) {
    let data = await fetchData(href);

    if (this._isImportDefaults && data) {
      // Localize default bookmarks.  Find rel="localization" links and manually
      // localize using them.
      let hrefs = [];
      let links = data.head.querySelectorAll("link[rel='localization']");
      for (let link of links) {
        if (link.getAttribute("href")) {
          // We need the text, not the fully qualified URL, so we use `getAttribute`.
          hrefs.push(link.getAttribute("href"));
        }
      }

      if (hrefs.length) {
        let domLoc = new DOMLocalization(hrefs);
        await domLoc.translateFragment(data.body);
      }
    }

    this._walkTreeForImport(data);
    return this._importBookmarks();
  },
};

function BookmarkExporter(aBookmarksTree) {
  // Create a map of the roots.
  let rootsMap = new Map();
  for (let child of aBookmarksTree.children) {
    if (child.root) {
      rootsMap.set(child.root, child);
      // Also take the opportunity to get the correctly localised title for the
      // root.
      child.title = PlacesUtils.bookmarks.getLocalizedTitle(child);
    }
  }

  // For backwards compatibility reasons the bookmarks menu is the root, while
  // the bookmarks toolbar and unfiled bookmarks will be child items.
  this._root = rootsMap.get("bookmarksMenuFolder");

  for (let key of ["toolbarFolder", "unfiledBookmarksFolder"]) {
    let root = rootsMap.get(key);
    if (root.children && root.children.length) {
      if (!this._root.children) {
        this._root.children = [];
      }
      this._root.children.push(root);
    }
  }
}

BookmarkExporter.prototype = {
  exportToFile: function exportToFile(aFilePath) {
    return (async () => {
      // Create a file that can be accessed by the current user only.
      let out = FileUtils.openAtomicFileOutputStream(
        new FileUtils.File(aFilePath)
      );
      try {
        // We need a buffered output stream for performance.  See bug 202477.
        let bufferedOut = Cc[
          "@mozilla.org/network/buffered-output-stream;1"
        ].createInstance(Ci.nsIBufferedOutputStream);
        bufferedOut.init(out, 4096);
        try {
          // Write bookmarks in UTF-8.
          this._converterOut = Cc[
            "@mozilla.org/intl/converter-output-stream;1"
          ].createInstance(Ci.nsIConverterOutputStream);
          this._converterOut.init(bufferedOut, "utf-8");
          try {
            this._writeHeader();
            await this._writeContainer(this._root);
            // Retain the target file on success only.
            bufferedOut.QueryInterface(Ci.nsISafeOutputStream).finish();
          } finally {
            this._converterOut.close();
            this._converterOut = null;
          }
        } finally {
          bufferedOut.close();
        }
      } finally {
        out.close();
      }
    })();
  },

  /**
   * @type {?nsIConverterOutputStream}
   */
  _converterOut: null,

  _write(aText) {
    this._converterOut.writeString(aText || "");
  },

  _writeAttribute(aName, aValue) {
    this._write(" " + aName + '="' + aValue + '"');
  },

  _writeLine(aText) {
    this._write(aText + "\n");
  },

  _writeHeader() {
    this._writeLine("<!DOCTYPE NETSCAPE-Bookmark-file-1>");
    this._writeLine("<!-- This is an automatically generated file.");
    this._writeLine("     It will be read and overwritten.");
    this._writeLine("     DO NOT EDIT! -->");
    this._writeLine(
      '<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=UTF-8">'
    );
    this._writeLine(`<meta http-equiv="Content-Security-Policy"
      content="default-src 'self'; script-src 'none'; img-src data: *; object-src 'none'"></meta>`);
    this._writeLine("<TITLE>Bookmarks</TITLE>");
  },

  async _writeContainer(aItem, aIndent = "") {
    if (aItem == this._root) {
      this._writeLine("<H1>" + escapeHtmlEntities(this._root.title) + "</H1>");
      this._writeLine("");
    } else {
      this._write(aIndent + "<DT><H3");
      this._writeDateAttributes(aItem);

      if (aItem.root === "toolbarFolder") {
        this._writeAttribute("PERSONAL_TOOLBAR_FOLDER", "true");
      } else if (aItem.root === "unfiledBookmarksFolder") {
        this._writeAttribute("UNFILED_BOOKMARKS_FOLDER", "true");
      }
      this._writeLine(">" + escapeHtmlEntities(aItem.title) + "</H3>");
    }

    this._writeLine(aIndent + "<DL><p>");
    if (aItem.children) {
      await this._writeContainerContents(aItem, aIndent);
    }
    if (aItem == this._root) {
      this._writeLine(aIndent + "</DL>");
    } else {
      this._writeLine(aIndent + "</DL><p>");
    }
  },

  async _writeContainerContents(aItem, aIndent) {
    let localIndent = aIndent + EXPORT_INDENT;

    for (let child of aItem.children) {
      if (child.type == PlacesUtils.TYPE_X_MOZ_PLACE_CONTAINER) {
        await this._writeContainer(child, localIndent);
      } else if (child.type == PlacesUtils.TYPE_X_MOZ_PLACE_SEPARATOR) {
        this._writeSeparator(child, localIndent);
      } else {
        await this._writeItem(child, localIndent);
      }
    }
  },

  _writeSeparator(aItem, aIndent) {
    this._write(aIndent + "<HR");
    // We keep exporting separator titles, but don't support them anymore.
    if (aItem.title) {
      this._writeAttribute("NAME", escapeHtmlEntities(aItem.title));
    }
    this._write(">");
  },

  async _writeItem(aItem, aIndent) {
    try {
      NetUtil.newURI(aItem.uri);
    } catch (ex) {
      // If the item URI is invalid, skip the item instead of failing later.
      return;
    }

    this._write(aIndent + "<DT><A");
    this._writeAttribute("HREF", escapeUrl(aItem.uri));
    this._writeDateAttributes(aItem);
    await this._writeFaviconAttribute(aItem);

    if (aItem.keyword) {
      this._writeAttribute("SHORTCUTURL", escapeHtmlEntities(aItem.keyword));
      if (aItem.postData) {
        this._writeAttribute("POST_DATA", escapeHtmlEntities(aItem.postData));
      }
    }

    if (aItem.charset) {
      this._writeAttribute("LAST_CHARSET", escapeHtmlEntities(aItem.charset));
    }
    if (aItem.tags) {
      this._writeAttribute("TAGS", escapeHtmlEntities(aItem.tags));
    }
    this._writeLine(">" + escapeHtmlEntities(aItem.title) + "</A>");
  },

  _writeDateAttributes(aItem) {
    if (aItem.dateAdded) {
      this._writeAttribute(
        "ADD_DATE",
        Math.floor(aItem.dateAdded / MICROSEC_PER_SEC)
      );
    }
    if (aItem.lastModified) {
      this._writeAttribute(
        "LAST_MODIFIED",
        Math.floor(aItem.lastModified / MICROSEC_PER_SEC)
      );
    }
  },

  async _writeFaviconAttribute(aItem) {
    if (!aItem.iconUri) {
      return;
    }

    try {
      let favicon = await PlacesUtils.favicons.getFaviconForPage(
        PlacesUtils.toURI(aItem.uri)
      );

      this._writeAttribute("ICON_URI", escapeUrl(favicon.uri.spec));

      if (favicon?.rawData.length && !favicon.uri.schemeIs("chrome")) {
        this._writeAttribute("ICON", favicon.dataURI.spec);
      }
    } catch (ex) {
      console.error("Unexpected Error trying to fetch icon data");
    }
  },
};

/**
 * Handles inserting favicons into the database for a bookmark node.
 * It is assumed the node has already been inserted into the bookmarks
 * database.
 *
 * @param {object} node The bookmark node for icons to be inserted.
 */
function insertFaviconForNode(node) {
  if (!node.icon && !node.iconUri) {
    // No favicon information.
    return;
  }

  try {
    // If icon is not specified, suppose iconUri may contain a data uri.
    let faviconDataURI = Services.io.newURI(node.icon || node.iconUri);
    if (!faviconDataURI.schemeIs("data")) {
      return;
    }

    PlacesUtils.favicons
      .setFaviconForPage(
        Services.io.newURI(node.url),
        // Use iconUri otherwise create a fake favicon URI to use (FIXME: bug 523932)
        Services.io.newURI(node.iconUri ?? "fake-favicon-uri:" + node.url),
        faviconDataURI
      )
      .catch(console.error);
  } catch (ex) {
    console.error("Failed to import favicon data:", ex);
  }
}

/**
 * Handles inserting favicons into the database for a bookmark tree - a node
 * and its children.
 *
 * It is assumed the nodes have already been inserted into the bookmarks
 * database.
 *
 * @param {object} nodeTree The bookmark node tree for icons to be inserted.
 */
function insertFaviconsForTree(nodeTree) {
  insertFaviconForNode(nodeTree);

  if (nodeTree.children) {
    for (let child of nodeTree.children) {
      insertFaviconsForTree(child);
    }
  }
}

/**
 * Handles fetching data from a URL.
 *
 * @param {string} href The url to fetch data from.
 * @returns {Promise} Returns a promise that is resolved with the data once
 *                   the fetch is complete, or is rejected if it fails.
 */
function fetchData(href) {
  return new Promise((resolve, reject) => {
    let xhr = new XMLHttpRequest();
    xhr.onload = () => {
      resolve(xhr.responseXML);
    };
    xhr.onabort =
      xhr.onerror =
      xhr.ontimeout =
        () => {
          reject(new Error("xmlhttprequest failed"));
        };
    xhr.open("GET", href);
    xhr.responseType = "document";
    xhr.overrideMimeType("text/html");
    xhr.send();
  });
}
