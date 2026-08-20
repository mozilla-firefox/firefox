/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useEffect, useState } from "react";
import PropTypes from "prop-types/prop-types";
import { Localized, CONFIGURABLE_STYLES } from "./MSLocalized";
import { MultiStageUtils } from "../lib/multistage-utils.mjs";
import {
  SecondaryCTA,
  StepsIndicator,
  ProgressBar,
} from "./MultiStageAboutWelcome";
import { LanguageSwitcher } from "./LanguageSwitcher";
import { CTAParagraph } from "./CTAParagraph";
import { HeroImage } from "./HeroImage";
import { OnboardingVideo } from "./OnboardingVideo";
import { AdditionalCTA } from "./AdditionalCTA";
import { LinkParagraph } from "./LinkParagraph";
import { ContentTiles } from "./ContentTiles";
import { InstallButton } from "./InstallButton";
import { SubmenuButton } from "./SubmenuButton";

const DEFAULT_AUTO_ADVANCE_MS = 20000;
const CORNER_IMAGE_POSITIONS = new Set([
  "bottom-left",
  "bottom-right",
  "top-left",
  "top-right",
]);
const DEFAULT_CORNER_IMAGE_POSITION = "bottom-right";

export const MultiStageProtonScreen = props => {
  const {
    autoAdvance,
    advanceOnExperimentLoad,
    handleAction,
    messageId,
    navigate,
    order,
  } = props;
  useEffect(() => {
    if (!autoAdvance && !advanceOnExperimentLoad) {
      return () => {};
    }

    if (autoAdvance) {
      const value = autoAdvance?.actionEl ?? autoAdvance;
      const timeout = autoAdvance?.actionTimeMS ?? DEFAULT_AUTO_ADVANCE_MS;

      const timer = setTimeout(() => {
        handleAction({
          currentTarget: {
            value,
          },
          name: "AUTO_ADVANCE",
        });
      }, timeout);
      return () => clearTimeout(timer);
    }

    // If not doing a standard auto advance, handle auto advancing when experiments load.

    // Defaults for when advance_on_experiment_load is true.
    const minMsDefault = 3000;
    const maxMsDefault = 8000;

    let minMs = advanceOnExperimentLoad?.minDisplayMs ?? minMsDefault;
    let maxMs = advanceOnExperimentLoad?.maxDisplayMs ?? maxMsDefault;

    // Ensure max >= min.
    if (maxMs < minMs) {
      maxMs = minMs;
    }

    const startTime = performance.now();
    let cancelled = false;
    let advanced = false;
    let minDone = false;
    let experimentsDone = false;
    let nimbusResult = null;
    let maxTimeoutFired = false;

    const doAdvance = () => {
      if (cancelled || advanced) {
        return;
      }
      advanced = true;

      const screen_duration = Math.round(performance.now() - startTime);
      let reason;
      if (maxTimeoutFired) {
        reason = "max_display_timeout";
      } else if (nimbusResult === "error") {
        reason = "nimbus_error";
      } else if (nimbusResult === "timeout") {
        reason = "nimbus_timeout";
      } else {
        reason = "nimbus_ready";
      }
      MultiStageUtils.sendActionTelemetry(
        messageId,
        "advance_on_experiment_load",
        "SPLASH_DISMISSED",
        { reason, screen_duration }
      );

      navigate(false);
    };

    const maybeAdvance = () => {
      if (minDone && experimentsDone) {
        doAdvance();
      }
    };

    const minTimerId = window.setTimeout(() => {
      minDone = true;
      maybeAdvance();
    }, minMs);

    const maxTimerId = window.setTimeout(() => {
      maxTimeoutFired = true;
      doAdvance();
    }, maxMs);

    // Use an IIFE to keep the effect itself synchronous
    (async () => {
      try {
        if (typeof window.AWWaitForNimbus === "function") {
          nimbusResult = await window.AWWaitForNimbus();
        }
        // If AWWaitForNimbus is missing, treat as "done" immediately.
      } catch (e) {
      } finally {
        experimentsDone = true;
        maybeAdvance();
      }
    })();

    return () => {
      cancelled = true;
      window.clearTimeout(minTimerId);
      window.clearTimeout(maxTimerId);
    };
  }, [
    autoAdvance,
    advanceOnExperimentLoad,
    handleAction,
    messageId,
    order,
    navigate,
  ]);

  // Set narrow on an outer element to allow for use of SCSS outer selector and
  // consolidation of styles for small screen widths with those for messages
  // configured to always be narrow
  if (props.content.narrow) {
    document
      .querySelector("#multi-stage-message-root")
      ?.setAttribute("narrow", "");
  } else {
    // Clear narrow attribute in case it was set by a previous screen
    document
      .querySelector("#multi-stage-message-root")
      ?.removeAttribute("narrow");
  }

  function useMediaQuery(query) {
    const [doesMatch, setDoesMatch] = useState(
      () => window.matchMedia(query).matches
    );

    useEffect(() => {
      const mediaQueryList = window.matchMedia(query);
      const onChange = event => setDoesMatch(event.matches);

      mediaQueryList.addEventListener("change", onChange);
      return () => mediaQueryList.removeEventListener("change", onChange);
    }, [query]);

    return doesMatch;
  }

  const isWideScreen = useMediaQuery("(min-width: 800px)");

  return (
    <ProtonScreen
      content={props.content}
      id={props.id}
      order={props.order}
      activeTheme={props.activeTheme}
      installedAddons={props.installedAddons}
      screenMultiSelects={props.screenMultiSelects}
      setScreenMultiSelects={props.setScreenMultiSelects}
      activeMultiSelect={props.activeMultiSelect}
      setActiveMultiSelect={props.setActiveMultiSelect}
      activeSingleSelectSelections={props.activeSingleSelectSelections}
      setActiveSingleSelectSelection={props.setActiveSingleSelectSelection}
      textInputs={props.textInputs}
      setTextInput={props.setTextInput}
      pinnedSites={props.pinnedSites}
      setPinnedSite={props.setPinnedSite}
      contentToggleChecked={props.contentToggleChecked}
      setContentToggleChecked={props.setContentToggleChecked}
      totalNumberOfScreens={props.totalNumberOfScreens}
      handleAction={props.handleAction}
      isFirstScreen={props.isFirstScreen}
      isLastScreen={props.isLastScreen}
      isSingleScreen={props.isSingleScreen}
      previousOrder={props.previousOrder}
      autoAdvance={props.autoAdvance}
      advanceOnExperimentLoad={props.advanceOnExperimentLoad}
      navigate={props.navigate}
      isRtamo={props.isRtamo}
      addonId={props.addonId}
      addonType={props.addonType}
      addonName={props.addonName}
      addonURL={props.addonURL}
      addonIconURL={props.addonIconURL}
      themeScreenshots={props.themeScreenshots}
      messageId={props.messageId}
      negotiatedLanguage={props.negotiatedLanguage}
      langPackInstallPhase={props.langPackInstallPhase}
      forceHideStepsIndicator={props.forceHideStepsIndicator}
      ariaRole={props.ariaRole}
      aboveButtonStepsIndicator={props.aboveButtonStepsIndicator}
      requireAction={props.requireAction}
      isWideScreen={isWideScreen}
      animationsPaused={props.animationsPaused}
      toggleAnimationsPaused={props.toggleAnimationsPaused}
    />
  );
};

