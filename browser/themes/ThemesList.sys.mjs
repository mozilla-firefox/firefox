/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonRepository: "resource://gre/modules/addons/AddonRepository.sys.mjs",
});

/**
 * @typedef {string} ThemesInstallSource
 *   Telemetry source string recorded when a theme is installed (e.g. "about:addons",
 *   "about:newprofile"). Add new values to the
 *   `addons_manager.install.extra_keys.source` description in
 *   `toolkit/mozapps/extensions/metrics.yaml`.
 */

/**
 * @typedef {object} ThemeColorVariant
 * @property {"color" | "gradient"} type - Whether the value is a plain CSS color or a gradient.
 * @property {string} value - The CSS string for this variant.
 */

/**
 * @typedef {object} ThemePickerColors
 * @property {ThemeColorVariant} light - The color variant to use in light mode.
 * @property {ThemeColorVariant} dark - The color variant to use in dark mode.
 */

/**
 * @typedef {object} ThemePreviewColors
 * @property {string} text - Color of the tab and urlbar text placeholders.
 * @property {string} icons - Color of the toolbar and app menu icons.
 * @property {string} activeTabStroke - Color of the active tab outline.
 * @property {string} [tabbarBackground] - Color of the tab bar, used instead of
 *   the themePickerColors variants when set.
 * @property {string} [activeTabBackground] - Color of the active tab, overrides
 *   the surface color shared by the Nova themes.
 * @property {string} [toolbarBackground] - Color of the toolbar, overrides the
 *   surface color shared by the Nova themes.
 * @property {string} [urlbarBackground] - Color of the urlbar, overrides the
 *   surface color shared by the Nova themes.
 * @property {string} [urlbarBorder] - Color of the urlbar outline, overrides
 *   the surface color shared by the Nova themes.
 */

/**
 * @typedef {object} ThemeListEntry
 * @property {string} id - The addon ID of the theme.
 * @property {boolean} [isBuiltIn] - Whether the theme ships built-in with Firefox.
 * @property {boolean} [showInCompactLayout] - Whether the theme should be shown
 *   in the theme picker's compact layout mode.
 * @property {ThemePickerColors} [themePickerColors]
 *   The light and dark color or gradient variants representing the theme in the
 *   Firefox Themes Picker UI. Expected to not be set for the classic themes.
 * @property {{light?: ThemePreviewColors, dark?: ThemePreviewColors}} [themePreviewColors]
 *   The colors used, along with themePickerColors, to render the theme preview
 *   image out of the default theme Nova preview svg through link-parameters.
 *   At least one of light and dark is required, and the missing one defaults
 *   to the other; set only one when both would be the same.
 *   Only expected for the AMO-hosted themes.
 */

// TODO(Bug 2053215): consider rolling the metadata related to built-in themes that is
// currently still provided by BuiltInThemeConfig.sys.mjs into this module.

// TODO(Bug 2053217): consider moving this metadata into a themes_list.json
// file built into the omni jar and load the data from it (similarly to how
// we manage the list of built-in add-ons through "built_in_addons.json").

