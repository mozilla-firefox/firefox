/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* This list allows pre-existing or 'unfixable' CSS issues to remain, while we
 * detect newly occurring issues in shipping CSS. It is a list of objects
 * specifying conditions under which an error should be ignored.
 *
 * Every property of the objects in it needs to consist of a regular expression
 * matching the offending error. If an object has multiple regex criteria, they
 * ALL need to match an error in order for that error not to cause a test
 * failure. */
let ignoreList = [
  // CodeMirror is imported as-is, see bug 1004423.
  { sourceName: /codemirror\.css$/i, isFromDevTools: true },
  // UA-only media features.
  {
    sourceName:
      /\b(contenteditable|EditorOverride|svg|forms|html|mathml|ua|scrollbars|xul)\.css$/i,
    errorMessage: /Unknown pseudo-class.*-moz-/i,
    isFromDevTools: false,
  },
  {
    sourceName:
      /\b(scrollbars|xul|html|mathml|ua|EditorOverride|contenteditable|forms|svg|manageDialog|formautofill)\.css$/i,
    errorMessage: /Unknown property.*-moz-/i,
    isFromDevTools: false,
  },
  // content: -moz-alt-content is UA-only.
  {
    sourceName: /\b(html)\.css$/i,
    errorMessage: /Error in parsing value for ‘content’/i,
    isFromDevTools: false,
  },
  // megalist-agent.css is loaded as an agent sheet, so its UA-only
  // ::-moz-reveal selector doesn't parse as an author sheet here.
  {
    sourceName: /\bmegalist-agent\.css$/i,
    errorMessage: /Unknown pseudo-class or pseudo-element ‘-moz-reveal’/i,
    isFromDevTools: false,
  },
  // These variables are declared somewhere else, and error when we load the
  // files directly. They're all marked intermittent because their appearance
  // in the error console seems to not be consistent.
  {
    sourceName: /jsonview\/css\/general\.css$/i,
    intermittent: true,
    errorMessage: /Property contained reference to invalid variable.*color/i,
    isFromDevTools: true,
  },
  {
    sourceName: /web\/viewer\.css$/i,
    errorMessage:
      /Unknown property ‘text-size-adjust’\. {2}Declaration dropped\./i,
    isFromDevTools: false,
  },
];

if (AppConstants.platform != "macosx") {
  ignoreList.push({
    errorMessage: /Unknown property.*-moz-osx-font-smoothing/i,
    isFromDevTools: false,
  });
}

if (!Services.prefs.getBoolPref("dom.select.customizable_select.enabled")) {
  ignoreList.push({
    sourceName: /\bforms\.css$/i,
    errorMessage: /Unknown pseudo-class or pseudo-element ‘picker’./i,
    isFromDevTools: false,
  });
  ignoreList.push({
    sourceName: /\bforms\.css$/i,
    errorMessage: /Unknown pseudo-class or pseudo-element ‘checkmark’./i,
    isFromDevTools: false,
  });
}

if (!Services.prefs.getBoolPref("layout.css.zoom.enabled")) {
  ignoreList.push({
    sourceName: /\bscrollbars\.css$/i,
    errorMessage: /Error in parsing value for ‘zoom’/i,
    isFromDevTools: false,
  });
}

if (!Services.prefs.getBoolPref("layout.css.scroll-anchoring.enabled")) {
  ignoreList.push({
    sourceName: /webconsole\.css$/i,
    errorMessage: /Unknown property .*\boverflow-anchor\b/i,
    isFromDevTools: true,
  });
}

if (!Services.prefs.getBoolPref("layout.css.text-decoration-inset.enabled")) {
  ignoreList.push({
    sourceName: /html\.css$/i,
    errorMessage: /Unknown property .*text-decoration-inset/i,
    isFromDevTools: false,
  });
  ignoreList.push({
    sourceName: /ua\.css$/i,
    errorMessage: /Unknown property .*text-decoration-inset/i,
    isFromDevTools: false,
  });
}

if (
  !Services.prefs.getBoolPref("layout.css.scroll-driven-animations.enabled")
) {
  ignoreList.push({
    sourceName: /\b(smartbar|ai-action-confirmation)\.css$/i,
    errorMessage:
      /Unknown property .*(animation-range|animation-timeline|scroll-timeline|view-timeline|timeline-scope)/i,
    isFromDevTools: false,
  });
}