export const ProtonScreenActionButtons = props => {
  const {
    content,
    isRtamo,
    addonId,
    addonType,
    addonName,
    activeMultiSelect,
    activeSingleSelectSelections,
    textInputs,
    pinnedSites,
    installedAddons,
  } = props;
  const defaultValue = content.checkbox?.defaultValue;

  const [isChecked, setIsChecked] = useState(defaultValue || false);
  const buttonRef = React.useRef(null);

  const shouldFocusButton = content?.primary_button?.should_focus_button;

  useEffect(() => {
    if (shouldFocusButton) {
      buttonRef.current?.focus();
    }
  }, [shouldFocusButton]);

  if (
    !content.primary_button &&
    !content.secondary_button &&
    !content.additional_button
  ) {
    return null;
  }

  if (isRtamo) {
    content.primary_button.label.string_id = addonType?.includes("theme")
      ? "return-to-amo-add-theme-label"
      : "mr1-return-to-amo-add-extension-label";
  }

  // If we have a multi-select screen, we want to disable the primary button
  // until the user has selected at least one item.
  const isPrimaryDisabled = disabledValue => {
    if (disabledValue === "hasActiveMultiSelect") {
      if (!activeMultiSelect) {
        return true;
      }

      // Check if there's at least one selection in any of the multiselects
      for (const selectKey in activeMultiSelect) {
        if (activeMultiSelect[selectKey]?.length > 0) {
          return false;
        }
      }
      return true;
    }
    // Only disables the primary button until at least one single-select
    // option is chosen.
    if (disabledValue === "hasActiveSingleSelect") {
      if (!activeSingleSelectSelections) {
        return true;
      }
      return !Object.values(activeSingleSelectSelections).some(
        val => val && val !== "none"
      );
    }
    if (disabledValue === "hasTextInput") {
      // For text input, we check if the user has entered any text in the
      // textarea(s) present on the screen.
      if (!textInputs) {
        return true;
      }
      return Object.values(textInputs).every(
        input => !input.isValid || input.value.trim().length === 0
      );
    }
    // Disables the primary button until the user has pinned at least one site.
    if (disabledValue === "hasPinnedSite") {
      return !pinnedSites;
    }
    return disabledValue;
  };

  return (
    <div
      className={`action-buttons ${
        content.additional_button ? "additional-cta-container" : ""
      }`}
      flow={content.additional_button?.flow}
      alignment={content.additional_button?.alignment}
    >
      {isRtamo ? (
        <InstallButton
          key={addonId}
          addonId={addonId}
          addonType={addonType}
          addonName={addonName}
          index={"primary_button"}
          handleAction={props.handleAction}
          installedAddons={installedAddons}
          install_label={content.primary_button.label}
          install_complete_label={content.primary_button.install_complete_label}
        />
      ) : (
        <Localized text={content.primary_button?.label}>
          <button
            ref={buttonRef}
            className={`${content.primary_button?.style ?? "primary"}${
              content.primary_button?.has_arrow_icon ? " arrow-icon" : ""
            }`}
            // Whether or not the checkbox is checked determines which action
            // should be handled. By setting value here, we indicate to
            // this.handleAction() where in the content tree it should take
            // the action to execute from.
            value={isChecked ? "checkbox" : "primary_button"}
            disabled={isPrimaryDisabled(content.primary_button?.disabled)}
            onClick={props.handleAction}
            data-l10n-args={
              addonName
                ? JSON.stringify({
                    "addon-name": addonName,
                  })
                : ""
            }
          />
        </Localized>
      )}
      {content.additional_button ? (
        <AdditionalCTA
          content={content}
          handleAction={props.handleAction}
          activeMultiSelect={activeMultiSelect}
          textInputs={textInputs}
        />
      ) : null}
      {content.checkbox ? (
        <div className="checkbox-container">
          <input
            type="checkbox"
            id="action-checkbox"
            checked={isChecked}
            onChange={() => {
              setIsChecked(!isChecked);
            }}
          ></input>
          <Localized text={content.checkbox.label}>
            <label htmlFor="action-checkbox"></label>
          </Localized>
        </div>
      ) : null}
      {content.secondary_button ? (
        <SecondaryCTA
          content={content}
          handleAction={props.handleAction}
          activeMultiSelect={activeMultiSelect}
          textInputs={textInputs}
        />
      ) : null}
    </div>
  );
};

export class ProtonScreen extends React.PureComponent {
  componentDidMount() {
    // Don't focus on main content if it is a feature callout
    // See Bug 1985939
    if (this.props.content?.position === "callout") {
      return;
    }

    // For requireAction screens, focus the title heading instead of the
    // alertdialog container.
    // Prevents Orca from misreading tab navigated elements.
    if (this.props.requireAction && this.titleHeader) {
      this.titleHeader.focus();
    } else {
      this.mainContentHeader.focus();
    }
  }

  getScreenClassName(
    includeNoodles,
    hasZapBorder,
    hasZapShadow,
    isVideoOnboarding,
    isAddonsPicker
  ) {
    if (isVideoOnboarding) {
      return "with-video";
    }

    if (isAddonsPicker) {
      return "addons-picker";
    }

    const screenClass = `screen-${this.props.order % 2 !== 0 ? 1 : 2}`;
    const dialogInitial =
      this.props.isFirstScreen && this.props.previousOrder < 0
        ? `dialog-initial`
        : ``;
    const dialogLast = this.props.isLastScreen ? `dialog-last` : ``;
    const zapBorder = hasZapBorder ? `zap-border` : ``;
    const zapShadow = hasZapShadow ? `zap-shadow` : ``;

    return `${screenClass} ${dialogInitial} ${dialogLast} ${zapBorder} ${zapShadow} ${includeNoodles ? `with-noodles` : ``}`;
  }

