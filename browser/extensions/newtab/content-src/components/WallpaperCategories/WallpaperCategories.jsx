/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { connect } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
// eslint-disable-next-line no-shadow
import { CSSTransition } from "react-transition-group";
import { calculateTheme } from "lib/Wallpapers/WallpaperThemeUtils.mjs";
import { WALLPAPER_CATEGORIES } from "content-src/lib/constants.mjs";
import { isWallpaperLibraryEnabled } from "lib/Wallpapers/WallpaperLibraryPref.mjs";
import { getWallpaperURL } from "lib/Wallpapers/WallpaperFileNames.mjs";

// A second save can start before the first one's reply arrives. The id keeps
// that older reply from moving focus to the wrong image.
let uploadRequestSeq = 0;

const PREF_WALLPAPER_UPLOADED_PREVIOUSLY =
  "newtabWallpapers.customWallpaper.uploadedPreviously";

const PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE =
  "newtabWallpapers.customWallpaper.fileSize";

const PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE_ENABLED =
  "newtabWallpapers.customWallpaper.fileSize.enabled";

const PREF_WALLPAPER_VISIBILITY_GROUPS = "newtabWallpapers.visibilityGroups";
const PREF_WALLPAPER_CATEGORY_ORDER = "newtabWallpapers.categoryOrder";
const PREF_WALLPAPER_MAX_CATEGORIES = "newtabWallpapers.maxCategories";
const DEFAULT_MAX_CATEGORIES = 6;

// The dedicated trainhopConfig.wallpapers object wins over the pref, so a value
// can be changed without a release. The pref is never read at call sites.
function resolveTrainhopCsv(prefs, key, prefName) {
  // A trainhop payload is raw JSON from a Nimbus recipe, so a malformed value
  // must fall back to the pref rather than throw while rendering the picker.
  const trainhop = prefs.trainhopConfig?.wallpapers?.[key];
  if (trainhop !== undefined && typeof trainhop !== "string") {
    console.warn(
      `trainhopConfig.wallpapers.${key} is ${JSON.stringify(
        trainhop
      )}; expected a string. Ignoring it.`
    );
  }
  const csv =
    (typeof trainhop === "string" && trainhop) || prefs[prefName] || "";
  return csv
    .split(",")
    .map(item => item.trim())
    .filter(Boolean);
}

export const resolveWallpaperVisibilityGroups = prefs =>
  resolveTrainhopCsv(
    prefs,
    "visibilityGroups",
    PREF_WALLPAPER_VISIBILITY_GROUPS
  );

export const resolveWallpaperCategoryOrder = prefs =>
  resolveTrainhopCsv(prefs, "categoryOrder", PREF_WALLPAPER_CATEGORY_ORDER);

export function resolveWallpaperMaxCategories(prefs) {
  const isValid = value => Number.isInteger(value) && value >= 1;
  return (
    [
      prefs.trainhopConfig?.wallpapers?.maxCategories,
      prefs[PREF_WALLPAPER_MAX_CATEGORIES],
    ].find(isValid) ?? DEFAULT_MAX_CATEGORIES
  );
}

// Remote Settings records may omit either field. A missing `visible` means
// visible; a missing `visibility_group` means ungated.
export const isWallpaperOffered = (wallpaper, activeGroups) =>
  wallpaper.visible !== false &&
  (!wallpaper.visibility_group ||
    activeGroups.includes(wallpaper.visibility_group));

// Returns a function will not be continuously triggered when called. The
// function will be triggered if called again after `wait` milliseconds.
function debounce(func, wait) {
  let timer;
  return (...args) => {
    if (timer) {
      return;
    }

    let wakeUp = () => {
      timer = null;
    };

    timer = setTimeout(wakeUp, wait);
    func.apply(this, args);
  };
}

// Cells per visual row in the wallpaper picker grids. The stylesheet reads it
// through --your-images-columns, the way Top Sites uses
// --top-sites-max-per-row, so the layout and the arrow keys share one number.
const GRID_COLUMN_COUNT = 3;

export function chunkIntoRows(items, columnCount) {
  const rows = [];
  for (let i = 0; i < items.length; i += columnCount) {
    rows.push(items.slice(i, i + columnCount));
  }
  return rows;
}

// Resolves which cell a layout grid navigation key moves focus to, following
// https://www.w3.org/WAI/ARIA/apg/patterns/grid/ pattern.
export function getNextGridIndex(event, index, count, columnCount) {
  let { key } = event;
  if (document.dir === "rtl" && (key === "ArrowRight" || key === "ArrowLeft")) {
    key = key === "ArrowRight" ? "ArrowLeft" : "ArrowRight";
  }
  const lastIndex = count - 1;
  const rowStart = index - (index % columnCount);
  switch (key) {
    case "ArrowRight":
      return Math.min(index + 1, lastIndex);
    case "ArrowLeft":
      return Math.max(index - 1, 0);
    case "ArrowDown":
      // Never clamps to the last cell: on a short bottom row that would move
      // focus sideways as well as down.
      return index + columnCount <= lastIndex ? index + columnCount : index;
    case "ArrowUp":
      return index >= columnCount ? index - columnCount : index;
    case "Home":
      return event.ctrlKey ? 0 : rowStart;
    case "End":
      return event.ctrlKey
        ? lastIndex
        : Math.min(rowStart + columnCount - 1, lastIndex);
    default:
      return null;
  }
}

export class _WallpaperCategories extends React.PureComponent {
  constructor(props) {
    super(props);
    this.handleColorInput = this.handleColorInput.bind(this);
    this.debouncedHandleChange = debounce(this.handleChange.bind(this), 999);
    this.handleChange = this.handleChange.bind(this);
    this.handleReset = this.handleReset.bind(this);
    this.handleCategory = this.handleCategory.bind(this);
    this.focusCategory = this.focusCategory.bind(this);
    this.handleUpload = this.handleUpload.bind(this);
    this.handleBack = this.handleBack.bind(this);
    this.handleWallpaperListEntered =
      this.handleWallpaperListEntered.bind(this);
    this.getRGBColors = this.getRGBColors.bind(this);
    this.prefersHighContrastQuery = null;
    this.prefersDarkQuery = null;
    this.categoryRef = []; // store references for wallpaper category list
    this.wallpaperRef = []; // store reference for wallpaper selection list
    this.savedWallpaperRef = []; // store references for the "Your images" list
    this.pendingUploadId = null;
    this.uploadStartedFromTile = false;
    this.arrowButtonRef = React.createRef(); // Used to focus arrow button when category opens
    this.customColorPickerRef = React.createRef(); // Used to determine contrast icon color for custom color picker
    this.customColorInput = React.createRef(); // Used to determine contrast icon color for custom color picker
    this.wallpaperListRef = React.createRef(); // Used for CSSTransition nodeRef
    this.state = {
      inputType: "radio",
      activeId: null,
      customWallpaperErrorType: null,
      // Roving tabindex for the two layout grids: only the cell at this index
      // is in the tab order, and the arrow keys move it.
      focusedCategoryIndex: 0,
      focusedWallpaperIndex: 0,
      pendingRemoveIndex: null,
      pendingRemoveFilename: null,
      savedFocusIndex: 0,
    };
  }