if (!Services.prefs.getBoolPref("dom.headingoffset.enabled")) {
  ignoreList.push({
    sourceName: /\b(html)\.css$/i,
    errorMessage: /Unknown pseudo-class.*heading/i,
    isFromDevTools: false,
  });
}

let propNameAllowlist = [
  // These custom properties are retrieved directly from CSSOM
  // in videocontrols.xml to get pre-defined style instead of computed
  // dimensions, which is why they are not referenced by CSS.
  { propName: "--clickToPlay-width", isFromDevTools: false },
  { propName: "--playButton-width", isFromDevTools: false },
  { propName: "--muteButton-width", isFromDevTools: false },
  { propName: "--closedCaptionButton-width", isFromDevTools: false },
  { propName: "--fullscreenButton-width", isFromDevTools: false },
  { propName: "--durationSpan-width", isFromDevTools: false },
  { propName: "--durationSpan-width-long", isFromDevTools: false },
  { propName: "--positionDurationBox-width", isFromDevTools: false },
  { propName: "--positionDurationBox-width-long", isFromDevTools: false },

  // These variables are used in a shorthand, but the CSS parser deletes the values
  // when expanding the shorthands. See https://github.com/w3c/csswg-drafts/issues/2515
  { propName: "--bezier-diagonal-color", isFromDevTools: true },
  { propName: "--highlighter-font-family", isFromDevTools: true },

  // These are referenced from devtools files.
  {
    propName: "--browser-container-z-index-devtools-toolbox",
    isFromDevTools: false,
  },
  {
    propName: "--browser-container-z-index-devtools-splitter",
    isFromDevTools: false,
  },

  // These variables are specified from devtools but read from non-devtools
  // styles, which confuses the test.
  { propName: "--panel-border-radius", isFromDevTools: true },
  { propName: "--panel-padding", isFromDevTools: true },
  { propName: "--panel-background-color", isFromDevTools: true },
  { propName: "--panel-border-color", isFromDevTools: true },
  { propName: "--panel-box-shadow", isFromDevTools: true },

  // This is a semantic panel design token provided by the design system that
  // currently has no chrome CSS consumer, so it isn't referenced via var().
  {
    propName: "--panel-background-color-dimmed-further",
    isFromDevTools: false,
  },

  // These variables are set in host CSS but consumed in shadow DOM CSS
  // (content-search-handoff-ui component), which confuses the test.
  { propName: /^--content-search-handoff-ui-/, isFromDevTools: false },

  // This variable defines the icon background for smart window first run model
  // cards and is consumed from JS in firstrun.js (built into a `background`
  // shorthand string), so it isn't referenced via var() in CSS.
  { propName: "--card-icon-bg", isFromDevTools: false },

  // These variables are used in JS in viewer.mjs (PDF.js).
  {
    propName: "--scale-round-x",
    isFromDevTools: false,
  },
  {
    propName: "--scale-round-y",
    isFromDevTools: false,
  },

  // These variables define accent colors for tab group chrome
  // and are used in JS in tabgroup.js
  { propName: "--tab-group-blue", isFromDevTools: false },
  { propName: "--tab-group-blue-invert", isFromDevTools: false },
  { propName: "--tab-group-blue-pale", isFromDevTools: false },
  { propName: "--tab-group-blue-hover", isFromDevTools: false },
  { propName: "--tab-group-blue-text", isFromDevTools: false },
  { propName: "--tab-group-blue-text-invert", isFromDevTools: false },

  { propName: "--tab-group-purple", isFromDevTools: false },
  { propName: "--tab-group-purple-invert", isFromDevTools: false },
  { propName: "--tab-group-purple-pale", isFromDevTools: false },
  { propName: "--tab-group-purple-hover", isFromDevTools: false },
  { propName: "--tab-group-purple-text", isFromDevTools: false },
  { propName: "--tab-group-purple-text-invert", isFromDevTools: false },

  { propName: "--tab-group-cyan", isFromDevTools: false },
  { propName: "--tab-group-cyan-invert", isFromDevTools: false },
  { propName: "--tab-group-cyan-pale", isFromDevTools: false },
  { propName: "--tab-group-cyan-hover", isFromDevTools: false },
  { propName: "--tab-group-cyan-text", isFromDevTools: false },
  { propName: "--tab-group-cyan-text-invert", isFromDevTools: false },

  { propName: "--tab-group-orange", isFromDevTools: false },
  { propName: "--tab-group-orange-invert", isFromDevTools: false },
  { propName: "--tab-group-orange-pale", isFromDevTools: false },
  { propName: "--tab-group-orange-hover", isFromDevTools: false },
  { propName: "--tab-group-orange-text", isFromDevTools: false },
  { propName: "--tab-group-orange-text-invert", isFromDevTools: false },

  { propName: "--tab-group-yellow", isFromDevTools: false },
  { propName: "--tab-group-yellow-invert", isFromDevTools: false },
  { propName: "--tab-group-yellow-pale", isFromDevTools: false },
  { propName: "--tab-group-yellow-hover", isFromDevTools: false },
  { propName: "--tab-group-yellow-text", isFromDevTools: false },
  { propName: "--tab-group-yellow-text-invert", isFromDevTools: false },

  { propName: "--tab-group-pink", isFromDevTools: false },
  { propName: "--tab-group-pink-invert", isFromDevTools: false },
  { propName: "--tab-group-pink-pale", isFromDevTools: false },
  { propName: "--tab-group-pink-hover", isFromDevTools: false },
  { propName: "--tab-group-pink-text", isFromDevTools: false },
  { propName: "--tab-group-pink-text-invert", isFromDevTools: false },

  { propName: "--tab-group-green", isFromDevTools: false },
  { propName: "--tab-group-green-invert", isFromDevTools: false },
  { propName: "--tab-group-green-pale", isFromDevTools: false },
  { propName: "--tab-group-green-hover", isFromDevTools: false },
  { propName: "--tab-group-green-text", isFromDevTools: false },
  { propName: "--tab-group-green-text-invert", isFromDevTools: false },

  { propName: "--tab-group-red", isFromDevTools: false },
  { propName: "--tab-group-red-invert", isFromDevTools: false },
  { propName: "--tab-group-red-pale", isFromDevTools: false },
  { propName: "--tab-group-red-hover", isFromDevTools: false },
  { propName: "--tab-group-red-text", isFromDevTools: false },
  { propName: "--tab-group-red-text-invert", isFromDevTools: false },

  { propName: "--tab-group-gray", isFromDevTools: false },
  { propName: "--tab-group-gray-invert", isFromDevTools: false },
  { propName: "--tab-group-gray-pale", isFromDevTools: false },
  { propName: "--tab-group-gray-hover", isFromDevTools: false },
  { propName: "--tab-group-gray-text", isFromDevTools: false },
  { propName: "--tab-group-gray-text-invert", isFromDevTools: false },

  /* Allow design tokens in devtools without all variables being used there */
  { sourceName: /\/design-system\/tokens-.*\.css$/, isFromDevTools: true },
  { sourceName: /\/in-content\/common-shared\.css/, isFromDevTools: true },

  // `--icon-stroke` is defined in commonDialog.css and used in stringified CSS
  // within adjustableTitle.js. The latter isn't statically parsed.
  { propName: "--icon-stroke", isFromDevTools: false },

  // Ignore token properties that follow the patterns --color-[name], --color-[name]-[number], or --color-[name]-alpha-[number]
  // This enables us to provide our full color palette for developers.
  { propName: /--color-[a-z]+(-alpha)?(-\d+)?/, isFromDevTools: false },

  // Ignore token properties that follow the patterns --dimension-[number] or --dimension-relative-[number]
  // This enables us to provide our full size/spacing system for developers.
  { propName: /--dimension(-relative)?-\d+/, isFromDevTools: false },

  // This variable is read from JS to determine the column count when handling
  // keyboard navigation in the New Tab sections grid.
  { propName: "--sections-col-count", isFromDevTools: false },

  // This property was used in the onboarding set up feature callout checklist.
  // It's currently not referenced in CSS but may be needed for future use.
  { propName: "--fc-icon-success-color", isFromDevTools: false },
];

