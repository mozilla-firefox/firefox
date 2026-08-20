/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
// Get the theme variables from the app resource directory.
// This allows per-app variables.
ChromeUtils.defineESModuleGetters(lazy, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ThemeContentPropertyList: "resource:///modules/ThemeVariableMap.sys.mjs",
  ThemeVariableMap: "resource:///modules/ThemeVariableMap.sys.mjs",
  BuiltInThemeConfig: "resource:///modules/BuiltInThemeConfig.sys.mjs",
  LightweightThemeManager:
    "resource://gre/modules/LightweightThemeManager.sys.mjs",
});

// Whether the content and chrome areas should always use the same color
// scheme (unless user-overridden). Thunderbird uses this.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "BROWSER_THEME_UNIFIED_COLOR_SCHEME",
  "browser.theme.unified-color-scheme",
  false
);

const DEFAULT_THEME_ID = "default-theme@mozilla.org";

const toolkitVariableMap = [
  [
    "--lwt-accent-color",
    {
      lwtProperty: "accentcolor",
      processColor(rgbaChannels) {
        if (!rgbaChannels || rgbaChannels.a == 0) {
          return "white";
        }
        // Remove the alpha channel
        const { r, g, b } = rgbaChannels;
        return `rgb(${r}, ${g}, ${b})`;
      },
    },
  ],
  [
    "--lwt-text-color",
    {
      lwtProperty: "textcolor",
      processColor(rgbaChannels) {
        if (!rgbaChannels) {
          rgbaChannels = { r: 0, g: 0, b: 0 };
        }
        // Remove the alpha channel
        const { r, g, b } = rgbaChannels;
        return `rgba(${r}, ${g}, ${b})`;
      },
    },
  ],
  [
    "--panel-background-color",
    {
      lwtProperty: "popup",
    },
  ],
  [
    "--panel-text-color",
    {
      lwtProperty: "popup_text",
    },
  ],
  [
    "--panel-border-color",
    {
      lwtProperty: "popup_border",
    },
  ],
  [
    "--toolbar-field-background-color",
    {
      lwtProperty: "toolbar_field",
      fallbackColor: "rgba(255, 255, 255, 0.8)",
    },
  ],
  [
    "--toolbar-background-color",
    {
      lwtProperty: "toolbarColor",
    },
  ],
  [
    "--toolbar-text-color",
    {
      lwtProperty: "toolbar_text",
    },
  ],
  [
    "--toolbar-field-text-color",
    {
      lwtProperty: "toolbar_field_text",
      fallbackColor: "black",
    },
  ],
  [
    "--toolbar-field-border-color",
    {
      lwtProperty: "toolbar_field_border",
      fallbackColor: "transparent",
    },
  ],
  [
    "--toolbar-field-background-color-focus",
    {
      lwtProperty: "toolbar_field_focus",
      fallbackProperty: "toolbar_field",
      fallbackColor: "white",
      processColor(rgbaChannels, element, propertyOverrides) {
        if (!rgbaChannels) {
          return null;
        }
        // Ensure minimum opacity as this is used behind address bar results.
        const min_opacity = 0.9;
        let { r, g, b, a } = rgbaChannels;
        if (a < min_opacity) {
          propertyOverrides.set(
            "toolbar_field_text_focus",
            _isColorDark(r, g, b) ? "white" : "black"
          );
          return `rgba(${r}, ${g}, ${b}, ${min_opacity})`;
        }
        return `rgba(${r}, ${g}, ${b}, ${a})`;
      },
    },
  ],
  [
    "--toolbar-field-text-color-focus",
    {
      lwtProperty: "toolbar_field_text_focus",
      fallbackProperty: "toolbar_field_text",
      fallbackColor: "black",
    },
  ],
  [
    "--toolbar-field-border-color-focus",
    {
      lwtProperty: "toolbar_field_border_focus",
    },
  ],
  [
    "--lwt-toolbar-field-highlight",
    {
      lwtProperty: "toolbar_field_highlight",
      processColor(rgbaChannels) {
        if (!rgbaChannels) {
          return null;
        }
        const { r, g, b, a } = rgbaChannels;
        return `rgba(${r}, ${g}, ${b}, ${a})`;
      },
    },
  ],
  [
    "--lwt-toolbar-field-highlight-text",
    {
      lwtProperty: "toolbar_field_highlight_text",
    },
  ],
  [
    "--toolbarbutton-icon-fill",
    {
      lwtProperty: "icon_color",
    },
  ],
  [
    "--toolbarbutton-icon-fill-attention",
    {
      lwtProperty: "icon_attention_color",
    },
  ],
  // The following 3 are given to the new tab page by contentTheme.js. They are
  // also exposed here, in the browser chrome, so popups anchored on top of the
  // new tab page can use them to avoid clashing with the new tab page content.
  [
    "--newtab-background-color",
    {
      lwtProperty: "ntp_background",
      processColor(rgbaChannels) {
        if (!rgbaChannels) {
          return null;
        }
        const { r, g, b } = rgbaChannels;
        // Drop alpha channel
        return `rgb(${r}, ${g}, ${b})`;
      },
    },
  ],
  [
    "--newtab-background-color-secondary",
    { lwtProperty: "ntp_card_background" },
  ],
  [
    "--newtab-text-primary-color",
    {
      lwtProperty: "ntp_text",
      processColor(rgbaChannels, element) {
        if (!rgbaChannels) {
          element.removeAttribute("lwt-newtab-brighttext");
          return null;
        }

        const { r, g, b } = rgbaChannels;
        element.toggleAttribute(
          "lwt-newtab-brighttext",
          0.2125 * r + 0.7154 * g + 0.0721 * b > 110
        );

        return _rgbaToString(rgbaChannels);
      },
    },
  ],
];