  componentDidMount() {
    this.prefersDarkQuery = globalThis.matchMedia(
      "(prefers-color-scheme: dark)"
    );
    this.requestLibrary();
  }

  componentDidUpdate(prevProps) {
    this.focusAfterRemoval();
    this.focusAfterUpload();

    const showing = !!this.props.panelShowing && this.libraryEnabled;
    const wasShowing =
      !!prevProps.panelShowing &&
      isWallpaperLibraryEnabled(prevProps.Prefs.values);

    if (showing && !wasShowing) {
      this.requestLibrary();
    }

    // A CTA can deep-link into a specific wallpaper category by dispatching
    // SHOW_PERSONALIZE with a wallpaperCategory. Open it once when it appears.
    const requestedCategory = this.props.customizePanelWallpaperCategory;
    if (
      requestedCategory &&
      requestedCategory !== prevProps.customizePanelWallpaperCategory
    ) {
      this.openRequestedCategory(requestedCategory);
    }
  }

  // An applied custom color keeps the picker even with the pref off.
  get colorPickerShown() {
    const prefs = this.props.Prefs.values;
    return (
      !!prefs["newtabWallpapers.customColor.enabled"] ||
      !!prefs["newtabWallpapers.wallpaper"]?.includes("solid-color-picker")
    );
  }

  // Categories with nothing to offer are hidden, except "Your images", which
  // is the upload button until an image is saved, and solid colors while the
  // color picker is shown.
  get visibleCategories() {
    const { categories } = this.props.Wallpapers;
    const prefs = this.props.Prefs.values;
    const offered = (categories || []).filter(
      category =>
        category === WALLPAPER_CATEGORIES.CustomWallpaper ||
        (category === WALLPAPER_CATEGORIES.SolidColors &&
          this.colorPickerShown) ||
        !!this.categoryWallpapers(category).length
    );
    const order = resolveWallpaperCategoryOrder(prefs);
    const rank = category => {
      const index = order.indexOf(category);
      return index === -1 ? order.length : index;
    };
    const sorted = offered.toSorted((a, b) => rank(a) - rank(b));
    // @nova-cleanup(remove-conditional): Always cap once Nova ships
    if (!prefs["nova.enabled"]) {
      return sorted;
    }
    const kept = sorted
      .filter(category => category !== WALLPAPER_CATEGORIES.CustomWallpaper)
      .slice(
        0,
        resolveWallpaperMaxCategories(prefs) -
          (sorted.includes(WALLPAPER_CATEGORIES.CustomWallpaper) ? 1 : 0)
      );
    return sorted.filter(
      category =>
        category === WALLPAPER_CATEGORIES.CustomWallpaper ||
        kept.includes(category)
    );
  }

  openRequestedCategory(categoryId) {
    const index = this.visibleCategories.indexOf(categoryId);
    // Category may not be available in every build/region; bail if missing.
    if (index === -1) {
      return;
    }
    this.setState({ focusedCategoryIndex: index });
    this.openCategory(categoryId, { fromUser: false });
  }

  handleColorInput(event) {
    let { id } = event.target;
    // Set ID to include hex value of custom color
    id = `solid-color-picker-${event.target.value}`;
    const rgbColors = this.getRGBColors(event.target.value);

    // Set background color to custom color
    event.target.style.backgroundColor = `rgb(${rgbColors.toString()})`;

    if (this.customColorPickerRef.current) {
      const colorInputBackground =
        this.customColorPickerRef.current.children[0].style.backgroundColor;
      this.customColorPickerRef.current.style.backgroundColor =
        colorInputBackground;
    }

    // Set icon color based on the selected color
    const isColorDark = this.isWallpaperColorDark(rgbColors);
    if (this.customColorPickerRef.current) {
      if (isColorDark) {
        this.customColorPickerRef.current.classList.add("is-dark");
      } else {
        this.customColorPickerRef.current.classList.remove("is-dark");
      }

      // Remove any possible initial classes
      this.customColorPickerRef.current.classList.remove(
        "custom-color-set",
        "custom-color-dark",
        "default-color-set"
      );
    }

    // Setting this now so when we remove v1 we don't have to migrate v1 values.
    this.props.setPref("newtabWallpapers.wallpaper", id);
    this.props.setPref("newtabWallpapers.initialWallpaper", "");
    this.props.setPref("newtabWallpapers.user.enabled", true);
  }

  // Note: There's a separate event (debouncedHandleChange) that fires the handleChange
  // event but is delayed so that it doesn't fire multiple events when a user
  // is selecting a custom color background
  handleChange(event) {
    let { id } = event.target;

    // Set ID to include hex value of custom color
    if (id === "solid-color-picker") {
      id = `solid-color-picker-${event.target.value}`;
    }

    this.props.setPref("newtabWallpapers.wallpaper", id);
    this.props.setPref("newtabWallpapers.initialWallpaper", "");
    this.props.setPref("newtabWallpapers.user.enabled", true);

    const uploadedPreviously =
      this.props.Prefs.values[PREF_WALLPAPER_UPLOADED_PREVIOUSLY];

    this.handleUserEvent(at.WALLPAPER_CLICK, {
      selected_wallpaper: id,
      had_previous_wallpaper: !!this.props.activeWallpaper,
      had_uploaded_previously: !!uploadedPreviously,
    });
  }

  focusCategory(focusIndex) {
    this.categoryRef[focusIndex]?.focus();
  }