// Add suffix to stylesheets' URI so that we always load them here and
// have them parsed. Add a random number so that even if we run this
// test multiple times, it would be unlikely to affect each other.
const kPathSuffix = "?always-parse-css-" + Math.random();

function dumpAllowlistItem(item) {
  return JSON.stringify(item, (key, value) => {
    return value instanceof RegExp ? value.toString() : value;
  });
}

/**
 * Check if an error should be ignored due to matching one of the allowlist
 * objects.
 *
 * @param aErrorObject the error to check
 * @return true if the error should be ignored, false otherwise.
 */
function ignoredError(aErrorObject) {
  for (let allowlistItem of ignoreList) {
    let matches = true;
    let catchAll = true;
    for (let prop of ["sourceName", "errorMessage"]) {
      if (allowlistItem.hasOwnProperty(prop)) {
        catchAll = false;
        if (!allowlistItem[prop].test(aErrorObject[prop] || "")) {
          matches = false;
          break;
        }
      }
    }
    if (catchAll) {
      ok(
        false,
        "An allowlist item is catching all errors. " +
          dumpAllowlistItem(allowlistItem)
      );
      continue;
    }
    if (matches) {
      allowlistItem.used = true;
      let { sourceName, errorMessage } = aErrorObject;
      info(
        `Ignored error "${errorMessage}" on ${sourceName} ` +
          "because of allowlist item " +
          dumpAllowlistItem(allowlistItem)
      );
      return true;
    }
  }
  return false;
}

