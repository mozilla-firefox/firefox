/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { connect } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
// eslint-disable-next-line no-shadow
import { CSSTransition } from "react-transition-group";

const PREF_WALLPAPER_UPLOADED_PREVIOUSLY =
  "newtabWallpapers.customWallpaper.uploadedPreviously";

const PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE =
  "newtabWallpapers.customWallpaper.fileSize";

const PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE_ENABLED =
  "newtabWallpapers.customWallpaper.fileSize.enabled";

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

export class _WallpaperCategories extends React.PureComponent {
  constructor(props) {
    super(props);
    this.handleColorInput = this.handleColorInput.bind(this);
    this.debouncedHandleChange = debounce(this.handleChange.bind(this), 999);
    this.handleChange = this.handleChange.bind(this);
    this.handleReset = this.handleReset.bind(this);
    this.handleCategory = this.handleCategory.bind(this);
    this.handleUpload = this.handleUpload.bind(this);
    this.handleBack = this.handleBack.bind(this);
    this.getRGBColors = this.getRGBColors.bind(this);
    this.prefersHighContrastQuery = null;
    this.prefersDarkQuery = null;
    this.categoryRef = []; // store references for wallpaper category list
    this.wallpaperRef = []; // store reference for wallpaper selection list
    this.customColorPickerRef = React.createRef(); // Used to determine contrast icon color for custom color picker
    this.customColorInput = React.createRef(); // Used to determine contrast icon color for custom color picker
    this.state = {
      activeCategory: null,
      activeCategoryFluentID: null,
      showColorPicker: false,
      inputType: "radio",
      activeId: null,
      isCustomWallpaperError: false,
    };
  }

  componentDidMount() {
    this.prefersDarkQuery = globalThis.matchMedia(
      "(prefers-color-scheme: dark)"
    );
  }

  componentDidUpdate(prevProps) {
    // Walllpaper category subpanel should close when parent menu is closed
    if (
      this.props.exitEventFired &&
      this.props.exitEventFired !== prevProps.exitEventFired
    ) {
      this.handleBack();
    }
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

    const uploadedPreviously =
      this.props.Prefs.values[PREF_WALLPAPER_UPLOADED_PREVIOUSLY];

    this.handleUserEvent(at.WALLPAPER_CLICK, {
      selected_wallpaper: id,
      had_previous_wallpaper: !!this.props.activeWallpaper,
      had_uploaded_previously: !!uploadedPreviously,
    });
  }

  // function implementing arrow navigation for wallpaper category selection
  handleCategoryKeyDown(event, category) {
    const getIndex = this.categoryRef.findIndex(cat => cat.id === category);
    if (getIndex === -1) {
      return; // prevents errors if wallpaper index isn't found when navigating with arrow keys
    }

    const isRTL = document.dir === "rtl"; // returns true is page language is right-to-left
    let eventKey = event.key;

    if (eventKey === "ArrowRight" || eventKey === "ArrowLeft") {
      if (isRTL) {
        eventKey = eventKey === "ArrowRight" ? "ArrowLeft" : "ArrowRight";
      }
    }

    let nextIndex = getIndex;

    if (eventKey === "ArrowRight") {
      nextIndex =
        getIndex + 1 < this.categoryRef.length ? getIndex + 1 : getIndex;
    } else if (eventKey === "ArrowLeft") {
      nextIndex = getIndex - 1 >= 0 ? getIndex - 1 : getIndex;
    }

    this.categoryRef[nextIndex].focus();
  }