/** @type {Array<ThemeListEntry>} */
export const FIREFOX_THEMES_LIST = [
  {
    id: "default-theme@mozilla.org",
    isBuiltIn: true,
    showInCompactLayout: true,
    themePickerColors: {
      light: {
        type: "gradient",
        value: "linear-gradient(96deg, #EADDFF 39.84%, #FFDBC5 101.72%)",
      },
      dark: {
        type: "gradient",
        value: "linear-gradient(96deg, #3E315F 39.84%, #701c07 101.72%)",
      },
    },
  },
  {
    id: "nova-sun@mozilla.org",
    showInCompactLayout: true,
    themePickerColors: {
      light: {
        type: "gradient",
        value: "linear-gradient(90deg, #F9F5E6 0%, #FDE8B5 60%, #FBCC77 100%)",
      },
      dark: { type: "color", value: "#270F00" },
    },
    themePreviewColors: {
      light: { text: "#270F00", icons: "#5F3100", activeTabStroke: "#F3A81E" },
      dark: { text: "#F2F0F8", icons: "#F9F5E6", activeTabStroke: "#854800" },
    },
  },
  {
    id: "nova-spark@mozilla.org",
    themePickerColors: {
      light: {
        type: "gradient",
        value: "linear-gradient(135deg, #F8F0EC 0%, #FFDBC5 60%, #FEBD99 100%)",
      },
      dark: {
        type: "gradient",
        value: "linear-gradient(135deg, #701C07 0%, #461209 60%, #250E0B 100%)",
      },
    },
    themePreviewColors: {
      light: { text: "#250E0B", icons: "#701C07", activeTabStroke: "#FF9565" },
      dark: { text: "#F8F0EC", icons: "#F8F0EC", activeTabStroke: "#9C2C05" },
    },
  },
  {
    id: "nova-flame@mozilla.org",
    themePickerColors: {
      light: {
        type: "gradient",
        value: "linear-gradient(135deg, #FCF2F3 0%, #FFD9DF 60%, #FFB6BF 100%)",
      },
      dark: {
        type: "gradient",
        value: "linear-gradient(135deg, #69172D 0%, #42121F 60%, #211014 100%)",
      },
    },
    themePreviewColors: {
      light: { text: "#211014", icons: "#69172D", activeTabStroke: "#FF8998" },
      dark: { text: "#FCF2F3", icons: "#FCF2F3", activeTabStroke: "#961E3D" },
    },
  },
  {
    id: "nova-flare@mozilla.org",
    showInCompactLayout: true,
    themePickerColors: {
      light: {
        type: "gradient",
        value: "linear-gradient(135deg, #F7EFF3 0%, #FFD5EE 60%, #FFB0E2 100%)",
      },
      dark: {
        type: "gradient",
        value: "linear-gradient(135deg, #5F1854 0%, #3C1334 60%, #1E111B 100%)",
      },
    },
    themePreviewColors: {
      light: { text: "#1E111B", icons: "#5F1854", activeTabStroke: "#F585D3" },
      dark: { text: "#F7EFF3", icons: "#F7EFF3", activeTabStroke: "#882078" },
    },
  },
  {
    id: "nova-lavender@mozilla.org",
    themePickerColors: {
      light: {
        type: "gradient",
        value: "linear-gradient(135deg, #FAEBFF 0%, #F6D7FF 60%, #E8B7FF 100%)",
      },
      dark: {
        type: "gradient",
        value: "linear-gradient(135deg, #4F216B 0%, #311842 60%, #1A1220 100%)",
      },
    },
    themePreviewColors: {
      light: { text: "#1A1220", icons: "#4F216B", activeTabStroke: "#D490FF" },
      dark: { text: "#FAEBFF", icons: "#FAEBFF", activeTabStroke: "#702E98" },
    },
  },
  {
    id: "nova-dusk@mozilla.org",
    themePickerColors: {
      light: {
        type: "gradient",
        value: "linear-gradient(135deg, #F5ECFF 0%, #EADDFF 60%, #D4C1FF 100%)",
      },
      dark: {
        type: "gradient",
        value: "linear-gradient(135deg, #3E2976 0%, #271C48 60%, #161423 100%)",
      },
    },
    themePreviewColors: {
      light: { text: "#161423", icons: "#3E2976", activeTabStroke: "#B89CFF" },
      dark: { text: "#F5ECFF", icons: "#F5ECFF", activeTabStroke: "#5939A8" },
    },
  },
  {
    id: "nova-lagoon@mozilla.org",
    showInCompactLayout: true,
    themePickerColors: {
      light: {
        type: "gradient",
        value: "linear-gradient(135deg, #ECF3F8 0%, #C5EAFE 60%, #A2D3FF 100%)",
      },
      dark: {
        type: "gradient",
        value: "linear-gradient(135deg, #23327B 0%, #17214C 60%, #111524 100%)",
      },
    },
    themePreviewColors: {
      light: { text: "#111524", icons: "#23327B", activeTabStroke: "#7BB2FF" },
      dark: { text: "#ECF3F8", icons: "#ECF3F8", activeTabStroke: "#3246B0" },
    },
  },
  {
    id: "nova-pine@mozilla.org",
    showInCompactLayout: true,
    themePickerColors: {
      light: {
        type: "gradient",
        value: "linear-gradient(135deg, #ECF4F1 0%, #C4F1E0 60%, #90E3C6 100%)",
      },
      dark: {
        type: "gradient",
        value: "linear-gradient(135deg, #004933 0%, #003020 60%, #001E12 100%)",
      },
    },
    themePreviewColors: {
      light: { text: "#001E12", icons: "#004933", activeTabStroke: "#4ACCA6" },
      dark: { text: "#ECF4F1", icons: "#ECF4F1", activeTabStroke: "#06674B" },
    },
  },
  {
    id: "nova-tide@mozilla.org",
    themePickerColors: {
      light: {
        type: "gradient",
        value: "linear-gradient(135deg, #EBF4F5 0%, #C3EEF8 60%, #8FDDF0 100%)",
      },
      dark: {
        type: "gradient",
        value: "linear-gradient(135deg, #034554 0%, #002D38 60%, #011C23 100%)",
      },
    },
    themePreviewColors: {
      light: { text: "#011C23", icons: "#034554", activeTabStroke: "#4CC4E1" },
      dark: { text: "#EBF4F5", icons: "#EBF4F5", activeTabStroke: "#066077" },
    },
  },
  {
    id: "nova-ash@mozilla.org",
    showInCompactLayout: true,
    themePickerColors: {
      light: {
        type: "gradient",
        value: "linear-gradient(90deg, #FCFBFF 0%, #EFEDF2 60%, #D6D5DA 100%)",
      },
      dark: {
        type: "gradient",
        value: "linear-gradient(90deg, #3F3E42 0%, #252428 60%, #171519 100%)",
      },
    },
    themePreviewColors: {
      light: { text: "#121114", icons: "#252428", activeTabStroke: "#B7B6BA" },
      dark: { text: "#FCFBFF", icons: "#FCFBFF", activeTabStroke: "#67666A" },
    },
  },
  {
    id: "nova-smoke@mozilla.org",
    themePickerColors: {
      light: {
        type: "gradient",
        value: "linear-gradient(135deg, #FFF9F6 0%, #FBF4EE 60%, #E3DBD7 100%)",
      },
      dark: {
        type: "gradient",
        value: "linear-gradient(135deg, #3B3532 0%, #2F2926 60%, #201B18 100%)",
      },
    },
    themePreviewColors: {
      light: { text: "#181310", icons: "#2F2926", activeTabStroke: "#BDB6B1" },
      dark: { text: "#FFF9F6", icons: "#FFF9F6", activeTabStroke: "#59524F" },
    },
  },
];