var gChromeReg = Cc["@mozilla.org/chrome/chrome-registry;1"].getService(
  Ci.nsIChromeRegistry
);
var gChromeMap = new Map();

var resHandler = Services.io
  .getProtocolHandler("resource")
  .QueryInterface(Ci.nsIResProtocolHandler);
var gResourceMap = [];
function trackResourcePrefix(prefix) {
  let uri = Services.io.newURI("resource://" + prefix + "/");
  gResourceMap.unshift([prefix, resHandler.resolveURI(uri)]);
}
trackResourcePrefix("gre");
trackResourcePrefix("app");

function getBaseUriForChromeUri(chromeUri) {
  let chromeFile = chromeUri + "nonexistentfile.reallynothere";
  let uri = Services.io.newURI(chromeFile);
  let fileUri = gChromeReg.convertChromeURL(uri);
  return fileUri.resolve(".");
}

function parseManifest(manifestUri) {
  return fetchFile(manifestUri.spec).then(data => {
    for (let line of data.split("\n")) {
      let [type, ...argv] = line.split(/\s+/);
      if (type == "content" || type == "skin") {
        let chromeUri = `chrome://${argv[0]}/${type}/`;
        gChromeMap.set(getBaseUriForChromeUri(chromeUri), chromeUri);
      } else if (type == "resource") {
        trackResourcePrefix(argv[0]);
      }
    }
  });
}

function convertToCodeURI(fileUri) {
  let baseUri = fileUri;
  let path = "";
  while (true) {
    let slashPos = baseUri.lastIndexOf("/", baseUri.length - 2);
    if (slashPos <= 0) {
      // File not accessible from chrome protocol, try resource://
      for (let res of gResourceMap) {
        if (fileUri.startsWith(res[1])) {
          return fileUri.replace(res[1], "resource://" + res[0] + "/");
        }
      }
      // Give up and return the original URL.
      return fileUri;
    }
    path = baseUri.slice(slashPos + 1) + path;
    baseUri = baseUri.slice(0, slashPos + 1);
    if (gChromeMap.has(baseUri)) {
      return gChromeMap.get(baseUri) + path;
    }
  }
}

function messageIsCSSError(msg) {
  // Only care about CSS errors generated by our iframe:
  if (
    msg instanceof Ci.nsIScriptError &&
    msg.category.includes("CSS") &&
    msg.sourceName.endsWith(kPathSuffix)
  ) {
    let sourceName = msg.sourceName.slice(0, -kPathSuffix.length);
    let msgInfo = { sourceName, errorMessage: msg.errorMessage };
    // Check if this error is allowlisted in allowlist
    if (!ignoredError(msgInfo)) {
      ok(false, `Got error message for ${sourceName}: ${msg.errorMessage}`);
      return true;
    }
  }
  return false;
}

let imageURIsToReferencesMap = new Map();
let customPropsToReferencesMap = new Map();
let customPropsDefinitionFileMap = new Map();

