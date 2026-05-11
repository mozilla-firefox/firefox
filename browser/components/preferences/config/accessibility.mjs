/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

Preferences.addAll([
  { id: "accessibility.blockautorefresh", type: "bool" },
  { id: "accessibility.tabfocus", type: "int" },
  { id: "browser.display.document_color_use", type: "int" },
  { id: "font.language.group", type: "string" },
  { id: "browser.zoom.full", type: "bool" },
  { id: "general.autoScroll", type: "bool" },
  { id: "general.smoothScroll", type: "bool" },
  { id: "widget.gtk.overlay-scrollbars.enabled", type: "bool", inverted: true },
  { id: "layout.css.always_underline_links", type: "bool" },
  { id: "media.hardwaremediakeys.enabled", type: "bool" },
]);

let srdEnabled = Services.prefs.getBoolPref(
  "browser.settings-redesign.enabled",
  false
);

if (!srdEnabled) {
  Preferences.addAll([
    { id: "accessibility.browsewithcaret", type: "bool" },
    { id: "accessibility.typeaheadfind", type: "bool" },
  ]);
}

if (AppConstants.platform === "win") {
  Preferences.addAll([{ id: "ui.osk.enabled", type: "bool" }]);
}

Preferences.addSetting({
  id: "useOnScreenKeyboard",
  pref: AppConstants.platform == "win" ? "ui.osk.enabled" : undefined,
  visible: () => AppConstants.platform == "win",
});

Preferences.addSetting(
  /** @type {{ _storedFullKeyboardNavigation: number } & SettingConfig} */ ({
    _storedFullKeyboardNavigation: -1,
    id: "useFullKeyboardNavigation",
    pref: "accessibility.tabfocus",
    visible: () => AppConstants.platform == "macosx",
    /**
     * Returns true if any full keyboard nav is enabled and false otherwise, caching
     * the current value to enable proper pref restoration if the checkbox is
     * never changed.
     *
     * accessibility.tabfocus
     * - an integer controlling the focusability of:
     *     1  text controls
     *     2  form elements
     *     4  links
     *     7  all of the above
     */
    get(prefVal) {
      this._storedFullKeyboardNavigation = prefVal;
      return prefVal == 7;
    },
    /**
     * Returns the value of the full keyboard nav preference represented by UI,
     * preserving the preference's "hidden" value if the preference is
     * unchanged and represents a value not strictly allowed in UI.
     */
    set(checked) {
      if (checked) {
        return 7;
      }
      if (this._storedFullKeyboardNavigation != 7) {
        return this._storedFullKeyboardNavigation;
      }
      return 1;
    },
  })
);

Preferences.addSetting({
  id: "alwaysUnderlineLinks",
  pref: "layout.css.always_underline_links",
});

Preferences.addSetting({
  id: "mediaControlToggleEnabled",
  pref: "media.hardwaremediakeys.enabled",
  // For media control toggle button, we support it on Windows, macOS and
  // gtk-based Linux.
  visible: () =>
    AppConstants.platform == "win" ||
    AppConstants.platform == "macosx" ||
    AppConstants.MOZ_WIDGET_GTK,
});
Preferences.addSetting({
  id: "useAutoScroll",
  pref: "general.autoScroll",
});
Preferences.addSetting({
  id: "useSmoothScrolling",
  pref: "general.smoothScroll",
});
Preferences.addSetting({
  id: "useOverlayScrollbars",
  pref: "widget.gtk.overlay-scrollbars.enabled",
  visible: () => AppConstants.MOZ_WIDGET_GTK,
});

/**
 * Helper object for managing the various zoom related settings.
 */
const ZoomHelpers = {
  win: window.browsingContext.topChromeWindow,
  get FullZoom() {
    return this.win.FullZoom;
  },
  get ZoomManager() {
    return this.win.ZoomManager;
  },

  /**
   * Set the global default zoom value.
   *
   * @param {number} newZoom - The new zoom
   * @returns {Promise<void>}
   */
  async setDefaultZoom(newZoom) {
    let cps2 = Cc["@mozilla.org/content-pref/service;1"].getService(
      Ci.nsIContentPrefService2
    );
    let nonPrivateLoadContext = Cu.createLoadContext();
    let resolvers = Promise.withResolvers();
    /* Because our setGlobal function takes in a browsing context, and
     * because we want to keep this property consistent across both private
     * and non-private contexts, we create a non-private context and use that
     * to set the property, regardless of our actual context.
     */
    cps2.setGlobal(this.FullZoom.name, newZoom, nonPrivateLoadContext, {
      handleCompletion: resolvers.resolve,
      handleError: resolvers.reject,
    });
    return resolvers.promise;
  },

  async getDefaultZoom() {
    /** @import { ZoomUI as GlobalZoomUI } from "resource:///modules/ZoomUI.sys.mjs" */
    /** @type {GlobalZoomUI} */
    let ZoomUI = this.win.ZoomUI;
    return await ZoomUI.getGlobalValue();
  },

  /**
   * The possible zoom values.
   *
   * @returns {number[]}
   */
  get zoomValues() {
    return this.ZoomManager.zoomValues;
  },

  toggleFullZoom() {
    this.ZoomManager.toggleZoom();
  },
};

