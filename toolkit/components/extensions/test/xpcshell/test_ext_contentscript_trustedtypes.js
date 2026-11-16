"use strict";

/* globals exportFunction, browserTestSendMessage, browserTestAssertEq, browserTestAssertTrue */

const server = createHttpServer({ hosts: ["example.com"] });
server.registerDirectory("/data/", do_get_file("data"));

async function test_contentscript_trusted_types(manifest) {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      content_scripts: [
        {
          matches: ["http://example.com/*/file_trusted_types.html"],
          run_at: "document_end",
          js: ["content_script_isolated.js"],
          world: "ISOLATED",
        },
        {
          matches: ["http://example.com/*/file_trusted_types.html"],
          run_at: "document_idle",
          js: ["content_script_main.js"],
          world: "MAIN",
        },
      ],
      ...manifest,
    },
    files: {
      async "content_script_isolated.js"() {
        // Make these functions usable in content_scripts_main.js
        exportFunction(browser.test.sendMessage, window, {
          defineAs: "browserTestSendMessage",
        });
        exportFunction(browser.test.assertEq, window, {
          defineAs: "browserTestAssertEq",
        });
        exportFunction(browser.test.assertTrue, window, {
          defineAs: "browserTestAssertTrue",
        });

        function onPostMessage(message) {
          return new Promise(resolve => {
            function listener(event) {
              if (event.data === message) {
                window.removeEventListener("message", listener);
                resolve(event.data);
              }
            }
            window.addEventListener("message", listener);
          });
        }
        function NavigateAndWaitForSecurityPolicyViolation(sample_prefix, url) {
          return new Promise((resolve, reject) => {
            function listener(event) {
              if (event.sample.startsWith(sample_prefix)) {
                window.removeEventListener("securitypolicyviolation", listener);
                resolve(event.effectiveDirective);
              }
            }
            window.addEventListener("securitypolicyviolation", listener);
            try {
              window.location = url;
            } catch (e) {
              reject(
                new Error(`window.location = ${url} threw '${e.toString()}'`)
              );
            }
            window.requestIdleCallback(_ => {
              window.removeEventListener("securitypolicyviolation", listener);
              reject(new Error("no violations observed after idle callback."));
            });
          });
        }
        const passthroughpolicy = window.trustedTypes.createPolicy(
          "passthroughpolicy",
          {
            createHTML: s => s,
            createScript: s => s,
            createScriptURL: s => s,
          }
        );

        let SINKS = {
          "script-text": {
            element: "script",
            property: "text",
            value: `window.postMessage("script-text", "*");`,
          },
          "script-innerText": {
            element: "script",
            property: "innerText",
            value: `window.postMessage("script-innerText", "*");`,
          },
          "script-src": {
            element: "script",
            property: "src",
            attribute: "src",
            value: browser.runtime.getURL("/test_script_src.js"),
          },
          "iframe-srcdoc": {
            element: "iframe",
            property: "srcdoc",
            attribute: "srcdoc",
            value: `<script>window.parent.postMessage("iframe-srcdoc", "*");</script>`,
          },
        };

        for (const [
          message,
          { element, property, attribute, value },
        ] of Object.entries(SINKS)) {
          let runTest = async fun => {
            let elem = document.createElement(element);
            document.body.append(elem);
            let messagePromise = onPostMessage(message);
            fun(elem);
            browser.test.assertEq(await messagePromise, message);
            elem.remove();
          };

          if (property) {
            await runTest(elem => (elem[property] = value));
          }

          if (attribute) {
            await runTest(elem => elem.setAttribute(attribute, value));

            await runTest(elem => {
              let attr = document.createAttribute(attribute);
              attr.value = value;
              elem.setAttributeNode(attr);
            });

            await runTest(elem => {
              let attr = document.createAttribute(attribute);
              attr.value = value;
              elem.attributes.setNamedItem(attr);
            });
          }
        }

        let div = document.createElement("div");
        document.body.append(div);

        // innerHTML sink
        div.innerHTML = "<b>Hello</b>";
        browser.test.assertEq(
          div.outerHTML,
          "<div><b>Hello</b></div>",
          "Trusted Types enforcement does not block setting Element.innerHTML to a string in web extension content script."
        );
        div.innerHTML = passthroughpolicy.createHTML("<i>Hello</i>");
        browser.test.assertEq(
          div.outerHTML,
          "<div><i>Hello</i></div>",
          "Setting Element.innerHTML to a TrustedHTML works in web extension content script."
        );

        // outerHTML sink
        div.firstChild.outerHTML = "<i>World</i>";
        browser.test.assertEq(
          div.innerHTML,
          "<i>World</i>",
          "Trusted Types enforcement does not block setting Element.outerHTML to a string in web extension content script."
        );
        div.firstChild.outerHTML = passthroughpolicy.createHTML("<b>World</b>");
        browser.test.assertEq(
          div.innerHTML,
          "<b>World</b>",
          "Setting Element.outerHTML to a TrustedHTML works in web extension content script."
        );

        // setHTMLUnsafe sink
        div.setHTMLUnsafe("<em>Foo</em>");
        browser.test.assertEq(
          div.innerHTML,
          "<em>Foo</em>",
          "Trusted Types enforcement does not block Element.setHTMLUnsafe() with a string parameter in web extension content script."
        );
        div.setHTMLUnsafe(passthroughpolicy.createHTML("<strong>Foo</strong>"));
        browser.test.assertEq(
          div.innerHTML,
          "<strong>Foo</strong>",
          "Element.setHTMLUnsafe() with a TrustedHTML parameter works in web extension content script."
        );

        // eval
        if (browser.runtime.getManifest().manifest_version == 2) {
          // MV2 content script does not have a default CSP, so eval() is allowed.
          browser.test.assertEq(
            3,
            // eslint-disable-next-line no-eval
            eval("1 + 2"),
            "Trusted Types enforcement does not block executing eval() with a string argument in MV2 content script."
          );
          browser.test.assertEq(
            7,
            // eslint-disable-next-line no-eval
            eval(passthroughpolicy.createScript("3 + 4")),
            "Trusted Types enforcement does not block executing eval() with a TrustedScript argument in MV2 content script."
          );
        } else {
          // MV3 blocks eval() in content scripts through CSP, without a way to override it.
          browser.test.assertThrows(
            // eslint-disable-next-line no-eval
            () => eval("1 + 2"),
            /call to eval\(\) blocked by CSP/,
            "eval() with a string argument blocked in MV3 content script, independently of Trusted Types enforcement."
          );
          browser.test.assertThrows(
            // eslint-disable-next-line no-eval
            () => eval(passthroughpolicy.createScript("3 + 4")),
            /call to eval\(\) blocked by CSP/,
            "eval() with a TrustedTypes argument blocked in MV3 content script, independently of Trusted Types enforcement."
          );
        }

        // window.eval()
        browser.test.assertTrue(
          // eslint-disable-next-line no-eval
          globalThis.eval != window.eval,
          "globalThis.eval() is not the same as window.eval() in web extension content script"
        );
        browser.test.assertThrows(
          _ => {
            // eslint-disable-next-line no-eval
            window.eval("9+3");
          },
          /call to eval\(\) blocked by CSP/,
          "Trusted Types enforcement blocks executing eval() with a string parameter."
        );
        browser.test.assertEq(
          13,
          // eslint-disable-next-line no-eval
          window.eval(passthroughpolicy.createScript("11+2")),
          "Trusted Types enforcement does not block executing eval() with a TrustedScript parameter."
        );

        // script enforcement
        let scriptEnforcementPromise = onPostMessage(
          "script-enforcement-isolated"
        );
        let scriptEnforcementScript = document.createElement("script");
        scriptEnforcementScript.appendChild(
          document.createTextNode(
            'window.postMessage("script-enforcement-isolated", "*");'
          )
        );
        document.body.appendChild(scriptEnforcementScript);
        browser.test.assertEq(
          "script-enforcement-isolated",
          await scriptEnforcementPromise,
          "Trusted Types enforcement does not block script modified via appendChild() in web extension content script."
        );

        // write/writeln
        const doc = new DOMParser().parseFromString(
          `<body></body>`,
          "text/html"
        );
        browser.test.assertThrows(
          () => doc.write("1", "2"),
          /The operation is insecure/,
          "write() with a string parameter is blocked in web extension content script, independently of Trusted Types enforcement."
        );
        browser.test.assertThrows(
          () => doc.write(passthroughpolicy.createHTML("3")),
          /The operation is insecure/,
          "write() with a TrustedHTML parameter is blocked in web extension content script, independently of Trusted Types enforcement."
        );
        browser.test.assertThrows(
          () => doc.writeln("4", "5"),
          /The operation is insecure/,
          "writeln() with a string parameter is not blocked in web extension content script, independently of Trusted Types enforcement."
        );
        browser.test.assertThrows(
          () => doc.write(passthroughpolicy.createHTML("6")),
          /The operation is insecure/,
          "writeln() with a TrustedHTML parameter is not blocked in web extension content script, independently of Trusted Types enforcement."
        );

        // pre-navigation check
        let preNavigationCheckPromise =
          NavigateAndWaitForSecurityPolicyViolation(
            `Location href|void 'js-url-isolated'`,
            "javascript:void 'js-url-isolated'"
          );
        await browser.test.assertRejects(
          preNavigationCheckPromise,
          "no violations observed after idle callback.",
          "Trusted Types enforcement does not block navigation to javascript: URL with a string parameter in web extension content script."
        );

        /* TODO(Bug 1963277)
        // Worker sink
        let worker = new Worker("data:text/javascript,self.postMessage('worker-script');");
        let workerMessage = new Promise(resolve => worker.addEventListener("message", evt => resolve(evt.data), {once: true}));
        browser.test.assertEq(await workerMessage, "worker-script");
        */

        browser.test.sendMessage("content_script_isolated.js-finished");
      },
      async "content_script_main.js"() {
        function assertThrows(fun, expectedError, message) {
          let error = undefined;
          try {
            fun();
          } catch (e) {
            error = e;
          }

          browserTestAssertEq(expectedError, error?.name, message);
        }
        function WaitForSecurityPolicyViolation(sample_prefix) {
          return new Promise(resolve => {
            function listener(event) {
              if (event.sample.startsWith(sample_prefix)) {
                window.removeEventListener("securitypolicyviolation", listener);
                resolve(event.effectiveDirective);
              }
            }
            window.addEventListener("securitypolicyviolation", listener);
          });
        }
        function NavigateAndWaitForSecurityPolicyViolation(sample_prefix, url) {
          return new Promise((resolve, reject) => {
            function listener(event) {
              if (event.sample.startsWith(sample_prefix)) {
                window.removeEventListener("securitypolicyviolation", listener);
                resolve(event.effectiveDirective);
              }
            }
            window.addEventListener("securitypolicyviolation", listener);
            try {
              window.location = url;
            } catch (e) {
              reject(
                new Error(`window.location = ${url} threw '${e.toString()}'`)
              );
            }
            window.requestIdleCallback(_ => {
              window.removeEventListener("securitypolicyviolation", listener);
              reject(new Error("no violations observed after idle callback."));
            });
          });
        }
        const passthroughpolicy = window.trustedTypes.createPolicy(
          "passthroughpolicy",
          {
            createHTML: s => s,
            createScript: s => s,
            createScriptURL: s => s,
          }
        );

        // HTMLScriptElement.text
        let script = document.createElement("script");
        assertThrows(
          () => {
            script.text = "foo";
          },
          "TypeError",
          "Trusted Types enforcement blocks setting HTMLScriptElement.text to a string."
        );
        script.text = passthroughpolicy.createScript(";");
        browserTestAssertEq(
          ";",
          script.text,
          "Trusted Types enforcement does not block setting HTMLScriptElement.text to a TrustedScript."
        );

        // HTMLScriptElement.innerText
        assertThrows(
          () => {
            script.innerText = "foo";
          },
          "TypeError",
          "Trusted Types enforcement blocks setting HTMLScriptElement.innerText to a string."
        );
        script.innerText = passthroughpolicy.createScript(";;");
        browserTestAssertEq(
          ";;",
          script.innerText,
          "Trusted Types enforcement does not block setting HTMLScriptElement.innerText to a TrustedScript."
        );

        // HTMLScriptElement.src
        const scriptSrc = "http://example.com/script.js";
        assertThrows(
          () => {
            script.src = scriptSrc;
          },
          "TypeError",
          "Trusted Types enforcement blocks setting HTMLScriptElement.src to a string."
        );
        script.src = passthroughpolicy.createScriptURL(scriptSrc);
        browserTestAssertEq(
          scriptSrc,
          script.src,
          "Trusted Types enforcement does not block setting HTMLScriptElement.src to a TrustedScriptURL."
        );

        // Element.setAttribute
        const scriptSrc2 = "http://example.com/script2.js";
        assertThrows(() => {
          script.setAttribute(
            "src",
            scriptSrc2,
            "Trusted Types enforcement blocks setting a script element's src attribute to a string."
          );
        }, "TypeError");
        script.setAttribute(
          "src",
          passthroughpolicy.createScriptURL(scriptSrc2)
        );
        browserTestAssertEq(
          scriptSrc2,
          script.getAttribute("src"),
          "Trusted Types enforcement does not block setting a script element's src attribute to a TrustedScriptURL."
        );

        // HTMLIframeElement.srcdoc
        let iframe = document.createElement("iframe");
        assertThrows(
          () => {
            iframe.srcdoc = "foo";
          },
          "TypeError",
          "Trusted Types enforcement blocks setting HTMLIframeElement.srcdoc to a string."
        );
        iframe.srcdoc = passthroughpolicy.createHTML("foo");
        browserTestAssertEq(
          "foo",
          iframe.srcdoc,
          "Trusted Types enforcement does not block setting HTMLIframeElement.srcdoc to a TrustedHTML."
        );

        // Element.innerHTML
        let div = document.createElement("div");
        assertThrows(
          () => {
            div.innerHTML = "foo";
          },
          "TypeError",
          "Trusted Types enforcement blocks setting Element.innerHTML to a string."
        );
        div.innerHTML = passthroughpolicy.createHTML("foo");
        browserTestAssertEq(
          "foo",
          div.innerHTML,
          "Trusted Types enforcement does not block setting Element.innerHTML to a TrustedHTML."
        );

        // Element.outerHTML
        const parentDiv = document.createElement("div");
        parentDiv.appendChild(div);
        assertThrows(
          () => {
            div.outerHTML = "<div>bar</div>";
          },
          "TypeError",
          "Trusted Types enforcement blocks setting Element.outerHTML to a string."
        );

        div.outerHTML = passthroughpolicy.createHTML("<div>bar</div>");
        browserTestAssertEq(
          "<div>bar</div>",
          parentDiv.innerHTML,
          "Trusted Types enforcement does not block setting Element.outerHTML to a TrustedHTML."
        );

        // Element.setHTMLUnsafe()
        assertThrows(
          () => div.setHTMLUnsafe("baz"),
          "TypeError",
          "Trusted Types enforcement blocks executing Element.setHTMLUnsafe() with a string parameter"
        );
        div.setHTMLUnsafe(passthroughpolicy.createHTML("<p>baz</p>"));
        browserTestAssertEq(
          "<p>baz</p>",
          div.innerHTML,
          "Trusted Types enforcement does not block executing Element.setHTMLUnsafe() with a TrustedHTML parameter"
        );

        // eval
        assertThrows(
          _ => {
            // eslint-disable-next-line no-eval
            eval("3+4");
          },
          "EvalError",
          "Trusted Types enforcement blocks executing eval() with a string parameter."
        );
        browserTestAssertEq(
          7,
          // eslint-disable-next-line no-eval
          eval(passthroughpolicy.createScript("3+4")),
          "Trusted Types enforcement does not block executing eval() with a TrustedScript parameter."
        );

        // script enforcement
        // Note: There is no way to pass a TrustedScript to appendChild().
        let scriptEnforcementPromise = WaitForSecurityPolicyViolation(
          `HTMLScriptElement text|window.postMessage("script-enforcement`
        );
        let scriptEnforcementScript = document.createElement("script");
        scriptEnforcementScript.appendChild(
          document.createTextNode(
            'window.postMessage("script-enforcement-main", "*");'
          )
        );
        document.body.appendChild(scriptEnforcementScript);
        browserTestAssertEq(
          "require-trusted-types-for",
          await scriptEnforcementPromise,
          "Trusted Types enforcement blocks script modified via appendChild()."
        );

        // write/writeln
        const doc = new DOMParser().parseFromString(
          passthroughpolicy.createHTML("<body></body>"),
          "text/html"
        );
        assertThrows(
          _ => doc.write("foo"),
          "TypeError",
          "Trusted Types enforcement blocks executing write() with a string parameter."
        );
        assertThrows(
          _ => doc.writeln("bar"),
          "TypeError",
          "Trusted Types enforcement blocks executing writeln() with a string parameter."
        );
        doc.write(passthroughpolicy.createHTML("1"));
        doc.writeln(passthroughpolicy.createHTML("2"));
        browserTestAssertEq(
          "12\n",
          doc.body.innerHTML,
          "Trusted Types enforcement does not block executing write/writeln() with a TrustedScript parameter."
        );

        // pre-navigation check
        // Note: There is no way to pass a 'TrustedScript with javascript: prefix' URL.
        let preNavigationCheckPromise =
          NavigateAndWaitForSecurityPolicyViolation(
            `Location href|void 'js-url-main'`,
            "javascript:void 'js-url-main'"
          );
        browserTestAssertEq(
          "require-trusted-types-for",
          await preNavigationCheckPromise,
          "Trusted Types enforcement blocks navigation to javascript: URL."
        );

        // Worker sink
        assertThrows(
          () => new Worker("data:text/javascript,void 0"),
          "TypeError",
          "Trusted Types enforcement blocks executing a Worker constructor with a string parameter."
        );
        let worker = new Worker(
          passthroughpolicy.createScriptURL("data:text/javascript,void 0")
        );
        browserTestAssertTrue(
          worker,
          "Trusted Types enforcement does not block executing a Worker constructor with a TrustedScriptURL parameter."
        );

        browserTestSendMessage("content_script_main.js-finished");
      },
      "test_script_src.js"() {
        window.postMessage("script-src", "*");
      },
    },
  });

  await extension.startup();
  let contentPage = await ExtensionTestUtils.loadContentPage(
    `http://example.com/data/file_trusted_types.html`
  );

  await Promise.all([
    extension.awaitMessage("content_script_isolated.js-finished"),
    extension.awaitMessage("content_script_main.js-finished"),
  ]);

  await extension.unload();
  await contentPage.close();
}

add_task(async function test_contentscript_trusted_types_v2() {
  await test_contentscript_trusted_types({
    manifest_version: 2,
    web_accessible_resources: ["test_script_src.js"],
  });
});

add_task(async function test_contentscript_trusted_types_v3() {
  await test_contentscript_trusted_types({
    manifest_version: 3,
    web_accessible_resources: [
      {
        resources: ["test_script_src.js"],
        matches: ["http://example.com/*"],
      },
    ],
  });
});
