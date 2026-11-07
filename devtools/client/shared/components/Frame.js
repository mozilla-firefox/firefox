/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Component,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  getUnicodeUrl,
  getUnicodeUrlPath,
  getUnicodeHostname,
} = require("resource://devtools/client/shared/unicode-url.js");
const {
  getSourceNames,
  parseURL,
  getSourceMappedFile,
} = require("resource://devtools/client/shared/source-utils.js");
const { LocalizationHelper } = require("resource://devtools/shared/l10n.js");
const {
  MESSAGE_SOURCE,
} = require("resource://devtools/client/webconsole/constants.js");

const l10n = new LocalizationHelper(
  "devtools/client/locales/components.properties"
);
const webl10n = new LocalizationHelper(
  "devtools/shared/locales/webconsole.properties"
);

function savedFrameToDebuggerLocation(frame) {
  const { source: url, line, column, sourceId } = frame;
  return {
    url,

    // Line is 1-based everywhere.
    line,

    // The column received from spidermonkey Frame objects are 1-based,
    // and RDP's console message as well as page errors are providing 1-based columns,
    // but most of DevTools frontend consider it to be 0-based, especially the debugger.
    //
    // Column set to 0 is unknown column location.
    column: column >= 1 ? column - 1 : null,

    // The sourceId will be a string if it's a source actor ID, otherwise
    // it is either a Spidermonkey-internal ID from a SavedFrame or missing,
    // and in either case we can't use the ID for anything useful.
    id: typeof sourceId === "string" ? sourceId : null,
  };
}

/**
 * Get the tooltip message.
 *
 * @param {string|undefined} messageSource
 * @param {string} url
 * @returns {string}
 */
function getTooltipMessage(messageSource, url) {
  if (messageSource && messageSource === MESSAGE_SOURCE.CSS) {
    return l10n.getFormatStr("frame.viewsourceinstyleeditor", url);
  }
  return l10n.getFormatStr("frame.viewsourceindebugger", url);
}

class Frame extends Component {
  static get propTypes() {
    return {
      // Optional className that will be put into the element.
      className: PropTypes.string,
      // SavedFrame, or an object containing all the required properties.
      frame: PropTypes.shape({
        functionDisplayName: PropTypes.string,
        // This could be a SavedFrame with a numeric sourceId, or it could
        // be a SavedFrame-like client-side object, in which case the
        // "sourceId" will be a source actor ID.
        sourceId: PropTypes.oneOfType([PropTypes.string, PropTypes.number]),
        source: PropTypes.string.isRequired,
        line: PropTypes.oneOfType([PropTypes.string, PropTypes.number]),
        column: PropTypes.oneOfType([PropTypes.string, PropTypes.number]),
      }).isRequired,
      // Clicking on the frame link -- probably should link to the debugger.
      onClick: PropTypes.func,
      // Option to display a function name before the source link.
      showFunctionName: PropTypes.bool,
      // Option to display a function name even if it's anonymous.
      showAnonymousFunctionName: PropTypes.bool,
      // Option to display a host name after the source link.
      showHost: PropTypes.bool,
      // Option to display a host name if the filename is empty or just '/'
      showEmptyPathAsHost: PropTypes.bool,
      // Option to display a full source instead of just the filename.
      showFullSourceUrl: PropTypes.bool,
      // Service to enable the source map feature for console.
      sourceMapURLService: PropTypes.object,
      // The source of the message
      messageSource: PropTypes.string,
    };
  }

  static get defaultProps() {
    return {
      showFunctionName: false,
      showAnonymousFunctionName: false,
      showHost: false,
      showEmptyPathAsHost: false,
      showFullSourceUrl: false,
    };
  }

  constructor(props) {
    super(props);
    this.state = {
      originalLocation: null,
    };
    this._locationChanged = this._locationChanged.bind(this);
  }