LightweightThemeConsumer.init = function (window) {
  new LightweightThemeConsumer(window.document);
};

export function LightweightThemeConsumer(aDocument) {
  this._doc = aDocument;
  this._win = aDocument.defaultView;
  this._winId = this._win.docShell.outerWindowID;
  this._isAIWindow = this._doc.documentElement.hasAttribute("ai-window");
  this._isPrivateWindow = lazy.PrivateBrowsingUtils.isWindowPrivate(this._win);

  XPCOMUtils.defineLazyPreferenceGetter(
    this,
    "FORCED_COLORS_OVERRIDE_ENABLED",
    "browser.theme.forced-colors-override.enabled",
    true,
    () => this._update(this._lastData)
  );

  XPCOMUtils.defineLazyPreferenceGetter(
    this,
    "BROWSER_NOVA_ENABLED",
    "browser.nova.enabled",
    false,
    () => this._update(this._lastData)
  );

  XPCOMUtils.defineLazyPreferenceGetter(
    this,
    "_toolbarTheme",
    "browser.theme.toolbar-theme",
    2,
    () => {
      if (this._isAIWindow) {
        this._update(this._lastData);
      }
    }
  );

  // Whether we should switch to a dark theme variant in private windows
  XPCOMUtils.defineLazyPreferenceGetter(
    this,
    "BROWSER_THEME_DARK_PRIVATE_WINDOWS",
    "browser.theme.dark-private-windows",
    true,
    () => this._update(this._lastData)
  );

  Services.obs.addObserver(this, "lightweight-theme-styling-update");

  this.darkThemeMediaQuery = this._win.matchMedia("(-moz-system-dark-theme)");
  this.darkThemeMediaQuery.addListener(this);

  this.forcedColorsMediaQuery = this._win.matchMedia("(forced-colors)");
  this.forcedColorsMediaQuery.addListener(this);

  this._aiWindowObserver = new this._win.MutationObserver(() => {
    this.toggleAIWindowMode(this._win);
  });
  this._aiWindowObserver.observe(this._doc.documentElement, {
    attributeFilter: ["ai-window"],
  });

  this._update(lazy.LightweightThemeManager.themeData);

  this._win.addEventListener("unload", this, { once: true });
}

