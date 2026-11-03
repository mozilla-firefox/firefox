/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsLookAndFeel
#define _nsLookAndFeel

#include "X11UndefineNone.h"
#include "gfxFont.h"
#include "nsXPLookAndFeel.h"

struct _GtkStyle;
typedef struct _GDBusProxy GDBusProxy;
typedef struct _GtkCssProvider GtkCssProvider;
typedef struct _GFile GFile;
typedef struct _GFileMonitor GFileMonitor;
typedef struct _GVariant GVariant;

namespace mozilla {
enum class StyleGtkThemeFamily : uint8_t;

namespace widget {

enum class NativeChangeKind : uint8_t {
  None = 0,
  GtkTheme = 1 << 0,
  OtherSettings = 1 << 1,
  All = GtkTheme | OtherSettings,
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(NativeChangeKind)

}  // namespace widget
}  // namespace mozilla

class nsLookAndFeel final : public nsXPLookAndFeel {
  using NativeChangeKind = mozilla::widget::NativeChangeKind;

 public:
  nsLookAndFeel();
  virtual ~nsLookAndFeel();

  void RecordChange(NativeChangeKind aKind) { mPendingChanges |= aKind; }
  void NativeInit() final;
  nsresult NativeGetInt(IntID aID, int32_t& aResult) override;
  nsresult NativeGetFloat(FloatID aID, float& aResult) override;
  nsresult NativeGetColor(ColorID, ColorScheme, nscolor& aResult) override;
  bool NativeGetFont(FontID aID, nsString& aFontName,
                     gfxFontStyle& aFontStyle) override;

  char16_t GetPasswordCharacterImpl() override;
  bool GetEchoPasswordImpl() override;

  void RefreshImpl() override {
    // When calling Refresh(), we don't need to reload all our GTK theme info,
    // but we might need to change our effective theme.
    RecordChange(NativeChangeKind::OtherSettings);
  }

  bool GetDefaultDrawInTitlebar() override;

  nsXPLookAndFeel::TitlebarAction GetTitlebarAction(
      TitlebarEvent aEvent) override;

  void GetThemeInfo(nsACString&) override;

  nsresult GetKeyboardLayoutImpl(nsACString& aLayout) override;

  static const nscolor kBlack = NS_RGB(0, 0, 0);
  static const nscolor kWhite = NS_RGB(255, 255, 255);
#ifdef MOZ_ENABLE_DBUS
  // Returns whether any setting changed.
  bool RecomputeDBusSettings();
  // Returns whether the setting really changed.
  bool RecomputeDBusAppearanceSetting(const nsACString& aKey, GVariant* aValue);
#endif

  struct ColorPair {
    nscolor mBg = kWhite;
    nscolor mFg = kBlack;

    bool operator==(const ColorPair& aOther) const = default;
  };

  struct ButtonColors : ColorPair {
    nscolor mBorder = kBlack;

    bool operator==(const ButtonColors& aOther) const {
      return mBg == aOther.mBg && mFg == aOther.mFg &&
             mBorder == aOther.mBorder;
    }
    bool operator!=(const ButtonColors& aOther) const {
      return !(*this == aOther);
    }
  };

  using ThemeFamily = mozilla::StyleGtkThemeFamily;

 protected:
  void RecordLookAndFeelSpecificTelemetry() override;
  static bool ShouldHonorThemeScrollbarColors();
  mozilla::Maybe<ColorScheme> ComputeColorSchemeSetting();

#ifdef MOZ_ENABLE_DBUS
  void WatchDBus();
  void UnwatchDBus();
#endif

  // We use up to two themes (one light, one dark), which might have different
  // sets of fonts and colors.
  struct PerThemeData {
    nsCString mName;
    bool mIsDark = false;
    bool mHighContrast = false;
    bool mPreferDarkTheme = false;
    bool mIsDefaultThemeFallback = false;

    ThemeFamily mFamily{0};

    // Cached fonts
    nsString mDefaultFontName;
    nsString mButtonFontName;
    nsString mFieldFontName;
    nsString mMenuFontName;
    gfxFontStyle mDefaultFontStyle;
    gfxFontStyle mButtonFontStyle;
    gfxFontStyle mFieldFontStyle;
    gfxFontStyle mMenuFontStyle;

    // Cached colors
    nscolor mGrayText = kBlack;
    ColorPair mInfo;
    ColorPair mMenu;
    ColorPair mMenuHover;
    ColorPair mHeaderBar;
    ColorPair mHeaderBarInactive;
    ButtonColors mButton;
    ButtonColors mButtonHover;
    ButtonColors mButtonActive;
    ButtonColors mButtonDisabled;
    nscolor mFrameBorder = kBlack;
    nscolor mNativeHyperLinkText = kBlack;
    nscolor mNativeVisitedHyperLinkText = kBlack;
    ColorPair mField;
    ColorPair mWindow;
    ColorPair mDialog;
    ColorPair mSidebar;
    nscolor mSidebarBorder = kBlack;

