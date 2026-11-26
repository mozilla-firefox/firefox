/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Utils: WebConsoleUtils,
} = require("resource://devtools/client/webconsole/utils.js");
const {
  EVALUATE_EXPRESSION,
  SET_TERMINAL_INPUT,
  SET_TERMINAL_EAGER_RESULT,
  EDITOR_PRETTY_PRINT,
  HELP_URL,
} = require("resource://devtools/client/webconsole/constants.js");
const {
  getAllPrefs,
} = require("resource://devtools/client/webconsole/selectors/prefs.js");
const ResourceCommand = require("resource://devtools/shared/commands/resource/resource-command.js");
const l10n = require("resource://devtools/shared/webconsole/l10n.js");

loader.lazyServiceGetter(
  this,
  "clipboardHelper",
  "@mozilla.org/widget/clipboardhelper;1",
  "nsIClipboardHelper"
);
loader.lazyRequireGetter(
  this,
  "messagesActions",
  "resource://devtools/client/webconsole/actions/messages.js"
);
loader.lazyRequireGetter(
  this,
  "historyActions",
  "resource://devtools/client/webconsole/actions/history.js"
);
loader.lazyRequireGetter(
  this,
  "ConsoleCommand",
  "resource://devtools/client/webconsole/types.js",
  true
);
loader.lazyRequireGetter(
  this,
  "netmonitorBlockingActions",
  "resource://devtools/client/netmonitor/src/actions/request-blocking.js"
);

loader.lazyRequireGetter(
  this,
  ["saveScreenshot", "captureAndSaveScreenshot"],
  "resource://devtools/client/shared/screenshot.js",
  true
);
loader.lazyRequireGetter(
  this,
  "createSimpleTableMessage",
  "resource://devtools/client/webconsole/utils/messages.js",
  true
);
loader.lazyRequireGetter(
  this,
  "getSelectedTarget",
  "resource://devtools/shared/commands/target/selectors/targets.js",
  true
);

async function getMappedExpression(hud, expression) {
  let mapResult;
  try {
    mapResult = await hud.getMappedExpression(expression);
  } catch (e) {
    console.warn("Error when calling getMappedExpression", e);
  }

  let mapped = null;
  if (mapResult) {
    ({ expression, mapped } = mapResult);
  }
  return { expression, mapped };
}

function evaluateExpression(expression, from = "input") {
  return async ({ dispatch, webConsoleUI, hud, commands }) => {
    if (!expression) {
      expression = hud.getInputSelection() || hud.getInputValue();
    }
    if (!expression) {
      return null;
    }

    // We use the messages action as it's doing additional transformation on the message.
    const { messages } = dispatch(
      messagesActions.messagesAdd([
        new ConsoleCommand({
          messageText: expression,
          timeStamp: Date.now(),
        }),
      ])
    );
    const [consoleCommandMessage] = messages;

    dispatch({
      type: EVALUATE_EXPRESSION,
      expression,
      from,
    });

    WebConsoleUtils.usageCount++;

    let mapped;
    ({ expression, mapped } = await getMappedExpression(hud, expression));

    // Even if the evaluation fails,
    // we still need to pass the error response to onExpressionEvaluated.
    const onSettled = res => res;

    const response = await commands.scriptCommand
      .execute(expression, {
        frameActor: hud.getSelectedFrameActorID(),
        selectedNodeActor: webConsoleUI.getSelectedNodeActorID(),
        selectedTargetFront: getSelectedTarget(
          webConsoleUI.hud.commands.targetCommand.store.getState()
        ),
        mapped,
        // Allow breakpoints to be triggerred and the evaluated source to be shown in debugger UI
        disableBreaks: false,
      })
      .then(onSettled, onSettled);

    const serverConsoleCommandTimestamp = response.startTime;

    // In case of remote debugging, it might happen that the debuggee page does not have
    // the exact same clock time as the client. This could cause some ordering issues
    // where the result message is displayed *before* the expression that lead to it.
    if (
      serverConsoleCommandTimestamp &&
      consoleCommandMessage.timeStamp > serverConsoleCommandTimestamp
    ) {
      // If we're in such case, we remove the original command message, and add it again,
      // with the timestamp coming from the server.
      dispatch(messagesActions.messageRemove(consoleCommandMessage.id));
      dispatch(
        messagesActions.messagesAdd([
          new ConsoleCommand({
            messageText: expression,
            timeStamp: serverConsoleCommandTimestamp,
          }),
        ])
      );
    }

    return dispatch(onExpressionEvaluated(response));
  };
}