LightweightThemeConsumer.prototype = {
  _lastData: null,

  observe(aSubject, aTopic) {
    if (aTopic != "lightweight-theme-styling-update") {
      return;
    }

    let data = aSubject.wrappedJSObject;
    if (data.window && data.window !== this._winId) {
      return;
    }

    this._update(data);
  },

  handleEvent(aEvent) {
    if (
      aEvent.target == this.darkThemeMediaQuery ||
      aEvent.target == this.forcedColorsMediaQuery
    ) {
      this._update(this._lastData);
      return;
    }

    switch (aEvent.type) {
      case "unload":
        Services.obs.removeObserver(this, "lightweight-theme-styling-update");
        Services.ppmm.sharedData.delete(`theme/${this._winId}`);
        this._aiWindowObserver?.disconnect();
        this._aiWindowObserver = null;
        this._win = this._doc = null;
        this.darkThemeMediaQuery?.removeListener(this);
        this.darkThemeMediaQuery = null;
        this.forcedColorsMediaQuery?.removeListener(this);
        this.forcedColorsMediaQuery = null;
        break;
    }
  },

  _update(themeData) {
    const manager = lazy.LightweightThemeManager;

    // Store user's theme before replacing with aiThemeData or privateThemeData.
    this._lastData = themeData;
    let isPrivateThemeActive = false;

    const shouldMakePrivateWindowDark =
      this._isPrivateWindow &&
      !lazy.PrivateBrowsingUtils.permanentPrivateBrowsing &&
      this.BROWSER_THEME_DARK_PRIVATE_WINDOWS;
    const themeId = themeData.theme?.id ?? DEFAULT_THEME_ID;
    // Used to set a `theme-in-app` attribute to allow rules to apply to default/in-app themes only
    let isDefaultOrInApp =
      DEFAULT_THEME_ID === themeId ||
      !!lazy.BuiltInThemeConfig.get(themeId)?.inApp;

    if (this._isAIWindow) {
      const useNova = this.BROWSER_NOVA_ENABLED;
      const cachedData = useNova
        ? manager.aiNovaThemeData
        : manager.aiThemeData;
      if (cachedData) {
        themeData = cachedData;
        isDefaultOrInApp = true;
      } else {
        const promise = useNova
          ? manager.promiseAINovathemeData()
          : manager.promiseAIThemeData();
        promise.then(() => {
          if (this._isAIWindow && this._win && !this._win.closed) {
            this._update(this._lastData);
          }
        });
        return;
      }
    } else if (
      shouldMakePrivateWindowDark &&
      this.BROWSER_NOVA_ENABLED &&
      isDefaultOrInApp
    ) {
      // When in a private window, with nova enabled and any of the in-app themes active,
      // substitute the built-in private-window theme. Other themes are unchanged.
      if (manager.privateThemeData) {
        themeData = manager.privateThemeData;
        isPrivateThemeActive = true;
      } else {
        manager.promisePrivateThemeData().then(() => {
          if (this._win && !this._win.closed) {
            this._update(this._lastData);
          }
        });
        return;
      }
    }

    let updateGlobalThemeData = true;
    const useDarkTheme = (() => {
      let supportsDarkTheme =
        !!themeData.darkTheme ||
        !themeData.theme ||
        themeData.theme.id == DEFAULT_THEME_ID;

      if (!supportsDarkTheme) {
        return false;
      }

      if (this.darkThemeMediaQuery?.matches) {
        return true;
      }

      // If enabled, apply the dark theme variant to private browsing windows.
      if (!shouldMakePrivateWindowDark) {
        return false;
      }
      // When applying the dark theme for a PBM window we need to skip setting
      // the global toolbar / content theme prefs, because those apply to all
      // windows.
      updateGlobalThemeData = false;
      return true;
    })();

    if (this._isAIWindow || isPrivateThemeActive) {
      updateGlobalThemeData = false;
    }

    let theme = useDarkTheme ? themeData.darkTheme : themeData.theme;
    let forcedColorsThemeOverride =
      this.FORCED_COLORS_OVERRIDE_ENABLED &&
      this.forcedColorsMediaQuery?.matches;
    if (!theme || forcedColorsThemeOverride) {
      theme = { id: DEFAULT_THEME_ID };
    }
    let builtinThemeConfig = lazy.BuiltInThemeConfig.get(theme.id);
    let hasTheme = theme.id != DEFAULT_THEME_ID && !builtinThemeConfig?.inApp;
    this._doc.forceNonNativeTheme = theme.id != DEFAULT_THEME_ID;

    let root = this._doc.documentElement;
    root.toggleAttribute("lwtheme-image", !!(hasTheme && theme.headerImage));
    root.toggleAttribute("theme-in-app", isDefaultOrInApp);
    // Expose the effective (post-substitution) theme id.
    root.setAttribute("theme-effective-id", theme.id);
    // Toolbox background images go either on the `<body>` or on the toolbox
    // itself. For most themes and pre-nova default theme, it goes on the
    // `<body>`. For themes that align the image in the y axis, and the nova
    // default theme, they go on the toolbox.
    root.toggleAttribute(
      "theme-image-in-toolbox",
      (() => {
        if (hasTheme) {
          // TODO(emilio): Consider adding an opt-in to lwthemes into this
          // behavior.
          return !!theme.backgroundsAlignment?.split(",").some(alignment => {
            if (alignment == "center" || alignment == "bottom") {
              return true;
            }
            let [, y] = alignment.split(" ");
            return y == "center" || y == "bottom";
          });
        }
        return this.BROWSER_NOVA_ENABLED;
      })()
    );
    this._setExperiment(hasTheme, themeData.experiment, theme.experimental);
    _setImage(
      this._win,
      root,
      hasTheme,
      "--lwt-header-image",
      theme.headerImage
    );
    _setImage(
      this._win,
      root,
      hasTheme,
      "--lwt-additional-images",
      theme.additionalBackgrounds
    );

    let processedColors = _setProperties(root, hasTheme, theme);
    _setDarkModeAttributes(this._doc, root, theme, processedColors, hasTheme);

    let toolbarColorScheme = (() => {
      if (useDarkTheme || themeData.darkTheme) {
        return useDarkTheme ? "dark" : "light";
      }
      switch (theme.color_scheme) {
        case "light":
        case "dark":
          return theme.color_scheme;
        case "system":
          return null;
        default:
          break;
      }
      if (!hasTheme) {
        return null;
      }
      return _isToolbarDark(this._doc, theme, processedColors, true)
        ? "dark"
        : "light";
    })();

    let contentColorScheme = (() => {
      if (lazy.BROWSER_THEME_UNIFIED_COLOR_SCHEME) {
        return toolbarColorScheme;
      }
      let themeOverride = theme.content_color_scheme || theme.color_scheme;
      switch (themeOverride) {
        case "light":
        case "dark":
          return themeOverride;
        case "system":
          return null;
        default:
          break;
      }
      return null;
    })();

    // NOTE(emilio): The !parent check shouldn't be needed ideally, but
    // apparently Thunderbird uses this code on child frames?
    // See bug 1752815.
    if (!this._win.browsingContext.parent) {
      this._win.browsingContext.prefersColorSchemeOverride =
        toolbarColorScheme || "none";
    }
    if (updateGlobalThemeData) {
      function colorSchemeToThemePref(scheme) {
        switch (scheme) {
          case "dark":
            return 0;
          case "light":
            return 1;
          default:
            return 2; // system
        }
      }
      Services.prefs.setIntPref(
        "browser.theme.toolbar-theme",
        colorSchemeToThemePref(toolbarColorScheme)
      );
      Services.prefs.setIntPref(
        "browser.theme.content-theme",
        colorSchemeToThemePref(contentColorScheme)
      );
    }
    root.toggleAttribute("lwtheme", hasTheme);
    root.toggleAttribute("builtintheme", !!builtinThemeConfig);

    let contentThemeData = _getContentProperties(this._doc, hasTheme, theme);
    Services.ppmm.sharedData.set(`theme/${this._winId}`, contentThemeData);
    // We flush sharedData because contentThemeData can be responsible for
    // painting large background surfaces. If this data isn't delivered to the
    // content process before about:home is painted, we will paint a default
    // background and then replace it when sharedData syncs, causing flashing.
    Services.ppmm.sharedData.flush();

    this._win.dispatchEvent(new CustomEvent("windowlwthemeupdate"));
  },

  _setExperiment(hasTheme, experiment, properties) {
    const root = this._doc.documentElement;
    if (this._lastExperimentData) {
      const { stylesheet, usedVariables } = this._lastExperimentData;
      if (stylesheet) {
        stylesheet.remove();
      }
      if (usedVariables) {
        for (const [variable] of usedVariables) {
          _setProperty(root, false, variable);
        }
      }
    }

    this._lastExperimentData = {};

    if (!hasTheme || !experiment) {
      return;
    }

    let usedVariables = [];
    if (properties.colors) {
      for (const property in properties.colors) {
        const cssVariable = experiment.colors[property];
        const value = _rgbaToString(
          _cssColorToRGBA(root.ownerDocument, properties.colors[property])
        );
        usedVariables.push([cssVariable, value]);
      }
    }

    if (properties.images) {
      for (const property in properties.images) {
        const cssVariable = experiment.images[property];
        usedVariables.push([
          cssVariable,
          `url(${properties.images[property]})`,
        ]);
      }
    }
    if (properties.properties) {
      for (const property in properties.properties) {
        const cssVariable = experiment.properties[property];
        usedVariables.push([cssVariable, properties.properties[property]]);
      }
    }
    for (const [variable, value] of usedVariables) {
      _setProperty(root, true, variable, value);
    }
    this._lastExperimentData.usedVariables = usedVariables;

    if (experiment.stylesheet) {
      let stylesheet = this._doc.createElement("link");
      stylesheet.rel = "stylesheet";
      // Stylesheet URLs are validated using WebExtension schemas
      stylesheet.href = experiment.stylesheet;
      this._doc.head.appendChild(stylesheet);
      this._lastExperimentData.stylesheet = stylesheet;
    }
  },

  toggleAIWindowMode(win) {
    this._isAIWindow = win.document.documentElement.hasAttribute("ai-window");
    this._update(lazy.LightweightThemeManager.themeData);
  },
};