function neverMatches(mediaList) {
  const perPlatformMediaQueryMap = {
    macosx: ["(-moz-platform: macos)"],
    win: ["(-moz-platform: windows)"],
    linux: ["(-moz-platform: linux)"],
    android: ["(-moz-platform: android)"],
  };
  for (let platform in perPlatformMediaQueryMap) {
    const inThisPlatform = platform === AppConstants.platform;
    for (const media of perPlatformMediaQueryMap[platform]) {
      if (inThisPlatform && mediaList.mediaText == "not " + media) {
        // This query can't match on this platform.
        return true;
      }
      if (!inThisPlatform && mediaList.mediaText == media) {
        // This query only matches on another platform that isn't ours.
        return true;
      }
    }
  }
  return false;
}

function processCSSRules(container) {
  for (let rule of container.cssRules) {
    if (rule.media && neverMatches(rule.media)) {
      continue;
    }
    if (rule.styleSheet) {
      processCSSRules(rule.styleSheet); // @import
      continue;
    }
    if (rule.cssRules) {
      processCSSRules(rule); // @supports, @media, @layer (block), @keyframes, style rules with nested rules.
    }
    if (!rule.style) {
      // @layer (statement), @font-feature-values, @counter-style, @container, …

      // Look for custom property usage in style queries
      if (rule.conditionText) {
        const lexer = new InspectorCSSParser(rule.conditionText);
        let token;
        let foundStyleFunc = false;
        while ((token = lexer.nextToken())) {
          // we're looking for usages of the `style()` function to collect referenced
          // custom property names
          if (token.tokenType === "Function" && token.value === "style") {
            foundStyleFunc = true;
            continue;
          }

          // If we saw a `style(` token before, we're looking for the custom property name
          // param
          if (
            foundStyleFunc &&
            token.tokenType === "Ident" &&
            token.text.startsWith("--")
          ) {
            foundStyleFunc = false;
            const prop = token.text;
            let prevValue = customPropsToReferencesMap.get(prop) || 0;
            customPropsToReferencesMap.set(prop, prevValue + 1);
            continue;
          }

          // When seeing a closing parenthesis we can reset the work variable
          if (token.tokenType === "CloseParenthesis") {
            foundStyleFunc = false;
            continue;
          }
        }
      }

      continue;
    }

    // We want to extract urls and variables from the css text.
    // Let's parse the css text so we can iterate through the tokens, which is more
    // reliable than trying to extract data with regexes.
    let cssText = rule.style.cssText;
    {
      const lexer = new InspectorCSSParser(cssText);
      let token;
      let currentDeclarationName;
      let foundVarFunc = false;
      let foundUrlFunc = false;

      while ((token = lexer.nextToken())) {
        // At the beginning, we're looking for the declaration name
        if (!currentDeclarationName) {
          // which should be the first Ident token we see
          if (token.tokenType === "Ident") {
            currentDeclarationName = token.text;

            // If it starts with "--", we have a custom property declaration
            if (token.text.startsWith("--")) {
              const prop = token.text;
              if (!customPropsToReferencesMap.has(prop)) {
                customPropsToReferencesMap.set(prop, undefined);
                if (!customPropsDefinitionFileMap.has(prop)) {
                  customPropsDefinitionFileMap.set(prop, new Set());
                }
                customPropsDefinitionFileMap
                  .get(prop)
                  .add(container.href || container.parentStyleSheet.href);
              }
            }
          }
          continue;
        }
        // At this point, we found the declaration name, so we're parsing the declaration value

        // we're looking for usages of the `var()` function to collect referenced custom property names
        if (token.tokenType === "Function" && token.value === "var") {
          foundVarFunc = true;
          continue;
        }
        // If we saw a `var(` token before, then the next Ident should contain a custom
        // property name
        if (
          foundVarFunc &&
          token.tokenType === "Ident" &&
          token.text.startsWith("--")
        ) {
          foundVarFunc = false;
          const prop = token.text;
          let prevValue = customPropsToReferencesMap.get(prop) || 0;
          customPropsToReferencesMap.set(prop, prevValue + 1);
          continue;
        }

        // we're also looking for usages of the `url()` function
        if (token.tokenType === "Function" && token.value === "url") {
          foundUrlFunc = true;
          continue;
        }
        // If we saw a `url(` token before, then the next QuotedString should contain
        // the actual URL (CSSRule.style.cssText always has double quotes around URLs
        // even when the original CSS file didn't).
        if (foundUrlFunc && token.tokenType === "QuotedString") {
          foundUrlFunc = false;
          let url = token.value;
          if (url.startsWith("data:")) {
            continue;
          }

          // Make the url absolute and remove the ref.
          let baseURI = Services.io.newURI(rule.parentStyleSheet.href);
          url = Services.io.newURI(url, null, baseURI).specIgnoringRef;

          // Store the image url along with the css file referencing it.
          let baseUrl = baseURI.spec.split("?always-parse-css")[0];
          if (!imageURIsToReferencesMap.has(url)) {
            imageURIsToReferencesMap.set(url, new Set([baseUrl]));
          } else {
            imageURIsToReferencesMap.get(url).add(baseUrl);
          }

          continue;
        }

        // When seeing a semi colon, we can reset the work variable so we're ready
        // to parse the next declaration
        if (token.tokenType === "Semicolon") {
          foundVarFunc = false;
          foundUrlFunc = false;
          currentDeclarationName = null;
          continue;
        }
      }
    }
  }
}