Preferences.addSetting(
  class extends Preferences.AsyncSetting {
    static id = "defaultZoom";
    /** @type {Record<"options", object[]>} */
    optionsConfig;

    /**
     * @param {string} val - zoom value as a string
     */
    async set(val) {
      await ZoomHelpers.setDefaultZoom(
        parseFloat((parseInt(val, 10) / 100).toFixed(2))
      );
    }
    async get() {
      return String(Math.round((await ZoomHelpers.getDefaultZoom()) * 100));
    }
    async getControlConfig() {
      if (!this.optionsConfig) {
        this.optionsConfig = {
          options: ZoomHelpers.zoomValues.map(a => {
            let value = String(Math.round(a * 100));
            return {
              value,
              controlAttrs: { label: `${value}%` },
              l10nId: "preferences-default-zoom-value",
              l10nArgs: { percentage: value },
            };
          }),
        };
      }
      return this.optionsConfig;
    }
  }
);

Preferences.addSetting({
  id: "zoomTextPref",
  pref: "browser.zoom.full",
});
Preferences.addSetting({
  id: "zoomText",
  deps: ["zoomTextPref"],
  // Use the Setting since the ZoomManager getter may not have updated yet.
  get: (_, { zoomTextPref }) => !zoomTextPref.value,
  set: () => ZoomHelpers.toggleFullZoom(),
  disabled: ({ zoomTextPref }) => zoomTextPref.locked,
});
Preferences.addSetting({
  id: "zoomWarning",
  deps: ["zoomText"],
  visible: ({ zoomText }) => Boolean(zoomText.value),
});

/**
 * Helper object for managing font-related settings.
 */
const FontHelpers = {
  _enumerator: null,
  _allFonts: null,
  _localizedDefaultLabels: new Map(),

  async fetchLocalizedDefaultLabel(langGroup, fontType) {
    const cacheKey = `${langGroup}:${fontType}`;
    let defaultFont = this.enumerator.getDefaultFont(langGroup, fontType);
    if (!defaultFont) {
      defaultFont = this.enumerator.getDefaultFont(langGroup, "");
    }
    const l10nId = defaultFont
      ? "fonts-label-default"
      : "fonts-label-default-unnamed";
    const l10nArgs = defaultFont ? { name: defaultFont } : undefined;
    const [msg] = await document.l10n.formatMessages([
      { id: l10nId, args: l10nArgs },
    ]);
    const labelAttr = msg?.attributes?.find(a => a.name === "label");
    if (labelAttr) {
      this._localizedDefaultLabels.set(cacheKey, labelAttr.value);
    }
  },

  get enumerator() {
    if (!this._enumerator) {
      this._enumerator = Cc["@mozilla.org/gfx/fontenumerator;1"].createInstance(
        Ci.nsIFontEnumerator
      );
    }
    return this._enumerator;
  },

  ensurePref(prefId, type) {
    let pref = Preferences.get(prefId);
    if (!pref) {
      pref = Preferences.add({ id: prefId, type });
    }
    return pref;
  },

  get langGroup() {
    return Services.locale.fontLanguageGroup;
  },

  getFontTypePrefId(langGroup) {
    return `font.default.${langGroup}`;
  },

  getFontType(langGroup) {
    const prefId = this.getFontTypePrefId(langGroup);
    return Services.prefs.getCharPref(prefId, "serif");
  },

  getFontPrefId(langGroup) {
    const fontType = this.getFontType(langGroup);
    return `font.name.${fontType}.${langGroup}`;
  },

  getSizePrefId(langGroup) {
    return `font.size.variable.${langGroup}`;
  },

  buildFontOptions(langGroup, fontType) {
    let fonts = this.enumerator.EnumerateFonts(langGroup, fontType);
    let defaultFont = null;
    if (fonts.length) {
      defaultFont = this.enumerator.getDefaultFont(langGroup, fontType);
    } else {
      fonts = this.enumerator.EnumerateFonts(langGroup, "");
      if (fonts.length) {
        defaultFont = this.enumerator.getDefaultFont(langGroup, "");
      }
    }

    if (!this._allFonts) {
      this._allFonts = this.enumerator.EnumerateAllFonts();
    }

    const options = [];

    if (fonts.length) {
      const cacheKey = `${langGroup}:${fontType}`;
      const localizedLabel = this._localizedDefaultLabels.get(cacheKey);

      if (defaultFont) {
        options.push({
          value: "",
          controlAttrs: { label: localizedLabel || defaultFont },
          l10nId: "fonts-label-default",
          l10nArgs: { name: defaultFont },
        });
      } else {
        options.push({
          value: "",
          ...(localizedLabel
            ? { controlAttrs: { label: localizedLabel } }
            : {}),
          l10nId: "fonts-label-default-unnamed",
        });
      }

      for (const font of fonts) {
        options.push({
          value: font,
          controlAttrs: { label: font },
        });
      }
    }

    if (this._allFonts.length > fonts.length) {
      const fontSet = new Set(fonts);
      for (const font of this._allFonts) {
        if (!fontSet.has(font)) {
          options.push({
            value: font,
            controlAttrs: { label: font },
          });
        }
      }
    }

    return options;
  },

  fontSizeOptions: [
    9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36,
    40, 44, 48, 56, 64, 72,
  ].map(size => ({ value: size, controlAttrs: { label: String(size) } })),
};