function _getContentProperties(doc, hasTheme, data) {
  let properties = { hasTheme };
  if (!hasTheme) {
    return properties;
  }
  for (let property in data) {
    if (lazy.ThemeContentPropertyList.includes(property)) {
      properties[property] = _cssColorToRGBA(doc, data[property]);
    }
  }
  if (data.experimental) {
    for (const property in data.experimental.colors) {
      if (lazy.ThemeContentPropertyList.includes(property)) {
        properties[property] = _cssColorToRGBA(
          doc,
          data.experimental.colors[property]
        );
      }
    }
    for (const property in data.experimental.images) {
      if (lazy.ThemeContentPropertyList.includes(property)) {
        properties[property] = `url(${data.experimental.images[property]})`;
      }
    }
    for (const property in data.experimental.properties) {
      if (lazy.ThemeContentPropertyList.includes(property)) {
        properties[property] = data.experimental.properties[property];
      }
    }
  }
  return properties;
}

function _imageToCss(aWin, aImage) {
  if (typeof aImage == "object") {
    // Note that the theme manifest post-processor (validCSSGradient) has
    // already validated this was a single valid gradient, or returned an
    // image(transparent) otherwise.
    const [gradient, args] = Object.entries(aImage)[0];
    return `${gradient}(${args})`;
  }
  return `url(${aWin.CSS.escape(aImage)})`;
}

