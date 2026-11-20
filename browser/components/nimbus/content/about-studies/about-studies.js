/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";
/* global PropTypes React ReactDOM */

/**
 * Shorthand for creating elements (to avoid using a JSX preprocessor)
 */
const r = React.createElement;

/**
 * Dispatches a page event to the privileged frame script for this tab.
 *
 * @param {string} action
 * @param {object} data
 */
function sendPageEvent(action, data) {
  const event = new CustomEvent("ShieldPageEvent", {
    bubbles: true,
    detail: { action, data },
  });
  document.dispatchEvent(event);
}

function readOptinParams() {
  let { searchParams } = new URL(location);
  return {
    slug: searchParams.get("optin_slug"),
    branch: searchParams.get("optin_branch"),
    collection: searchParams.get("optin_collection"),
    applyTargeting: !!searchParams.get("apply_targeting"),
  };
}

/**
 * Handle basic layout and routing within about:studies.
 */
class AboutStudies extends React.Component {
  constructor(props) {
    super(props);

    this.remoteValueNameMap = {
      MessagingSystemList: "experiments",
      ShieldLearnMoreHref: "learnMoreHref",
      StudiesEnabled: "studiesEnabled",
      ShieldTranslations: "translations",
    };

    this.state = {};
    for (const stateName of Object.values(this.remoteValueNameMap)) {
      this.state[stateName] = null;
    }
    this.state.optInMessage = false;
  }

  initializeData() {
    for (const remoteName of Object.keys(this.remoteValueNameMap)) {
      document.addEventListener(`ReceiveRemoteValue:${remoteName}`, this);
      sendPageEvent(`GetRemoteValue:${remoteName}`);
    }
  }

  componentWillMount() {
    let optinParams = readOptinParams();
    if (optinParams.branch && optinParams.slug) {
      const onOptIn = ({ detail: value }) => {
        this.setState({ optInMessage: value });
        this.initializeData();
        document.removeEventListener(
          `ReceiveRemoteValue:OptInMessage`,
          onOptIn
        );
      };
      document.addEventListener(`ReceiveRemoteValue:OptInMessage`, onOptIn);
      sendPageEvent(`ExperimentOptIn`, optinParams);
    } else {
      this.initializeData();
    }
  }

  componentWillUnmount() {
    for (const remoteName of Object.keys(this.remoteValueNameMap)) {
      document.removeEventListener(`ReceiveRemoteValue:${remoteName}`, this);
    }
  }

  /** Event handle to receive remote values from documentAddEventListener */
  handleEvent({ type, detail: value }) {
    const prefix = "ReceiveRemoteValue:";
    if (type.startsWith(prefix)) {
      const name = type.substring(prefix.length);
      this.setState({ [this.remoteValueNameMap[name]]: value });
    }
  }

  render() {
    const {
      translations,
      learnMoreHref,
      studiesEnabled,
      experiments,
      optInMessage,
    } = this.state;
    // Wait for all values to be loaded before rendering. Some of the values may
    // be falsey, so an explicit null check is needed.
    if (Object.values(this.state).some(v => v === null)) {
      return null;
    }

    document.title = translations.pageTitle;

    return r(
      "div",
      { className: "about-studies-container main-content" },
      r(WhatsThisBox, { translations, learnMoreHref, studiesEnabled }),
      optInMessage && r(OptInBox, optInMessage),
      r(StudyList, {
        translations,
        experiments,
      })
    );
  }
}

/**
 * Explains the contents of the page, and offers a way to learn more and update preferences.
 */
class WhatsThisBox extends React.Component {
  handleUpdateClick() {
    sendPageEvent("NavigateToDataPreferences");
  }