function chromeFileExists(aURI) {
  let available = 0;
  try {
    let uri = NetUtil.newURI(aURI);
    // moz-icon: is only loadable as an image, so we pretend to do that.
    let contentPolicyType = uri.schemeIs("moz-icon")
      ? Ci.nsIContentPolicy.TYPE_IMAGE
      : Ci.nsIContentPolicy.TYPE_OTHER;
    let channel = NetUtil.newChannel({
      uri,
      contentPolicyType,
      loadUsingSystemPrincipal: true,
    });
    let stream = channel.open();
    let sstream = Cc["@mozilla.org/scriptableinputstream;1"].createInstance(
      Ci.nsIScriptableInputStream
    );
    sstream.init(stream);
    available = sstream.available();
    sstream.close();
  } catch (e) {
    if (e.result != Cr.NS_ERROR_FILE_NOT_FOUND) {
      dump("Checking " + aURI + ": " + e + "\n");
      console.error(e);
    }
  }
  return available > 0;
}

function shouldIgnorePropSource(item, prop) {
  if (!item.sourceName || !customPropsDefinitionFileMap.has(prop)) {
    return false;
  }
  return customPropsDefinitionFileMap
    .get(prop)
    .values()
    .some(f => item.sourceName.test(f));
}

function shouldIgnorePropPattern(item, prop) {
  if (!item.propName || !(item.propName instanceof RegExp)) {
    return false;
  }
  return item.propName.test(prop);
}