function _setImage(aWin, aRoot, aActive, aVariableName, aImages) {
  if (aImages && !Array.isArray(aImages)) {
    aImages = [aImages];
  }
  _setProperty(
    aRoot,
    aActive,
    aVariableName,
    aImages && aImages.map(v => _imageToCss(aWin, v)).join(", ")
  );
}

function _setProperty(elem, hasTheme, variableName, value) {
  if (hasTheme && value) {
    elem.style.setProperty(variableName, value);
  } else {
    elem.style.removeProperty(variableName);
  }
}

function _isToolbarDark(doc, theme, colors, hasTheme) {
  // We prefer looking at toolbar background first (if it's opaque) because
  // some text colors can be dark enough for our heuristics, but still
  // contrast well enough with a dark background, see bug 1743010.
  if (colors.toolbarColor) {
    let color = _cssColorToRGBA(doc, colors.toolbarColor);
    if (color.a == 1) {
      return _isColorDark(color.r, color.g, color.b);
    }
  }
  if (colors.toolbar_text) {
    let color = _cssColorToRGBA(doc, colors.toolbar_text);
    return !_isColorDark(color.r, color.g, color.b);
  }
  return _hasDarkFrame(doc, theme, colors, hasTheme);
}

function _hasDarkFrame(doc, theme, colors, hasTheme) {
  if (!hasTheme) {
    return false;
  }
  // We prefer looking at the background first (if it's opaque and there's no
  // background image on top) because some text colors can be dark enough for
  // our heuristics, but still contrast well enough with a dark background,
  // see bug 1743010.
  if (!theme.headerImage && colors.accentcolor) {
    let color = _cssColorToRGBA(doc, colors.accentcolor);
    if (color.a == 1) {
      return _isColorDark(color.r, color.g, color.b);
    }
  }
  // Fall back to black as per the textcolor processing.
  let textColor = _cssColorToRGBA(doc, colors.textcolor || "black");
  return !_isColorDark(textColor.r, textColor.g, textColor.b);
}