const FIREFOX_THEMES_MAP = new Map(
  FIREFOX_THEMES_LIST.map(theme => [theme.id, theme])
);

// The themePreviewColors light and dark values of the classic themes are taken
// from the colors of the AMO-hosted theme manifest.json, "theme" for light and
// "dark_theme" for dark, as follows:
//
// - tabbarBackground:    colors.frame
// - text:                colors.tab_background_text
// - icons:               colors.icons
// - activeTabStroke:     colors.tab_line
// - activeTabBackground: colors.tab_selected
// - toolbarBackground:   colors.toolbar
// - urlbarBackground:    colors.toolbar_field
// - urlbarBorder:        colors.toolbar_field_border
//
// When the AMO-hosted theme manifest only has "theme", or its "theme" and
// "dark_theme" colors are identical, only "light" is set.
//
// Manifest colors with an alpha channel are flattened into opaque colors by
// blending them over the color they are drawn on (toolbar_field over toolbar,
// tab_selected over frame).

/** @type {Array<ThemeListEntry>} */
export const CLASSIC_THEMES_LIST = [
  {
    id: "proton-theme@mozilla.org",
    themePreviewColors: {
      light: {
        tabbarBackground: "#EAEAED",
        text: "#15141A",
        icons: "#5B5B66",
        activeTabStroke: "#B7B6BA",
        activeTabBackground: "#FFFFFF",
        toolbarBackground: "#F9F9FB",
        urlbarBackground: "#EDEDEE",
        urlbarBorder: "#00000000",
      },
      dark: {
        tabbarBackground: "#1C1B22",
        text: "#FBFBFE",
        icons: "#FBFBFE",
        activeTabStroke: "#312F33",
        activeTabBackground: "#53525E",
        toolbarBackground: "#2B2A33",
        urlbarBackground: "#1E1D24",
        urlbarBorder: "#00000000",
      },
    },
  },
  {
    id: "{4223a94a-d3f9-40e9-95dd-99aca80ea04b}",
    themePreviewColors: {
      light: {
        tabbarBackground: "#ABDFFF",
        text: "#00317E",
        icons: "#0053CB",
        activeTabStroke: "#60ADFF",
        activeTabBackground: "#FFFFFF",
        toolbarBackground: "#E2F7FF",
        urlbarBackground: "#FFFFFF",
        urlbarBorder: "#60ADFF",
      },
      dark: {
        tabbarBackground: "#08152C",
        text: "#ABDFFF",
        icons: "#84C6FF",
        activeTabStroke: "#0041A4",
        activeTabBackground: "#00317E",
        toolbarBackground: "#042356",
        urlbarBackground: "#161616",
        urlbarBorder: "#00317E",
      },
    },
  },
  {
    id: "{0683b144-0d4a-4815-963e-55a8ec8d386b}",
    themePreviewColors: {
      light: {
        tabbarBackground: "#A6ECF4",
        text: "#00485A",
        icons: "#007792",
        activeTabStroke: "#00CADB",
        activeTabBackground: "#FFFFFF",
        toolbarBackground: "#CFFFFF",
        urlbarBackground: "#FFFFFF",
        urlbarBorder: "#00CADB",
      },
      dark: {
        tabbarBackground: "#001F2B",
        text: "#A6ECF4",
        icons: "#61DCE9",
        activeTabStroke: "#005F75",
        activeTabBackground: "#00485A",
        toolbarBackground: "#00323D",
        urlbarBackground: "#161616",
        urlbarBorder: "#00485A",
      },
    },
  },
  {
    id: "{aab1adac-5449-47fd-b836-c2f43dc28f3f}",
    themePreviewColors: {
      light: {
        tabbarBackground: "#DDCFFF",
        text: "#3B2279",
        icons: "#623AC3",
        activeTabStroke: "#B295FF",
        activeTabBackground: "#FFFFFF",
        toolbarBackground: "#F4F0FF",
        urlbarBackground: "#FFFFFF",
        urlbarBorder: "#B295FF",
      },
      dark: {
        tabbarBackground: "#16112B",
        text: "#DCD3FF",
        icons: "#C7B2FF",
        activeTabStroke: "#4E2E9E",
        activeTabBackground: "#3B2279",
        toolbarBackground: "#281953",
        urlbarBackground: "#161616",
        urlbarBorder: "#3B2279",
      },
    },
  },
  {
    id: "{7063abff-a690-4b87-a548-fc32d3ce5708}",
    themePreviewColors: {
      light: {
        tabbarBackground: "#B5F0B5",
        text: "#004800",
        icons: "#007700",
        activeTabStroke: "#61CC69",
        activeTabBackground: "#FFFFFF",
        toolbarBackground: "#E1FFE1",
        urlbarBackground: "#FFFFFF",
        urlbarBorder: "#61CC69",
      },
      dark: {
        tabbarBackground: "#051C07",
        text: "#B5F0B5",
        icons: "#8ADF8D",
        activeTabStroke: "#005F00",
        activeTabBackground: "#004800",
        toolbarBackground: "#003200",
        urlbarBackground: "#161616",
        urlbarBorder: "#004800",
      },
    },
  },
  {
    id: "{1d73a1eb-128d-4e9e-83f8-c0c51f8c5fd3}",
    themePreviewColors: {
      light: {
        tabbarBackground: "#F7CAFF",
        text: "#521568",
        icons: "#8627A8",
        activeTabStroke: "#D98DFA",
        activeTabBackground: "#FFFFFF",
        toolbarBackground: "#FFECFF",
        urlbarBackground: "#FFFFFF",
        urlbarBorder: "#D98DFA",
      },
      dark: {
        tabbarBackground: "#1E0E25",
        text: "#F7CAFF",
        icons: "#F0A3FF",
        activeTabStroke: "#6B1E87",
        activeTabBackground: "#521568",
        toolbarBackground: "#381147",
        urlbarBackground: "#161616",
        urlbarBorder: "#521568",
      },
    },
  },
  {
    id: "{2b0fadbf-238d-43db-aa9d-e06c9a7e000b}",
    themePreviewColors: {
      light: {
        tabbarBackground: "#FFC2DB",
        text: "#6B0035",
        icons: "#AD0059",
        activeTabStroke: "#FF7EAD",
        activeTabBackground: "#FFFFFF",
        toolbarBackground: "#FFE8F4",
        urlbarBackground: "#FFFFFF",
        urlbarBorder: "#FF7EAD",
      },
      dark: {
        tabbarBackground: "#270B15",
        text: "#FFC2DB",
        icons: "#FF94C3",
        activeTabStroke: "#8B0047",
        activeTabBackground: "#6B0035",
        toolbarBackground: "#490624",
        urlbarBackground: "#161616",
        urlbarBorder: "#6B0035",
      },
    },
  },
  {
    id: "{cd6791f7-4b6d-47b4-8877-1d4c82c6699d}",
    themePreviewColors: {
      light: {
        tabbarBackground: "#FFE699",
        text: "#5D2800",
        icons: "#9C5200",
        activeTabStroke: "#F1AF00",
        activeTabBackground: "#FFFFFF",
        toolbarBackground: "#FFF4D0",
        urlbarBackground: "#FFFFFF",
        urlbarBorder: "#F1AF00",
      },
      dark: {
        tabbarBackground: "#271000",
        text: "#FFE699",
        icons: "#F5CC58",
        activeTabStroke: "#773800",
        activeTabBackground: "#5D2800",
        toolbarBackground: "#421B00",
        urlbarBackground: "#161616",
        urlbarBorder: "#592B00",
      },
    },
  },
  {
    id: "{8de5f8c3-bfc2-443b-9913-7bbadbd1ba0d}",
    themePreviewColors: {
      light: {
        tabbarBackground: "#FFC3C9",
        text: "#6E001F",
        icons: "#B20037",
        activeTabStroke: "#FF7E8E",
        activeTabBackground: "#FFFFFF",
        toolbarBackground: "#FFE8EA",
        urlbarBackground: "#FFFFFF",
        urlbarBorder: "#FF7E8E",
      },
      dark: {
        tabbarBackground: "#290B0F",
        text: "#FFC3C9",
        icons: "#FFA0AA",
        activeTabStroke: "#90002B",
        activeTabBackground: "#6E001F",
        toolbarBackground: "#4C0516",
        urlbarBackground: "#161616",
        urlbarBorder: "#6E001F",
      },
    },
  },
  {
    id: "{7a301b7b-c3e2-40bf-a06b-6d517bbf138b}",
    themePreviewColors: {
      light: {
        tabbarBackground: "#FFCD9E",
        text: "#6A1500",
        icons: "#AB2700",
        activeTabStroke: "#FF8F31",
        activeTabBackground: "#FFFFFF",
        toolbarBackground: "#FFEDD6",
        urlbarBackground: "#FFFFFF",
        urlbarBorder: "#FF8F31",
      },
      dark: {
        tabbarBackground: "#270F00",
        text: "#FFCD9E",
        icons: "#FFAD69",
        activeTabStroke: "#8A1E00",
        activeTabBackground: "#6A1500",
        toolbarBackground: "#481200",
        urlbarBackground: "#161616",
        urlbarBorder: "#6A1500",
      },
    },
  },
  {
    id: "firefox-oled@oled.firefox",
    themePreviewColors: {
      light: {
        tabbarBackground: "#000000",
        text: "#D6D5DA",
        icons: "#949297",
        activeTabStroke: "#817F84",
        activeTabBackground: "#000000",
        toolbarBackground: "#000000",
        urlbarBackground: "#000000",
        urlbarBorder: "#3F3E42",
      },
    },
  },
];