  // Keyboard handling for the category grid.
  // preventDefault keeps the arrows from scrolling the panel.
  handleCategoryKeyDown(event, index, count) {
    const nextIndex = getNextGridIndex(event, index, count, GRID_COLUMN_COUNT);
    if (nextIndex === null) {
      return;
    }
    event.preventDefault();
    if (nextIndex === index) {
      return;
    }
    this.setState({ focusedCategoryIndex: nextIndex }, () =>
      this.focusCategory(nextIndex)
    );
  }

  handleWallpaperKeyDown(event, index, count) {
    const nextIndex = getNextGridIndex(event, index, count, GRID_COLUMN_COUNT);
    if (nextIndex === null) {
      return;
    }
    event.preventDefault();
    if (nextIndex === index) {
      return;
    }
    this.setState({ focusedWallpaperIndex: nextIndex }, () => {
      const cell = this.wallpaperRef[nextIndex];
      if (!cell) {
        return;
      }
      cell.focus();
      // Clicking a radio applies that wallpaper. The color input is only
      // focused, because clicking it would open the color picker dialog.
      if (cell.type === "radio") {
        cell.click();
      }
    });
  }

  handleReset() {
    const uploadedPreviously =
      this.props.Prefs.values[PREF_WALLPAPER_UPLOADED_PREVIOUSLY];

    const selectedWallpaper =
      this.props.Prefs.values["newtabWallpapers.wallpaper"];

    // With the library off there is nowhere to see or delete a saved image, so
    // resetting still removes it, the way it did before the library existed.
    if (selectedWallpaper === "custom" && !this.libraryEnabled) {
      this.props.dispatch(
        ac.OnlyToMain({
          type: at.WALLPAPER_REMOVE_UPLOAD,
        })
      );
    }

    // Reset active wallpaper
    this.props.setPref("newtabWallpapers.wallpaper", "");
    this.props.setPref("newtabWallpapers.initialWallpaper", "");

    // Fire WALLPAPER_CLICK telemetry event
    this.handleUserEvent(at.WALLPAPER_CLICK, {
      selected_wallpaper: "none",
      had_previous_wallpaper: !!this.props.activeWallpaper,
      had_uploaded_previously: !!uploadedPreviously,
    });
  }

  categoryFluentID(categoryId) {
    switch (categoryId) {
      case WALLPAPER_CATEGORIES.Abstracts:
        return "newtab-wallpaper-category-title-abstract";
      case WALLPAPER_CATEGORIES.Celestial:
        return "newtab-wallpaper-category-title-celestial";
      case WALLPAPER_CATEGORIES.Photographs:
        return "newtab-wallpaper-category-title-photographs";
      case WALLPAPER_CATEGORIES.SolidColors:
        // @nova-cleanup(remove-conditional): Remove novaEnabled conditional and always use newtab-wallpaper-colors
        return this.props.Prefs.values["nova.enabled"]
          ? "newtab-wallpaper-colors"
          : "newtab-wallpaper-category-title-colors";
      case WALLPAPER_CATEGORIES.Firefox:
        return "newtab-wallpaper-category-title-firefox";
      case WALLPAPER_CATEGORIES.CustomWallpaper:
        return "newtab-wallpaper-your-images";
      default:
        return undefined;
    }
  }

  // A page restored from the startup cache never sees the library broadcast,
  // so what it holds can be out of date. Opening the panel reads it again.
  requestLibrary() {
    if (!this.props.panelShowing || !this.libraryEnabled) {
      return;
    }
    this.props.dispatch(
      ac.OnlyToMain({ type: at.WALLPAPERS_CUSTOM_LIBRARY_REQUEST })
    );
  }

  get libraryEnabled() {
    return isWallpaperLibraryEnabled(this.props.Prefs.values);
  }

  get savedWallpapers() {
    const saved = this.props.Wallpapers.customWallpapers || [];

    if (this.libraryEnabled) {
      return saved;
    }

    // Without the library this is the one custom wallpaper someone has, which
    // is what the picker showed before "Your images" existed.
    const applied =
      this.props.Prefs.values["newtabWallpapers.customWallpaper.uuid"];
    return saved.filter(wallpaper => wallpaper.filename === applied);
  }

  get appliedSavedWallpaper() {
    const filename =
      this.props.Prefs.values["newtabWallpapers.customWallpaper.uuid"];
    const isCustomSelected =
      this.props.Prefs.values["newtabWallpapers.wallpaper"] === "custom";
    if (!isCustomSelected || !filename) {
      return null;
    }
    return this.savedWallpapers.find(
      wallpaper => wallpaper.filename === filename
    );
  }

  handleSavedWallpaper(wallpaper, index) {
    // Assistive technology can activate a radio without focusing it first, so
    // the roving tab stop moves here as well as on focus.
    if (index !== undefined) {
      this.setState({ savedFocusIndex: index });
    }
    const uploadedPreviously =
      this.props.Prefs.values[PREF_WALLPAPER_UPLOADED_PREVIOUSLY];

    // The parent copies the image out of the library and sets every pref that
    // names it, including the selection. It can refuse, for an image another
    // tab deleted, and setting them here would leave the page pointing at one
    // it cannot show.
    this.props.dispatch(
      ac.OnlyToMain({
        type: at.WALLPAPERS_CUSTOM_APPLY,
        data: { filename: wallpaper.filename },
      })
    );

    this.handleUserEvent(at.WALLPAPER_CLICK, {
      selected_wallpaper: "custom",
      had_previous_wallpaper: !!this.props.activeWallpaper,
      had_uploaded_previously: !!uploadedPreviously,
    });
  }

  handleRemoveSavedWallpaper(wallpaper, index) {
    // The name rather than the slot: the dialog has no cancel callback, so a
    // canceled removal is told apart by its image still being there.
    this.setState({
      pendingRemoveIndex: index,
      pendingRemoveFilename: wallpaper.filename,
    });

    this.props.dispatch({
      type: at.DIALOG_OPEN,
      data: {
        onConfirm: [
          ac.OnlyToMain({
            type: at.WALLPAPER_REMOVE_UPLOAD,
            data: { filename: wallpaper.filename },
          }),
          ac.AlsoToMain({ type: at.DIALOG_CLOSE }),
        ],
        eventSource: "WALLPAPERS",
        body_string_id: [
          "newtab-wallpaper-remove-image-title",
          "newtab-wallpaper-remove-image-body",
        ],
        confirm_button_string_id: "newtab-wallpaper-remove-image-confirm",
        confirm_button_type: "primary",
        cancel_button_string_id: "newtab-wallpaper-remove-image-cancel",
      },
    });
  }

