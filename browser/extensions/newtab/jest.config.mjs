/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

export default {
  testEnvironment: "jsdom",
  moduleDirectories: ["node_modules", "<rootDir>"],
  transform: {
    "^.+\\.m?jsx?$": [
      "babel-jest",
      {
        presets: [
          ["@babel/preset-env", { targets: { node: "current" } }],
          ["@babel/preset-react", { runtime: "automatic" }],
        ],
      },
    ],
  },
  moduleNameMapper: {
    "^resource://newtab/(.*)$": "<rootDir>/$1",
    "^resource:///modules/asrouter/(.*)$":
      "<rootDir>/../../components/asrouter/modules/$1",
    "^resource:///modules/(.*)$": "<rootDir>/../../modules/$1",
    "^moz-src:///(.*)$": "<rootDir>/../../../$1",
  },
  setupFilesAfterEnv: ["<rootDir>/test/jest/jest-setup.mjs"],
  testMatch: [
    "<rootDir>/test/jest/**/*.test.js",
    "<rootDir>/test/jest/**/*.test.jsx",
  ],
  collectCoverageFrom: [
    "content-src/**/*.jsx",
    "content-src/**/*.js",
    "!content-src/**/*.test.*",
  ],
  // projectRoot makes lcov paths relative to the repository root, which the
  // code coverage bot and Searchfox need.
  coverageReporters: [["lcov", { projectRoot: "../../.." }], "text-summary"],
  coverageDirectory: "logs/coverage",
};