  componentDidMount() {
    if (this.props.sourceMapURLService) {
      const { line, column } = this.props.frame;

      // If the location has a line=0 and column=0 or 1, we assume this is a
      // default location which means there is no real line and column
      // information related to this location. Since the sourcemap service is
      // unable to resolve locations with line=0 anyway, bail out here.
      if (line === 0 && (column === 0 || column === 1)) {
        return;
      }

      const location = savedFrameToDebuggerLocation(this.props.frame);

      // Many things that make use of this component either:
      // a) Pass in no sourceId because they have no way to know.
      // b) Pass in no sourceId because the actor wasn't created when the
      //    server sent its response.
      //
      // and due to that, we need to use subscribeByLocation in order to
      // handle both cases with an without an ID.
      this.unsubscribeSourceMapURLService =
        this.props.sourceMapURLService.subscribeByLocation(
          location,
          this._locationChanged
        );
    }
  }

  componentWillUnmount() {
    if (this.unsubscribeSourceMapURLService) {
      this.unsubscribeSourceMapURLService();
    }
  }

  _locationChanged(originalLocation) {
    this.setState({ originalLocation });
  }

  /**
   * Get current location's source, line, and column.
   *
   * @returns {{sourceURL: string, line: number|null, column: number|null}}
   */
  #getCurrentLocationInfo = () => {
    const { frame } = this.props;
    const { originalLocation } = this.state;

    const generatedLocation = savedFrameToDebuggerLocation(frame);
    const currentLocation = originalLocation || generatedLocation;

    const column = Number.parseInt(currentLocation.column, 10);