  // What a screen reader calls this image. A kept Picture of the Day carries
  // its description and a rescued Firefox wallpaper its name. An image someone
  // added has neither and goes by its number, which every saved image has.
  renderSavedWallpaperLabel(id, { number, fallbackName }) {
    // The name goes through Fluent like every other one in the picker, so it
    // picks up the same bidi isolation the remove button's copy of it gets.
    return (
      <label
        id={`${id}-label`}
        htmlFor={id}
        className="sr-only"
        // Hidden from assistive technology; the control is named from this text through aria-labelledby
        aria-hidden="true"
        data-l10n-id={
          fallbackName
            ? "newtab-wallpaper-your-images-item"
            : "newtab-wallpaper-your-images-item-numbered"
        }
        data-l10n-args={JSON.stringify(
          fallbackName ? { name: fallbackName } : { number }
        )}
      ></label>
    );
  }

  // The answer to one upload, so it is stale once acted on. Local dispatch,
  // since no other tab is waiting on it.
  clearUploadResult() {
    this.props.dispatch({ type: at.WALLPAPER_UPLOAD_RESULT, data: null });
  }

  // An upload lands at the top of the list and becomes the applied image, so the
  // tab stop moves to it. Focus follows only if it was already in this folder.
  focusAfterUpload() {
    const result = this.props.Wallpapers.uploadResult;
    if (!this.pendingUploadId || result?.requestId !== this.pendingUploadId) {
      return;
    }

    if (!result.filename) {
      this.pendingUploadId = null;
      this.clearUploadResult();
      return;
    }

    // The folder is not open on a first save, since the tile that started it
    // was still "Add an image". Focus the tile that replaced it.
    if (this.props.activeCategory !== WALLPAPER_CATEGORIES.CustomWallpaper) {
      this.pendingUploadId = null;
      this.clearUploadResult();
      if (this.uploadStartedFromTile) {
        this.uploadStartedFromTile = false;
        document.querySelector("#custom-wallpaper")?.focus();
      }
      return;
    }

    const addedIndex = this.savedWallpapers.findIndex(
      wallpaper => wallpaper.filename === result.filename
    );
    if (addedIndex === -1) {
      return;
    }

    this.pendingUploadId = null;
    this.clearUploadResult();

    const hadFocus = this.savedWallpaperRef.some(
      element => element && element === element.ownerDocument.activeElement
    );

    this.setState({ savedFocusIndex: addedIndex }, () => {
      if (hadFocus) {
        this.savedWallpaperRef[addedIndex]?.focus();
      }
    });
  }

  // The removed tile is gone, so focus whatever took its place. The tile that
  // adds an image is always last, so there is always something to focus.
  focusAfterRemoval() {
    const { pendingRemoveIndex, pendingRemoveFilename } = this.state;
    if (
      pendingRemoveIndex === null ||
      this.props.activeCategory !== WALLPAPER_CATEGORIES.CustomWallpaper
    ) {
      return;
    }

    const current = this.savedWallpapers;
    // Still listed means the dialog was canceled, or something else was
    // removed. Either way this is not the removal that was asked for here.
    if (current.some(w => w.filename === pendingRemoveFilename)) {
      return;
    }
    const nextIndex = Math.min(pendingRemoveIndex, current.length);
    this.setState(
      {
        pendingRemoveIndex: null,
        pendingRemoveFilename: null,
        savedFocusIndex: nextIndex,
      },
      () => {
        this.savedWallpaperRef[nextIndex]?.focus();
      }
    );
  }

  // Arrow key navigation for "Your images". The last tile adds an image, so it
  // is focused rather than activated.
  handleSavedWallpaperKeyDown(event, index) {
    const nextIndex = getNextGridIndex(
      event,
      index,
      this.savedWallpaperRef.length,
      GRID_COLUMN_COUNT
    );
    if (nextIndex === null) {
      return;
    }

    // Consume it either way. Left at the grid edge the native radio group would
    // wrap around and apply a different wallpaper.
    event.preventDefault();

    const next = this.savedWallpaperRef[nextIndex];
    if (!next || nextIndex === index) {
      return;
    }

    this.setState({ savedFocusIndex: nextIndex }, () => {
      next.focus();
      if (next.tagName === "INPUT") {
        next.click();
      }
    });
  }

  // The wallpaper cells a category renders, in grid order. render() and
  // openCategory share it so the tab stop cannot point at a different cell
  // than the grid draws.
  categoryWallpapers(categoryId) {
    const prefs = this.props.Prefs.values;
    const activeGroups = resolveWallpaperVisibilityGroups(prefs);
    const offered = (this.props.Wallpapers.wallpaperList || []).filter(
      wallpaper =>
        wallpaper.category === categoryId &&
        isWallpaperOffered(wallpaper, activeGroups)
    );
    // the last item of the solid colors category should be the color picker
    if (
      prefs["newtabWallpapers.customColor.enabled"] &&
      categoryId === "solid-colors"
    ) {
      while (
        offered.length &&
        offered.length % GRID_COLUMN_COUNT !== GRID_COLUMN_COUNT - 1
      ) {
        offered.pop();
      }
    }
    return offered;
  }

  // Which cell a category should open on. The applied wallpaper usually belongs
  // to a different category, so this falls back to the first cell.
  appliedWallpaperIndex(categoryId) {
    const { activeWallpaper } = this.props;
    if (!activeWallpaper) {
      return 0;
    }
    // A custom color is applied through the color input, which is the last cell
    // of the Colors grid rather than one of the wallpapers.
    if (activeWallpaper.startsWith("solid-color-picker")) {
      return categoryId === "solid-colors"
        ? this.categoryWallpapers(categoryId).length
        : 0;
    }
    const appliedIndex = this.categoryWallpapers(categoryId).findIndex(
      wallpaper => wallpaper.title === activeWallpaper
    );
    return Math.max(appliedIndex, 0);
  }

