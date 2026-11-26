/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

loader.lazyServiceGetter(
  this,
  "clipboardHelper",
  "@mozilla.org/widget/clipboardhelper;1",
  "nsIClipboardHelper"
);

loader.lazyRequireGetter(
  this,
  "getHelperResultErrorMessage",
  "resource://devtools/client/webconsole/utils/messages.js",
  true
);

loader.lazyRequireGetter(
  this,
  "actions",
  "resource://devtools/client/webconsole/actions/index.js",
  false
);

loader.lazyRequireGetter(
  this,
  "PriorityLevels",
  "resource://devtools/client/shared/components/NotificationBox.js",
  true
);

function storeAsGlobal(actor) {
  return async ({ commands, hud }) => {
    const evalString = `{ let i = 0;
      while (this.hasOwnProperty("temp" + i) && i < 1000) {
        i++;
      }
      this["temp" + i] = _self;
      "temp" + i;
    }`;

    const res = await commands.scriptCommand.execute(evalString, {
      selectedObjectActor: actor,
      // Prevent any type of breakpoint when evaluating this code
      disableBreaks: true,
      // Ensure always overriding "$0" and "_self" console command, even if the page implements a variable with the same name.
      preferConsoleCommandsOverLocalSymbols: true,
    });

    // Select the adhoc target in the console.
    if (hud.toolbox) {
      const objectFront = commands.client.getFrontByID(actor);
      if (objectFront) {
        const targetActorID = objectFront.targetFront?.actorID;
        if (targetActorID) {
          hud.toolbox.selectTarget(targetActorID);
        }
      }
    }

    hud.focusInput();
    hud.setInputValue(res.result);
  };
}

function copyMessageObject(actor, variableText) {
  return async ({ commands, dispatch }) => {
    if (actor) {
      // The Debugger.Object of the OA will be bound to |_self| during evaluation.
      // See server/actors/webconsole/eval-with-debugger.js `evalWithDebugger`.
      const res = await commands.scriptCommand.execute("copy(_self)", {
        selectedObjectActor: actor,
        // Prevent any type of breakpoint when evaluating this code
        disableBreaks: true,
        // ensure always overriding "$0" and "_self" console command, even if the page implements a variable with the same name.
        preferConsoleCommandsOverLocalSymbols: true,
      });
      if (res.helperResult.type === "copyValueToClipboard") {
        clipboardHelper.copyString(res.helperResult.value);
      } else if (res.helperResult.type === "error") {
        const nofificationName = "copy-value-to-clipboard-notification";
        dispatch(
          actions.appendNotification(
            getHelperResultErrorMessage(res.helperResult),
            nofificationName,
            null,
            PriorityLevels.PRIORITY_CRITICAL_HIGH,
            null,
            () => dispatch(actions.removeNotification(nofificationName))
          )
        );
      }
    } else {
      clipboardHelper.copyString(variableText);
    }
  };
}

module.exports = {
  storeAsGlobal,
  copyMessageObject,
};
