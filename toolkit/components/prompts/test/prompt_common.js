const { Cc, Ci, Cu: ChromeUtils } = SpecialPowers;

/**
 * Converts a property bag to object.
 *
 * @param {nsIPropertyBag} bag - The property bag to convert
 * @returns {object} - The object representation of the nsIPropertyBag
 */
function propBagToObject(bag) {
  if (!(bag instanceof Ci.nsIPropertyBag)) {
    throw new TypeError("Not a property bag");
  }
  let result = {};
  for (let { name, value } of bag.enumerator) {
    result[name] = value;
  }
  return result;
}

var modalType;
var isSelectDialog = false;
var isOSX = "nsILocalFileMac" in SpecialPowers.Ci;
var isE10S = SpecialPowers.Services.appinfo.processType == 2;

var gChromeScript = SpecialPowers.loadChromeScript(
  SimpleTest.getTestFileURL("chromeScript.js")
);
SimpleTest.registerCleanupFunction(() => gChromeScript.destroy());

async function runPromptCombinations(window, testFunc) {
  let util = new PromptTestUtil(window);
  let run = () => {
    info(
      `Running tests (modalType=${modalType}, usePromptService=${util.usePromptService}, useBrowsingContext=${util.useBrowsingContext}, useAsync=${util.useAsync})`
    );
    return testFunc(util);
  };

  // Prompt service with dom window parent only supports window prompts
  util.usePromptService = true;
  util.useBrowsingContext = false;
  util.modalType = Ci.nsIPrompt.MODAL_TYPE_WINDOW;
  modalType = util.modalType;
  util.useAsync = false;
  await run();

  let modalTypes = [
    Ci.nsIPrompt.MODAL_TYPE_WINDOW,
    Ci.nsIPrompt.MODAL_TYPE_TAB,
    Ci.nsIPrompt.MODAL_TYPE_CONTENT,
  ];

  for (let type of modalTypes) {
    util.modalType = type;
    modalType = type;

    // Prompt service with browsing context sync
    util.usePromptService = true;
    util.useBrowsingContext = true;
    util.useAsync = false;
    await run();

    // Prompt service with browsing context async
    util.usePromptService = true;
    util.useBrowsingContext = true;
    util.useAsync = true;
    await run();

    // nsIPrompt
    // modalType is set via nsIWritablePropertyBag (legacy)
    util.usePromptService = false;
    util.useBrowsingContext = false;
    util.useAsync = false;
    await run();
  }
}

class PromptTestUtil {
  constructor(window) {
    this.window = window;
    this.browsingContext =
      SpecialPowers.wrap(window).windowGlobalChild.browsingContext;
    this.promptService = SpecialPowers.Services.prompt;
    this.nsPrompt = Cc["@mozilla.org/prompter;1"]
      .getService(Ci.nsIPromptFactory)
      .getPrompt(window, Ci.nsIPrompt);

    this.usePromptService = null;
    this.useBrowsingContext = null;
    this.useAsync = null;
    this.modalType = null;
  }

  get _prompter() {
    if (this.usePromptService) {
      return this.promptService;
    }
    return this.nsPrompt;
  }

  async prompt(funcName, promptArgs) {
    if (
      this.useBrowsingContext == null ||
      this.usePromptService == null ||
      this.useAsync == null ||
      this.modalType == null
    ) {
      throw new Error("Not initialized");
    }
    let args = [];
    if (this.usePromptService) {
      if (this.useBrowsingContext) {
        if (this.useAsync) {
          funcName = `async${funcName[0].toUpperCase()}${funcName.substring(
            1
          )}`;
        } else {
          funcName += "BC";
        }
        args = [this.browsingContext, this.modalType];
      } else {
        args = [this.window];
      }
    } else {
      let bag = this.nsPrompt.QueryInterface(Ci.nsIWritablePropertyBag2);
      bag.setPropertyAsUint32("modalType", this.modalType);
    }
    // Append the prompt arguments
    args = args.concat(promptArgs);

    let interfaceName = this.usePromptService ? "Services.prompt" : "prompt";
    ok(
      this._prompter[funcName],
      `${interfaceName} should have method ${funcName}.`
    );

    info(`Calling ${interfaceName}.${funcName}(${args})`);
    let result = this._prompter[funcName](...args);
    is(
      this.useAsync,
      result != null &&
        result.constructor != null &&
        result.constructor.name === "Promise",
      "If method is async it should return a promise."
    );

    if (this.useAsync) {
      let propBag = await result;
      return propBag && propBagToObject(propBag);
    }
    return result;
  }
}