  // Open a wallpaper category subpanel. `fromUser` distinguishes a real category
  // click (records telemetry) from a programmatic deep-link via a CTA.
  openCategory(categoryId, { fromUser = true } = {}) {
    // Entering "Your images" starts the tab stop on the applied image.
    const applied = this.appliedSavedWallpaper;
    const appliedIndex = applied ? this.savedWallpapers.indexOf(applied) : 0;

    this.setState({
      savedFocusIndex: Math.max(appliedIndex, 0),
      focusedWallpaperIndex: this.appliedWallpaperIndex(categoryId),
    });
    this.props.openPanel(categoryId);

    if (fromUser) {
      this.handleUserEvent(at.WALLPAPER_CATEGORY_CLICK, categoryId);
    }
  }

  handleCategory = event => {
    this.openCategory(event.target.id);
  };

  // Custom wallpaper image upload
  async handleUpload() {
    const wallpaperUploadMaxFileSizeEnabled =
      this.props.Prefs.values[PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE_ENABLED];

    const wallpaperUploadMaxFileSize =
      this.props.Prefs.values[PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE];

    const uploadedPreviously =
      this.props.Prefs.values[PREF_WALLPAPER_UPLOADED_PREVIOUSLY];

    // Create a file input since category buttons are radio inputs
    const fileInput = document.createElement("input");
    fileInput.type = "file";
    fileInput.accept = "image/*"; // only allow image files

    // Catch cancel events
    fileInput.oncancel = () => {
      this.setState({ customWallpaperErrorType: null });
    };

    // Reset error state when user begins file selection
    this.setState({ customWallpaperErrorType: null });

    // Fire when user selects a file
    fileInput.onchange = async event => {
      const [file] = event.target.files;

      if (file) {
        // Validate file type: Only accept files with a valid image MIME type
        const isValidImage = file.type && file.type.startsWith("image/");
        if (!isValidImage) {
          console.error("Invalid file type");
          this.setState({ customWallpaperErrorType: "fileType" });
          return;
        }

        // Limit image uploaded to a maximum file size if enabled
        // Note: The max file size pref (customWallpaper.fileSize) is converted to megabytes (MB)
        // Example: if pref value is 5, max file size is 5 MB
        const maxSize = wallpaperUploadMaxFileSize * 1024 * 1024;
        if (wallpaperUploadMaxFileSizeEnabled && file.size > maxSize) {
          console.error("File size exceeds limit");
          this.setState({ customWallpaperErrorType: "fileSize" });
          return;
        }

        let theme;
        try {
          theme = await calculateTheme(globalThis, file);
        } catch (e) {
          console.error("Failed to decode wallpaper image", e);
          this.setState({ customWallpaperErrorType: "fileType" });
          return;
        }

        const requestId = String(++uploadRequestSeq);
        this.pendingUploadId = requestId;
        // The first save turns the "Add an image" tile into the folder, and the
        // two use different keys, so React replaces the node and focus lands on
        // the body. Remember where it was so it can be put back.
        const tile = document.querySelector("#custom-wallpaper");
        this.uploadStartedFromTile =
          !!tile && tile === tile.ownerDocument.activeElement;

        this.props.dispatch(
          ac.OnlyToMain({
            type: at.WALLPAPER_UPLOAD,
            data: { file, theme, requestId },
          })
        );

        // Set active wallpaper ID to "custom"
        this.props.setPref("newtabWallpapers.wallpaper", "custom");
        this.props.setPref("newtabWallpapers.initialWallpaper", "");
        this.props.setPref("newtabWallpapers.user.enabled", true);

        // Update the uploadedPreviously pref to TRUE
        // Note: this pref used for telemetry. Do not reset to false.
        this.props.setPref(PREF_WALLPAPER_UPLOADED_PREVIOUSLY, true);

        this.handleUserEvent(at.WALLPAPER_CLICK, {
          selected_wallpaper: "custom",
          had_previous_wallpaper: !!this.props.activeWallpaper,
          had_uploaded_previously: !!uploadedPreviously,
        });
      }
    };

    fileInput.click();
  }

  handleBack() {
    this.props.closePanel();
    // Wait for the parent's state update before moving focus back to the category tile.
    requestAnimationFrame(() => {
      this.focusCategory(this.state.focusedCategoryIndex);
    });
  }

  handleWallpaperListEntered() {
    this.arrowButtonRef.current?.focus();
  }

  // Record user interaction when changing wallpaper and reseting wallpaper to default
  handleUserEvent(type, data) {
    this.props.dispatch(ac.OnlyToMain({ type, data }));
  }

  setActiveId = id => {
    this.setState({ activeId: id }); // Set the active ID
  };

  getRGBColors(input) {
    if (input.length !== 7) {
      return [];
    }

    const r = parseInt(input.substr(1, 2), 16);
    const g = parseInt(input.substr(3, 2), 16);
    const b = parseInt(input.substr(5, 2), 16);

    return [r, g, b];
  }

  isWallpaperColorDark([r, g, b]) {
    return 0.2125 * r + 0.7154 * g + 0.0721 * b <= 110;
  }

  renderCustomWallpaperError(id) {
    if (!this.state.customWallpaperErrorType) {
      return null;
    }

    const wallpaperUploadMaxFileSize =
      this.props.Prefs.values[PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE];

    return (
      <div className="custom-wallpaper-error" id={id} role="alert">
        <span className="icon icon-info"></span>
        {this.state.customWallpaperErrorType === "fileSize" ? (
          <span
            data-l10n-id="newtab-wallpaper-error-max-file-size"
            data-l10n-args={`{"file_size": ${wallpaperUploadMaxFileSize}}`}
          ></span>
        ) : (
          <span data-l10n-id="newtab-wallpaper-error-upload-file-type"></span>
        )}
      </div>
    );
  }