const CLASSIC_THEMES_MAP = new Map(
  CLASSIC_THEMES_LIST.map(theme => [theme.id, theme])
);

/**
 * @param {string} themeId
 * @returns {ThemeListEntry|undefined} Returns the theme metadata entry from either
 *   FIREFOX_THEMES_LIST or CLASSIC_THEMES_LIST, or undefined if the theme isn't
 *   managed by this module.
 */
function getThemeEntry(themeId) {
  return FIREFOX_THEMES_MAP.get(themeId) ?? CLASSIC_THEMES_MAP.get(themeId);
}

// Surface colors shared by the previews of all the AMO-hosted Nova themes.
const NOVA_THEMES_PREVIEW_SURFACES = {
  "--active-tab-background": "light-dark(#FFFFFF, #1D1B1F)",
  "--toolbar-background": "light-dark(#FFFFFF, #1D1B1F)",
  "--urlbar-background": "light-dark(#FFFFFF, #1D1B1F)",
  "--urlbar-border": "light-dark(#00000024, #3F3E42)",
};

// Link-parameters of the preview surfaces mapped to the ThemePreviewColors key
// that overrides them when set.
const PREVIEW_SURFACE_OVERRIDES = {
  "--active-tab-background": "activeTabBackground",
  "--toolbar-background": "toolbarBackground",
  "--urlbar-background": "urlbarBackground",
  "--urlbar-border": "urlbarBorder",
};

