/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const path = require("path");
const webpack = require("webpack");
const { ResourceUriPlugin } = require("../../tools/resourceUriPlugin");
const { MozSrcUriPlugin } = require("../../tools/mozsrcUriPlugin");

const PATHS = {
  // Where is the entry point for the unit tests?
  testEntryFile: path.resolve(__dirname, "test/unit/unit-entry.js"),

  // A glob-style pattern matching all unit tests
  testFilesPattern: "test/unit/**/*.js",

  // The base directory of all source files (used for path resolution in webpack importing)
  moduleResolveDirectory: __dirname,

  // a RegEx matching all Cu.import statements of local files
  resourcePathRegEx: /^resource:\/\/activity-stream\//,

  coverageReportingPath: "logs/coverage/",
};

// When tweaking here, be sure to review the docs about the execution ordering
// semantics of the preprocessors array, as they are somewhat odd.
const preprocessors = {};
preprocessors[PATHS.testFilesPattern] = [
  "webpack", // require("karma-webpack")
  "sourcemap", // require("karma-sourcemap-loader")
];

module.exports = function (config) {
  const isTDD = config.tdd;
  const browsers = isTDD ? ["Firefox"] : ["FirefoxHeadless"]; // require("karma-firefox-launcher")
  config.set({
    singleRun: !isTDD,
    browsers,
    customLaunchers: {
      FirefoxHeadless: {
        base: "Firefox",
        flags: ["--headless"],
      },
    },
    frameworks: [
      "chai", // require("chai") require("karma-chai")
      "mocha", // require("mocha") require("karma-mocha")
      "sinon", // require("sinon") require("karma-sinon")
    ],
    reporters: [
      "coverage-istanbul", // require("karma-coverage")
      "mocha", // require("karma-mocha-reporter")

      // for bin/try-runner.js to parse the output easily
      "json", // require("karma-json-reporter")
    ],
    jsonReporter: {
      // So this doesn't get interleaved with other karma output
      stdout: false,
      outputFile: path.join("logs", "karma-run-results.json"),
    },
    coverageIstanbulReporter: {
      reports: ["lcov", "text-summary"], // for some reason "lcov" reallys means "lcov" and "html"
      "report-config": {
        // so the full m-c path gets printed; needed for Searchfox integration
        lcov: {
          projectRoot: "../../..",
        },
      },
      dir: PATHS.coverageReportingPath,
      // This will make karma fail if coverage reporting is less than the minimums here
      thresholds: !isTDD && {
        each: {
          statements: 100,
          lines: 100,
          functions: 100,
          branches: 66,
          overrides: {
            "lib/AboutPreferences.sys.mjs": {
              statements: 98,
              lines: 98,
              functions: 94,
              branches: 66,
            },
            /**
             * TelemetryFeed.sys.mjs is tested via an xpcshell test
             */
            "lib/TelemetryFeed.sys.mjs": {
              statements: 10,
              lines: 10,
              functions: 9,
              branches: 0,
            },
            "content-src/lib/init-store.js": {
              statements: 98,
              lines: 98,
              functions: 100,
              branches: 100,
            },
            "lib/DownloadsManager.sys.mjs": {
              statements: 100,
              lines: 100,
              functions: 100,
              branches: 78,
            },
            /**
             * PlacesFeed.sys.mjs is tested via an xpcshell test
             */
            "lib/PlacesFeed.sys.mjs": {
              statements: 7,
              lines: 7,
              functions: 8,
              branches: 0,
            },
            "lib/UTEventReporting.sys.mjs": {
              statements: 100,
              lines: 100,
              functions: 100,
              branches: 75,
            },
            "lib/Screenshots.sys.mjs": {
              statements: 94,
              lines: 94,
              functions: 75,
              branches: 84,
            },
            /**
             * Store.sys.mjs is tested via an xpcshell test
             */
            "lib/Store.sys.mjs": {
              statements: 8,
              lines: 8,
              functions: 0,
              branches: 0,
            },
            /**
             * TopSitesFeed.sys.mjs is tested via an xpcshell test
             */
            "lib/TopSitesFeed.sys.mjs": {
              statements: 9,
              lines: 9,
              functions: 5,
              branches: 0,
            },
            /**
             * TopStoresFeed.sys.mjs is not tested in automation and is slated
             * for eventual removal.
             */
            "lib/TopStoriesFeed.sys.mjs": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            /**
             * WallpaperFeed.sys.mjs is tested via an xpcshell test
             */
            "lib/Wallpapers/WallpaperFeed.sys.mjs": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            "content-src/components/DiscoveryStreamComponents/PersonalizedCard/PersonalizedCard.jsx":
              {
                statements: 0,
                lines: 0,
                functions: 0,
                branches: 0,
              },
            "content-src/components/Base/Base.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            "content-src/components/DiscoveryStreamComponents/FeatureHighlight/FollowSectionButtonHighlight.jsx":
              {
                statements: 88,
                lines: 88,
                functions: 80,
              },
            "content-src/components/DiscoveryStreamComponents/FeatureHighlight/FeatureHighlight.jsx":
              {
                statements: 88,
                lines: 88,
                functions: 80,
              },
            // for all other components in FeatureHighlight that dont have bespoke functionality
            "content-src/components/DiscoveryStreamComponents/FeatureHighlight/!(FeatureHighlight).jsx":
              {
                statements: 0,
                lines: 0,
                functions: 0,
                branches: 0,
              },
            "content-src/components/DiscoveryStreamComponents/ReportContent/ReportContent.jsx":
              {
                statements: 0,
                lines: 0,
                functions: 0,
                branches: 0,
              },
            /**
             * TopicSelection.jsx is tested via an xpcshell test
             */
            "content-src/components/DiscoveryStreamComponents/TopicSelection/*.jsx":
              {
                statements: 0,
                lines: 0,
                functions: 0,
                branches: 0,
              },
            "content-src/components/DiscoveryStreamComponents/InterestPicker/*.jsx":
              {
                statements: 40,
                lines: 39,
                functions: 37,
                branches: 25,
              },
            "content-src/components/DiscoveryStreamComponents/DSCard/DSCard.jsx":
              {
                statements: 94.94,
                lines: 94.84,
                functions: 9.91,
                branches: 70.72,
              },
            "content-src/components/DiscoveryStreamComponents/CardSections/CardSections.jsx":
              {
                statements: 83.11,
                lines: 82.52,
                functions: 79.31,
                branches: 52.8,
              },
            "content-src/components/DiscoveryStreamComponents/SectionContextMenu/SectionContextMenu.jsx":
              {
                statements: 0,
                lines: 0,
                functions: 0,
                branches: 0,
              },
            /**
             * SectionFollowButton.jsx is covered by Jest tests
             */
            "content-src/components/DiscoveryStreamComponents/SectionFollowButton/SectionFollowButton.jsx":
              {
                statements: 0,
                lines: 0,
                functions: 0,
                branches: 0,
              },
            "content-src/components/DiscoveryStreamComponents/AdBanner/AdBanner.jsx":
              {
                statements: 94.12,
                lines: 94.12,
                functions: 75,
                branches: 60,
              },
            "content-src/components/DiscoveryStreamComponents/AdBannerContextMenu/AdBannerContextMenu.jsx":
              {
                statements: 84,
                lines: 84,
                functions: 83,
              },
            "content-src/components/DiscoveryStreamComponents/BriefingCard/BriefingCard.jsx":
              {
                statements: 92.31,
                branches: 66.67,
                functions: 90,
                lines: 94.59,
              },
            /**
             * PromoCard currently has no functionality and is a placeholder component
             */
            "content-src/components/DiscoveryStreamComponents/PromoCard/PromoCard.jsx":
              {
                statements: 0,
                lines: 0,
                functions: 0,
                branches: 0,
              },
            "content-src/components/DiscoveryStreamComponents/**/*.jsx": {
              statements: 80.95,
              lines: 80.95,
              functions: 71.43,
              branches: 70.9,
            },
            /**
             * WallpaperCategories.jsx is tested via an xpcshell test
             */
            "content-src/components/WallpaperCategories/WallpaperCategories.jsx":
              {
                statements: 0,
                lines: 0,
                functions: 0,
                branches: 0,
              },
            /**
             * Notifications.jsx is tested via an xpcshell test
             */
            "content-src/components/Notifications/**/*.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            "content-src/components/Weather/Weather.jsx": {
              statements: 48,
              lines: 49,
              functions: 31.2,
              branches: 31.2,
            },
            "content-src/components/Widgets/WeatherForecast/WeatherForecast.jsx":
              {
                statements: 0,
                lines: 0,
                functions: 0,
                branches: 0,
              },
            // Coverage for this component lives in Jest (test/jest/content-src/components/DiscoveryStreamComponents/InterestPicker.test.jsx)
            "content-src/components/Nova/InterestPicker/InterestPicker.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            // Coverage for this component lives in Jest (test/jest/content-src/components/Widgets/Weather.test.jsx)
            "content-src/components/Widgets/Weather/Weather.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            // Coverage for this component lives in Jest (test/jest/content-src/components/Widgets/WidgetsSidebar.test.jsx)
            "content-src/components/Widgets/WidgetsSidebar.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            // Coverage for this module lives in Jest (test/jest/content-src/components/Widgets/WidgetsRegistry.test.jsx)
            "content-src/components/Widgets/WidgetsRegistry.mjs": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            // Coverage for this component lives in Jest
            "content-src/components/Widgets/WidgetsComponentRegistry.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            // Coverage for this component lives in Jest (test/jest/content-src/components/Widgets/Clocks.test.jsx)
            "content-src/components/Widgets/Clocks/Clocks.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            // Coverage for this component lives in Jest (test/jest/content-src/components/Widgets/AddClockForm.test.jsx)
            "content-src/components/Widgets/Clocks/AddClockForm.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            // Coverage for this component lives in Jest (test/jest/content-src/components/Widgets/EditClocksPanel.test.jsx)
            "content-src/components/Widgets/Clocks/EditClocksPanel.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            // Coverage for this component lives in Jest (test/jest/content-src/components/Widgets/ClocksRow.test.jsx)
            "content-src/components/Widgets/Clocks/ClocksRow.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            // Coverage for this module lives in Jest (test/jest/content-src/components/Widgets/ClocksHelpers.test.jsx)
            "content-src/components/Widgets/Clocks/ClocksHelpers.mjs": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            "content-src/components/Weather/LocationSearch.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            "content-src/components/DiscoveryStreamAdmin/*.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            "content-src/components/CustomizeMenu/SectionsMgmtPanel/*.jsx": {
              statements: 86,
              lines: 76,
              functions: 86,
              branches: 76,
            },
            "content-src/components/CustomizeMenu/ContentSection/*.jsx": {
              statements: 82,
              lines: 82,
              functions: 100,
              branches: 67,
            },
            "content-src/components/CustomizeMenu/**/*.jsx": {
              statements: 68,
              lines: 66,
              functions: 80,
              branches: 16,
            },
            "content-src/components/CustomizeMenu/*.jsx": {
              statements: 98,
              lines: 98,
              functions: 98,
              branches: 98,
            },
            "content-src/lib/link-menu-options.js": {
              statements: 96,
              lines: 96,
              functions: 96,
              branches: 70,
            },
            "content-src/lib/utils.jsx": {
              branches: 60,
              statements: 95.56,
              lines: 95.4,
              functions: 92.86,
            },
            "content-src/components/MessageWrapper/MessageWrapper.jsx": {
              lines: 50,
              statements: 51.1,
              functions: 31.2,
              branches: 31.2,
            },
            "content-src/components/Search/Search.jsx": {
              statements: 38,
              lines: 39,
              functions: 28,
              branches: 25,
            },
            "content-src/components/Widgets/SportsWidget/SportsWidget.jsx": {
              statements: 0,
              lines: 0,
              functions: 0,
              branches: 0,
            },
            "content-src/components/**/*.jsx": {
              statements: 51.1,
              lines: 52.38,
              functions: 31.2,
              branches: 31.2,
            },
          },
        },
      },
    },
    files: [
      // Load React 16 from toolkit vendor for enzyme compatibility, instead of React19
      "../../../toolkit/content/vendor/react/react.js",
      "../../../toolkit/content/vendor/react/react-dom.js",
      "../../../toolkit/content/vendor/react/react-dom-server.js",
      "test/vendor/react-dom-test-utils.js",
      "../../../toolkit/content/vendor/react/prop-types.js",
      "../../../toolkit/content/vendor/react/react-redux.js",
      "../../../toolkit/content/vendor/react/redux.js",
      PATHS.testEntryFile,
    ],
    preprocessors,
    webpack: {
      mode: "none",
      devtool: "inline-source-map",
      // This resolve config allows us to import with paths relative to the root directory, e.g. "lib/ActivityStream.sys.mjs"
      resolve: {
        extensions: [".js", ".jsx", ".mjs"],
        modules: [PATHS.moduleResolveDirectory, "node_modules"],
      },
      plugins: [
        // The ResourceUriPlugin handles translating resource URIs in import
        // statements in .mjs files to paths on the filesystem.
        new ResourceUriPlugin({
          resourcePathRegExes: [
            [new RegExp("^resource://newtab/"), path.join(__dirname, "./")],
            [
              new RegExp("^resource:///modules/asrouter/"),
              path.join(__dirname, "../../components/asrouter/modules/"),
            ],
            [
              new RegExp("^resource:///modules/topsites/"),
              path.join(__dirname, "../../components/topsites/"),
            ],
            [
              new RegExp(
                "^moz-src:///toolkit/components/search/SearchShortcuts.sys.mjs"
              ),
              path.join(
                __dirname,
                "../../../toolkit/components/search/SearchShortcuts.sys.mjs"
              ),
            ],
            [
              new RegExp("^resource:///modules/Dedupe.sys.mjs"),
              path.join(__dirname, "../../modules/Dedupe.sys.mjs"),
            ],
          ],
        }),
        new MozSrcUriPlugin({
          baseDir: path.join(__dirname, "..", "..", ".."),
        }),

        new webpack.DefinePlugin({
          "process.env.NODE_ENV": JSON.stringify("development"),
        }),

        // Replace react-redux imports with React 16-compatible version from toolkit
        new webpack.NormalModuleReplacementPlugin(
          /^react-redux$/,
          path.resolve(
            __dirname,
            "../../../toolkit/content/vendor/react/react-redux.js"
          )
        ),
      ],
      externals: [
        // Use React 16 from toolkit vendor files for enzyme compatibility
        {
          react: "React",
          "react-dom": "ReactDOM",
          "react-dom/client": "ReactDOM",
          "react-dom/server": "ReactDOMServer",
          "react-dom/server.browser": "ReactDOMServer",
          "react-dom/test-utils": "ReactTestUtils",
          "prop-types": "PropTypes",
          "react-redux": "ReactRedux",
          redux: "Redux",
          // enzyme needs these for backwards compatibility with 0.13.
          // see https://github.com/airbnb/enzyme/blob/master/docs/guides/webpack.md#using-enzyme-with-webpack
          "react/addons": true,
          "react/lib/ReactContext": true,
          "react/lib/ExecutionEnvironment": true,
        },
        // Exclude use-sync-external-store completely
        // eslint-disable-next-line consistent-return
        function ({ request }, callback) {
          if (/^use-sync-external-store/.test(request)) {
            return callback(null, "var {}");
          }
          callback();
        },
      ],
      module: {
        rules: [
          {
            test: /\.js$/,
            exclude: [/node_modules\/(?!@fluent\/).*/, /test/],
            loader: "babel-loader",
          },
          {
            test: /\.jsx$/,
            exclude: /node_modules/,
            loader: "babel-loader",
            options: {
              presets: ["@babel/preset-react"],
            },
          },
          {
            test: /\.md$/,
            use: "raw-loader",
          },
          {
            enforce: "post",
            test: /\.js[x]?$/,
            loader: "@jsdevtools/coverage-istanbul-loader",
            options: { esModules: true },
            include: [
              path.resolve("content-src"),
              path.resolve("lib"),
              path.resolve("common"),
            ],
            exclude: [path.resolve("test"), path.resolve("vendor")],
          },
        ],
      },
    },
    // Silences some overly-verbose logging of individual module builds
    webpackMiddleware: { noInfo: true },
  });
};