  // The "Your images" folder: everything saved, then a tile to add another.
  renderYourImages() {
    const applied = this.appliedSavedWallpaper;
    const { savedWallpapers } = this;
    this.savedWallpaperRef.length = savedWallpapers.length + 1;

    return (
      <fieldset
        className="your-images"
        style={{ "--your-images-columns": GRID_COLUMN_COUNT }}
      >
        <legend
          className="sr-only"
          data-l10n-id="newtab-wallpaper-your-images"
        ></legend>
        {savedWallpapers.map((wallpaper, index) => {
          const { filename, position, number, fallbackName } = wallpaper;
          // Named the same way as the tile it belongs to.
          const removeId = fallbackName
            ? "newtab-wallpaper-remove-image"
            : "newtab-wallpaper-remove-image-numbered";
          const removeArgs = JSON.stringify(
            fallbackName ? { name: fallbackName } : { number }
          );
          const url = getWallpaperURL(filename, this.libraryEnabled);
          const id = `your-images-${filename}`;
          const isApplied = applied?.filename === filename;
          return (
            <div className="your-images-item" key={filename}>
              {url && (
                <img
                  className="your-images-preview"
                  src={url}
                  alt=""
                  aria-hidden="true"
                  draggable="false"
                  loading="lazy"
                  style={{ objectPosition: position }}
                />
              )}
              <input
                ref={el => {
                  if (el) {
                    this.savedWallpaperRef[index] = el;
                  }
                }}
                onChange={() => this.handleSavedWallpaper(wallpaper, index)}
                onFocus={() => this.setState({ savedFocusIndex: index })}
                onKeyDown={e => this.handleSavedWallpaperKeyDown(e, index)}
                type="radio"
                name="wallpaper-your-images"
                id={id}
                value={filename}
                checked={isApplied}
                className="wallpaper-input"
                tabIndex={index === this.state.savedFocusIndex ? 0 : -1}
                aria-labelledby={`${id}-label`}
              />
              {this.renderSavedWallpaperLabel(id, wallpaper)}
              <moz-button
                type="ghost"
                size="small"
                className="your-images-remove"
                iconSrc="chrome://global/skin/icons/close.svg"
                data-l10n-id={removeId}
                data-l10n-args={removeArgs}
                tabIndex={index === this.state.savedFocusIndex ? 0 : -1}
                onClick={() =>
                  this.handleRemoveSavedWallpaper(wallpaper, index)
                }
              ></moz-button>
            </div>
          );
        })}
        <div className="your-images-item">
          <button
            ref={el => {
              if (el) {
                this.savedWallpaperRef[savedWallpapers.length] = el;
              }
            }}
            id="your-images-add"
            className="wallpaper-input theme-custom-wallpaper"
            onFocus={() =>
              this.setState({ savedFocusIndex: savedWallpapers.length })
            }
            onClick={this.handleUpload}
            onKeyDown={e =>
              this.handleSavedWallpaperKeyDown(e, savedWallpapers.length)
            }
            tabIndex={
              savedWallpapers.length === this.state.savedFocusIndex ? 0 : -1
            }
            aria-labelledby="your-images-add-label"
          />
          <label
            id="your-images-add-label"
            htmlFor="your-images-add"
            // Deliberately hidden from assistive technology even though the text is visible
            aria-hidden="true"
            data-l10n-id="newtab-wallpaper-add-an-image"
          ></label>
        </div>
      </fieldset>
    );
  }

  sortWallpapersByOrder(wallpapers) {
    return wallpapers.sort((a, b) => {
      const aOrder = a.order || 0;
      const bOrder = b.order || 0;
      if (aOrder === 0 && bOrder === 0) {
        return 0;
      }
      if (aOrder === 0) {
        return 1;
      }
      if (bOrder === 0) {
        return -1;
      }
      return aOrder - bOrder;
    });
  }