Preferences.addSetting({
  id: "fontLanguageGroup",
  pref: "font.language.group",
});

Preferences.addSetting({
  id: "fontType",
  deps: ["fontLanguageGroup"],
  setup(emitChange, deps, setting) {
    const handleChange = () => {
      setting.pref = FontHelpers.ensurePref(
        FontHelpers.getFontTypePrefId(FontHelpers.langGroup),
        "string"
      );
      emitChange();
    };

    handleChange();
    deps.fontLanguageGroup.on("change", handleChange);
    return () => deps.fontLanguageGroup.off("change", handleChange);
  },
});

Preferences.addSetting({
  id: "defaultFont",
  deps: ["fontType"],
  optionsConfig: null,
  setup(emitChange, deps, setting) {
    const handleChange = () => {
      setting.pref = FontHelpers.ensurePref(
        FontHelpers.getFontPrefId(FontHelpers.langGroup),
        "fontname"
      );
      this.optionsConfig = null;
      emitChange();

      const langGroup = FontHelpers.langGroup;
      const fontType = FontHelpers.getFontType(langGroup);
      FontHelpers.fetchLocalizedDefaultLabel(langGroup, fontType)
        .then(() => {
          this.optionsConfig = null;
          emitChange();
        })
        .catch(console.error);
    };
    handleChange();
    deps.fontType.on("change", handleChange);
    return () => deps.fontType.off("change", handleChange);
  },
  getControlConfig(config) {
    if (!this.optionsConfig) {
      this.optionsConfig = {
        ...config,
        options: FontHelpers.buildFontOptions(
          FontHelpers.langGroup,
          FontHelpers.getFontType(FontHelpers.langGroup)
        ),
      };
    }
    return this.optionsConfig;
  },
});

Preferences.addSetting({
  id: "defaultFontSize",
  deps: ["fontLanguageGroup"],
  setup(emitChange, deps, setting) {
    const handleLangChange = () => {
      setting.pref = FontHelpers.ensurePref(
        FontHelpers.getSizePrefId(FontHelpers.langGroup),
        "int"
      );
      emitChange();
    };
    handleLangChange();
    deps.fontLanguageGroup.on("change", handleLangChange);
    return () => deps.fontLanguageGroup.off("change", handleLangChange);
  },
  getControlConfig(config) {
    return { ...config, options: FontHelpers.fontSizeOptions };
  },
});

Preferences.addSetting({
  id: "advancedFonts",
  onUserClick: () =>
    window.gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/fonts.xhtml",
      { features: "resizable=no" }
    ),
});

Preferences.addSetting({
  id: "contrastControlSettings",
  pref: "browser.display.document_color_use",
});
Preferences.addSetting({
  id: "colors",
  onUserClick() {
    window.gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/colors.xhtml",
      { features: "resizable=no" }
    );
  },
});

// Bug 2028609: remove these settings when the pref is flipped
if (!srdEnabled) {
  Preferences.addSetting({
    id: "useCursorNavigationAccess",
    pref: "accessibility.browsewithcaret",
  });
  Preferences.addSetting({
    id: "searchStartTypingAccess",
    pref: "accessibility.typeaheadfind",
  });
}