  renderTitle({ title, title_logo }) {
    const titleRef = this.props.requireAction
      ? input => {
          this.titleHeader = input;
        }
      : null;
    if (title_logo) {
      const { alignment, ...rest } = title_logo;
      return (
        <div
          className="inline-icon-container"
          alignment={alignment ?? "center"}
        >
          {this.renderPicture({ ...rest })}
          <Localized text={title}>
            <h1
              id="mainContentHeader"
              tabIndex={this.props.requireAction ? -1 : undefined}
              ref={titleRef}
            />
          </Localized>
        </div>
      );
    }
    return (
      <Localized text={title}>
        <h1
          id="mainContentHeader"
          tabIndex={this.props.requireAction ? -1 : undefined}
          ref={titleRef}
        />
      </Localized>
    );
  }

  renderPicture({
    imageURL = "chrome://branding/content/about-logo.svg",
    darkModeImageURL,
    reducedMotionImageURL,
    darkModeReducedMotionImageURL,
    alt = "",
    width,
    height,
    marginBlock,
    marginInline,
    style,
    className = "logo-container",
  }) {
    function getLoadingStrategy() {
      for (let url of [
        imageURL,
        darkModeImageURL,
        reducedMotionImageURL,
        darkModeReducedMotionImageURL,
      ]) {
        if (MultiStageUtils.getLoadingStrategyFor(url) === "lazy") {
          return "lazy";
        }
      }
      return "eager";
    }

    const pictureStyle = {
      marginInline,
      marginBlock,
      ...style,
    };

    return (
      <picture className={className} style={pictureStyle}>
        {darkModeReducedMotionImageURL ? (
          <source
            srcset={darkModeReducedMotionImageURL}
            media="(prefers-color-scheme: dark) and (prefers-reduced-motion: reduce)"
          />
        ) : null}
        {darkModeImageURL ? (
          <source
            srcset={darkModeImageURL}
            media="(prefers-color-scheme: dark)"
          />
        ) : null}
        {reducedMotionImageURL ? (
          <source
            srcset={reducedMotionImageURL}
            media="(prefers-reduced-motion: reduce)"
          />
        ) : null}
        <Localized text={alt}>
          <div className="sr-only logo-alt" />
        </Localized>
        <img
          className="brand-logo"
          style={{ height, width }}
          src={imageURL}
          alt=""
          loading={getLoadingStrategy()}
          role={alt ? null : "presentation"}
        />
      </picture>
    );
  }

  renderNoodles() {
    return (
      <React.Fragment>
        <div className={"noodle orange-L"} />
        <div className={"noodle purple-C"} />
        <div className={"noodle solid-L"} />
        <div className={"noodle outline-L"} />
        <div className={"noodle yellow-circle"} />
      </React.Fragment>
    );
  }

  renderCornerImage() {
    const cornerImage = this.props.content.corner_image;
    const position = CORNER_IMAGE_POSITIONS.has(cornerImage.position)
      ? cornerImage.position
      : DEFAULT_CORNER_IMAGE_POSITION;

    return (
      <div className={"corner-image-container"}>
        {this.renderPicture({
          imageURL: cornerImage.imageURL,
          darkModeImageURL: cornerImage.darkModeImageURL,
          reducedMotionImageURL: cornerImage.reducedMotionImageURL,
          darkModeReducedMotionImageURL:
            cornerImage.darkModeReducedMotionImageURL,
          height: cornerImage.height,
          width: cornerImage.width,
          marginBlock: cornerImage.marginBlock,
          marginInline: cornerImage.marginInline,
          style: cornerImage.style,
          className: `corner-image ${position}`,
        })}
      </div>
    );
  }

  renderLanguageSwitcher() {
    return this.props.content.languageSwitcher ? (
      <LanguageSwitcher
        content={this.props.content}
        handleAction={this.props.handleAction}
        negotiatedLanguage={this.props.negotiatedLanguage}
        langPackInstallPhase={this.props.langPackInstallPhase}
        messageId={this.props.messageId}
      />
    ) : null;
  }

  renderDismissButton() {
    const { size, marginBlock, marginInline, label, background } =
      this.props.content.dismiss_button;
    return (
      <button
        className={`dismiss-button ${background ? "with-background" : ""}`}
        onClick={this.props.handleAction}
        value="dismiss_button"
        data-l10n-id={label?.string_id || "spotlight-dialog-close-button"}
        button-size={size}
        style={{ marginBlock, marginInline }}
      ></button>
    );
  }

  renderMoreButton() {
    return (
      <SubmenuButton
        content={this.props.content}
        handleAction={this.props.handleAction}
        buttonType="more"
      />
    );
  }

  renderStepsIndicator() {
    const {
      order,
      previousOrder,
      content,
      totalNumberOfScreens: total,
      aboveButtonStepsIndicator,
    } = this.props;
    const currentStep = (order ?? 0) + 1;
    const previousStep = (previousOrder ?? -1) + 1;
    return (
      <div
        id="steps"
        className={`steps${content.progress_bar ? " progress-bar" : ""}`}
        above-button={aboveButtonStepsIndicator ? "" : null}
        data-l10n-id={
          content.steps_indicator?.string_id ||
          "onboarding-welcome-steps-indicator-label"
        }
        data-l10n-args={JSON.stringify({
          current: currentStep,
          total: total ?? 0,
        })}
        data-l10n-attrs="aria-label"
        role="progressbar"
        aria-valuenow={currentStep}
        aria-valuemin={1}
        aria-valuemax={total}
      >
        {content.progress_bar ? (
          <ProgressBar
            step={currentStep}
            previousStep={previousStep}
            totalNumberOfScreens={total}
          />
        ) : (
          <StepsIndicator order={order} totalNumberOfScreens={total} />
        )}
      </div>
    );
  }

  // We consider a screen to have animated content when it includes both
  // default and _static versions of its background image or hero_image url.
  // The pause toggle swaps the rendered asset to the _static asset when activated.
  // Without _both_ default and static, there is nothing for the toggle to
  // switch to, so we don't render the toggle button at all.
  hasAnimatedContent(content) {
    return !!(
      (content.background && content.background_static) ||
      (content.hero_image?.url && content.hero_image?.static_url)
    );
  }

  getEffectiveBackground(content) {
    if (content.position !== "split") {
      const combinedBackground =
        content.background && content.zap_border
          ? `linear-gradient(96deg, #B89CFF 20.68%, #FF9565 79.34%) border-box border-area, image(${content.background}) padding-box`
          : content.background;

      const combinedBackgroundStatic =
        content.background_static && content.zap_border
          ? `linear-gradient(96deg, #B89CFF 20.68%, #FF9565 79.34%) border-box border-area, image(${content.background_static}) padding-box`
          : content.background_static;

      return this.props.animationsPaused && content.background_static
        ? combinedBackgroundStatic
        : combinedBackground;
    }
    return this.props.animationsPaused && content.background_static
      ? content.background_static
      : content.background;
  }