/**
 * Sets dark mode attributes on root, if required. We must do this here,
 * instead of in each color's processColor function, because multiple colors
 * are considered.
 *
 * @param {Document} doc
 * @param {Element} root
 * @param {object} colors
 *   The `processedColors` object from the object created for our theme.
 * @param {boolean} hasTheme
 */
function _setDarkModeAttributes(doc, root, theme, colors, hasTheme) {
  root.toggleAttribute(
    "lwtheme-brighttext",
    _hasDarkFrame(doc, theme, colors, hasTheme)
  );

  if (hasTheme) {
    root.setAttribute(
      "lwt-toolbar",
      _isToolbarDark(doc, theme, colors, hasTheme) ? "dark" : "light"
    );
  } else {
    root.removeAttribute("lwt-toolbar");
  }

  const setAttribute = function (
    attribute,
    textPropertyName,
    backgroundPropertyName
  ) {
    let dark = _determineIfColorPairIsDark(
      doc,
      colors,
      textPropertyName,
      backgroundPropertyName
    );
    if (dark === null) {
      root.removeAttribute(attribute);
    } else {
      root.setAttribute(attribute, dark ? "dark" : "light");
    }
  };

  setAttribute("lwt-tab-selected", "tab_text", "tab_selected");
  setAttribute("lwt-toolbar-field", "toolbar_field_text", "toolbar_field");
  setAttribute(
    "lwt-toolbar-field-focus",
    "toolbar_field_text_focus",
    "toolbar_field_focus"
  );
  setAttribute("lwt-popup", "popup_text", "popup");
  setAttribute("lwt-sidebar", "sidebar_text", "sidebar");
  // NOTE: icon_attention_text prop does never really exist.
  setAttribute(
    "lwt-icon-fill-attention",
    /* textPropertyName = */ null,
    "icon_attention_color"
  );
}