/**
 * The JavaScript evaluation response handler.
 *
 * @private
 * @param {object} response
 *        The message received from the server.
 */
function onExpressionEvaluated(response) {
  return async ({ dispatch, webConsoleUI }) => {
    if (response.error) {
      console.error(`Evaluation error`, response.error, ": ", response.message);
      return;
    }

    // If the evaluation was a top-level await expression that was rejected, there will
    // be an uncaught exception reported, so we don't need to do anything.
    if (response.topLevelAwaitRejected === true) {
      return;
    }

    if (!response.helperResult) {
      webConsoleUI.wrapper.dispatchMessageAdd(response);
      return;
    }

    await dispatch(handleHelperResult(response));
  };
}

function handleHelperResult(response) {
  // eslint-disable-next-line complexity
  return async ({ dispatch, hud, toolbox, webConsoleUI, getState }) => {
    const { result, helperResult } = response;
    const helperHasRawOutput = !!helperResult?.rawOutput;

    if (helperResult?.type) {
      switch (helperResult.type) {
        case "exception":
          dispatch(
            messagesActions.messagesAdd([
              {
                level: "error",
                arguments: [helperResult.message],
                chromeContext: true,
                resourceType: ResourceCommand.TYPES.CONSOLE_MESSAGE,
              },
            ])
          );
          break;
        case "clearOutput":
          dispatch(messagesActions.messagesClear());
          break;
        case "clearHistory":
          dispatch(historyActions.clearHistory());
          break;
        case "historyOutput": {
          const history = getState().history.entries || [];
          const columns = new Map([
            ["_index", "(index)"],
            ["expression", "Expressions"],
          ]);
          dispatch(
            messagesActions.messagesAdd([
              {
                ...createSimpleTableMessage(
                  columns,
                  history.map((expression, index) => {
                    return { _index: index, expression };
                  })
                ),
              },
            ])
          );
          break;
        }
        case "inspectObject": {
          const objectActor = helperResult.object;
          if (hud.toolbox && !helperResult.forceExpandInConsole) {
            hud.toolbox.inspectObjectActor(objectActor);
          } else {
            webConsoleUI.inspectObjectActor(objectActor);
          }
          break;
        }
        case "help":
          hud.openLink(HELP_URL);
          break;
        case "copyValueToClipboard":
          clipboardHelper.copyString(helperResult.value);
          dispatch(
            messagesActions.messagesAdd([
              {
                resourceType: ResourceCommand.TYPES.PLATFORM_MESSAGE,
                message: l10n.getStr(
                  "webconsole-commands-copy-value-to-clipboard"
                ),
              },
            ])
          );
          break;
        case "screenshotOutput": {
          const { args } = helperResult;
          const targetFront =
            getSelectedTarget(hud.commands.targetCommand.store.getState()) ||
            hud.currentTarget;

          const screenshotMessages = await captureAndSaveScreenshot(
            targetFront,
            webConsoleUI.getPanelWindow(),
            args
          );

          if (screenshotMessages?.length) {
            dispatch(
              messagesActions.messagesAdd(
                screenshotMessages.map(message => ({
                  level: message.level || "log",
                  arguments: [message.text],
                  chromeContext: true,
                  resourceType: ResourceCommand.TYPES.CONSOLE_MESSAGE,
                }))
              )
            );
          }
          break;
        }
        case "blockURL": {
          const blockURL = helperResult.args.url;
          // The console actor isn't able to block the request as the console actor runs in the content
          // process, while the request has to be blocked from the parent process.
          // Then, calling the Netmonitor action will only update the visual state of the Netmonitor,
          // but we also have to block the request via the NetworkParentActor.
          await hud.commands.networkCommand.blockRequestForUrl(blockURL);
          toolbox
            .getPanel("netmonitor")
            ?.panelWin.store.dispatch(
              netmonitorBlockingActions.addBlockedUrl(blockURL)
            );

          dispatch(
            messagesActions.messagesAdd([
              {
                resourceType: ResourceCommand.TYPES.PLATFORM_MESSAGE,
                message: l10n.getStr("webconsole-commands-blocked-url", {
                  url: blockURL,
                }),
              },
            ])
          );
          break;
        }
        case "unblockURL": {
          const unblockURL = helperResult.args.url;
          await hud.commands.networkCommand.unblockRequestForUrl(unblockURL);
          toolbox
            .getPanel("netmonitor")
            ?.panelWin.store.dispatch(
              netmonitorBlockingActions.removeBlockedUrl(unblockURL)
            );

          dispatch(
            messagesActions.messagesAdd([
              {
                resourceType: ResourceCommand.TYPES.PLATFORM_MESSAGE,
                message: l10n.getStr("webconsole-commands-unblocked-url", {
                  url: unblockURL,
                }),
              },
            ])
          );
          // early return as we already dispatched necessary messages.
          return;
        }

        // Sent when using ":command --help or :command --usage"
        // to help discover command arguments.
        //
        // The remote runtime will tell us about the usage as it may
        // be different from the client one.
        case "usage":
          dispatch(
            messagesActions.messagesAdd([
              {
                resourceType: ResourceCommand.TYPES.PLATFORM_MESSAGE,
                message: helperResult.message,
              },
            ])
          );
          break;

        case "traceOutput":
          // Nothing in particular to do.
          // The JSTRACER_STATE resource will report the start/stop of the profiler.
          break;
      }
    }

    const hasErrorMessage =
      response.exceptionMessage ||
      (helperResult && helperResult.type === "error");

    // Hide undefined results coming from helper functions.
    const hasUndefinedResult =
      result && typeof result == "object" && result.type == "undefined";

    if (hasErrorMessage || helperHasRawOutput || !hasUndefinedResult) {
      dispatch(messagesActions.messagesAdd([response]));
    }
  };
}