  render() {
    const { learnMoreHref, studiesEnabled, translations } = this.props;

    return r(
      "div",
      { className: "info-box" },
      r(
        "div",
        { className: "info-box-content" },
        r(
          "span",
          {},
          studiesEnabled ? translations.enabledList : translations.disabledList
        ),
        r(
          "a",
          { id: "shield-studies-learn-more", href: learnMoreHref },
          translations.learnMore
        ),

        r(
          "button",
          {
            id: "shield-studies-update-preferences",
            onClick: this.handleUpdateClick,
          },
          r(
            "div",
            { className: "button-box" },
            navigator.platform.includes("Win")
              ? translations.updateButtonWin
              : translations.updateButtonUnix
          )
        )
      )
    );
  }
}
/**OptInMessage
 * Explains the contents of the page, and offers a way to learn more and update preferences.
 */
function OptInBox({ error, message }) {
  return r(
    "div",
    { className: "opt-in-box" + (error ? " opt-in-error" : "") },
    message
  );
}

/**
 * Shows a list of studies, with an option to end in-progress ones.
 */
class StudyList extends React.Component {
  render() {
    const { translations, experiments } = this.props;

    if (!experiments.length) {
      return r("p", { className: "study-list-info" }, translations.noStudies);
    }

    const activeStudies = [];
    const inactiveStudies = [];

    // Since we are modifying the study objects, it is polite to make copies
    for (const study of experiments) {
      const clonedStudy = Object.assign({}, study, {
        sortDate: new Date(study.lastSeen),
      });
      if (!study.active) {
        inactiveStudies.push(clonedStudy);
      } else {
        activeStudies.push(clonedStudy);
      }
    }

    activeStudies.sort((a, b) => b.sortDate - a.sortDate);
    inactiveStudies.sort((a, b) => b.sortDate - a.sortDate);
    return r(
      "div",
      {},
      r("h2", {}, translations.activeStudiesList),
      r(
        "ul",
        { className: "study-list active-study-list" },
        activeStudies.map(study =>
          r(MessagingSystemListItem, {
            key: study.slug,
            study,
            translations,
          })
        )
      ),
      r("h2", {}, translations.completedStudiesList),
      r(
        "ul",
        { className: "study-list inactive-study-list" },
        inactiveStudies.map(study =>
          r(MessagingSystemListItem, {
            key: study.slug,
            study,
            translations,
          })
        )
      )
    );
  }
}
StudyList.propTypes = {
  translations: PropTypes.object.isRequired,
};

class MessagingSystemListItem extends React.Component {
  constructor(props) {
    super(props);
    this.handleClickRemove = this.handleClickRemove.bind(this);
  }

  handleClickRemove() {
    sendPageEvent("RemoveMessagingSystemExperiment", {
      slug: this.props.study.slug,
    });
  }

  render() {
    const { study, translations } = this.props;
    const userFacingName = study.userFacingName || study.slug;
    const userFacingDescription =
      study.userFacingDescription || "Nimbus experiment.";
    return r(
      "li",
      {
        className: `study nimbus${study.active ? "" : " disabled"}`,
        "data-study-slug": study.slug, // used to identify this row in tests
      },
      r("div", { className: "study-icon" }, userFacingName.slice(0, 1)),
      r(
        "div",
        { className: "study-details" },
        r(
          "div",
          { className: "study-header" },
          r("span", { className: "study-name" }, userFacingName),
          r("span", {}, "\u2022"), // &bullet;
          !study.isRollout && [
            r("span", { className: "study-branch-slug" }, study.branch.slug),
            r("span", {}, "\u2022"), // &bullet;
          ],
          r(
            "span",
            { className: "study-status" },
            study.active
              ? translations.activeStatus
              : translations.completeStatus
          )
        ),
        r("div", { className: "study-description" }, userFacingDescription)
      ),
      r(
        "div",
        { className: "study-actions" },
        study.active &&
          r(
            "button",
            { className: "remove-button", onClick: this.handleClickRemove },
            r("div", { className: "button-box" }, translations.removeButton)
          )
      )
    );
  }
}

ReactDOM.render(r(AboutStudies), document.getElementById("app"));