/**
 * Determines if a themed color pair should be considered to have a dark color
 * scheme. We consider both the background and foreground (i.e. usually text)
 * colors because some text colors can be dark enough for our heuristics, but
 * still contrast well enough with a dark background
 *
 * @param {Document} doc
 * @param {object} colors
 * @param {string?} textPropertyName
 *   The key for the foreground element in `colors`.
 * @param {string?} backgroundPropertyName
 *   The key for the background element in `colors`.
 * @returns {boolean | null} True if the element should be considered dark, false
 *   if light, null for preferred scheme.
 */
function _determineIfColorPairIsDark(
  doc,
  colors,
  textPropertyName,
  backgroundPropertyName
) {
  let backgroundColor =
    backgroundPropertyName && colors[backgroundPropertyName];
  let textColor = textPropertyName && colors[textPropertyName];
  if (!backgroundColor && !textColor) {
    // Handles the system theme.
    return null;
  }

  let color = _cssColorToRGBA(doc, backgroundColor);
  if (color && color.a == 1) {
    return _isColorDark(color.r, color.g, color.b);
  }

  color = _cssColorToRGBA(doc, textColor);
  if (!color) {
    // Handles the case where a theme only provides a background color and it is
    // semi-transparent.
    return null;
  }

  return !_isColorDark(color.r, color.g, color.b);
}

function _setProperties(root, hasTheme, themeData) {
  let propertyOverrides = new Map();
  let doc = root.ownerDocument;

  // Copy the theme into processedColors. We'll replace values with processed
  // colors if necessary. We copy because some colors (such as those used in
  // content) are not processed here, but are referenced in places that check
  // processedColors. Copying means processedColors will contain irrelevant
  // properties like `id`. There aren't too many, so that's OK.
  let processedColors = { ...themeData };
  for (let map of [toolkitVariableMap, lazy.ThemeVariableMap]) {
    for (let [cssVarName, definition] of map) {
      const {
        lwtProperty,
        fallbackProperty,
        fallbackColor,
        processColor,
        isColor = true,
      } = definition;
      let val = propertyOverrides.get(lwtProperty) || themeData[lwtProperty];
      if (isColor) {
        val = _cssColorToRGBA(doc, val);
        if (!val && fallbackProperty) {
          val = _cssColorToRGBA(doc, themeData[fallbackProperty]);
        }
        if (!val && hasTheme && fallbackColor) {
          val = _cssColorToRGBA(doc, fallbackColor);
        }
        if (processColor) {
          val = processColor(val, root, propertyOverrides);
        } else {
          val = _rgbaToString(val);
        }
      }

      // Add processed color to themeData.
      processedColors[lwtProperty] = val;

      _setProperty(root, hasTheme, cssVarName, val);
    }
  }
  return processedColors;
}

const kInvalidColor = { r: 0, g: 0, b: 0, a: 1 };

function _cssColorToRGBA(doc, cssColor) {
  if (!cssColor) {
    return null;
  }
  return doc.defaultView.InspectorUtils.colorToRGBA(cssColor) || kInvalidColor;
}

function _rgbaToString(parsedColor) {
  if (!parsedColor) {
    return null;
  }
  let { r, g, b, a } = parsedColor;
  if (a == 1) {
    return `rgb(${r}, ${g}, ${b})`;
  }
  return `rgba(${r}, ${g}, ${b}, ${a})`;
}

function _isColorDark(r, g, b) {
  return 0.2125 * r + 0.7154 * g + 0.0721 * b <= 127;
}
