/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

ChromeUtils.defineESModuleGetters(this, {
  WebChannel: "resource://gre/modules/WebChannel.sys.mjs",
});

const HTTP_PATH = "http://example.com";
const HTTP_ENDPOINT =
  getRootDirectory(gTestPath).replace("chrome://mochitests/content", "") +
  "file_web_channel.html";
const HTTP_MISMATCH_PATH = "http://example.org";
const HTTP_IFRAME_PATH = "http://mochi.test:8888";
const HTTP_REDIRECTED_IFRAME_PATH = "http://example.org";

requestLongerTimeout(2); // timeouts in debug builds.

// Keep this synced with /mobile/android/tests/browser/robocop/testWebChannel.js
// as much as possible.  (We only have that since we can't run browser chrome
// tests on Android.  Yet?)
var gTests = [
  {
    desc: "WebChannel generic message",
    run() {
      return new Promise(function (resolve) {
        let tab;
        let channel = new WebChannel("generic", Services.io.newURI(HTTP_PATH));
        channel.listen(function (id, message) {
          is(id, "generic");
          is(message.something.nested, "hello");
          channel.stopListening();
          gBrowser.removeTab(tab);
          resolve();
        });

        tab = BrowserTestUtils.addTab(
          gBrowser,
          HTTP_PATH + HTTP_ENDPOINT + "?generic"
        );
      });
    },
  },
  {
    desc: "WebChannel generic message in a private window.",
    async run() {
      let promiseTestDone = new Promise(function (resolve) {
        let channel = new WebChannel("generic", Services.io.newURI(HTTP_PATH));
        channel.listen(function (id, message) {
          is(id, "generic");
          is(message.something.nested, "hello");
          channel.stopListening();
          resolve();
        });
      });

      const url = HTTP_PATH + HTTP_ENDPOINT + "?generic";
      let privateWindow = await BrowserTestUtils.openNewBrowserWindow({
        private: true,
      });
      await BrowserTestUtils.openNewForegroundTab(privateWindow.gBrowser, url);
      await promiseTestDone;
      await BrowserTestUtils.closeWindow(privateWindow);
    },
  },
  {
    desc: "WebChannel two way communication",
    run() {
      return new Promise(function (resolve) {
        let tab;
        let channel = new WebChannel("twoway", Services.io.newURI(HTTP_PATH));

        channel.listen(function (id, message, sender) {
          is(id, "twoway", "bad id");
          ok(message.command, "command not ok");

          if (message.command === "one") {
            channel.send({ data: { nested: true } }, sender);
          }

          if (message.command === "two") {
            is(message.detail.data.nested, true);
            channel.stopListening();
            gBrowser.removeTab(tab);
            resolve();
          }
        });

        tab = BrowserTestUtils.addTab(
          gBrowser,
          HTTP_PATH + HTTP_ENDPOINT + "?twoway"
        );
      });
    },
  },
  {
    desc: "WebChannel two way communication in an iframe",
    async run() {
      let parentChannel = new WebChannel("echo", Services.io.newURI(HTTP_PATH));
      let iframeChannel = new WebChannel(
        "twoway",
        Services.io.newURI(HTTP_IFRAME_PATH)
      );
      let promiseTestDone = new Promise(function (resolve, reject) {
        parentChannel.listen(function () {
          reject(new Error("WebChannel message incorrectly sent to parent"));
        });

        iframeChannel.listen(function (id, message, sender) {
          is(id, "twoway", "bad id (2)");
          ok(message.command, "command not ok (2)");

          if (message.command === "one") {
            iframeChannel.send({ data: { nested: true } }, sender);
          }

          if (message.command === "two") {
            is(message.detail.data.nested, true);
            resolve();
          }
        });
      });
      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url: HTTP_PATH + HTTP_ENDPOINT + "?iframe",
        },
        async function () {
          await promiseTestDone;
          parentChannel.stopListening();
          iframeChannel.stopListening();
        }
      );
    },
  },
  {
    desc: "WebChannel response to a redirected iframe",
    async run() {
      /**
       * This test checks that WebChannel responses are only sent
       * to an iframe if the iframe has not redirected to another origin.
       * Test flow:
       * 1. create a page, embed an iframe on origin A.
       * 2. the iframe sends a message `redirecting`, then redirects to
       *    origin B.
       * 3. the iframe at origin B is set up to echo any messages back to the
       *    test parent.
       * 4. the test parent receives the `redirecting` message from origin A.
       *    the test parent creates a new channel with origin B.
       * 5. when origin B is ready, it sends a `loaded` message to the test
       *    parent, letting the test parent know origin B is ready to echo
       *    messages.
       * 5. the test parent tries to send a response to origin A. If the
       *    WebChannel does not perform a valid origin check, the response
       *    will be received by origin B. If the WebChannel does perform
       *    a valid origin check, the response will not be sent.
       * 6. the test parent sends a `done` message to origin B, which origin
       *    B echoes back. If the response to origin A is not echoed but
       *    the message to origin B is, then hooray, the test passes.
       */

      let preRedirectChannel = new WebChannel(
        "pre_redirect",
        Services.io.newURI(HTTP_IFRAME_PATH)
      );
      let postRedirectChannel = new WebChannel(
        "post_redirect",
        Services.io.newURI(HTTP_REDIRECTED_IFRAME_PATH)
      );

      let promiseTestDone = new Promise(function (resolve, reject) {
        preRedirectChannel.listen(function (id, message, preRedirectSender) {
          if (message.command === "redirecting") {
            postRedirectChannel.listen(
              function (aId, aMessage, aPostRedirectSender) {
                is(aId, "post_redirect");
                isnot(aMessage.command, "no_response_expected");

                if (aMessage.command === "loaded") {
                  // The message should not be received on the preRedirectChannel
                  // because the target window has redirected.
                  preRedirectChannel.send(
                    { command: "no_response_expected" },
                    preRedirectSender
                  );
                  postRedirectChannel.send(
                    { command: "done" },
                    aPostRedirectSender
                  );
                } else if (aMessage.command === "done") {
                  resolve();
                } else {
                  reject(new Error(`Unexpected command ${aMessage.command}`));
                }
              }
            );
          } else {
            reject(new Error(`Unexpected command ${message.command}`));
          }
        });
      });

      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url: HTTP_PATH + HTTP_ENDPOINT + "?iframe_pre_redirect",
        },
        async function () {
          await promiseTestDone;
          preRedirectChannel.stopListening();
          postRedirectChannel.stopListening();
        }
      );
    },
  },
  {
    desc: "WebChannel multichannel",
    run() {
      return new Promise(function (resolve) {
        let tab;
        let channel = new WebChannel(
          "multichannel",
          Services.io.newURI(HTTP_PATH)
        );

        channel.listen(function (id) {
          is(id, "multichannel");
          gBrowser.removeTab(tab);
          resolve();
        });

        tab = BrowserTestUtils.addTab(
          gBrowser,
          HTTP_PATH + HTTP_ENDPOINT + "?multichannel"
        );
      });
    },
  },
  {
    desc: "WebChannel unsolicited send, using system principal",
    async run() {
      let channel = new WebChannel("echo", Services.io.newURI(HTTP_PATH));

      // an unsolicted message is sent from Chrome->Content which is then
      // echoed back. If the echo is received here, then the content
      // received the message.
      let messagePromise = new Promise(function (resolve) {
        channel.listen(function (id, message) {
          is(id, "echo");
          is(message.command, "unsolicited");

          resolve();
        });
      });

      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url: HTTP_PATH + HTTP_ENDPOINT + "?unsolicited",
        },
        async function (targetBrowser) {
          channel.send(
            { command: "unsolicited" },
            {
              browsingContext: targetBrowser.browsingContext,
              principal: Services.scriptSecurityManager.getSystemPrincipal(),
            }
          );
          await messagePromise;
          channel.stopListening();
        }
      );
    },
  },
  {
    desc: "WebChannel unsolicited send, using target origin's principal",
    async run() {
      let targetURI = Services.io.newURI(HTTP_PATH);
      let channel = new WebChannel("echo", targetURI);

      // an unsolicted message is sent from Chrome->Content which is then
      // echoed back. If the echo is received here, then the content
      // received the message.
      let messagePromise = new Promise(function (resolve) {
        channel.listen(function (id, message) {
          is(id, "echo");
          is(message.command, "unsolicited");

          resolve();
        });
      });

      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url: HTTP_PATH + HTTP_ENDPOINT + "?unsolicited",
        },
        async function (targetBrowser) {
          channel.send(
            { command: "unsolicited" },
            {
              browsingContext: targetBrowser.browsingContext,
              principal: Services.scriptSecurityManager.createContentPrincipal(
                targetURI,
                {}
              ),
            }
          );

          await messagePromise;
          channel.stopListening();
        }
      );
    },
  },
  {
    desc: "WebChannel unsolicited send with principal mismatch",
    async run() {
      let targetURI = Services.io.newURI(HTTP_PATH);
      let channel = new WebChannel("echo", targetURI);

      // two unsolicited messages are sent from Chrome->Content. The first,
      // `unsolicited_no_response_expected` is sent to the wrong principal
      // and should not be echoed back. The second, `done`, is sent to the
      // correct principal and should be echoed back.
      let messagePromise = new Promise(function (resolve, reject) {
        channel.listen(function (id, message) {
          is(id, "echo");

          if (message.command === "done") {
            resolve();
          } else {
            reject(new Error(`Unexpected command ${message.command}`));
          }
        });
      });

      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url: HTTP_PATH + HTTP_ENDPOINT + "?unsolicited",
        },
        async function (targetBrowser) {
          let mismatchURI = Services.io.newURI(HTTP_MISMATCH_PATH);
          let mismatchPrincipal =
            Services.scriptSecurityManager.createContentPrincipal(
              mismatchURI,
              {}
            );

          // send a message to the wrong principal. It should not be delivered
          // to content, and should not be echoed back.
          channel.send(
            { command: "unsolicited_no_response_expected" },
            {
              browsingContext: targetBrowser.browsingContext,
              principal: mismatchPrincipal,
            }
          );

          let targetPrincipal =
            Services.scriptSecurityManager.createContentPrincipal(
              targetURI,
              {}
            );

          // send the `done` message to the correct principal. It
          // should be echoed back.
          channel.send(
            { command: "done" },
            {
              browsingContext: targetBrowser.browsingContext,
              principal: targetPrincipal,
            }
          );

          await messagePromise;
          channel.stopListening();
        }
      );
    },
  },
  {
    desc: "WebChannel non-window target",
    async run() {
      /**
       * This test ensures messages can be received from and responses
       * sent to non-window elements.
       *
       * First wait for the non-window element to send a "start" message.
       * Then send the non-window element a "done" message.
       * The non-window element will echo the "done" message back, if it
       * receives the message.
       * Listen for the response. If received, good to go!
       */
      let channel = new WebChannel(
        "not_a_window",
        Services.io.newURI(HTTP_PATH)
      );

      let testDonePromise = new Promise(function (resolve, reject) {
        channel.listen(function (id, message, sender) {
          if (message.command === "start") {
            channel.send({ command: "done" }, sender);
          } else if (message.command === "done") {
            resolve();
          } else {
            reject(new Error(`Unexpected command ${message.command}`));
          }
        });
      });

      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url: HTTP_PATH + HTTP_ENDPOINT + "?bubbles",
        },
        async function () {
          await testDonePromise;
          channel.stopListening();
        }
      );
    },
  },
  {
    desc: "WebChannel disallows non-string messages",
    async run() {
      /**
       * This test ensures that non-string messages can't be sent via WebChannels.
       * We create a page which should send us two messages immediately. The first
       * message has an object for its detail, and the second has a string. We
       * check that we only get the second message.
       */
      let channel = new WebChannel("objects", Services.io.newURI(HTTP_PATH));
      let testDonePromise = new Promise(resolve => {
        channel.listen((id, message) => {
          is(id, "objects");
          is(message.type, "string");
          resolve();
        });
      });
      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url: HTTP_PATH + HTTP_ENDPOINT + "?object",
        },
        async function () {
          await testDonePromise;
          channel.stopListening();
        }
      );
    },
  },
  {
    desc: "WebChannel errors handling the message are delivered back to content",
    async run() {
      const ERRNO_UNKNOWN_ERROR = 999; // WebChannel.sys.mjs doesn't export this.

      // The channel where we purposely fail responding to a command.
      let channel = new WebChannel("error", Services.io.newURI(HTTP_PATH));
      // The channel where we see the response when the content sees the error
      let echoChannel = new WebChannel("echo", Services.io.newURI(HTTP_PATH));

      let testDonePromise = new Promise(resolve => {
        // listen for the confirmation that content saw the error.
        echoChannel.listen((id, message) => {
          is(id, "echo");
          is(message.error, "oh no");
          is(message.errno, ERRNO_UNKNOWN_ERROR);
          resolve();
        });

        // listen for a message telling us to simulate an error.
        channel.listen((id, message) => {
          is(id, "error");
          is(message.command, "oops");
          throw new Error("oh no");
        });
      });
      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url: HTTP_PATH + HTTP_ENDPOINT + "?error_thrown",
        },
        async function () {
          await testDonePromise;
          channel.stopListening();
          echoChannel.stopListening();
        }
      );
    },
  },
  {
    desc: "WebChannel errors due to an invalid channel are delivered back to content",
    async run() {
      const ERRNO_NO_SUCH_CHANNEL = 2; // WebChannel.sys.mjs doesn't export this.
      // The channel where we see the response when the content sees the error
      let echoChannel = new WebChannel("echo", Services.io.newURI(HTTP_PATH));

      let testDonePromise = new Promise(resolve => {
        // listen for the confirmation that content saw the error.
        echoChannel.listen((id, message) => {
          is(id, "echo");
          is(message.error, "No Such Channel");
          is(message.errno, ERRNO_NO_SUCH_CHANNEL);
          resolve();
        });
      });
      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url: HTTP_PATH + HTTP_ENDPOINT + "?error_invalid_channel",
        },
        async function () {
          await testDonePromise;
          echoChannel.stopListening();
        }
      );
    },
  },
]; // gTests

function test() {
  waitForExplicitFinish();

  (async function () {
    await SpecialPowers.pushPrefEnv({
      set: [["dom.security.https_first_pbm", false]],
    });

    for (let testCase of gTests) {
      info("Running: " + testCase.desc);
      await testCase.run();
    }
  })().then(finish, ex => {
    ok(false, "Unexpected Exception: " + ex);
    finish();
  });
}
