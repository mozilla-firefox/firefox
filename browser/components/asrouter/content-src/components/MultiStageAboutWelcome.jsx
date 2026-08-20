/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useState, useEffect, useRef } from "react";
import { Localized } from "./MSLocalized";
import { MultiStageUtils } from "../lib/multistage-utils.mjs";
import { MultiStageProtonScreen } from "./MultiStageProtonScreen";
import { useLanguageSwitcher } from "./LanguageSwitcher";
import { SubmenuButton } from "./SubmenuButton";
import { BASE_PARAMS, addUtmParams } from "../lib/addUtmParams.mjs";

// Amount of milliseconds for all transitions to complete (including delays).
const TRANSITION_OUT_TIME = 1000;
const LANGUAGE_MISMATCH_SCREEN_ID = "AW_LANGUAGE_MISMATCH";

export const MultiStageAboutWelcome = props => {
  const gateInitialPaint = props.gateInitialPaint ?? false;
  let { defaultScreens } = props;
  const didFilter = useRef(false);
  const [didMount, setDidMount] = useState(false);
  const [contentToggleChecked, setContentToggleChecked] = useState(true);
  const [screens, setScreens] = useState(defaultScreens);

  const [index, setScreenIndex] = useState(props.startScreen);
  const [previousOrder, setPreviousOrder] = useState(props.startScreen - 1);
  // Gate first paint until we've finished the initial filtering pass.
  const [ready, setReady] = useState(false);

  useEffect(() => {
    (async () => {
      // If we want to load index from history state, we don't want to send impression yet
      if (!didMount) {
        return;
      }
      // On about:welcome first load, screensVisited should be empty
      let screensVisited = didFilter.current ? screens.slice(0, index) : [];
      let upcomingScreens = defaultScreens
        .filter(s => !screensVisited.find(v => v.id === s.id))
        // Filter out Language Mismatch screen from upcoming
        // screens if screens set from useLanguageSwitcher hook
        // has filtered language screen
        .filter(
          upcomingScreen =>
            !(
              !screens.find(s => s.id === LANGUAGE_MISMATCH_SCREEN_ID) &&
              upcomingScreen.id === LANGUAGE_MISMATCH_SCREEN_ID
            )
        );

      let filteredScreens = screensVisited.concat(
        (await window.AWEvaluateScreenTargeting(upcomingScreens)) ??
          upcomingScreens
      );

      // Use existing screen for the filtered screen to carry over any modification
      // e.g. if AW_LANGUAGE_MISMATCH exists, use it from existing screens
      setScreens(
        filteredScreens.map(filtered =>
          filtered.id === LANGUAGE_MISMATCH_SCREEN_ID
            ? (screens.find(s => s.id === filtered.id) ?? filtered)
            : filtered
        )
      );
      // Mark the initial filter pass complete and allow the first paint.
      if (!didFilter.current) {
        didFilter.current = true;
        setReady(true);
      }

      // After completing screen filtering, trigger any unhandled campaign
      // action present in the attribution campaign data. This updates the
      // "trailhead.firstrun.didHandleCampaignAction" preference, marking the
      // actions as complete to prevent them from being handled on subsequent
      // visits to about:welcome. Do not await getting the action to avoid
      // blocking the thread.
      window
        .AWGetUnhandledCampaignAction?.()
        .then(action => {
          if (typeof action === "string") {
            MultiStageUtils.handleCampaignAction(action, props.message_id);
          }
        })
        .catch(error => {
          console.error("Failed to get unhandled campaign action:", error);
        });

      const screenInitials = filteredScreens
        .map(({ id }) => id?.split("_")[1]?.[0])
        .join("");
      // Send impression ping when respective screen first renders
      // eslint-disable-next-line no-shadow
      filteredScreens.forEach((screen, order) => {
        if (index === order) {
          const messageId = `${props.message_id}_${order}_${screen.id}_${screenInitials}`;

          MultiStageUtils.sendImpressionTelemetry(messageId, {
            screen_family: props.message_id,
            screen_index: order,
            screen_id: screen.id,
            screen_initials: screenInitials,
          });

          // Impression actions should be fired before recording the
          // impression, so the check that ensures that actions run only once
          // has an accurate count of how many impressions have actually occured
          if (screen.content?.impression_action) {
            MultiStageUtils.handleImpressionAction(
              screen.content.impression_action,
              messageId,
              screen.id
            );
          }

          window.AWAddScreenImpression?.(screen);
        }
      });

      // Remember that a new screen has loaded for browser navigation
      if (props.updateHistory && index > window.history.state) {
        window.history.pushState(index, "");
      }

      // Remember the previous screen index so we can animate the transition
      setPreviousOrder(index);
    })();
  }, [index, didMount]); // eslint-disable-line react-hooks/exhaustive-deps

  const [flowParams, setFlowParams] = useState(null);
  const { metricsFlowUri } = props;
  useEffect(() => {
    (async () => {
      if (metricsFlowUri) {
        setFlowParams(await MultiStageUtils.fetchFlowParams(metricsFlowUri));
      }
    })();
  }, [metricsFlowUri]);

  // Allow "in" style to render to actually transition towards regular state,
  // which also makes using browser back/forward navigation skip transitions.
  const [transition, setTransition] = useState(props.transitions ? "in" : "");
  useEffect(() => {
    if (transition === "in") {
      requestAnimationFrame(() =>
        requestAnimationFrame(() => setTransition(""))
      );
    }
  }, [transition]);

  // Transition to next screen, opening about:home on last screen button CTA
  const handleTransition = goBack => {
    // Only handle transitioning out from a screen once.
    if (transition === "out") {
      return;
    }

    // Start transitioning things "out" immediately when moving forwards.
    setTransition(props.transitions ? "out" : "");

    // Actually move forwards after all transitions finish.
    setTimeout(
      () => {
        if (goBack) {
          setTransition(props.transitions ? "in" : "");
          setScreenIndex(prevState => prevState - 1);
        } else if (index < screens.length - 1) {
          setTransition(props.transitions ? "in" : "");
          setScreenIndex(prevState => prevState + 1);
        } else {
          window.AWFinish();
        }
      },
      props.transitions ? TRANSITION_OUT_TIME : 0
    );
  };

  useEffect(() => {
    // When about:welcome loads (on refresh or pressing back button
    // from about:home), ensure history state usEffect runs before
    // useEffect hook that send impression telemetry
    setDidMount(true);

    if (props.updateHistory) {
      // Switch to the screen tracked in state (null for initial state)
      // or last screen index if a user navigates by pressing back
      // button from about:home
      const handler = ({ state }) => {
        if (transition === "out") {
          return;
        }
        setTransition(props.transitions ? "out" : "");
        setTimeout(
          () => {
            setTransition(props.transitions ? "in" : "");
            setScreenIndex(Math.min(state, screens.length - 1));
          },
          props.transitions ? TRANSITION_OUT_TIME : 0
        );
      };

      // Handle page load, e.g., going back to about:welcome from about:home
      const { state } = window.history;
      if (state) {
        setScreenIndex(Math.min(state, screens.length - 1));
        setPreviousOrder(Math.min(state, screens.length - 1));
      }

      // Watch for browser back/forward button navigation events
      window.addEventListener("popstate", handler);
      return () => window.removeEventListener("popstate", handler);
    }
    return false;
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  const [multiSelects, setMultiSelects] = useState({});

  // Save the active multi select state for each screen as an object keyed by
  // screen id. Each screen id has an array containing checkbox ids used in
  // handleAction to update MULTI_ACTION data. This allows us to remember the
  // state of each screen's multi select checkboxes when navigating back and
  // forth between screens, while also allowing a message to have more than one
  // multi select screen.
  const [activeMultiSelects, setActiveMultiSelects] = useState({});

  // Save the active single select state for each screen as an object keyed
  // by screen id. Similar to above, this allows us to remember the state of
  // each screen's single select picker when navigating back and forth between
  // screens, and allows us to have multiple single selects on a screen.
  const [activeSingleSelectSelections, setActiveSingleSelectSelections] =
    useState({});

  // Save textarea inputs for each screen as an object keyed by screen id. It's
  // structured like this: { screenId: { textareaId: { value, isValid } } }
  const [textInputs, setTextInputs] = useState({});

  // Track whether each screen has had at least one successful pin, keyed by
  // screen id. This is a boolean (there is no "unpin"), used to gate a
  // primary button with `disabled: "hasPinnedSite"`.
  const [pinnedSites, setPinnedSites] = useState({});

  // Whether animated backgrounds/illustrations are paused for this session.
  // Defaults to paused when the user has prefers-reduced-motion: reduce set,
  // so we never autoplay motion for those users. The toggle stays consistent
  // among screens once the user interacts with it.
  const [animationsPaused, setAnimationsPaused] = useState(() =>
    typeof window !== "undefined" && typeof window.matchMedia === "function"
      ? window.matchMedia("(prefers-reduced-motion: reduce)").matches
      : false
  );
  const toggleAnimationsPaused = () => setAnimationsPaused(prev => !prev);

  // Get the active theme so the rendering code can make it selected
  // by default.
  const [activeTheme, setActiveTheme] = useState(null);
  const [initialTheme, setInitialTheme] = useState(null);
  useEffect(() => {
    (async () => {
      let theme = await window.AWGetSelectedTheme();
      setInitialTheme(theme);
      setActiveTheme(theme);
    })();
  }, []);

  const { negotiatedLanguage, langPackInstallPhase, languageFilteredScreens } =
    useLanguageSwitcher(
      props.appAndSystemLocaleInfo,
      screens,
      index,
      setScreenIndex
    );

  useEffect(() => {
    setScreens(languageFilteredScreens);
  }, [languageFilteredScreens]);

  const [installedAddons, setInstalledAddons] = useState(null);
  useEffect(() => {
    (async () => {
      let addons = await window.AWGetInstalledAddons();
      setInstalledAddons(addons);
    })();
  }, [index]);

  // Do not render anything until the first filtering pass completes if gating
  // initial paint is enabled.
  if (gateInitialPaint && !ready) {
    return null;
  }

  return (
    <React.Fragment>
      <div
        className={`outer-wrapper onboardingContainer proton transition-${transition}`}
        style={props.backdrop ? { background: props.backdrop } : {}}
      >
        {screens.map((currentScreen, order) => {
          const isFirstScreen = currentScreen === screens[0];
          const isLastScreen = currentScreen === screens[screens.length - 1];
          const totalNumberOfScreens = screens.length;
          const isSingleScreen = totalNumberOfScreens === 1;

          const setActiveMultiSelect = (valueOrFn, multiSelectId) => {
            setActiveMultiSelects(prevState => {
              const currentScreenSelections = prevState[currentScreen.id] || {};

              return {
                ...prevState,
                [currentScreen.id]: {
                  ...currentScreenSelections,
                  [multiSelectId]:
                    typeof valueOrFn === "function"
                      ? valueOrFn(currentScreenSelections[multiSelectId])
                      : valueOrFn,
                },
              };
            });
          };

          const setScreenMultiSelects = (valueOrFn, multiSelectId) => {
            setMultiSelects(prevState => {
              const currentMultiSelects = prevState[currentScreen.id] || {};

              return {
                ...prevState,
                [currentScreen.id]: {
                  ...currentMultiSelects,
                  [multiSelectId]:
                    typeof valueOrFn === "function"
                      ? valueOrFn(currentMultiSelects[multiSelectId])
                      : valueOrFn,
                },
              };
            });
          };

          const setActiveSingleSelectSelection = (
            valueOrFn,
            singleSelectId
          ) => {
            setActiveSingleSelectSelections(prevState => {
              const currentScreenSelections = prevState[currentScreen.id] || {};

              return {
                ...prevState,
                [currentScreen.id]: {
                  ...currentScreenSelections,
                  [singleSelectId]:
                    typeof valueOrFn === "function"
                      ? valueOrFn(prevState[currentScreen.id])
                      : valueOrFn,
                },
              };
            });
          };

          const setPinnedSite = () => {
            setPinnedSites(prevState => ({
              ...prevState,
              [currentScreen.id]: true,
            }));
          };

          const setTextInput = (value, inputId) => {
            setTextInputs(prevState => {
              const currentScreenInputs = prevState[currentScreen.id] || {};

              return {
                ...prevState,
                [currentScreen.id]: {
                  ...currentScreenInputs,
                  [inputId]: value,
                },
              };
            });
          };

          return index === order ? (
            <WelcomeScreen
              key={currentScreen.id + order}
              id={currentScreen.id}
              totalNumberOfScreens={totalNumberOfScreens}
              isFirstScreen={isFirstScreen}
              isLastScreen={isLastScreen}
              isSingleScreen={isSingleScreen}
              order={order}
              previousOrder={previousOrder}
              content={currentScreen.content}
              navigate={handleTransition}
              autoAdvance={currentScreen.auto_advance}
              advanceOnExperimentLoad={currentScreen.advance_on_experiment_load}
              messageId={`${props.message_id}_${order}_${currentScreen.id}`}
              UTMTerm={props.utm_term}
              flowParams={flowParams}
              activeTheme={activeTheme}
              initialTheme={initialTheme}
              setActiveTheme={setActiveTheme}
              setInitialTheme={setInitialTheme}
              screenMultiSelects={multiSelects[currentScreen.id]}
              setScreenMultiSelects={setScreenMultiSelects}
              activeMultiSelect={activeMultiSelects[currentScreen.id]}
              setActiveMultiSelect={setActiveMultiSelect}
              activeSingleSelectSelections={
                activeSingleSelectSelections[currentScreen.id]
              }
              setActiveSingleSelectSelection={setActiveSingleSelectSelection}
              textInputs={textInputs[currentScreen.id]}
              setTextInput={setTextInput}
              pinnedSites={pinnedSites[currentScreen.id]}
              setPinnedSite={setPinnedSite}
              contentToggleChecked={contentToggleChecked}
              setContentToggleChecked={setContentToggleChecked}
              negotiatedLanguage={negotiatedLanguage}
              langPackInstallPhase={langPackInstallPhase}
              forceHideStepsIndicator={currentScreen.force_hide_steps_indicator}
              ariaRole={props.ariaRole}
              requireAction={props.requireAction}
              aboveButtonStepsIndicator={
                currentScreen.above_button_steps_indicator
              }
              installedAddons={installedAddons}
              setInstalledAddons={setInstalledAddons}
              addonId={props.addonId}
              addonType={props.addonType}
              addonName={props.addonName}
              addonURL={props.addonURL}
              addonIconURL={props.addonIconURL}
              themeScreenshots={props.themeScreenshots}
              isRtamo={currentScreen.content.isRtamo}
              animationsPaused={animationsPaused}
              toggleAnimationsPaused={toggleAnimationsPaused}
            />
          ) : null;
        })}
      </div>
    </React.Fragment>
  );
};

const renderSingleSecondaryCTAButton = ({
  content,
  button,
  targetElement,
  position,
  handleAction,
  activeMultiSelect,
  textInputs,
  isArrayItem,
  index = null,
}) => {
  let buttonStyling = button?.has_arrow_icon
    ? `secondary arrow-icon`
    : `secondary`;
  const isPrimary = button?.style === "primary";
  const isTextLink =
    !["split", "callout", "center-large"].includes(content.position) &&
    content.tiles?.type !== "addons-picker" &&
    !isPrimary;
  const isSplitButton = content.submenu_button?.attached_to === targetElement;

  let className = "secondary-cta";
  if (position) {
    className += ` ${position}`;
  }
  if (isSplitButton) {
    className += " split-button-container";
  }

  const computeDisabled = disabledValue => {
    if (disabledValue === "hasActiveMultiSelect") {
      if (!activeMultiSelect) {
        return true;
      }

      for (const key in activeMultiSelect) {
        if (activeMultiSelect[key]?.length > 0) {
          return false;
        }
      }

      return true;
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
    return disabledValue;
  };

  if (isTextLink) {
    buttonStyling += " text-link";
  }

  if (isPrimary) {
    buttonStyling = button?.has_arrow_icon ? `primary arrow-icon` : `primary`;
  }

  // We have to provide handleAction with the expected action here,
  // since the data doesn't actually exist in JSON content
  const shimmedHandleAction = event => {
    if (isArrayItem && button?.action) {
      return handleAction(event, button.action);
    }
    return handleAction(event);
  };

  let buttonId = "secondary_button";
  buttonId += index !== null ? `_${index}` : "";

  return (
    <div className={className} key={targetElement}>
      <Localized text={button?.text}>
        <span />
      </Localized>
      <Localized text={button?.label}>
        <button
          id={buttonId}
          className={buttonStyling}
          value={targetElement}
          disabled={computeDisabled(button?.disabled)}
          onClick={shimmedHandleAction}
        />
      </Localized>
      {isSplitButton ? (
        <SubmenuButton content={content} handleAction={handleAction} />
      ) : null}
    </div>
  );
};

export const SecondaryCTA = props => {
  const { content, position } = props;

  const targetElement = position
    ? `secondary_button_${position}`
    : "secondary_button";
  const buttonData = content[targetElement];

  if (!buttonData) {
    return null;
  }

  const buttons = React.useMemo(
    () => (Array.isArray(buttonData) ? buttonData : [buttonData]),
    [buttonData]
  );

  const [visibleButtons, setVisibleButtons] = React.useState([]);

  React.useEffect(() => {
    (async () => {
      const filteredButtons = [];
      for (const button of buttons) {
        // No targeting, show by default for backwards compatibility
        if (!button?.targeting) {
          filteredButtons.push(button);
          continue;
        }

        try {
          const shouldShowButton = await window.AWEvaluateAttributeTargeting(
            button.targeting
          );
          if (shouldShowButton) {
            filteredButtons.push(button);
          }
        } catch (e) {
          console.error("SecondaryCTA targeting failed:", button.targeting, e);
        }
      }
      setVisibleButtons(filteredButtons);
    })();
  }, [buttons]);

  if (!visibleButtons.length) {
    return null;
  }

  if (Array.isArray(buttonData)) {
    return (
      <div className="secondary-buttons-top-container">
        {visibleButtons.map((button, index) =>
          renderSingleSecondaryCTAButton({
            content,
            button,
            targetElement: `${targetElement}_${index}`,
            position,
            handleAction: props.handleAction,
            activeMultiSelect: props.activeMultiSelect,
            textInputs: props.textInputs,
            isArrayItem: true,
            index,
          })
        )}
      </div>
    );
  }

  return renderSingleSecondaryCTAButton({
    content,
    button: visibleButtons[0],
    targetElement,
    position,
    handleAction: props.handleAction,
    activeMultiSelect: props.activeMultiSelect,
    textInputs: props.textInputs,
    isArrayItem: false,
  });
};

export const StepsIndicator = props => {
  let steps = [];
  for (let i = 0; i < props.totalNumberOfScreens; i++) {
    let className = `${i === props.order ? "current" : ""} ${
      i < props.order ? "complete" : ""
    }`;
    steps.push(
      <div key={i} className={`indicator ${className}`} role="presentation" />
    );
  }
  return steps;
};

export const ProgressBar = ({ step, previousStep, totalNumberOfScreens }) => {
  const [progress, setProgress] = React.useState(
    previousStep / totalNumberOfScreens
  );
  useEffect(() => {
    // We don't need to hook any dependencies because any time the step changes,
    // the screen's entire DOM tree will be re-rendered.
    setProgress(step / totalNumberOfScreens);
  }, []); // eslint-disable-line react-hooks/exhaustive-deps
  return (
    <div
      className="indicator"
      role="presentation"
      style={{ "--progress-bar-progress": `${progress * 100}%` }}
    />
  );
};

export class WelcomeScreen extends React.PureComponent {
  constructor(props) {
    super(props);
    this.handleAction = this.handleAction.bind(this);
  }

  handleOpenURL(action, flowParams, UTMTerm) {
    let { type, data } = action;
    if (type === "SHOW_FIREFOX_ACCOUNTS") {
      let params = {
        ...BASE_PARAMS,
        utm_term: `${UTMTerm}-screen`,
      };
      if (action.addFlowParams && flowParams) {
        params = {
          ...params,
          ...flowParams,
        };
      }
      data = {
        ...data,
        extraParams: { ...params, ...data?.extraParams },
      };
    } else if (type === "OPEN_URL") {
      let url = new URL(data.args);
      addUtmParams(url, `${UTMTerm}-screen`);
      if (action.addFlowParams && flowParams) {
        url.searchParams.append("device_id", flowParams.deviceId);
        url.searchParams.append("flow_id", flowParams.flowId);
        url.searchParams.append("flow_begin_time", flowParams.flowBeginTime);
      }
      data = { ...data, args: url.toString() };
    }
    return MultiStageUtils.handleUserAction({ type, data });
  }

  async handleMigrationIfNeeded(action, props) {
    const hasMigrate = a =>
      a.type === "SHOW_MIGRATION_WIZARD" ||
      (a.type === "MULTI_ACTION" && a.data?.actions?.some(hasMigrate));

    if (hasMigrate(action)) {
      await window.AWWaitForMigrationClose();
      MultiStageUtils.sendActionTelemetry(
        props.messageId,
        "migrate_close",
        "CLICK_BUTTON"
      );
    }
  }

  applyThemeIfNeeded(action, event) {
    if (!action.theme) {
      return;
    }

    const themeToUse =
      action.theme === "<event>"
        ? event.currentTarget.value
        : this.props.initialTheme || action.theme;

    this.props.setActiveTheme(themeToUse);
    window.AWSelectTheme(themeToUse);
  }

  handlePickerAction(value) {
    const tileGroups = Array.isArray(this.props.content.tiles)
      ? this.props.content.tiles
      : [this.props.content.tiles];

    for (const tile of tileGroups) {
      if (!tile?.data) {
        continue;
      }

      for (const opt of tile.data) {
        if (opt.id === value) {
          MultiStageUtils.handleUserAction(opt.action);
          return;
        }
      }
    }
  }

  resolveActionFromContent(value, event, props) {
    if (
      ["submenu_button", "more_button", "tile_button"].includes(value) &&
      event.action
    ) {
      return event.action;
    }

    const { content } = props;
    const targetContent =
      content[value] || content.tiles || content.languageSwitcher;

    if (!targetContent) {
      return null;
    }

    if (Array.isArray(targetContent)) {
      for (const tile of targetContent) {
        const matchedTile = tile.data.find(t => t.id === value);
        if (matchedTile?.action) {
          return matchedTile.action;
        }
      }
      return null;
    }

    return targetContent.action ?? null;
  }

  async handleAction(event, providedAction = null) {
    const { props } = this;
    const value =
      event.currentTarget.value ?? event.currentTarget.getAttribute("value");
    const source = event.source || value;

    let action =
      providedAction || this.resolveActionFromContent(value, event, props);

    let actionResult;

    if (!action) {
      console.error("Failed to resolve action");
      return actionResult;
    }

    action = JSON.parse(JSON.stringify(action));

    const context = {};
    if (action.collectContentToggleState) {
      context.contentToggleState = props.contentToggleChecked;
    }
    MultiStageUtils.sendActionTelemetry(
      props.messageId,
      source,
      event.name,
      context
    );
    if (
      (value === "dismiss_button" && !event.name) ||
      action.sendDismissTelemetry
    ) {
      MultiStageUtils.sendDismissTelemetry(props.messageId, source);
    }

    if (action.collectSelect) {
      this.setMultiSelectActions(action);
    }

    if (action.collectTextInput && Object.values(props.textInputs).length) {
      this.setTextInputActions(action);
    }
    if (["OPEN_URL", "SHOW_FIREFOX_ACCOUNTS"].includes(action.type)) {
      this.handleOpenURL(action, props.flowParams, props.UTMTerm);
    } else if (action.type === "INSTALL_ADDON_FROM_URL") {
      const url =
        props.addonURL && props.isRtamo ? props.addonURL : action.data?.url;
      // Set add-on url in action.data.url property from JSON
      action.data = { ...action.data, url };
      MultiStageUtils.handleUserAction(action);
    } else if (action.type) {
      let actionPromise = MultiStageUtils.handleUserAction(action);
      if (action.needsAwait) {
        actionResult = await actionPromise;
      }
      if (action.type === "FXA_SIGNIN_FLOW") {
        MultiStageUtils.sendActionTelemetry(
          props.messageId,
          actionResult ? "sign_in" : "sign_in_cancel",
          "FXA_SIGNIN_FLOW"
        );
      }
      // Wait until migration closes to complete the action
      await this.handleMigrationIfNeeded(action, props);
    }

    // A special tiles.action.theme value indicates we should use the event's value vs provided value.
    this.applyThemeIfNeeded(action, event);

    if (action.picker) {
      this.handlePickerAction(value);
    }

    // If the action has persistActiveTheme: true, we set the initial theme to the currently active theme
    // so that it can be reverted to in the event that the user navigates away from the screen
    if (action.persistActiveTheme) {
      this.props.setInitialTheme(this.props.activeTheme);
    }

    // `navigate`, `goBack` and `dismiss` can be true/false/undefined, or they can be a
    // string "actionResult" in which case we should use the actionResult
    // (boolean resolved by handleUserAction)
    const shouldDoBehavior = behavior => {
      if (behavior !== "actionResult") {
        return behavior;
      }

      if (action.needsAwait) {
        return actionResult;
      }

      console.error(
        "actionResult is only supported for actions with needsAwait"
      );
      return false;
    };

    if (shouldDoBehavior(action.navigate)) {
      props.navigate(action.goBack);
    }

    // Used by FeatureCallout to advance screens by re-rendering the whole
    // wrapper, updating anchor, page_event_listeners, etc. `navigate` only
    // updates the inner content. Only implemented by FeatureCallout.
    if (action.advance_screens) {
      if (shouldDoBehavior(action.advance_screens.behavior ?? true)) {
        window.AWAdvanceScreens?.(action.advance_screens);
      }
    }

    if (shouldDoBehavior(action.dismiss)) {
      window.AWFinish();
    }

    return actionResult;
  }

  setMultiSelectActions(action) {
    let { props } = this;
    // Populate MULTI_ACTION data actions property with selected checkbox
    // actions from tiles data
    if (action.type !== "MULTI_ACTION") {
      console.error(
        "collectSelect is only supported for MULTI_ACTION type actions"
      );
      action.type = "MULTI_ACTION";
    }
    if (!Array.isArray(action.data?.actions)) {
      console.error(
        "collectSelect is only supported for MULTI_ACTION type actions with an array of actions"
      );
      action.data = { actions: [] };
    }

    // Prepend the multi-select actions to the CTA's actions array, but keep
    // the actions in the same order they appear in. This way the CTA action
    // can go last, after the multi-select actions are processed. For example,
    // 1. checkbox action 1
    // 2. checkbox action 2
    // 3. radio action
    // 4. CTA action (which perhaps depends on the radio action)
    // Note, this order is only guaranteed if action.data has the
    // `orderedExecution` flag set to true.
    let multiSelectActions = [];

    const processTile = (tile, tileIndex) => {
      if (tile?.type !== "multiselect" || !Array.isArray(tile.data)) {
        return;
      }

      const multiSelectId = `tile-${tileIndex}`;

      const activeSelections = props.activeMultiSelect?.[multiSelectId] || [];

      for (const checkbox of tile.data) {
        let checkboxAction;
        if (activeSelections.includes(checkbox.id)) {
          checkboxAction = checkbox.checkedAction ?? checkbox.action;
        } else {
          checkboxAction = checkbox.uncheckedAction;
        }

        if (checkboxAction) {
          multiSelectActions.push(checkboxAction);
        }
      }
    };

    // Process tiles (this may be a single tile object or an array consisting of
    // tile objects)
    if (props.content?.tiles) {
      if (Array.isArray(props.content.tiles)) {
        props.content.tiles.forEach(processTile);
      } else {
        // Handle case where tiles is a single tile object
        processTile(props.content.tiles, 0);
      }
    }

    // Prepend the collected multi-select actions to the CTA's actions array
    action.data.actions.unshift(...multiSelectActions);

    for (const value of Object.values(props.activeMultiSelect || {})) {
      // Send telemetry with selected checkbox ids
      MultiStageUtils.sendActionTelemetry(
        props.messageId,
        value.flat(),
        "SELECT_CHECKBOX"
      );
    }
  }

  setTextInputActions(action) {
    let { props } = this;

    if (action.type !== "MULTI_ACTION") {
      console.error(
        "collectTextInput is only supported for MULTI_ACTION type actions"
      );
      action.type = "MULTI_ACTION";
    }
    if (!Array.isArray(action.data?.actions)) {
      console.error(
        "collectTextInput is only supported for MULTI_ACTION type actions with an array of actions"
      );
      action.data = { actions: [] };
    }

    const collectedActions = [];

    // If there is no character_limit, we still need to limit the size of the
    // input to avoid sending huge payloads. We'll go with 8KB.
    const truncateToByteSize = (str, maxBytes) => {
      const encoder = new TextEncoder();
      const encoded = encoder.encode(str);
      if (encoded.length <= maxBytes) {
        return str;
      }
      let end = maxBytes;
      // Step back until we find a valid UTF-8 start byte
      while (end > 0 && (encoded[end] & 0b11000000) === 0b10000000) {
        end--; // this is a continuation byte
      }
      return new TextDecoder().decode(encoded.subarray(0, end));
    };

    const processTile = (tile, tileIndex) => {
      if (tile?.type !== "textarea" || !tile.data) {
        return;
      }

      const inputId = tile.data.id || `tile-${tileIndex}`;
      const inputData = props.textInputs[inputId];
      if (inputData?.isValid && inputData.value.trim().length) {
        if (tile.data.action) {
          collectedActions.push(tile.data.action);
        }
        MultiStageUtils.sendActionTelemetry(
          props.messageId,
          inputId,
          "TEXT_INPUT",
          { value: truncateToByteSize(inputData.value, 8192) }
        );
      }
    };

    if (props.content?.tiles) {
      if (Array.isArray(props.content.tiles)) {
        for (const [index, tile] of props.content.tiles.entries()) {
          processTile(tile, index);
        }
      } else {
        processTile(props.content.tiles, 0);
      }
    }

    action.data.actions.unshift(...collectedActions);
  }

  render() {
    return (
      <MultiStageProtonScreen
        content={this.props.content}
        id={this.props.id}
        order={this.props.order}
        previousOrder={this.props.previousOrder}
        activeTheme={this.props.activeTheme}
        installedAddons={this.props.installedAddons}
        screenMultiSelects={this.props.screenMultiSelects}
        setScreenMultiSelects={this.props.setScreenMultiSelects}
        activeMultiSelect={this.props.activeMultiSelect}
        setActiveMultiSelect={this.props.setActiveMultiSelect}
        activeSingleSelectSelections={this.props.activeSingleSelectSelections}
        setActiveSingleSelectSelection={
          this.props.setActiveSingleSelectSelection
        }
        textInputs={this.props.textInputs}
        setTextInput={this.props.setTextInput}
        pinnedSites={this.props.pinnedSites}
        setPinnedSite={this.props.setPinnedSite}
        contentToggleChecked={this.props.contentToggleChecked}
        setContentToggleChecked={this.props.setContentToggleChecked}
        totalNumberOfScreens={this.props.totalNumberOfScreens}
        appAndSystemLocaleInfo={this.props.appAndSystemLocaleInfo}
        negotiatedLanguage={this.props.negotiatedLanguage}
        langPackInstallPhase={this.props.langPackInstallPhase}
        handleAction={this.handleAction}
        messageId={this.props.messageId}
        isFirstScreen={this.props.isFirstScreen}
        isLastScreen={this.props.isLastScreen}
        isSingleScreen={this.props.isSingleScreen}
        startsWithCorner={this.props.startsWithCorner}
        autoAdvance={this.props.autoAdvance}
        advanceOnExperimentLoad={this.props.advanceOnExperimentLoad}
        navigate={this.props.navigate}
        forceHideStepsIndicator={this.props.forceHideStepsIndicator}
        ariaRole={this.props.ariaRole}
        requireAction={this.props.requireAction}
        aboveButtonStepsIndicator={this.props.aboveButtonStepsIndicator}
        addonId={this.props.addonId}
        addonType={this.props.addonType}
        addonName={this.props.addonName}
        addonURL={this.props.addonURL}
        addonIconURL={this.props.addonIconURL}
        themeScreenshots={this.props.themeScreenshots}
        isRtamo={this.props.content.isRtamo}
        animationsPaused={this.props.animationsPaused}
        toggleAnimationsPaused={this.props.toggleAnimationsPaused}
      />
    );
  }
}