  getEffectiveHeroImageUrl(content) {
    if (!content.hero_image) {
      return null;
    }
    return this.props.animationsPaused && content.hero_image.static_url
      ? content.hero_image.static_url
      : content.hero_image.url;
  }

  renderAnimationPlayPauseButton() {
    const { toggleAnimationsPaused } = this.props;
    const paused = !!this.props.animationsPaused;
    const labelId = paused
      ? "onboarding-animation-play-button"
      : "onboarding-animation-pause-button";
    return (
      <button
        className={`animation-play-pause-button${paused ? " paused" : ""}`}
        type="button"
        aria-pressed={paused}
        data-l10n-id={labelId}
        onClick={toggleAnimationsPaused}
      />
    );
  }

  renderSecondarySection(content) {
    const background = this.getEffectiveBackground(content);
    const heroImageUrl = this.getEffectiveHeroImageUrl(content);
    // pinnable_sites exposes its background through a custom property so the
    // stylesheet can swap the wide-layout image for a narrow gradient without
    // overriding an inline `background` shorthand.
    const tiles = Array.isArray(content.tiles)
      ? content.tiles
      : [content.tiles];
    const isPinnableSites = tiles.some(tile => tile?.type === "pinnable_sites");
    return (
      <div
        className={`section-secondary ${
          content.hide_secondary_section ? "with-secondary-section-hidden" : ""
        }`}
        style={
          background
            ? {
                [isPinnableSites ? "--pinnable-sites-bkg" : "background"]:
                  background,
                "--mr-secondary-background-position-y":
                  content.split_narrow_bkg_position,
              }
            : {}
        }
      >
        {content.dismiss_button && content.reverse_split
          ? this.renderDismissButton()
          : null}
        <Localized text={content.image_alt_text}>
          <div className="sr-only image-alt" role="img" />
        </Localized>
        {content.hero_image ? (
          <HeroImage url={heroImageUrl} />
        ) : (
          this.renderHeroText(content.hero_text)
        )}
        {this.hasAnimatedContent(content)
          ? this.renderAnimationPlayPauseButton()
          : null}
      </div>
    );
  }

  renderHeroText(hero_text) {
    if (!hero_text) {
      return null;
    }

    // Check if hero_text is a string or an object with string_id property
    // essentially checking if we're using old or new design
    const isSimpleText =
      typeof hero_text === "string" ||
      (typeof hero_text === "object" &&
        hero_text !== null &&
        ("string_id" in hero_text || "raw" in hero_text));

    const HeroTextWrapper = ({ children, className }) => (
      <React.Fragment>
        <div className={`message-text ${className}`}>
          <div className="spacer-top" />
          {children}
          <div className="spacer-bottom" />
        </div>
      </React.Fragment>
    );

    if (isSimpleText) {
      return (
        <HeroTextWrapper className="simple">
          <Localized text={hero_text}>
            <h1 />
          </Localized>
        </HeroTextWrapper>
      );
    }

    return (
      <HeroTextWrapper className="hero-text">
        <Localized text={hero_text.title}>
          <h1 />
        </Localized>
        {hero_text.subtitle && (
          <Localized text={hero_text.subtitle}>
            <h2 />
          </Localized>
        )}
      </HeroTextWrapper>
    );
  }

  renderOrderedContent(content) {
    const elements = [];
    for (const item of content) {
      switch (item.type) {
        case "text":
          elements.push(
            <LinkParagraph
              text_content={item}
              handleAction={this.props.handleAction}
            />
          );
          break;
        case "image":
          elements.push(
            this.renderPicture({
              imageURL: item.url,
              darkModeImageURL: item.darkModeImageURL,
              height: item.height,
              width: item.width,
              alt: item.alt_text,
              marginInline: item.marginInline,
              className: "inline-image",
            })
          );
      }
    }
    return <>{elements}</>;
  }

  renderRTAMOIcon(addonType, themeScreenshots, addonIconURL) {
    return (
      <div className="rtamo-icon">
        <img
          className={`${addonType?.includes("theme") ? "rtamo-theme-icon" : "brand-logo"}`}
          src={
            addonType?.includes("theme")
              ? themeScreenshots[0].url
              : addonIconURL
          }
          loading={MultiStageUtils.getLoadingStrategyFor(addonIconURL)}
          alt=""
          role="presentation"
        />
      </div>
    );
  }

  getCombinedInnerStyles(content, isWideScreen) {
    const INNER_CONTENT_CONFIGURABLE_STYLES = [
      "overflow",
      "display",
      "paddingInline",
      "paddingInlineStart",
      "paddingInlineEnd",
      "paddingBlock",
      "paddingBlockStart",
      "paddingBlockEnd",
      "width",
    ];

    const innerContentStyles = isWideScreen
      ? content.main_content_style || {}
      : content.main_content_style_narrow || {};

    const validInnerStyles =
      MultiStageUtils.getValidStyle(
        innerContentStyles,
        INNER_CONTENT_CONFIGURABLE_STYLES
      ) || {};

    return {
      ...validInnerStyles,
      justifyContent: content.split_content_justify_content,
    };
  }

  getActionButtonsPosition(content) {
    const VALID_POSITIONS = [
      "after_subtitle",
      "after_supporting_content",
      "end",
    ];

    if (VALID_POSITIONS.includes(content.action_buttons_position)) {
      return content.action_buttons_position;
    }
    // Legacy mapping
    if (content.action_buttons_above_content) {
      return "after_subtitle";
    }
    // Default
    return "end";
  }

  renderActionButtons(position, content) {
    return this.getActionButtonsPosition(content) === position ? (
      <ProtonScreenActionButtons
        content={content}
        isRtamo={this.props.isRtamo}
        installedAddons={this.props.installedAddons}
        addonId={this.props.addonId}
        addonName={this.props.addonName}
        addonType={this.props.addonType}
        handleAction={this.props.handleAction}
        activeMultiSelect={this.props.activeMultiSelect}
        activeSingleSelectSelections={this.props.activeSingleSelectSelections}
        textInputs={this.props.textInputs}
        pinnedSites={this.props.pinnedSites}
      />
    ) : null;
  }