SettingGroupManager.registerGroups({
  zoom: {
    l10nId: "preferences-default-zoom-label",
    iconSrc: "chrome://browser/skin/preferences/category-search.svg",
    headingLevel: 2,
    items: [
      {
        id: "defaultZoom",
        l10nId: "preferences-default-zoom-select",
        control: "moz-select",
      },
      {
        id: "zoomText",
        l10nId: "preferences-zoom-text-only",
      },
      {
        id: "zoomWarning",
        l10nId: "preferences-text-zoom-override-warning",
        control: "moz-message-bar",
        controlAttrs: {
          type: "warning",
        },
      },
    ],
  },
  fonts: {
    l10nId: "preferences-fonts-header2",
    headingLevel: 2,
    items: [
      {
        id: "defaultFont",
        l10nId: "preferences-fonts-family",
        control: "moz-select",
      },
      {
        id: "defaultFontSize",
        l10nId: "preferences-fonts-size",
        control: "moz-select",
      },
      {
        id: "advancedFonts",
        l10nId: "preferences-fonts-advanced-settings",
        control: "moz-box-button",
        controlAttrs: {
          "search-l10n-ids":
            "fonts-window.title,fonts-langgroup-header,fonts-proportional-size,fonts-proportional-header,fonts-serif,fonts-sans-serif,fonts-monospace,fonts-langgroup-arabic.label,fonts-langgroup-armenian.label,fonts-langgroup-bengali.label,fonts-langgroup-simpl-chinese.label,fonts-langgroup-trad-chinese-hk.label,fonts-langgroup-trad-chinese.label,fonts-langgroup-cyrillic.label,fonts-langgroup-devanagari.label,fonts-langgroup-ethiopic.label,fonts-langgroup-georgian.label,fonts-langgroup-el.label,fonts-langgroup-gujarati.label,fonts-langgroup-gurmukhi.label,fonts-langgroup-japanese.label,fonts-langgroup-hebrew.label,fonts-langgroup-kannada.label,fonts-langgroup-khmer.label,fonts-langgroup-korean.label,fonts-langgroup-latin.label,fonts-langgroup-malayalam.label,fonts-langgroup-math.label,fonts-langgroup-odia.label,fonts-langgroup-sinhala.label,fonts-langgroup-tamil.label,fonts-langgroup-telugu.label,fonts-langgroup-thai.label,fonts-langgroup-tibetan.label,fonts-langgroup-canadian.label,fonts-langgroup-other.label,fonts-minsize,fonts-minsize-none.label,fonts-default-serif.label,fonts-default-sans-serif.label,fonts-allow-own.label",
        },
      },
    ],
  },
  contrast: {
    l10nId: "preferences-contrast-control-group",
    iconSrc: "chrome://browser/skin/contrast.svg",
    headingLevel: 2,
    items: [
      {
        id: "contrastControlSettings",
        control: "moz-radio-group",
        l10nId: "preferences-contrast-control-radio-group",
        options: [
          {
            id: "contrastSettingsAuto",
            value: 0,
            l10nId: "preferences-contrast-control-use-platform-settings",
          },
          {
            id: "contrastSettingsOff",
            value: 1,
            l10nId: "preferences-contrast-control-off",
          },
          {
            id: "contrastSettingsOn",
            value: 2,
            l10nId: "preferences-contrast-control-custom",
            items: [
              {
                id: "colors",
                l10nId: "preferences-colors-manage-button",
                control: "moz-box-button",
                controlAttrs: {
                  "search-l10n-ids":
                    "colors-text-and-background, colors-text.label, colors-text-background.label, colors-links-header, colors-links-unvisited.label, colors-links-visited.label",
                },
              },
            ],
          },
        ],
      },
    ],
  },
  keyboardAndScrolling: {
    l10nId: "keyboard-and-scrolling-group",
    headingLevel: 2,
    items: [
      { id: "useOnScreenKeyboard", l10nId: "browsing-use-onscreen-keyboard" },
      // Bug 2028609: remove these settings when the pref is flipped
      ...(!srdEnabled
        ? [
            {
              id: "useCursorNavigationAccess",
              l10nId: "browsing-use-cursor-navigation",
            },
            {
              id: "searchStartTypingAccess",
              l10nId: "browsing-search-on-start-typing",
            },
          ]
        : []),
      {
        id: "useFullKeyboardNavigation",
        l10nId: "browsing-use-full-keyboard-navigation",
      },
      {
        id: "mediaControlToggleEnabled",
        l10nId: "browsing-media-control",
        supportPage: "media-keyboard-control",
      },
      {
        id: "useAutoScroll",
        l10nId: "browsing-use-autoscroll",
      },
      {
        id: "useOverlayScrollbars",
        l10nId: "browsing-gtk-use-non-overlay-scrollbars",
      },
    ],
  },
  motionAndLink: {
    l10nId: "motion-and-link-group",
    headingLevel: 2,
    items: [
      { id: "alwaysUnderlineLinks", l10nId: "browsing-always-underline-links" },
      {
        id: "useSmoothScrolling",
        l10nId: "browsing-use-smooth-scrolling",
      },
    ],
  },
});