add_task(async function checkAllTheCSS() {
  // Since we later in this test use Services.console.getMessageArray(),
  // better to not have some messages from previous tests in the array.
  Services.console.reset();

  let appDir = Services.dirsvc.get("GreD", Ci.nsIFile);
  // This asynchronously produces a list of URLs (sadly, mostly sync on our
  // test infrastructure because it runs against jarfiles there, and
  // our zipreader APIs are all sync)
  let uris = await generateURIsFromDirTree(appDir, [".css", ".manifest"]);

  // Create a clean iframe to load all the files into. This needs to live at a
  // chrome URI so that it's allowed to load and parse any styles.
  let testFile = getRootDirectory(gTestPath) + "dummy_page.html";
  let { HiddenFrame } = ChromeUtils.importESModule(
    "resource://gre/modules/HiddenFrame.sys.mjs"
  );
  let hiddenFrame = new HiddenFrame();
  let win = await hiddenFrame.get();
  let iframe = win.document.createElementNS(
    "http://www.w3.org/1999/xhtml",
    "html:iframe"
  );
  win.document.documentElement.appendChild(iframe);
  let iframeLoaded = BrowserTestUtils.waitForEvent(iframe, "load", true);
  iframe.contentWindow.location = testFile;
  await iframeLoaded;
  let doc = iframe.contentWindow.document;
  iframe.contentWindow.docShell.cssErrorReportingEnabled = true;

  // Parse and remove all manifests from the list.
  // NOTE that this must be done before filtering out devtools paths
  // so that all chrome paths can be recorded.
  let manifestURIs = [];
  uris = uris.filter(uri => {
    if (uri.pathQueryRef.endsWith(".manifest")) {
      manifestURIs.push(uri);
      return false;
    }
    return true;
  });
  // Wait for all manifest to be parsed
  await PerfTestHelpers.throttledMapPromises(manifestURIs, parseManifest);

  // filter out either the devtools paths or the non-devtools paths:
  let isDevtools = SimpleTest.harnessParameters.subsuite == "devtools";
  let devtoolsPathBits = ["devtools"];
  uris = uris.filter(
    uri => isDevtools == devtoolsPathBits.some(path => uri.spec.includes(path))
  );

  let loadCSS = chromeUri =>
    new Promise(resolve => {
      let linkEl, onLoad, onError;
      onLoad = () => {
        processCSSRules(linkEl.sheet);
        resolve();
        linkEl.removeEventListener("load", onLoad);
        linkEl.removeEventListener("error", onError);
      };
      onError = () => {
        ok(
          false,
          "Loading " + linkEl.getAttribute("href") + " threw an error!"
        );
        resolve();
        linkEl.removeEventListener("load", onLoad);
        linkEl.removeEventListener("error", onError);
      };
      linkEl = doc.createElement("link");
      linkEl.setAttribute("rel", "stylesheet");
      linkEl.setAttribute("type", "text/css");
      linkEl.addEventListener("load", onLoad);
      linkEl.addEventListener("error", onError);
      linkEl.setAttribute("href", chromeUri + kPathSuffix);
      doc.head.appendChild(linkEl);
    });

  // We build a list of promises that get resolved when their respective
  // files have loaded and produced no errors.
  const kInContentCommonCSS = "chrome://global/skin/in-content/common.css";
  let allPromises = uris
    .map(uri => convertToCodeURI(uri.spec))
    .filter(uri => uri !== kInContentCommonCSS);

  // Make sure chrome://global/skin/in-content/common.css is loaded before other
  // stylesheets in order to guarantee the --in-content variables can be
  // correctly referenced.
  if (allPromises.length !== uris.length) {
    await loadCSS(kInContentCommonCSS);
  }

  // Wait for all the files to have actually loaded:
  await PerfTestHelpers.throttledMapPromises(allPromises, loadCSS);

  // Check if all the files referenced from CSS actually exist.
  // Files in browser/ should never be referenced outside browser/.
  for (let [image, references] of imageURIsToReferencesMap) {
    if (!chromeFileExists(image)) {
      for (let ref of references) {
        ok(false, "missing " + image + " referenced from " + ref);
      }
    }

    let imageHost = image.split("/")[2];
    if (imageHost == "browser") {
      for (let ref of references) {
        let refHost = ref.split("/")[2];
        if (!["builtin-addons", "newtab", "browser"].includes(refHost)) {
          ok(
            false,
            "browser file " + image + " referenced outside browser in " + ref
          );
        }
      }
    }
  }

  // Check if all the properties that are defined are referenced.
  for (let [prop, refCount] of customPropsToReferencesMap) {
    if (!refCount) {
      let ignored = false;
      for (let item of propNameAllowlist) {
        if (
          isDevtools == item.isFromDevTools &&
          (item.propName == prop ||
            shouldIgnorePropPattern(item, prop) ||
            shouldIgnorePropSource(item, prop))
        ) {
          item.used = true;
          if (
            !item.platforms ||
            item.platforms.includes(AppConstants.platform)
          ) {
            ignored = true;
          }
          break;
        }
      }
      if (!ignored) {
        ok(false, "custom property `" + prop + "` is not referenced");
      }
    }
  }

  let messages = Services.console.getMessageArray();
  // Count errors (the test output will list actual issues for us, as well
  // as the ok(false) in messageIsCSSError.
  let errors = messages.filter(messageIsCSSError);
  is(
    errors.length,
    0,
    "All the styles (" + allPromises.length + ") loaded without errors."
  );

  // Confirm that all allowlist rules have been used.
  function checkAllowlist(list) {
    for (let item of list) {
      if (
        !item.used &&
        isDevtools == item.isFromDevTools &&
        (!item.platforms || item.platforms.includes(AppConstants.platform)) &&
        !item.intermittent
      ) {
        ok(false, "Unused allowlist item: " + dumpAllowlistItem(item));
      }
    }
  }
  checkAllowlist(ignoreList);
  checkAllowlist(propNameAllowlist);

  // Clean up to avoid leaks:
  doc.head.innerHTML = "";
  doc = null;
  iframe.remove();
  iframe = null;
  win = null;
  hiddenFrame.destroy();
  hiddenFrame = null;
  imageURIsToReferencesMap = null;
  customPropsToReferencesMap = null;
});