  // eslint-disable-next-line complexity
  render() {
    const {
      autoAdvance,
      content,
      isRtamo,
      addonType,
      isSingleScreen,
      forceHideStepsIndicator,
      ariaRole,
      aboveButtonStepsIndicator,
      isWideScreen,
    } = this.props;
    const includeNoodles = content.has_noodles;
    const isCenterLargeFullscreen =
      content.position === "center-large" && !!content.fullscreen;
    const includeCornerImage =
      !!content.corner_image && isCenterLargeFullscreen;
    const secondaryCTATop = content.secondary_button_top ? (
      <SecondaryCTA
        content={content}
        handleAction={this.props.handleAction}
        position="top"
      />
    ) : null;
    const hasZapBorder = content.zap_border;
    const hasZapShadow = content.zap_shadow;
    // The default screen position is "center"
    const isCenterPosition =
      ["center", "center-large"].includes(content.position) ||
      !content.position;
    const hideStepsIndicator =
      autoAdvance ||
      content?.video_container ||
      isSingleScreen ||
      forceHideStepsIndicator;
    const textColorClass = content.text_color
      ? `${content.text_color}-text`
      : "";
    // Assign proton screen style 'screen-1' or 'screen-2' to centered screens
    // by checking if screen order is even or odd.
    const screenClassName = isCenterPosition
      ? this.getScreenClassName(
          includeNoodles,
          hasZapBorder,
          hasZapShadow,
          content?.video_container,
          content.tiles?.type === "addons-picker"
        )
      : `${hasZapBorder ? "zap-border" : ""} ${hasZapShadow ? " zap-shadow" : ""}`;
    const isEmbeddedMigration = content.tiles?.type === "migration-wizard";
    const isSystemPromptStyleSpotlight =
      content.isSystemPromptStyleSpotlight === true;
    const combinedStyles = this.getCombinedInnerStyles(content, isWideScreen);

    return (
      <main
        className={`screen ${this.props.id || ""}
          ${screenClassName} ${textColorClass}`}
        reverse-split={content.reverse_split ? "" : null}
        fullscreen={content.fullscreen ? "" : null}
        style={
          content.screen_style &&
          MultiStageUtils.getValidStyle(content.screen_style, [
            "overflow",
            "display",
          ])
        }
        role={ariaRole ?? "alertdialog"}
        layout={content.layout}
        pos={content.position || "center"}
        tabIndex="-1"
        aria-labelledby={`mainContentHeader${content.subtitle ? " mainContentSubheader" : ""}`}
        aria-describedby="mainContentInner"
        ref={input => {
          this.mainContentHeader = input;
        }}
        no-rdm={content.no_rdm ? "" : null}
      >
        {includeCornerImage ? this.renderCornerImage() : null}
        {isCenterPosition ? null : this.renderSecondarySection(content)}
        <div
          className={`section-main ${
            isEmbeddedMigration ? "embedded-migration" : ""
          }${isSystemPromptStyleSpotlight ? "system-prompt-spotlight" : ""}`}
          hide-secondary-section={
            content.hide_secondary_section
              ? String(content.hide_secondary_section)
              : null
          }
          role="document"
          style={
            content.screen_style &&
            MultiStageUtils.getValidStyle(content.screen_style, [
              "width",
              "padding",
              "height",
            ])
          }
        >
          {isCenterLargeFullscreen ? null : secondaryCTATop}
          {includeNoodles ? this.renderNoodles() : null}
          {content.more_button ? this.renderMoreButton() : null}
          {content.dismiss_button && !content.reverse_split
            ? this.renderDismissButton()
            : null}
          <div
            className={`main-content ${hideStepsIndicator ? "no-steps" : ""}`}
            style={{
              background:
                isCenterPosition && this.getEffectiveBackground(content)
                  ? this.getEffectiveBackground(content)
                  : null,
              width:
                content.width && content.position !== "split"
                  ? content.width
                  : null,
              paddingBlock: content.split_content_padding_block
                ? content.split_content_padding_block
                : null,
              paddingInline: content.split_content_padding_inline
                ? content.split_content_padding_inline
                : null,
            }}
          >
            {isCenterLargeFullscreen ? secondaryCTATop : null}
            {isCenterPosition && this.hasAnimatedContent(content)
              ? this.renderAnimationPlayPauseButton()
              : null}
            {content.logo && !content.fullscreen
              ? this.renderPicture(content.logo)
              : null}
            {isRtamo && !content.fullscreen
              ? this.renderRTAMOIcon(
                  addonType,
                  this.props.themeScreenshots,
                  this.props.addonIconURL
                )
              : null}
            <div
              className="main-content-inner"
              id="mainContentInner"
              style={combinedStyles}
            >
              {content.logo && content.fullscreen
                ? this.renderPicture(content.logo)
                : null}
              {isRtamo && content.fullscreen
                ? this.renderRTAMOIcon(
                    addonType,
                    this.props.themeScreenshots,
                    this.props.addonIconURL
                  )
                : null}
              {content.title || content.subtitle ? (
                <div
                  id="multi-stage-message-welcome-text"
                  className={`welcome-text ${content.title_style || ""}`}
                >
                  {content.title ? this.renderTitle(content) : null}

                  {content.subtitle ? (
                    <Localized text={content.subtitle}>
                      <h2
                        data-l10n-args={JSON.stringify({
                          "addon-name": this.props.addonName,
                          ...this.props.appAndSystemLocaleInfo?.displayNames,
                        })}
                        aria-flowto={
                          this.props.messageId?.includes("FEATURE_TOUR")
                            ? "steps"
                            : ""
                        }
                        id="mainContentSubheader"
                      />
                    </Localized>
                  ) : null}
                  {this.renderActionButtons("after_subtitle", content)}
                  {content.cta_paragraph ? (
                    <CTAParagraph
                      content={content.cta_paragraph}
                      handleAction={this.props.handleAction}
                    />
                  ) : null}
                </div>
              ) : null}
              {content.video_container ? (
                <OnboardingVideo
                  content={content.video_container}
                  handleAction={this.props.handleAction}
                />
              ) : null}
              {this.renderLanguageSwitcher()}
              {content?.tiles_container?.position !==
              "after_supporting_content" ? (
                <ContentTiles {...this.props} />
              ) : null}
              {content.above_button_content
                ? this.renderOrderedContent(content.above_button_content)
                : null}
              {this.renderActionButtons("after_supporting_content", content)}
              {content?.tiles_container?.position ===
              "after_supporting_content" ? (
                <ContentTiles {...this.props} />
              ) : null}
              {!hideStepsIndicator && aboveButtonStepsIndicator
                ? this.renderStepsIndicator()
                : null}
              {this.renderActionButtons("end", content)}
              {
                /* Fullscreen dot-style step indicator should sit inside the
              main inner content to share its padding, which will be
              configurable with Bug 1956042 */
                !hideStepsIndicator &&
                !aboveButtonStepsIndicator &&
                !content.progress_bar &&
                content.fullscreen
                  ? this.renderStepsIndicator()
                  : null
              }
            </div>
            {!hideStepsIndicator &&
            !aboveButtonStepsIndicator &&
            !(content.fullscreen && !content.progress_bar)
              ? this.renderStepsIndicator()
              : null}
          </div>
        </div>
        <Localized text={content.info_text}>
          <span className="info-text" />
        </Localized>
      </main>
    );
  }
}

