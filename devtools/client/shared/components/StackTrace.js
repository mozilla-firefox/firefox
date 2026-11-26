/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const l10n = require("resource://devtools/shared/webconsole/l10n.js");
const Frame = createFactory(
  require("resource://devtools/client/shared/components/Frame.js")
);

class AsyncFrameClass extends Component {
  static get propTypes() {
    return {
      asyncCause: PropTypes.string.isRequired,
    };
  }

  render() {
    const { asyncCause } = this.props;

    return dom.span(
      { className: "frame-link-async-cause" },
      l10n.getStr("webconsole-stacktrace-async-stack", { cause: asyncCause })
    );
  }
}

class StackTrace extends Component {
  static get propTypes() {
    return {
      stacktrace: PropTypes.array.isRequired,
      onViewSourceInDebugger: PropTypes.func.isRequired,
      // Service to enable the source map feature.
      sourceMapURLService: PropTypes.object,
    };
  }

  render() {
    const { stacktrace, onViewSourceInDebugger, sourceMapURLService } =
      this.props;

    if (!stacktrace || !stacktrace.length) {
      return null;
    }

    const frames = [];
    stacktrace.forEach((s, i) => {
      if (s.asyncCause) {
        frames.push(
          "\t",
          AsyncFrame({
            key: `${i}-asyncframe`,
            asyncCause: s.asyncCause,
          }),
          "\n"
        );
      }

      frames.push(
        "\t",
        Frame({
          key: `${i}-frame`,
          frame: {
            functionDisplayName: s.functionName,
            source: s.filename,
            line: s.lineNumber,
            column: s.columnNumber,
          },
          showFunctionName: true,
          showAnonymousFunctionName: true,
          showFullSourceUrl: true,
          onClick: onViewSourceInDebugger,
          sourceMapURLService,
        }),
        "\n"
      );
    });

    return dom.div({ className: "stack-trace" }, frames);
  }
}

const AsyncFrame = createFactory(AsyncFrameClass);

module.exports = StackTrace;