function onloadPromiseFor(id) {
  var iframe = document.getElementById(id);
  return new Promise(resolve => {
    iframe.addEventListener(
      "load",
      function () {
        resolve(true);
      },
      { once: true }
    );
  });
}

/**
 * Take an action on the next prompt that appears without checking the state in advance.
 * This is useful when the action doesn't depend on which prompt is shown and you
 * are expecting multiple prompts at once in an indeterminate order.
 * If you know the state of the prompt you expect you should use `handlePrompt` instead.
 *
 * @param {object} action defining how to handle the prompt
 * @returns {Promise} resolving with the prompt state.
 */
function handlePromptWithoutChecks(action) {
  return new Promise(resolve => {
    gChromeScript.addMessageListener("promptHandled", function handled(msg) {
      gChromeScript.removeMessageListener("promptHandled", handled);
      resolve(msg.promptState);
    });
    gChromeScript.sendAsyncMessage("handlePrompt", { action, modalType });
  });
}

async function handlePrompt(state, action) {
  let actualState = await handlePromptWithoutChecks(action);
  checkPromptState(actualState, state);
}

function checkPromptState(promptState, expectedState) {
  info(`checkPromptState: Expected: ${expectedState.msg}`);
  // XXX check title? OS X has title in content
  is(promptState.msg, expectedState.msg, "Checking expected message");

  if (isOSX || promptState.isSubDialogPrompt || promptState.showCallerOrigin) {
    ok(
      !promptState.titleHidden,
      "Checking title always visible on OS X or when opened with common dialog"
    );
  } else {
    is(
      promptState.titleHidden,
      expectedState.titleHidden,
      "Checking title visibility"
    );
  }
  is(
    promptState.textHidden,
    expectedState.textHidden,
    "Checking textbox visibility"
  );
  is(
    promptState.passHidden,
    expectedState.passHidden,
    "Checking passbox visibility"
  );
  is(
    promptState.checkHidden,
    expectedState.checkHidden,
    "Checking checkbox visibility"
  );
  is(promptState.checkMsg, expectedState.checkMsg, "Checking checkbox label");
  is(promptState.checked, expectedState.checked, "Checking checkbox checked");
  if (
    modalType === Ci.nsIPrompt.MODAL_TYPE_WINDOW ||
    modalType === Ci.nsIPrompt.MODAL_TYPE_TAB
  ) {
    is(
      promptState.iconClass,
      expectedState.iconClass,
      "Checking expected icon CSS class"
    );
  }
  is(promptState.textValue, expectedState.textValue, "Checking textbox value");
  is(promptState.passValue, expectedState.passValue, "Checking passbox value");

  if (expectedState.butt0Label) {
    is(
      promptState.butt0Label,
      expectedState.butt0Label,
      "Checking accept-button label"
    );
  }
  if (expectedState.butt1Label) {
    is(
      promptState.butt1Label,
      expectedState.butt1Label,
      "Checking cancel-button label"
    );
  }
  if (expectedState.butt2Label) {
    is(
      promptState.butt2Label,
      expectedState.butt2Label,
      "Checking extra1-button label"
    );
  }
  for (let i = 0; i < 3; i++) {
    let key = `butt${i}AccessKey`;
    if (key in expectedState) {
      is(
        promptState[key],
        expectedState[key],
        `Checking button${i} access key`
      );
    }
  }

  // For prompts with a time-delay button.
  if (expectedState.butt0Disabled) {
    is(promptState.butt0Disabled, true, "Checking accept-button is disabled");
    is(
      promptState.butt1Disabled,
      false,
      "Checking cancel-button isn't disabled"
    );
  }

  is(
    promptState.defButton0,
    expectedState.defButton == "button0",
    "checking button0 default"
  );
  is(
    promptState.defButton1,
    expectedState.defButton == "button1",
    "checking button1 default"
  );
  is(
    promptState.defButton2,
    expectedState.defButton == "button2",
    "checking button2 default"
  );

  if (
    isOSX &&
    expectedState.focused &&
    expectedState.focused.startsWith("button") &&
    !promptState.infoRowHidden
  ) {
    is(
      promptState.focused,
      "infoBody",
      "buttons don't focus on OS X, but infoBody does instead"
    );
  } else {
    is(promptState.focused, expectedState.focused, "Checking focused element");
  }

  if (expectedState.hasOwnProperty("chrome")) {
    is(
      promptState.chrome,
      expectedState.chrome,
      "Dialog should be opened as chrome"
    );
  }
  if (expectedState.hasOwnProperty("dialog")) {
    is(
      promptState.dialog,
      expectedState.dialog,
      "Dialog should be opened as a dialog"
    );
  }
  if (expectedState.hasOwnProperty("chromeDependent")) {
    is(
      promptState.chromeDependent,
      expectedState.chromeDependent,
      "Dialog should be opened as dependent"
    );
  }
  if (expectedState.hasOwnProperty("isWindowModal")) {
    is(
      promptState.isWindowModal,
      expectedState.isWindowModal,
      "Dialog should be modal"
    );
  }
}