const localizableThingPropTypes = PropTypes.oneOfType([
  PropTypes.string,
  PropTypes.exact({
    // A raw, untranslated string or a $l10n object.
    raw: PropTypes.oneOfType([PropTypes.string, PropTypes.object]),
    // Fluent string identifier from a .ftl file.
    string_id: PropTypes.string,
    // Arguments for Fluent strings that have variables.
    args: PropTypes.object,
    // A string to use as the element's aria-label attribute value.
    aria_label: PropTypes.string,
    // CSS overrides for configurable styles.
    ...Object.fromEntries(
      CONFIGURABLE_STYLES.map(key => [
        key,
        PropTypes.oneOfType([PropTypes.string, PropTypes.number]),
      ])
    ),
  }),
]);

const actionPropTypes = PropTypes.exact({
  // The special message action id.
  type: PropTypes.string,
  // The data to pass to the action if needed.
  data: PropTypes.object,
  // If true, dismisses the screen. Can be used in addition to or instead of
  // a special message action type. If set to 'actionResult', the callout
  // will only be dismissed after the special message action has resolved
  // successfully, will only take effect for certain special message ids, and
  // requires setting 'needsAwait' to true.
  dismiss: PropTypes.oneOfType([PropTypes.string, PropTypes.bool]),
  // If true, the action needs to be awaited. Must be true if 'dismiss' is
  // set to 'actionResult'.
  needsAwait: PropTypes.bool,
  // If true, navigates to the next step.
  navigate: PropTypes.oneOfType([PropTypes.string, PropTypes.bool]),
  // If true, go back to the previous step.
  goBack: PropTypes.oneOfType([PropTypes.string, PropTypes.bool]),
  // If true, the actions for selected checkbox/radio selections are
  // performed in series. Set to true if this action is for the primary
  // button and the 'multiselect' tile is used.
  collectSelect: PropTypes.bool,
  // If true, collects all text input on action.
  collectTextInput: PropTypes.bool,
  // If true, collects content toggle state on action.
  collectContentToggleState: PropTypes.bool,
  // Indicates that the action should navigate to a different screen.
  advance_screens: PropTypes.shape({
    // The behavior of the screen navigation. Behaves like 'dismiss'.
    behavior: PropTypes.oneOfType([PropTypes.string, PropTypes.bool]),
    // How many screens and in which direction to advance. Positive integers
    // advance forward while negative integers advance backward.
    direction: PropTypes.number,
    // The id of the screen to advance to. Id takes priority if both id and
    // direction are provided.
    id: PropTypes.string,
  }),
});

const buttonPropTypes = PropTypes.exact({
  // The text to show inside the button.
  label: localizableThingPropTypes,
  // The JEXL targeting for conditional button display.
  targeting: PropTypes.string,
  // The special message action invoked if the button is clicked.
  action: actionPropTypes,
  // If true, shows an arrow icon next to the label.
  has_arrow_icon: PropTypes.bool,
  // If true, disables the button. If set to 'hasActiveMultiSelect', disables
  // the button until the user selects something in the multiselect tiles. If
  // set to 'hasTextInput', disables the button while the textarea tile is
  // empty or exceeds the character limit. If set to 'hasActiveSingleSelect',
  // disables the button until the user selects an option in the single-select
  // tile.
  disabled: PropTypes.oneOfType([PropTypes.string, PropTypes.bool]),
  // Overrides the style of the primary or secondary button.
  style: PropTypes.oneOf(["primary", "secondary", "link"]),
  // The text for the RTAMO install button, provided in the primary_button.
  install_complete_label: localizableThingPropTypes,
  // If true, the primary_button will be focused when the screen is displayed.
  should_focus_button: PropTypes.bool,
  // If there are several buttons, this property of an additional_button can
  // control the orientation of all the buttons.
  flow: PropTypes.oneOf(["row", "column"]),
  // Extra text to show before a secondary button.
  text: localizableThingPropTypes,
  // If there are several buttons, this property of an additional_button can
  // control the justification and alignment of the buttons row/column.
  // Defaults to 'end'.
  alignment: PropTypes.oneOf(["end", "start", "space-between"]),
  // The size of the dismiss_button (20px, 24px, 32px). Defaults to 32px.
  size: PropTypes.oneOf(["x-small", "small", "large"]),
  // If true, adds a background to the dismiss_button.
  background: PropTypes.bool,
  // CSS override for the marginBlock property.
  marginBlock: PropTypes.string,
  // CSS override for the marginInline property.
  marginInline: PropTypes.string,
});