    nscolor mMozWindowActiveBorder = kBlack;
    nscolor mMozWindowInactiveBorder = kBlack;

    ColorPair mCellHighlight;
    ColorPair mSelectedText;
    ColorPair mAccent;
    ColorPair mSelectedItem;

    ColorPair mMozColHeader;
    ColorPair mMozColHeaderHover;
    ColorPair mMozColHeaderActive;

    ColorPair mTitlebar;
    ColorPair mTitlebarInactive;

    nscolor mThemedScrollbar = kWhite;
    nscolor mThemedScrollbarThumb = kBlack;
    nscolor mThemedScrollbarThumbHover = kBlack;
    nscolor mThemedScrollbarThumbActive = kBlack;

    float mCaretRatio = 0.0f;
    int32_t mTitlebarRadius = 0;
    int32_t mTooltipRadius = 0;
    char16_t mInvisibleCharacter = 0;
    bool mMenuSupportsDrag = false;

    void Init();
    nsresult GetColor(ColorID, nscolor&) const;
    bool GetFont(FontID, nsString& aFontName, gfxFontStyle&,
                 float aTextScaleFactor) const;
    void InitCellHighlightColors();
    void RestoreColorOverrides();
    void ApplyColorOverride(nscolor* aMember, nscolor aNewColor);
    void ApplyColorOverride(ColorPair* aMember, const ColorPair& aNewPair);

    struct ColorOverride {
      uint32_t mByteOffset;
      nscolor mOriginalColor;
    };
    nsTArray<ColorOverride> mOverrides;
  };

  PerThemeData mSystemTheme;

  // If the system theme is light, a dark theme. Otherwise, a light theme. The
  // alternative theme to the current one is preferred, but otherwise we fall
  // back to Adwaita / Adwaita Dark, respectively.
  PerThemeData mAltTheme;

  const PerThemeData& LightTheme() const {
    return mSystemTheme.mIsDark ? mAltTheme : mSystemTheme;
  }

  const PerThemeData& DarkTheme() const {
    return mSystemTheme.mIsDark ? mSystemTheme : mAltTheme;
  }

  const PerThemeData& EffectiveTheme() const {
    return mSystemThemeOverridden ? mAltTheme : mSystemTheme;
  }

  uint32_t mDBusID = 0;
  RefPtr<GFile> mKdeColors;
  RefPtr<GFileMonitor> mKdeColorsMonitor;

  mozilla::Maybe<ColorScheme> mColorSchemePreference;
  RefPtr<GDBusProxy> mDBusSettingsProxy;
  // DBus settings from:
  // https://github.com/flatpak/xdg-desktop-portal/blob/main/data/org.freedesktop.portal.Settings.xml
  struct DBusSettings {
    mozilla::Maybe<ColorScheme> mColorScheme;
    bool mPrefersContrast = false;
    // Transparent means no accent-color. Note that the real accent color cannot
    // have transparency.
    ColorPair mAccentColor{NS_TRANSPARENT, NS_TRANSPARENT};

    bool HasAccentColor() const { return NS_GET_A(mAccentColor.mBg); }
  } mDBusSettings;
  int32_t mCaretBlinkTime = 0;
  int32_t mCaretBlinkCount = -1;
  bool mCSDMaximizeButton = false;
  bool mCSDMinimizeButton = false;
  bool mCSDCloseButton = false;
  bool mCSDReversedPlacement = false;
  bool mPrefersReducedMotion = false;
  bool mSystemThemeOverridden = false;
  NativeChangeKind mPendingChanges = NativeChangeKind::All;
  int32_t mCSDMaximizeButtonPosition = 0;
  int32_t mCSDMinimizeButtonPosition = 0;
  int32_t mCSDCloseButtonPosition = 0;
  TitlebarAction mDoubleClickAction = TitlebarAction::None;
  TitlebarAction mMiddleClickAction = TitlebarAction::None;
  float mTextScaleFactor = 1.0f;

  int32_t mRoundedCornerProviderRadius = 0;
  RefPtr<GtkCssProvider> mRoundedCornerProvider;
  void UpdateRoundedBottomCornerStyles();

  void ClearRoundedCornerProvider();

  void EnsureInit() {
    if (mPendingChanges == NativeChangeKind::None) {
      return;
    }
    Initialize();
  }

  void Initialize();

  void RestoreSystemTheme();
  void InitializeGlobalSettings();
  // Returns whether we found an alternative theme.
  bool ConfigureAltTheme();
  void ConfigureAndInitializeAltTheme();
  void ConfigureFinalEffectiveTheme();
  void MaybeApplyColorOverrides();
};

#endif