function checkEchoedAuthInfo(expectedState, browsingContext) {
  return SpecialPowers.spawn(
    browsingContext,
    [expectedState.user, expectedState.pass],
    (expectedUser, expectedPass) => {
      let doc = this.content.document;

      // The server echos back the HTTP auth info it received.
      let username = doc.getElementById("user").textContent;
      let password = doc.getElementById("pass").textContent;
      let authok = doc.getElementById("ok").textContent;

      Assert.equal(authok, "PASS", "Checking for successful authentication");
      Assert.equal(username, expectedUser, "Checking for echoed username");
      Assert.equal(password, expectedPass, "Checking for echoed password");
    }
  );
}

/**
 * Create a Proxy to relay method calls on an nsIAuthPrompt[2] prompter to a chrome script which can
 * perform the calls in the parent. Out and inout params will be copied back from the parent to
 * content.
 *
 * @param chromeScript The reference to the chrome script that will listen to `proxyPrompter`
 *                     messages in the parent and call the `methodName` method.
 *                     The return value from the message handler should be an object with properties:
 * `rv` - containing the return value of the method call.
 * `args` - containing the array of arguments passed to the method since out or inout ones could have
 *          been modified.
 */
function PrompterProxy(chromeScript) {
  return new Proxy(
    {},
    {
      get(target, prop) {
        return (...args) => {
          // Array of indices of out/inout params to copy from the parent back to the caller.
          let outParams = [];

          switch (prop) {
            case "prompt": {
              outParams = [/* result */ 5];
              break;
            }
            case "promptAuth": {
              outParams = [];
              break;
            }
            case "promptPassword":
            case "asyncPromptPassword": {
              outParams = [/* pwd */ 4];
              break;
            }
            case "promptUsernameAndPassword":
            case "asyncPromptUsernameAndPassword": {
              outParams = [/* user */ 4, /* pwd */ 5];
              break;
            }
            default: {
              throw new Error("Unknown nsIAuthPrompt method");
            }
          }

          let result;
          chromeScript
            .sendQuery("proxyPrompter", {
              args,
              methodName: prop,
            })
            .then(val => {
              result = val;
            });
          SpecialPowers.Services.tm.spinEventLoopUntil(
            "Test(prompt_common.js:get)",
            () => result
          );

          for (let outParam of outParams) {
            // Copy the out or inout param value over the original
            args[outParam].value = result.args[outParam].value;
          }

          if (prop == "promptAuth") {
            args[2].username = result.args[2].username;
            args[2].password = result.args[2].password;
            args[2].domain = result.args[2].domain;
          }

          return result.rv;
        };
      },
    }
  );
}