/**
 * @param {ThemeColorVariant} variant
 * @returns {string} The variant as a CSS <image>, so that it can be used in
 *   a light-dark() together with a gradient variant.
 */
function themeColorVariantAsImage({ type, value }) {
  return type == "gradient" ? value : `linear-gradient(${value}, ${value})`;
}

/**
 * @param {object} options
 * @param {ThemeListEntry} options.theme
 * @param {ThemePreviewColors} options.light
 * @param {ThemePreviewColors} options.dark
 * @returns {Array<ThemeColorVariant>} The light and dark variants of the tab
 *   bar background: the theme's own tabbarBackground color when set (classic
 *   themes), otherwise its themePickerColors variants.
 */
function tabbarBackgroundVariants({ theme, light, dark }) {
  if (light.tabbarBackground) {
    return [light, dark].map(({ tabbarBackground }) => ({
      type: "color",
      value: tabbarBackground,
    }));
  }
  return [theme.themePickerColors.light, theme.themePickerColors.dark];
}

/**
 * Manages the set of Firefox built-in and AMO-hosted themes exposed in the
 * Firefox Themes Picker UI.
 */
class ThemesList {
  #installSource;

  /**
   * @param {object} options
   * @param {ThemesInstallSource} options.installSource
   */
  constructor({ installSource }) {
    this.#installSource = installSource;
  }