    return {
      sourceURL: currentLocation.url || "",
      // line is 1-based
      line: Number(currentLocation.line) || null,
      // column is 0-based while we display 1-based numbers
      column: typeof column == "number" ? column + 1 : null,
    };
  };

  /**
   * Get unicode hostname of the source link.
   *
   * @returns {string}
   */
  #getCurrentLocationUnicodeHostName = () => {
    const { sourceURL } = this.#getCurrentLocationInfo();

    const { host } = getSourceNames(sourceURL);
    return host ? getUnicodeHostname(host) : "";
  };

  /**
   * Check if the current location is linkable.
   *
   * @returns {boolean}
   */
  #isCurrentLocationLinkable = () => {
    const { frame } = this.props;
    const { originalLocation } = this.state;

    const generatedLocation = savedFrameToDebuggerLocation(frame);

    // Reparse the URL to determine if we should link this; `getSourceNames`
    // has already cached this indirectly. We don't want to attempt to
    // link to "self-hosted" and "(unknown)".
    // Source mapped sources might not necessary linkable, but they
    // are still valid in the debugger.
    // If we have a source ID then we can show the source in the debugger.
    return !!(
      originalLocation ||
      generatedLocation.id ||
      !!parseURL(generatedLocation.url)
    );
  };

  /**
   * Get the props of the top element.
   */
  #getTopElementProps = () => {
    const { className } = this.props;

    const { sourceURL, line, column } = this.#getCurrentLocationInfo();
    const { long } = getSourceNames(sourceURL);
    const props = {
      "data-url": long,
      className: "frame-link" + (className ? ` ${className}` : ""),
    };

    // If we have a line number > 0.
    if (line) {
      // Add `data-line` attribute for testing
      props["data-line"] = line;

      // Intentionally exclude 0
      if (column) {
        // Add `data-column` attribute for testing
        props["data-column"] = column;
      }
    }
    return props;
  };

  /**
   * Get the props of the source element.
   */
  #getSourceElementsProps = () => {
    const { frame, onClick, messageSource } = this.props;

    const generatedLocation = savedFrameToDebuggerLocation(frame);
    const { sourceURL, line, column } = this.#getCurrentLocationInfo();
    const { long } = getSourceNames(sourceURL);
    let url = getUnicodeUrl(long);

    // Exclude all falsy values, including `0`, as line numbers start with 1.
    if (line) {
      url += `:${line}`;
      // Intentionally exclude 0
      if (column) {
        url += `:${column}`;
      }
    }

    const isLinkable = this.#isCurrentLocationLinkable();

    // Inner el is useful for achieving ellipsis on the left and correct LTR/RTL
    // ordering. See CSS styles for frame-link-source-[inner] and bug 1290056.
    const tooltipMessage = getTooltipMessage(messageSource, url);

    const sourceElConfig = {
      key: "source",
      className: "frame-link-source",
      title: isLinkable ? tooltipMessage : url,
    };

    if (isLinkable) {
      return {
        ...sourceElConfig,
        onClick: e => {
          // We always need to prevent the default behavior of <a> link
          e.preventDefault();
          if (onClick) {
            e.stopPropagation();

            onClick(generatedLocation);
          }
        },
        href: sourceURL,
        draggable: false,
      };
    }

    return sourceElConfig;
  };

  /**
   * Render the source elements.
   *
   * @returns {React.ReactNode}
   */
  #renderSourceElements = () => {
    const { line, column } = this.#getCurrentLocationInfo();

    const sourceElements = [this.#renderDisplaySource()];

    if (line) {
      let lineInfo = `:${line}`;

      // Intentionally exclude 0
      if (column) {
        lineInfo += `:${column}`;
      }

      sourceElements.push(
        dom.span(
          {
            key: "line",
            className: "frame-link-line",
          },
          lineInfo
        )
      );
    }

    if (this.#isCurrentLocationLinkable()) {
      return dom.a(this.#getSourceElementsProps(), sourceElements);
    }
    // If source is not a URL (self-hosted, eval, etc.), don't make
    // it an anchor link, as we can't link to it.
    return dom.span(this.#getSourceElementsProps(), sourceElements);
  };

  /**
   * Render the display source.
   *
   * @returns {React.ReactNode}
   */
  #renderDisplaySource = () => {
    const { showEmptyPathAsHost, showFullSourceUrl } = this.props;
    const { originalLocation } = this.state;

    const { sourceURL } = this.#getCurrentLocationInfo();
    const { short, long, host } = getSourceNames(sourceURL);
    const unicodeShort = getUnicodeUrlPath(short);
    const unicodeLong = getUnicodeUrl(long);
    let displaySource = showFullSourceUrl ? unicodeLong : unicodeShort;
    if (originalLocation) {
      displaySource = getSourceMappedFile(displaySource);

      // In case of pretty-printed HTML file, we would only get the formatted suffix; replace
      // it with the full URL instead
      if (showEmptyPathAsHost && displaySource == ":formatted") {
        displaySource = host + displaySource;
      }
    } else if (
      showEmptyPathAsHost &&
      (displaySource === "" || displaySource === "/")
    ) {
      displaySource = host;
    }

    return dom.span(
      {
        key: "filename",
        className: "frame-link-filename",
      },
      displaySource
    );
  };

  /**
   * Render the function display name.
   *
   * @returns {React.ReactNode}
   */
  #renderFunctionDisplayName = () => {
    const { frame, showFunctionName, showAnonymousFunctionName } = this.props;
    if (!showFunctionName) {
      return null;
    }
    const functionDisplayName = frame.functionDisplayName;
    if (functionDisplayName || showAnonymousFunctionName) {
      return [
        dom.span(
          {
            key: "function-display-name",
            className: "frame-link-function-display-name",
          },
          functionDisplayName || webl10n.getStr("stacktrace.anonymousFunction")
        ),
        " ",
      ];
    }
    return null;
  };

  render() {
    const { showHost } = this.props;

    const elements = [
      this.#renderFunctionDisplayName(),
      this.#renderSourceElements(),
    ];

    const unicodeHost = showHost
      ? this.#getCurrentLocationUnicodeHostName()
      : null;
    if (unicodeHost) {
      elements.push(" ");
      elements.push(
        dom.span(
          {
            key: "host",
            className: "frame-link-host",
          },
          unicodeHost
        )
      );
    }

    return dom.span(this.#getTopElementProps(), ...elements);
  }
}

module.exports = Frame;