  render() {
    const prefs = this.props.Prefs.values;
    // @nova-cleanup(remove-conditional): Remove novaEnabled once Nova ships
    const novaEnabled = prefs["nova.enabled"];
    const { wallpaperList } = this.props.Wallpapers;
    const categories = this.visibleCategories;
    const { savedWallpapers } = this;
    const hasSavedWallpapers = !!savedWallpapers.length;
    // Without the library the tile stays the single upload button it was, so
    // there is no folder to browse and no folder to put an error in.
    const showYourImagesFolder = hasSavedWallpapers && this.libraryEnabled;
    const { activeWallpaper, activeCategory, showPanel } = this.props;
    const activeCategoryFluentID = this.categoryFluentID(activeCategory);
    // @nova-cleanup(remove-conditional): Remove novaEnabled check, keep arrowIconSrc computation
    let arrowIconSrc;
    if (novaEnabled) {
      const isRTL = typeof document !== "undefined" && document.dir === "rtl";
      arrowIconSrc = `chrome://global/skin/icons/shaft-arrow-${isRTL ? "right" : "left"}.svg`;
    }
    // Enable custom color select if pref'ed on
    const showColorPicker = this.colorPickerShown;
    const activeGroups = resolveWallpaperVisibilityGroups(prefs);
    const filteredWallpapers = this.categoryWallpapers(activeCategory);

    let wallpaperCustomSolidColorHex = null;

    const wallpapersUserEnabled = prefs["newtabWallpapers.user.enabled"];
    const selectedWallpaper = prefs["newtabWallpapers.wallpaper"];

    // User has previous selected a custom color
    if (selectedWallpaper.includes("solid-color-picker")) {
      const regex = /#([a-fA-F0-9]{6})/;
      [wallpaperCustomSolidColorHex] = selectedWallpaper.match(regex);
    }

    // Bug 1953012 - If nothing selected, default to color of customize panel
    // --color-blue-70 : #054096
    // --color-blue-05 : #deeafc
    const starterColorHex = this.prefersDarkQuery?.matches
      ? "#054096"
      : "#deeafc";

    // Set initial state of the color picker (depending if the user has already set a custom color)
    let initStateClassname = wallpaperCustomSolidColorHex
      ? "custom-color-set"
      : "default-color-set";

    // If a custom color picker is set, make sure the icon has the correct contrast
    if (wallpaperCustomSolidColorHex) {
      const rgbColors = this.getRGBColors(wallpaperCustomSolidColorHex);
      const isColorDark = this.isWallpaperColorDark(rgbColors);
      if (isColorDark) {
        initStateClassname += " custom-color-dark";
      }
    }

    // The custom color input is the last cell of the Colors grid and now takes
    // part in the roving index.
    const showCustomColorPicker =
      showColorPicker && activeCategory === "solid-colors";
    const colorPickerIndex = filteredWallpapers.length;
    const wallpaperCellCount =
      filteredWallpapers.length + (showCustomColorPicker ? 1 : 0);
    const focusedWallpaperIndex = Math.min(
      this.state.focusedWallpaperIndex,
      Math.max(wallpaperCellCount - 1, 0)
    );

    const colorPickerInput = showCustomColorPicker ? (
      <div
        role="gridcell"
        key="solid-color-picker"
        className={`theme-custom-color-picker ${initStateClassname}`}
        ref={this.customColorPickerRef}
      >
        <input
          ref={el => {
            this.customColorInput.current = el;
            if (el) {
              this.wallpaperRef[colorPickerIndex] = el;
            }
          }}
          onInput={this.handleColorInput}
          onChange={this.debouncedHandleChange}
          onClick={() => this.setActiveId("solid-color-picker")} //
          onKeyDown={e =>
            this.handleWallpaperKeyDown(e, colorPickerIndex, wallpaperCellCount)
          }
          type="color"
          name={`wallpaper-solid-color-picker`}
          id="solid-color-picker"
          // aria-checked is not applicable for input[type="color"] elements
          aria-current={this.state.activeId === "solid-color-picker"}
          value={wallpaperCustomSolidColorHex || starterColorHex}
          className={`wallpaper-input
            ${this.state.activeId === "solid-color-picker" ? "active" : ""}`}
          tabIndex={focusedWallpaperIndex === colorPickerIndex ? 0 : -1}
          aria-labelledby="solid-color-picker-label"
        />
        <label
          id="solid-color-picker-label"
          htmlFor="solid-color-picker"
          // Deliberately hidden from assistive technology even though the text is visible
          aria-hidden="true"
          data-l10n-id="newtab-wallpaper-custom-color"
        ></label>
      </div>
    ) : null;

    // Both pickers follow the ARIA layout grid pattern
    // (https://www.w3.org/WAI/ARIA/apg/patterns/grid/): grid > row > gridcell,
    // each cell holding exactly one control.
    // Clamped so a shrinking category list cannot leave the grid without a tab
    // stop, the same way focusedWallpaperIndex is clamped.
    const focusedCategoryIndex = Math.min(
      this.state.focusedCategoryIndex,
      Math.max(categories.length - 1, 0)
    );
    const categoryCells = categories.map((category, index) => {
      const filteredList = wallpaperList.filter(
        wallpaper =>
          wallpaper.category === category &&
          isWallpaperOffered(wallpaper, activeGroups)
      );
      const sortedList = this.sortWallpapersByOrder(filteredList);
      const activeWallpaperObj =
        activeWallpaper && sortedList.find(wp => wp.title === activeWallpaper);
      // Detect custom solid color
      const isCustomSolidColor =
        category === "solid-colors" &&
        activeWallpaper.startsWith("solid-color-picker");
      const thumbnail = activeWallpaperObj || sortedList[0];
      // The custom wallpaper tile adds an image until something is
      // saved, and is the "Your images" folder after that.
      const isYourImagesFolder =
        category === WALLPAPER_CATEGORIES.CustomWallpaper &&
        showYourImagesFolder;
      let fluent_id;
      if (category === WALLPAPER_CATEGORIES.CustomWallpaper) {
        // @nova-cleanup(remove-conditional): Remove novaEnabled conditional and always use newtab-wallpaper-add-an-image
        const addImageFluentID = novaEnabled
          ? "newtab-wallpaper-add-an-image"
          : "newtab-wallpaper-upload-image";
        fluent_id = isYourImagesFolder
          ? "newtab-wallpaper-your-images"
          : addImageFluentID;
      } else {
        fluent_id = this.categoryFluentID(category);
      }
      let style = {};
      let folderPreview = null;
      let folderHasPreview = false;
      if (isYourImagesFolder) {
        // The applied image and its URL reach the page ahead of the library,
        // so prefer them. The library can still name the previous image,
        // which would show the wrong picture entirely.
        const appliedUrl =
          prefs["newtabWallpapers.wallpaper"] === "custom"
            ? this.props.Wallpapers.uploadedWallpaper
            : null;
        const preview = this.appliedSavedWallpaper || savedWallpapers[0];
        const previewUrl =
          appliedUrl || getWallpaperURL(preview.filename, this.libraryEnabled);
        if (previewUrl) {
          folderHasPreview = true;
          // An img for the same reason the tiles in the folder are: a CSS
          // background holds a full resolution decode of the photo.
          folderPreview = (
            <img
              className="your-images-preview"
              src={previewUrl}
              alt=""
              aria-hidden="true"
              draggable="false"
              style={{
                objectPosition: appliedUrl
                  ? prefs["newtabWallpapers.customWallpaper.position"] ||
                    "center"
                  : preview.position,
              }}
            />
          );
        }
      } else if (thumbnail?.wallpaperUrl) {
        style.backgroundImage = `url(${thumbnail?.thumbnail || thumbnail?.wallpaperUrl})`;
        style.backgroundPosition = thumbnail.background_position || "center";
      } else {
        style.backgroundColor = thumbnail?.solid_color || "";
      }
      // If custom solid color is active, override the thumbnail to the chosen hex
      if (isCustomSolidColor) {
        const hex = activeWallpaper.split("solid-color-picker-")[1] || "";
        style.backgroundColor = hex;
      }
      const isCategorySelected =
        wallpapersUserEnabled &&
        (activeWallpaperObj ||
          isCustomSolidColor ||
          (isYourImagesFolder && this.appliedSavedWallpaper));
      return (
        <div role="gridcell" key={category}>
          <button
            // Fluent only tidies the attributes of elements it is still
            // translating, so dropping data-l10n-id when this stops
            // being the folder would strand its aria-label on the node.
            // A new key means a new element with nothing left over.
            key={isYourImagesFolder ? "your-images" : "add-an-image"}
            ref={el => {
              if (el) {
                this.categoryRef[index] = el;
              }
            }}
            id={category}
            style={style}
            onKeyDown={e =>
              this.handleCategoryKeyDown(e, index, categories.length)
            }
            // Add overrides for custom wallpaper upload UI
            onClick={event => {
              this.setState({ focusedCategoryIndex: index });
              if (
                category !== WALLPAPER_CATEGORIES.CustomWallpaper ||
                isYourImagesFolder
              ) {
                this.handleCategory(event);
              } else {
                this.handleUpload();
              }
            }}
            className={`wallpaper-input
                      ${category === WALLPAPER_CATEGORIES.CustomWallpaper && (!isYourImagesFolder || !folderHasPreview) ? "theme-custom-wallpaper" : ""}
                      ${isYourImagesFolder ? "your-images-folder" : ""}
                      ${isCategorySelected ? "selected" : ""}`}
            tabIndex={focusedCategoryIndex === index ? 0 : -1}
            // The folder tile is named by Fluent through data-l10n-id below,
            // so the hidden label only names the other tiles.
            aria-labelledby={
              isYourImagesFolder ? undefined : `${category}-label`
            }
            data-l10n-id={
              isYourImagesFolder
                ? "newtab-wallpaper-your-images-folder"
                : undefined
            }
            aria-expanded={
              // The upload tile opens a file picker rather than the
              // subpanel, so it is the one tile this does not describe.
              isYourImagesFolder ||
              category !== WALLPAPER_CATEGORIES.CustomWallpaper
                ? activeCategory === category
                : undefined
            }
          >
            {folderPreview}
          </button>
          <label
            id={`${category}-label`}
            htmlFor={category}
            // Deliberately hidden from assistive technology even though the text is visible
            aria-hidden="true"
            data-l10n-id={fluent_id}
          >
            {fluent_id}
          </label>
        </div>
      );
    });

    // Same structure for the wallpaper grid. The sorted position is the cell
    // index; the color input, when shown, is appended as the last cell.
    const wallpaperCells = this.sortWallpapersByOrder(filteredWallpapers).map(
      (
        {
          background_position,
          fluent_id,
          solid_color,
          theme,
          title,
          thumbnail,
          wallpaperUrl,
        },
        index
      ) => {
        let style = {};
        if (wallpaperUrl) {
          style.backgroundImage = `url(${thumbnail || wallpaperUrl})`;
          style.backgroundPosition = background_position || "center";
        } else {
          style.backgroundColor = solid_color || "";
        }
        // Named from the hidden label the same way as the category buttons.
        return (
          <div role="gridcell" key={title}>
            <input
              ref={el => {
                if (el) {
                  this.wallpaperRef[index] = el;
                }
              }}
              onChange={this.handleChange}
              onKeyDown={e =>
                this.handleWallpaperKeyDown(e, index, wallpaperCellCount)
              }
              style={style}
              type="radio"
              name={`wallpaper-${activeCategory}`}
              id={title}
              value={title}
              checked={wallpapersUserEnabled && title === activeWallpaper}
              className={`wallpaper-input theme-${theme} ${this.state.activeId === title ? "active" : ""}`}
              onClick={() => this.setActiveId(title)} //
              tabIndex={focusedWallpaperIndex === index ? 0 : -1}
              aria-labelledby={`${title}-label`}
            />
            <label
              id={`${title}-label`}
              htmlFor={title}
              className="sr-only"
              // Hidden from assistive technology; the control is named from this text through aria-labelledby
              aria-hidden="true"
              data-l10n-id={fluent_id}
            >
              {fluent_id}
            </label>
          </div>
        );
      }
    );
    if (colorPickerInput) {
      wallpaperCells.push(colorPickerInput);
    }

    return (
      // @nova-cleanup(remove-conditional): Remove nova-enabled class from root div
      <div className={novaEnabled ? "nova-enabled" : undefined}>
        <div className="category-header">
          {
            // @nova-cleanup(remove-conditional): Remove h2 once Nova ships — title moves to the wallpaper toggle
            !novaEnabled && <h2 data-l10n-id="newtab-wallpaper-title"></h2>
          }
          {
            // @nova-cleanup(remove-conditional): Remove reset button once Nova ships — toggle handles reset
            !novaEnabled && (
              <button
                className="wallpapers-reset"
                onClick={this.handleReset}
                data-l10n-id="newtab-wallpaper-reset"
              />
            )
          }
        </div>
        <div>
          {/* Each row is its own three-column CSS grid and the container
              stacks the rows, so the layout matches the row/cell structure
              the accessibility tree needs. */}
          <div
            role="grid"
            className="category-list"
            data-l10n-id="newtab-wallpaper-category-list"
          >
            {chunkIntoRows(categoryCells, GRID_COLUMN_COUNT).map(
              (row, rowIndex) => (
                <div role="row" className="wallpaper-grid-row" key={rowIndex}>
                  {row}
                </div>
              )
            )}
          </div>
          {!showYourImagesFolder &&
            this.renderCustomWallpaperError("customWallpaperError")}
        </div>

        <CSSTransition
          nodeRef={this.wallpaperListRef}
          in={!!showPanel}
          timeout={300}
          classNames="wallpaper-list"
          unmountOnExit={true}
          onEntered={this.handleWallpaperListEntered}
        >
          <section
            ref={this.wallpaperListRef}
            className="category wallpaper-list ignore-color-mode"
          >
            {
              // @nova-cleanup(remove-conditional): Remove novaEnabled check and the else branch, keep the nova branch
              novaEnabled ? (
                <div className="arrow-wrapper">
                  <moz-button
                    ref={this.arrowButtonRef}
                    type="ghost"
                    className="arrow-button"
                    iconSrc={arrowIconSrc}
                    data-l10n-id="newtab-customize-panel-back-button"
                    onClick={this.handleBack}
                  />
                  <h2
                    id="wallpaper-list-title"
                    data-l10n-id={activeCategoryFluentID}
                  ></h2>
                </div>
              ) : (
                <button
                  ref={this.arrowButtonRef}
                  id="wallpaper-list-title"
                  className="arrow-button"
                  data-l10n-id={activeCategoryFluentID}
                  onClick={this.handleBack}
                />
              )
            }
            {/* "Your images" keeps its own list. Every other category is a
                layout grid like the category picker, named by the heading
                above it. */}
            {activeCategory === WALLPAPER_CATEGORIES.CustomWallpaper ? (
              this.renderYourImages()
            ) : (
              <div
                role="grid"
                className="wallpaper-grid"
                aria-labelledby="wallpaper-list-title"
              >
                {chunkIntoRows(wallpaperCells, GRID_COLUMN_COUNT).map(
                  (row, rowIndex) => (
                    <div
                      role="row"
                      className="wallpaper-grid-row"
                      key={rowIndex}
                    >
                      {row}
                    </div>
                  )
                )}
              </div>
            )}
            {activeCategory === WALLPAPER_CATEGORIES.CustomWallpaper &&
              this.renderCustomWallpaperError("yourImagesWallpaperError")}
          </section>
        </CSSTransition>
      </div>
    );
  }
}

export const WallpaperCategories = connect(state => {
  return {
    Wallpapers: state.Wallpapers,
    Prefs: state.Prefs,
    customizePanelWallpaperCategory: state.App.customizePanelWallpaperCategory,
  };
})(_WallpaperCategories);