  /**
   * @param {object} [options]
   * @param {boolean} [options.showInCompactLayout] - If true, only return themes
   *   marked to be shown in the theme picker's compact layout mode.
   * @returns {Array<{id: string, themePickerColors: ThemePickerColors}>}
   *   Array of theme objects with id and themePickerColors.
   */
  getThemesInfo({ showInCompactLayout = false } = {}) {
    let themes = Array.from(FIREFOX_THEMES_MAP.values());
    if (showInCompactLayout) {
      themes = themes.filter(theme => theme.showInCompactLayout);
    }
    return themes.map(({ id, themePickerColors }) => ({
      id,
      themePickerColors,
    }));
  }

  /**
   * @returns {Array<{id: string}>} Array of theme objects with the id of each
   *   classic theme, which are not included in the {@link getThemesInfo}
   *   results. Used for the "Classic themes" section shown in the about:addons
   *   themes list view.
   */
  getClassicThemesInfo() {
    return Array.from(CLASSIC_THEMES_MAP.values(), ({ id }) => ({ id }));
  }

  /**
   * @param {string} themeId
   * @returns {boolean} Whether the given ID belongs to a theme managed by this instance.
   */
  hasThemeId(themeId) {
    return !!getThemeEntry(themeId);
  }

  /**
   * @param {string} themeId
   * @returns {boolean} Whether the given theme is a Firefox built-in (i.e. ships without
   *   requiring an AMO install).
   */
  isBuiltIn(themeId) {
    return !!getThemeEntry(themeId)?.isBuiltIn;
  }