function focusInput() {
  return ({ hud }) => {
    return hud.focusInput();
  };
}

function setInputValue(value) {
  return ({ hud }) => {
    return hud.setInputValue(value);
  };
}

/**
 * Request an eager evaluation from the server.
 *
 * @param {string} expression: The expression to evaluate.
 * @param {boolean} force: When true, will request an eager evaluation again, even if
 *                         the expression is the same one than the one that was used in
 *                         the previous evaluation.
 */
function terminalInputChanged(expression, force = false) {
  return async ({ dispatch, webConsoleUI, hud, commands, getState }) => {
    const prefs = getAllPrefs(getState());
    if (!prefs.eagerEvaluation) {
      return null;
    }

    const { terminalInput = "" } = getState().history;

    // Only re-evaluate if the expression did change.
    if (
      (!terminalInput && !expression) ||
      (typeof terminalInput === "string" &&
        typeof expression === "string" &&
        expression.trim() === terminalInput.trim() &&
        !force)
    ) {
      return null;
    }

    dispatch({
      type: SET_TERMINAL_INPUT,
      expression: expression.trim(),
    });

    // There's no need to evaluate an empty string.
    if (!expression || !expression.trim()) {
      return dispatch({
        type: SET_TERMINAL_EAGER_RESULT,
        expression,
        result: null,
      });
    }

    let mapped;
    ({ expression, mapped } = await getMappedExpression(hud, expression));

    // We don't want to evaluate top-level await expressions (see Bug 1786805)
    if (mapped?.await) {
      return dispatch({
        type: SET_TERMINAL_EAGER_RESULT,
        expression,
        result: null,
      });
    }

    const response = await commands.scriptCommand.execute(expression, {
      frameActor: hud.getSelectedFrameActorID(),
      selectedNodeActor: webConsoleUI.getSelectedNodeActorID(),
      selectedTargetFront: getSelectedTarget(
        hud.commands.targetCommand.store.getState()
      ),
      mapped,
      eager: true,
    });

    // If the terminal input changed while the expression was evaluated, don't render
    // the results of the eager evaluation, it will be handled by the last call to
    // terminalInputChanged
    if (expression.trim() !== getState().history?.terminalInput) {
      return null;
    }

    return dispatch({
      type: SET_TERMINAL_EAGER_RESULT,
      result: getEagerEvaluationResult(response),
    });
  };
}

/**
 * Refresh the current eager evaluation by requesting a new eager evaluation.
 */
function updateInstantEvaluationResultForCurrentExpression() {
  return ({ getState, dispatch }) =>
    dispatch(terminalInputChanged(getState().history.terminalInput, true));
}

function getEagerEvaluationResult(response) {
  const result = response.exception || response.result;
  // Don't show syntax errors results to the user.
  if (result?.isSyntaxError || (result && result.type == "undefined")) {
    return null;
  }

  return result;
}

function prettyPrintEditor() {
  return {
    type: EDITOR_PRETTY_PRINT,
  };
}

module.exports = {
  evaluateExpression,
  focusInput,
  setInputValue,
  terminalInputChanged,
  updateInstantEvaluationResultForCurrentExpression,
  prettyPrintEditor,
};