  // function implementing arrow navigation for wallpaper selection
  handleWallpaperKeyDown(event, title) {
    if (event.key === "Tab") {
      if (event.shiftKey) {
        event.preventDefault();
        this.backToMenuButton?.focus();
      } else {
        event.preventDefault(); // prevent tabbing within wallpaper selection. We should only be using the Tab key to tab between groups
      }
      return;
    }

    const isRTL = document.dir === "rtl"; // returns true if page language is right-to-left
    let eventKey = event.key;

    if (eventKey === "ArrowRight" || eventKey === "ArrowLeft") {
      if (isRTL) {
        eventKey = eventKey === "ArrowRight" ? "ArrowLeft" : "ArrowRight";
      }
    }

    const getIndex = this.wallpaperRef.findIndex(
      wallpaper => wallpaper.id === title
    );

    if (getIndex === -1) {
      return; // prevents errors if wallpaper index isn't found when navigating with arrow keys
    }

    // the set layout of columns per row for the wallpaper selection
    const columnCount = 3;
    let nextIndex = getIndex;

    if (eventKey === "ArrowRight") {
      nextIndex =
        getIndex + 1 < this.wallpaperRef.length ? getIndex + 1 : getIndex;
    } else if (eventKey === "ArrowLeft") {
      nextIndex = getIndex - 1 >= 0 ? getIndex - 1 : getIndex;
    } else if (eventKey === "ArrowDown") {
      nextIndex =
        getIndex + columnCount < this.wallpaperRef.length
          ? getIndex + columnCount
          : getIndex;
    } else if (eventKey === "ArrowUp") {
      nextIndex =
        getIndex - columnCount >= 0 ? getIndex - columnCount : getIndex;
    }

    this.wallpaperRef[nextIndex].tabIndex = 0;
    this.wallpaperRef[getIndex].tabIndex = -1;
    this.wallpaperRef[nextIndex].focus();
    this.wallpaperRef[nextIndex].click();
  }

  handleReset() {
    const uploadedPreviously =
      this.props.Prefs.values[PREF_WALLPAPER_UPLOADED_PREVIOUSLY];

    const selectedWallpaper =
      this.props.Prefs.values["newtabWallpapers.wallpaper"];

    // If a custom wallpaper is set, remove it
    if (selectedWallpaper === "custom") {
      this.props.dispatch(
        ac.OnlyToMain({
          type: at.WALLPAPER_REMOVE_UPLOAD,
        })
      );
    }

    // Reset active wallpaper
    this.props.setPref("newtabWallpapers.wallpaper", "");

    // Fire WALLPAPER_CLICK telemetry event
    this.handleUserEvent(at.WALLPAPER_CLICK, {
      selected_wallpaper: "none",
      had_previous_wallpaper: !!this.props.activeWallpaper,
      had_uploaded_previously: !!uploadedPreviously,
    });
  }