export const screenContentShape = {
  // The layout position of the screen.
  position: PropTypes.oneOf(["center", "split", "callout"]),
  // If true, the screens are displayed in fullscreen.
  fullscreen: PropTypes.bool,
  // If true, the progress bar will be shown. Defaults to true.
  progress_bar: PropTypes.oneOfType([PropTypes.bool, PropTypes.string]),
  // The default CSS background for the screen.
  background: PropTypes.string,
  // The static CSS background for the screen, provided if the default
  // background has any animated content.
  background_static: PropTypes.string,
  // The hero image in the content.
  hero_image: PropTypes.shape({
    // The default URL of the hero image.
    static_url: PropTypes.string,
    // The URL of the static hero image, provided if the default hero image
    // has animated content.
    url: PropTypes.string,
  }),
  // A single checkbox action.
  checkbox: PropTypes.shape({
    // The text to show next to the checkbox.
    label: localizableThingPropTypes,
    // The default value of the checkbox.
    defaultValue: PropTypes.bool,
    // The special message action invoked if the checkbox is selected.
    action: actionPropTypes,
  }),
  // The primary button.
  primary_button: buttonPropTypes,
  // The secondary button.
  secondary_button: buttonPropTypes,
  // The secondary button(s) that are displayed at the top of the screen, above
  // the main content.
  secondary_button_top: PropTypes.oneOfType([
    PropTypes.object,
    PropTypes.array,
  ]),
  // The additional button.
  additional_button: buttonPropTypes,
  // The dismiss button.
  dismiss_button: buttonPropTypes,
  // A submenu button that can be attached to another button.
  submenu_button: PropTypes.shape({
    // Optionally used to control the aria label and can be used to override
    // CSS styles.
    label: localizableThingPropTypes,
    // Overrides the style of the button. Should be set to the same style as
    // the button it's attached to.
    style: PropTypes.oneOf(["primary", "secondary"]),
    // The dropdown menu that appears when the user clicks the split button.
    submenu: PropTypes.arrayOf(
      PropTypes.shape({
        // Submenu can have 3 types of items.
        type: PropTypes.oneOf(["action", "menu", "separator"]),
        // The id used to identify the submenu item in telemetry.
        id: PropTypes.string,
        // The text to show inside the submenu item.
        label: localizableThingPropTypes,
        // Used only for type 'action'. The special message action invoked if
        // the submenu item is clicked.
        action: actionPropTypes,
        // An optional URL specifying an icon to show next to the label.
        icon: PropTypes.string,
        // Used only for type 'menu'. The submenu items to show when the user
        // hovers over this item. Recursive.
        submenu: PropTypes.arrayOf(PropTypes.object),
      })
    ),
    // The button that the submenu split button is attached to.
    attached_to: PropTypes.oneOf(["secondary_button", "additional_button"]),
  }),
  // Hides the secondary section.
  hide_secondary_section: PropTypes.string,
  // The background position for narrow split layouts.
  split_narrow_bkg_position: PropTypes.string,
  // If true, the callout hides on outside clicks.
  autohide: PropTypes.bool,
  // The callout card width as a CSS value.
  width: PropTypes.string,
  // The callout card padding as a CSS value.
  padding: PropTypes.oneOfType([PropTypes.string, PropTypes.number]),
  // Used when a single row with a more inline layout is desired. Works well in
  // tandem with title_logo.
  layout: PropTypes.string,
  // If true, adds a colorful gradient border to the screen. This is only
  // supported for screens with 'hide_arrow' set to true. There is no effect
  // if HCM or a custom theme add-on is enabled.
  zap_border: PropTypes.bool,
  // If true, adds a colorful gradient shadow to the screen. This is only
  // supported for screens with 'hide_arrow' set to true and either
  // 'absolute_position' or 'arrow_position'. There is no effect if HCM or a
  // custom theme add-on is enabled.
  zap_shadow: PropTypes.bool,
  // An optional object representing a large illustration to show above other
  // content.
  logo: PropTypes.shape({
    // The image URL.
    imageURL: PropTypes.string,
    // The dark mode image URL.
    darkModeImageURL: PropTypes.string,
    // The reduced motion image URL.
    reducedMotionImageURL: PropTypes.string,
    // The dark mode reduced motion image URL.
    darkModeReducedMotionImageURL: PropTypes.string,
    // The <img> alt text.
    alt: PropTypes.oneOfType([PropTypes.string, PropTypes.object]),
    // The CSS style overriding the width property.
    width: PropTypes.string,
    // The CSS style overriding the height property.
    height: PropTypes.string,
  }),
  // The text for the headline.
  title: localizableThingPropTypes,
  // An optional object representing an icon to show next to the title.
  title_logo: PropTypes.shape({
    // The image URL.
    imageURL: PropTypes.string,
    // The dark mode image URL.
    darkModeImageURL: PropTypes.string,
    // The reduced motion image URL.
    reducedMotionImageURL: PropTypes.string,
    // The dark mode reduced motion image URL.
    darkModeReducedMotionImageURL: PropTypes.string,
    // The <img> alt text.
    alt: PropTypes.oneOfType([PropTypes.string, PropTypes.object]),
    // The CSS style overriding the width property.
    width: PropTypes.string,
    // The CSS style overriding the height property.
    height: PropTypes.string,
    // The logo alignment relative to the title.
    alignment: PropTypes.oneOf(["top", "center", "bottom"]),
  }),
  // Optional styling for the title.
  title_style: PropTypes.string,
  // The text to show below the title.
  subtitle: localizableThingPropTypes,
  // An extra block of configurable content below the title/subtitle but above
  // the main buttons. Can be placed above the 'tiles' by setting
  // 'tiles_container.position' to 'after_supporting_content'.
  above_button_content: PropTypes.arrayOf(
    PropTypes.shape({
      // The type of content.
      type: PropTypes.oneOf(["text", "image"]),
      // The paragraph text. Can be either a localizableThing that can be
      // combined with 'link_key's to attach actions to the
      // `<a data-l10n-name='...'> or an array of either raw strings or a
      // localizableThing with 'href'/'link_key'/neither.
      text: PropTypes.oneOfType([localizableThingPropTypes, PropTypes.array]),
      // A list of the link keys that exist in screen.content. The value of
      // that key must be an object with an 'action' property and the
      // 'string_id' in the 'text' object must refer to a Fluent string that
      // contains an anchor element with `data-l10n-name='LINK_KEY_NAME'`.
      link_keys: PropTypes.arrayOf(PropTypes.string),
      // Optional paragraph style.
      font_styles: PropTypes.string,
    })
  ),
  // Optionally used to override the aria attributes or tooltip of the steps
  // indicator. Not recommended.
  steps_indicator: PropTypes.shape({
    string_id: PropTypes.string,
  }),
  // Tile object(s) to display in the screen.
  tiles: PropTypes.oneOfType([
    PropTypes.array,
    PropTypes.shape({
      // The type of tile. More details can be found in ContentTiles.jsx.
      type: PropTypes.oneOf([
        "link",
        "textbox",
        "multiselect",
        "single-select",
        "theme",
        "backup_restore",
        "addons-picker",
        "migration-wizard",
        "mobile_downloads",
        "textarea",
        "theme-picker",
        "embedded_browser",
        "fx_backup_file_path",
        "fx_backup_password",
        "confirmation-checklist",
        "pinnable_sites",
        "content-toggle",
        "action_checklist",
      ]).isRequired,
      // CSS overrides of the tile container. Any CSS properties starting with
      // '--' are also allowed.
      style: PropTypes.object,
      // Array of tile configurations needed for the tile type.
      data: PropTypes.oneOfType([PropTypes.array, PropTypes.object]),
    }),
  ]),
  // The tiles container.
  tiles_container: PropTypes.shape({
    // The position of the tiles container relative to supporting content like
    // 'above_button_content'. By default, it comes before the supporting
    // content.
    position: PropTypes.string,
    // CSS overrides.
    style: PropTypes.object,
  }),
  // The header for a tiles container.
  tiles_header: PropTypes.exact({
    title: localizableThingPropTypes,
  }),
  // If true, applies the reverse split layout onto the screen (i.e. inverts
  // the visual order of the split screens).
  reverse_split: PropTypes.bool,
  // The <img> alt text.
  image_alt_text: localizableThingPropTypes,
  // The hero text.
  hero_text: PropTypes.oneOfType([
    localizableThingPropTypes,
    PropTypes.shape({
      title: localizableThingPropTypes,
      subtitle: localizableThingPropTypes,
    }),
  ]),
  // CSS overrides for the main content style on wide screens.
  main_content_style: PropTypes.object,
  // CSS overrides for the main content style on narrow screens.
  main_content_style_narrow: PropTypes.object,
  // A CSS override allowing custom justify content on split screens.
  split_content_justify_content: PropTypes.string,
  // The action button position.
  action_buttons_position: PropTypes.oneOf([
    "after_subtitle",
    "after_supporting_content",
    "end",
  ]),
  // If true, displays action buttons above main content.
  action_buttons_above_content: PropTypes.oneOfType([
    PropTypes.bool,
    PropTypes.string,
  ]),
  // If true, adds noodle illustrations that peek out behind the screen.
  has_noodles: PropTypes.bool,
  // If true, sets narrow attribute to the outer element of the screen.
  narrow: PropTypes.bool,
  // The language switcher component that appears of there is a language
  // mismatch screen.
  languageSwitcher: PropTypes.exact({
    downloading: localizableThingPropTypes,
    cancel: localizableThingPropTypes,
    waiting: localizableThingPropTypes,
    skip: localizableThingPropTypes,
    switch: localizableThingPropTypes,
    continue: localizableThingPropTypes,
    action: actionPropTypes,
  }),
  // Displays the OnboardingVideo component.
  video_container: PropTypes.object,
  // Overrides all text color of the screen.
  text_color: PropTypes.oneOf(["light", "dark"]),
  // If true, overrides the spotlight screen styling to look like the system
  // prompt style.
  isSystemPromptStyleSpotlight: PropTypes.bool,
  // CSS overrides for the screen style.
  screen_style: PropTypes.exact({
    overflow: PropTypes.string,
    display: PropTypes.string,
    height: PropTypes.string,
    width: PropTypes.string,
    padding: PropTypes.string,
  }),
  // If true, prevents the spotlight from entering responsive design mode at
  // widths less than 800px.
  no_rdm: PropTypes.bool,
  // The CSS override for the paddingBlock styling for split layout contents.
  split_content_padding_block: PropTypes.string,
  // The CSS override for the paddingInline styling for split layout contents.
  split_content_padding_inline: PropTypes.string,
  // Text below the subtitle that can include hyperlinks.
  cta_paragraph: PropTypes.shape({
    text: PropTypes.shape({
      // The Fluent string id for the paragraph text.
      string_id: PropTypes.string,
      // The name of the '<a data-l10n-name'=''></a>' marker inside the
      // string_id.
      string_name: PropTypes.string,
    }),
    // The special message action to be invoked when the cta text is clicked.
    action: actionPropTypes,
  }),
  // Info text displayed at the bottom of the screen.
  info_text: localizableThingPropTypes,
  // If true, displays the encrypted backup method as part of the
  // 'fx_backup_file_path' or 'fx_backup_password' tile type.
  isEncryptedBackup: PropTypes.bool,
  // The action for the migration start event as part of the embedded migration
  // wizard component within about:welcome.
  migrate_start: PropTypes.shape({ action: actionPropTypes }),
  // The action for the migration close event as part of the embedded migration
  // wizard component within about:welcome.
  migrate_close: PropTypes.shape({ action: actionPropTypes }),
  // The subtitle text for the action checklist.
  action_checklist_subtitle: localizableThingPropTypes,
  // The remove checklist button in the action checklist.
  remove_checklist_button: PropTypes.shape({
    // The text inside the remove checklist button.
    label: localizableThingPropTypes,
    // The id used to identify the button in telemetry.
    source_id: PropTypes.string,
    // The special message action to invoke when the button is clicked.
    action: actionPropTypes,
  }),
  // A submenu button with a button type of 'more'.
  more_button: PropTypes.shape({
    // Submenu can have 3 types of items.
    type: PropTypes.oneOf(["action", "menu", "separator"]),
    // The id used to identify the submenu item in telemetry.
    id: PropTypes.string,
    // The text to show inside the submenu item.
    label: localizableThingPropTypes,
    // Used only for type 'action'. The special message action invoked if
    // the submenu item is clicked.
    action: actionPropTypes,
    // An optional URL specifying an icon to show next to the label.
    icon: PropTypes.string,
    // Used only for type 'menu'. The submenu items to show when the user
    // hovers over this item. Recursive.
    submenu: PropTypes.arrayOf(PropTypes.object),
  }),
  // An optional array of event listeners to add to the page where the screen
  // is shown.
  page_event_listeners: PropTypes.arrayOf(
    PropTypes.shape({
      params: PropTypes.shape({
        // The event type string. Supports any DOM event type, timeout and
        // interval for timers, internal feature callout events 'touradvance'
        // and 'tourend'.
        type: PropTypes.string,
        // The target selector.
        selector: PropTypes.string,
        options: PropTypes.shape({
          // If true, handles events in capturing phase.
          capture: PropTypes.bool,
          // If true, removes listener after first event.
          once: PropTypes.bool,
          // If true, prevents default action in event handler.
          preventDefault: PropTypes.bool,
          // Used only for timeout and interval event types to invoke the action
          // on a timer.
          interval: PropTypes.number,
          // If true, extends addEventListener to all windows.
          every_window: PropTypes.bool,
        }),
      }),
      action: actionPropTypes,
    })
  ),

  // Add any `link_key` properties that are used in messages in
  //    OnboardingMessageProvider.sys.mjs
  //    FeatureCalloutMessages.sys.mjs
  //    PanelTestProvider.sys.mjs
  // to prevent the propTypes validation unit test from throwing an invalid
  // prop error.
  here: PropTypes.object,
  settings: PropTypes.object,
  ios: PropTypes.object,
  android: PropTypes.object,
  terms_of_use: PropTypes.object,
  privacy_notice: PropTypes.object,
  "learn-more": PropTypes.object,
  email_link: PropTypes.object,
};

const screenContentPropTypes = PropTypes.exact(screenContentShape);

// Update PropTypes here and in `screenContentShape` whenever any content
// properties are added/removed. See MultiStageProtonScreenSchemas.json for a
// more detailed, non-enforcing schema of the multistage screen content
// properties.
MultiStageProtonScreen.propTypes = {
  // The unique identifier of the screen content. Each screen in a
  // message should have a different ID, which can be referenced in
  // actions to update the tour pref and advance screens.
  id: PropTypes.string.isRequired,
  // The main content of this screen.
  content: screenContentPropTypes,
};
