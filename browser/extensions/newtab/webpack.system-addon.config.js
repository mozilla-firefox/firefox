/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const path = require("path");

const absolute = relPath => path.join(__dirname, relPath);

const nodeModules = process.env.MOZ_NODE_MODULES
  ? path.resolve(process.env.MOZ_NODE_MODULES)
  : absolute("node_modules");
const vendored = name => path.join(nodeModules, ...name.split("/"));

const webpack = require(vendored("webpack"));
const MinimizerPlugin = require(vendored("minimizer-webpack-plugin"));
const { ResourceUriPlugin } = require("../../tools/resourceUriPlugin");
const { MozSrcUriPlugin } = require("../../tools/mozsrcUriPlugin");

const baseConfig = env => ({
  mode: "none",
  devtool: env.development ? "inline-source-map" : false,
  module: {
    rules: [
      {
        test: /\.jsx?$/,
        exclude: /node_modules[\\/]/,
        loader: vendored("babel-loader"),
        options: {
          presets: [vendored("@babel/preset-react")],
        },
      },
      {
        // webpack 5 enforces fully-specified paths for ESM imports; disable for
        // .mjs so that bare specifiers like "react-dom/server.browser" resolve.
        test: /\.mjs$/,
        resolve: { fullySpecified: false },
      },
    ],
  },
  resolve: {
    extensions: [".js", ".jsx", ".mjs"],
    modules: [nodeModules, "."],
  },
  resolveLoader: {
    modules: [nodeModules],
  },
});

class DependencyListPlugin {
  constructor(outputPath) {
    this.outputPath = outputPath;
  }

  apply(compiler) {
    compiler.hooks.done.tap("DependencyListPlugin", stats => {
      const deps = [...stats.compilation.fileDependencies].sort();
      require("fs").appendFileSync(this.outputPath, `${deps.join("\n")}\n`);
    });
  }
}

const dependencyListPlugins = () =>
  process.env.MOZ_WEBPACK_DEPS
    ? [new DependencyListPlugin(process.env.MOZ_WEBPACK_DEPS)]
    : [];

const vendorOptimization = {
  minimize: true,
  minimizer: [
    new MinimizerPlugin({
      extractComments: false,
      terserOptions: {
        format: {
          comments: /THIS FILE IS AUTO-GENERATED/,
        },
      },
    }),
  ],
};

module.exports = (env = {}) => {
  // When invoked from the Firefox build system, outputPath is set to the objdir
  // so the bundles are generated there rather than in the source tree.
  const contentPath = env.outputPath
    ? path.resolve(env.outputPath)
    : absolute("data/content");

  return [
    // Vendor bundle with React
    Object.assign({}, baseConfig(env), {
      name: "vendor",
      entry: absolute("content-src/vendor.mjs"),
      output: {
        path: contentPath,
        filename: "vendor.bundle.js",
      },
      devtool: false,
      optimization: vendorOptimization,
      plugins: [
        new webpack.DefinePlugin({
          "process.env.NODE_ENV": JSON.stringify(
            env.development ? "development" : "production"
          ),
        }),
        new webpack.BannerPlugin(
          `THIS FILE IS AUTO-GENERATED: ${path.basename(__filename)}`
        ),
        ...dependencyListPlugins(),
      ],
    }),
    // Activity stream bundle (uses vendor as externals)
    Object.assign({}, baseConfig(env), {
      name: "activity-stream",
      entry: absolute("content-src/activity-stream.jsx"),
      output: {
        path: contentPath,
        filename: "activity-stream.bundle.js",
        library: {
          name: "NewtabRenderUtils",
          type: "var",
        },
      },
      externalsType: "window",
      externals: {
        react: "React",
        "react-dom": "ReactDOM",
        "react-dom/client": {
          root: "ReactDOM",
          commonjs: "react-dom/client",
          commonjs2: "react-dom/client",
        },
        "react-dom/server.browser": {
          root: "ReactDOMServer",
          commonjs: "react-dom/server.browser",
          commonjs2: "react-dom/server.browser",
        },
        "prop-types": "PropTypes",
        "react-transition-group": "ReactTransitionGroup",
        "react-redux": "ReactRedux",
        redux: "Redux",
      },
      plugins: [
        new webpack.DefinePlugin({
          "process.env.NODE_ENV": JSON.stringify(
            env.development ? "development" : "production"
          ),
        }),
        new ResourceUriPlugin({
          resourcePathRegExes: [
            [new RegExp("^resource://newtab/"), path.join(__dirname, "./")],
            [
              new RegExp("^resource:///modules/Dedupe.sys.mjs"),
              path.join(__dirname, "../../modules/Dedupe.sys.mjs"),
            ],
          ],
        }),
        new MozSrcUriPlugin({
          baseDir: path.join(__dirname, "..", "..", ".."),
        }),
        new webpack.BannerPlugin(
          `THIS FILE IS AUTO-GENERATED: ${path.basename(__filename)}`
        ),
        new webpack.optimize.ModuleConcatenationPlugin(),
        ...dependencyListPlugins(),
      ],
    }),
  ];
};