  handleCategory = event => {
    this.setState({ activeCategory: event.target.id });

    this.handleUserEvent(at.WALLPAPER_CATEGORY_CLICK, event.target.id);

    let fluent_id;
    switch (event.target.id) {
      case "abstracts":
        fluent_id = "newtab-wallpaper-category-title-abstract";
        break;
      case "celestial":
        fluent_id = "newtab-wallpaper-category-title-celestial";
        break;
      case "photographs":
        fluent_id = "newtab-wallpaper-category-title-photographs";
        break;
      case "solid-colors":
        fluent_id = "newtab-wallpaper-category-title-colors";
    }

    this.setState({ activeCategoryFluentID: fluent_id });
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
    fileInput.oncancel = async () => {
      this.setState({ isCustomWallpaperError: false });
    };

    // Reset error state when user begins file selection
    this.setState({ isCustomWallpaperError: false });

    // Fire when user selects a file
    fileInput.onchange = async event => {
      const [file] = event.target.files;

      // Limit image uploaded to a maximum file size if enabled
      // Note: The max file size pref (customWallpaper.fileSize) is converted to megabytes (MB)
      // Example: if pref value is 5, max file size is 5 MB
      const maxSize = wallpaperUploadMaxFileSize * 1024 * 1024;
      if (wallpaperUploadMaxFileSizeEnabled && file && file.size > maxSize) {
        console.error("File size exceeds limit");
        this.setState({ isCustomWallpaperError: true });
        return;
      }

      if (file) {
        this.props.dispatch(
          ac.OnlyToMain({
            type: at.WALLPAPER_UPLOAD,
            data: file,
          })
        );

        // Set active wallpaper ID to "custom"
        this.props.setPref("newtabWallpapers.wallpaper", "custom");

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
    this.setState({ activeCategory: null });
    this.categoryRef[0]?.focus();
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

  render() {
    const prefs = this.props.Prefs.values;
    const { wallpaperList, categories } = this.props.Wallpapers;
    const { activeWallpaper } = this.props;
    const { activeCategory, showColorPicker } = this.state;
    const { activeCategoryFluentID } = this.state;
    let filteredWallpapers = wallpaperList.filter(
      wallpaper => wallpaper.category === activeCategory
    );
    const wallpaperUploadMaxFileSize =
      this.props.Prefs.values[PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE];

    function reduceColorsToFitCustomColorInput(arr) {
      // Reduce the amount of custom colors to make space for the custom color picker
      while (arr.length % 3 !== 2) {
        arr.pop();
      }
      return arr;
    }

    let categorySectionClassname = "category wallpaper-list";
    if (prefs["newtabWallpapers.v2.enabled"]) {
      categorySectionClassname += " ignore-color-mode";
    }

    let wallpaperCustomSolidColorHex = null;

    const selectedWallpaper = prefs["newtabWallpapers.wallpaper"];

    // User has previous selected a custom color
    if (selectedWallpaper.includes("solid-color-picker")) {
      this.setState({ showColorPicker: true });
      const regex = /#([a-fA-F0-9]{6})/;
      [wallpaperCustomSolidColorHex] = selectedWallpaper.match(regex);
    }

    // Enable custom color select if pref'ed on
    this.setState({
      showColorPicker: prefs["newtabWallpapers.customColor.enabled"],
    });

    // Remove last item of solid colors to make space for custom color picker
    if (
      prefs["newtabWallpapers.customColor.enabled"] &&
      activeCategory === "solid-colors"
    ) {
      filteredWallpapers =
        reduceColorsToFitCustomColorInput(filteredWallpapers);
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

    let colorPickerInput =
      showColorPicker && activeCategory === "solid-colors" ? (
        <div
          className={`theme-custom-color-picker ${initStateClassname}`}
          ref={this.customColorPickerRef}
        >
          <input
            onInput={this.handleColorInput}
            onChange={this.debouncedHandleChange}
            onClick={() => this.setActiveId("solid-color-picker")} //
            type="color"
            name={`wallpaper-solid-color-picker`}
            id="solid-color-picker"
            // aria-checked is not applicable for input[type="color"] elements
            aria-current={this.state.activeId === "solid-color-picker"}
            value={wallpaperCustomSolidColorHex || starterColorHex}
            className={`wallpaper-input
              ${this.state.activeId === "solid-color-picker" ? "active" : ""}`}
            ref={this.customColorInput}
          />
          <label
            htmlFor="solid-color-picker"
            data-l10n-id="newtab-wallpaper-custom-color"
          ></label>
        </div>
      ) : (
        ""
      );

    return (
      <div>
        <div className="category-header">
          <h2 data-l10n-id="newtab-wallpaper-title"></h2>
          <button
            className="wallpapers-reset"
            onClick={this.handleReset}
            data-l10n-id="newtab-wallpaper-reset"
          />
        </div>
        <div
          role="grid"
          aria-label="Wallpaper category selection. Use arrow keys to navigate."
        >
          <fieldset className="category-list">
            {categories.map((category, index) => {
              const filteredList = wallpaperList.filter(
                wallpaper => wallpaper.category === category
              );
              const activeWallpaperObj =
                activeWallpaper &&
                filteredList.find(wp => wp.title === activeWallpaper);
              const thumbnail = activeWallpaperObj || filteredList[0];
              let fluent_id;
              switch (category) {
                case "abstracts":
                  fluent_id = "newtab-wallpaper-category-title-abstract";
                  break;
                case "celestial":
                  fluent_id = "newtab-wallpaper-category-title-celestial";
                  break;
                case "custom-wallpaper":
                  fluent_id = "newtab-wallpaper-upload-image";
                  break;
                case "photographs":
                  fluent_id = "newtab-wallpaper-category-title-photographs";
                  break;
                case "solid-colors":
                  fluent_id = "newtab-wallpaper-category-title-colors";
              }
              let style = {};
              if (thumbnail?.wallpaperUrl) {
                style.backgroundImage = `url(${thumbnail.wallpaperUrl})`;
              } else {
                style.backgroundColor = thumbnail?.solid_color || "";
              }
              return (
                <div key={category}>
                  <button
                    ref={el => {
                      if (el) {
                        this.categoryRef[index] = el;
                      }
                    }}
                    id={category}
                    style={style}
                    onKeyDown={e => this.handleCategoryKeyDown(e, category)}
                    // Add overrides for custom wallpaper upload UI
                    onClick={
                      category !== "custom-wallpaper"
                        ? this.handleCategory
                        : this.handleUpload
                    }
                    className={
                      category !== "custom-wallpaper"
                        ? `wallpaper-input`
                        : `wallpaper-input theme-custom-wallpaper`
                    }
                    tabIndex={index === 0 ? 0 : -1}
                    {...(category === "custom-wallpaper"
                      ? { "aria-errormessage": "customWallpaperError" }
                      : {})}
                  />
                  <label htmlFor={category} data-l10n-id={fluent_id}>
                    {fluent_id}
                  </label>
                </div>
              );
            })}
          </fieldset>
          {this.state.isCustomWallpaperError && (
            <div className="custom-wallpaper-error" id="customWallpaperError">
              <span className="icon icon-info"></span>
              <span
                data-l10n-id="newtab-wallpaper-error-max-file-size"
                data-l10n-args={`{"file_size": ${wallpaperUploadMaxFileSize}}`}
              ></span>
            </div>
          )}
        </div>

        <CSSTransition
          in={!!activeCategory}
          timeout={300}
          classNames="wallpaper-list"
          unmountOnExit={true}
        >
          <section className={categorySectionClassname}>
            <button
              className="arrow-button"
              data-l10n-id={activeCategoryFluentID}
              onClick={this.handleBack}
              ref={el => {
                this.backToMenuButton = el;
              }}
            />
            <div
              role="grid"
              aria-label="Wallpaper selection. Use arrow keys to navigate."
            >
              <fieldset>
                {filteredWallpapers.map(
                  (
                    { title, theme, fluent_id, solid_color, wallpaperUrl },
                    index
                  ) => {
                    let style = {};
                    if (wallpaperUrl) {
                      style.backgroundImage = `url(${wallpaperUrl})`;
                    } else {
                      style.backgroundColor = solid_color || "";
                    }
                    return (
                      <>
                        <input
                          ref={el => {
                            if (el) {
                              this.wallpaperRef[index] = el;
                            }
                          }}
                          onChange={this.handleChange}
                          onKeyDown={e => this.handleWallpaperKeyDown(e, title)}
                          style={style}
                          type="radio"
                          name={`wallpaper-${title}`}
                          id={title}
                          value={title}
                          checked={title === activeWallpaper}
                          aria-checked={title === activeWallpaper}
                          className={`wallpaper-input theme-${theme} ${this.state.activeId === title ? "active" : ""}`}
                          onClick={() => this.setActiveId(title)} //
                          tabIndex={index === 0 ? 0 : -1} //the first wallpaper in the array will have a tabindex of 0 so we can tab into it. The rest will have a tabindex of -1
                        />
                        <label
                          htmlFor={title}
                          className="sr-only"
                          data-l10n-id={fluent_id}
                        >
                          {fluent_id}
                        </label>
                      </>
                    );
                  }
                )}
                {colorPickerInput}
              </fieldset>
            </div>
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
  };
})(_WallpaperCategories);