  /**
   * @param {string} themeId
   * @returns {string|null} The CSS link-parameters value to apply to the
   *   default theme Nova preview svg to turn it into a preview for the given
   *   theme, or null for built-in themes and themes that aren't managed by
   *   this module.
   */
  getThemePreviewLinkParameters(themeId) {
    const theme = getThemeEntry(themeId);
    if (!theme?.themePreviewColors) {
      return null;
    }
    const light =
      theme.themePreviewColors.light ?? theme.themePreviewColors.dark;
    const dark = theme.themePreviewColors.dark ?? light;
    const lightDark = key => `light-dark(${light[key]}, ${dark[key]})`;
    const params = {
      "--tabbar-background": `light-dark(${tabbarBackgroundVariants({
        theme,
        light,
        dark,
      })
        .map(themeColorVariantAsImage)
        .join(", ")})`,
      "--text-color": lightDark("text"),
      "--icon-color": lightDark("icons"),
      "--active-tab-stroke": lightDark("activeTabStroke"),
      ...NOVA_THEMES_PREVIEW_SURFACES,
    };
    for (const [name, key] of Object.entries(PREVIEW_SURFACE_OVERRIDES)) {
      if (light[key]) {
        params[name] = lightDark(key);
      }
    }
    return Object.entries(params)
      .map(([name, value]) => `param(${name}, ${value})`)
      .join(", ");
  }

  /**
   * Enables or disables one of the Firefox Themes managed by this module by its addon ID,
   * installing it from AMO if not yet locally installed.
   *
   * @param {string} themeId - The addon ID of the theme to toggle.
   * @param {boolean} enabled - Whether to enable or disable the theme.
   * @param {object} [options]
   * @param {string} [options.layout] - The layout mode of the picker that made
   *   the selection, recorded on `theme_picker.change`. When absent, the call
   *   logs an error and records `"unknown"`.
   * @param {AddonInstallListener} [options.installListener] - Optional listener forwarded to
   *   {@link AddonInstall#addListener} when a download and install is required.
   * @returns {Promise<boolean>} `true` if the request completed successfully, `false` if it
   *   failed or the given `themeId` is not managed by this instance.
   */
  async updateThemeState(themeId, enabled, { layout, installListener } = {}) {
    if (!this.hasThemeId(themeId)) {
      console.error(
        "ThemesList.updateThemeState can only update themes managed by it"
      );
      return false;
    }

    let install;
    try {
      const addon = await lazy.AddonManager.getAddonByID(themeId);
      if (!enabled) {
        await addon?.disable();
        return true;
      }

      if (!layout) {
        console.error(
          'ThemesList.updateThemeState called without a layout, so theme_picker.change records "unknown"'
        );
      }
      const eventExtras = {
        source: this.#installSource,
        layout: layout ?? "unknown",
        property: "theme",
        theme_id: themeId,
      };

      if (addon) {
        await addon.enable();
        Glean.themePicker.change.record(eventExtras);
        return true;
      }

      const [repoAddon] = await lazy.AddonRepository.getAddonsByIDs([themeId]);
      if (!repoAddon?.sourceURI) {
        console.error("Unable to resolve the XPI url for the theme", themeId);
        return false;
      }

      const installUrl = repoAddon.sourceURI.spec;
      const installName = repoAddon.name;
      const installVersion = repoAddon.version;

      install = await lazy.AddonManager.getInstallForURL(installUrl, {
        id: themeId,
        name: installName,
        type: "theme",
        version: installVersion,
        telemetryInfo: {
          source: this.#installSource,
          method: "FirefoxThemesList",
        },
      });
      if (installListener) {
        install.addListener(installListener);
      }
      const theme = await install.install();
      await theme.enable();
      Glean.themePicker.change.record(eventExtras);
      return true;
    } catch (err) {
      console.error(
        "Error on downloading or installing the theme",
        themeId,
        err
      );
      return false;
    } finally {
      if (installListener) {
        install?.removeListener(installListener);
      }
    }
  }
}

/**
 * Returns a {@link ThemesList} for the given install source.
 *
 * Marked async so callers cannot assume the theme list is synchronously available;
 * the source may move to RemoteSettings in the future (see Bug 1922374).
 *
 * @param {object} options
 * @param {ThemesInstallSource} options.installSource
 * @returns {Promise<ThemesList>}
 */
export async function getThemesList({ installSource }) {
  if (!installSource) {
    throw new Error("getThemesList installSource option is mandatory");
  }
  return new ThemesList({ installSource });
}
