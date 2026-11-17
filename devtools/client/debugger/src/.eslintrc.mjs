/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

import globals from "globals";
import react from "eslint-plugin-react";
import mozilla from "eslint-plugin-mozilla";
import { importX } from "eslint-plugin-import-x";
import jestPlugin from "eslint-plugin-jest";

export default [
  {
    files: ["**/tests/**"],
    ...jestPlugin.configs["flat/recommended"],
  },
  {
    plugins: { jest: jestPlugin, react, mozilla, "import-x": importX },
    languageOptions: {
      parserOptions: {
        ecmaVersion: "latest",
        ecmaFeatures: { jsx: true },
      },
      sourceType: "module",
      globals: {
        ...globals.es6,
        ...globals.browser,
        ...globals.commonjs,
        ...globals.jest,
        atob: true,
        btoa: true,
        Cc: true,
        Ci: true,
        Components: true,
        console: true,
        Cr: true,
        Cu: true,
        devtools: true,
        dump: true,
        EventEmitter: true,
        isWorker: true,
        loader: true,
        Services: true,
        Task: true,
        XPCOMUtils: true,
        _Iterator: true,
        __dirname: true,
        process: true,
        global: true,
        L10N: true,
      },
    },
    rules: {
      // These are the rules that have been configured so far to match the
      // devtools coding style.

      // Rules from the mozilla plugin
      "mozilla/mark-test-function-used": "error",
      "mozilla/no-aArgs": "error",
      // See bug 1224289.
      "mozilla/reject-importGlobalProperties": "error",
      "mozilla/var-only-at-top-level": "error",

      // Rules from the React plugin
      "react/jsx-uses-react": "error",
      "react/jsx-uses-vars": "error",
      "react/no-danger": "error",
      "react/no-did-mount-set-state": "error",
      "react/no-did-update-set-state": "error",
      "react/no-direct-mutation-state": "error",
      "react/no-unknown-property": "error",
      "react/prop-types": "off",
      "react/sort-comp": [
        "error",
        {
          order: ["propTypes", "everything-else", "render"],
        },
      ],

      // Check for import errors.
      "import-x/no-duplicates": "error",
      "import-x/named": "error",
      "import-x/export": "error",

      // Adds compatibility with jest-in-case cases. See related GitHub issue
      // https://github.com/jest-community/eslint-plugin-jest/issues/534
      "jest/no-standalone-expect": [
        "error",
        { additionalTestBlockFunctions: ["cases"] },
      ],

      // Disallow flow control that escapes from "finally".
      "no-unsafe-finally": "error",

      // Disallow using variables outside the blocks they are defined (especially
      // since only let and const are used, see "no-var").
      "block-scoped-var": 2,
      // Require camel case names
      camelcase: ["error", { properties: "never" }],
      // Warn about cyclomatic complexity in functions.
      complexity: ["error", { max: 23 }],
      // Don't warn for inconsistent naming when capturing this (not so important
      // with auto-binding fat arrow functions).
      "consistent-this": 0,
      // Don't require a default case in switch statements. Avoid being forced to
      // add a bogus default when you know all possible cases are handled.
      "default-case": 0,
      // Encourage the use of dot notation whenever possible.
      "dot-notation": 2,
      // Allow using == instead of ===, in the interest of landing something since
      // the devtools codebase is split on convention here.
      eqeqeq: 0,
      // Don't require function expressions to have a name.
      // This makes the code more verbose and hard to read. Our engine already
      // does a fantastic job assigning a name to the function, which includes
      // the enclosing function name, and worst case you have a line number that
      // you can just look up.
      "func-names": 0,
      // Allow use of function declarations and expressions.
      "func-style": 0,
      // Deprecated, will be removed in 1.0.
      "global-strict": 0,
      // Only useful in a node environment.
      "handle-callback-err": 0,
      // Maximum depth callbacks can be nested.
      "max-nested-callbacks": [2, 4],
      // Don't limit the number of parameters that can be used in a function.
      "max-params": 0,
      // Don't limit the maximum number of statement allowed in a function. We
      // already have the complexity rule that's a better measurement.
      "max-statements": 0,
      // Require a capital letter for constructors, only check if all new
      // operators are followed by a capital letter. Don't warn when capitalized
      // functions are used without the new operator.
      "new-cap": [2, { capIsNew: false }],
      // Disallow use of the Array constructor.
      "no-array-constructor": 2,
      // Allow use of bitwise operators.
      "no-bitwise": 0,
      // Disallow use of arguments.caller or arguments.callee.
      "no-caller": 2,
      // Disallow the catch clause parameter name being the same as a variable in
      // the outer scope, to avoid confusion.
      "no-catch-shadow": 2,
      // Disallow assignment in conditional expressions.
      "no-cond-assign": 2,
      // Allow use of the continue statement.
      "no-continue": 0,
      // Disallow control characters in regular expressions.
      "no-control-regex": 2,
      // Disallow use of debugger.
      "no-debugger": 2,
      // Disallow deletion of variables (deleting properties is fine).
      "no-delete-var": 2,
      // Allow division operators explicitly at beginning of regular expression.
      "no-div-regex": 0,
      // Disallow duplicate arguments in functions.
      "no-dupe-args": 2,
      // Disallow a duplicate case label.
      "no-duplicate-case": 2,
      // Disallow else after a return in an if. The else around the second return
      // here is useless:
      // if (something) { return false; } else { return true; }
      "no-else-return": 2,
      // Disallow empty statements. This will report an error for:
      // try { something(); } catch (e) {}
      // but will not report it for:
      // try { something(); } catch (e) { /* Silencing the error because ...*/ }
      // which is a valid use case.
      "no-empty": 2,
      // Disallow the use of empty character classes in regular expressions.
      "no-empty-character-class": 2,
      // Disallow use of labels for anything other then loops and switches.
      "no-labels": 2,
      // Disallow use of eval(). We have other APIs to evaluate code in content.
      "no-eval": 2,
      // Disallow assigning to the exception in a catch block.
      "no-ex-assign": 2,
      // Disallow adding to native types
      "no-extend-native": 2,
      // Disallow unnecessary function binding.
      "no-extra-bind": 2,
      // Disallow double-negation boolean casts in a boolean context.
      "no-extra-boolean-cast": 2,
      // Deprecated, will be removed in 1.0.
      "no-extra-strict": 0,
      // Disallow comments inline after code.
      "no-inline-comments": 2,
      // Disallow if as the only statement in an else block.
      "no-lonely-if": 2,
      // Allow mixing regular variable and require declarations (not a node env).
      "no-mixed-requires": 0,
      // Disallow use of multiline strings (use template strings instead).
      "no-multi-str": 2,
      "prefer-template": "error",
      "prefer-const": [
        "error",
        {
          destructuring: "all",
          ignoreReadBeforeAssign: false,
        },
      ],
      // Disallow reassignments of native objects.
      "no-native-reassign": 2,
      // Disallow nested ternary expressions, they make the code hard to read.
      "no-nested-ternary": 2,
      // Allow use of new operator with the require function.
      "no-new-require": 0,
      // Disallow use of octal literals.
      "no-octal": 2,
      // Allow reassignment of function parameters.
      "no-param-reassign": 0,
      // Allow string concatenation with __dirname and __filename (not a node env).
      "no-path-concat": 0,
      // Allow use of unary operators, ++ and --.
      "no-plusplus": 0,
      // Allow using process.env (not a node environment).
      "no-process-env": 0,
      // Allow using process.exit (not a node environment).
      "no-process-exit": 0,
      // Disallow usage of __proto__ property.
      "no-proto": 2,
      // Disallow multiple spaces in a regular expression literal.
      "no-regex-spaces": 2,
      // Don't restrict usage of specified node modules (not a node environment).
      "no-restricted-modules": 0,
      // Disallow use of assignment in return statement. It is preferable for a
      // single line of code to have only one easily predictable effect.
      "no-return-assign": 2,
      // Allow use of javascript: urls.
      "no-script-url": 0,
      // Disallow comparisons where both sides are exactly the same.
      "no-self-compare": 2,
      // Disallow use of comma operator.
      "no-sequences": 2,
      // Disallow sparse arrays, eg. let arr = [,,2].
      // Array destructuring is fine though:
      // for (let [, breakpointPromise] of aPromises)
      "no-sparse-arrays": 2,
      // Allow use of synchronous methods (not a node environment).
      "no-sync": 0,
      // Allow the use of ternary operators.
      "no-ternary": 0,
      // Disallow throwing literals (eg. throw "error" instead of
      // throw new Error("error")).
      "no-throw-literal": 2,
      // Disallow use of undeclared variables unless mentioned in a /*global */
      // block. Note that globals from head.js are automatically imported in tests
      // by the import-headjs-globals rule form the mozilla eslint plugin.
      "no-undef": 2,
      // Allow dangling underscores in identifiers (for privates).
      "no-underscore-dangle": 0,
      // Allow use of undefined variable.
      "no-undefined": 0,
      // Disallow the use of Boolean literals in conditional expressions.
      "no-unneeded-ternary": 2,
      // Disallow unreachable statements after a return, throw, continue, or break
      // statement.
      "no-unreachable": 2,
      // Disallow global and local variables that arent used. Allow unused function arguments
      // that are prefixed with `_`.
      "no-unused-vars": [
        2,
        { vars: "all", caughtErrors: "none", argsIgnorePattern: "^_" },
      ],
      // Allow using variables before they are defined.
      "no-use-before-define": 0,
      // We use var-only-at-top-level instead of no-var as we allow top level
      // vars.
      "no-var": 0,
      // Allow using TODO/FIXME comments.
      "no-warning-comments": 0,
      // Allow more than one variable declaration per function.
      "one-var": 0,
      // Require use of the second argument for parseInt().
      radix: 2,
      // Dont require to sort variables within the same declaration block.
      // Anyway, one-var is disabled.
      "sort-vars": 0,
      // Require "use strict" to be defined globally in the script.
      strict: [2, "global"],
      // Disallow comparisons with the value NaN.
      "use-isnan": 2,
      // Ensure that the results of typeof are compared against a valid string.
      "valid-typeof": 2,
      // Allow vars to be declared anywhere in the scope.
      "vars-on-top": 0,
      // Disallow Yoda conditions (where literal value comes first).
      yoda: 2,

      // And these are the rules that haven't been discussed so far, and that are
      // disabled for now until we introduce them, one at a time.

      // Require for-in loops to have an if statement.
      "guard-for-in": 0,
      // allow/disallow an empty newline after var statement
      "newline-after-var": 0,
      // disallow the use of alert, confirm, and prompt
      "no-alert": 0,
      // disallow comparisons to null without a type-checking operator
      "no-eq-null": 0,
      // disallow overwriting functions written as function declarations
      "no-func-assign": 0,
      // disallow use of eval()-like methods
      "no-implied-eval": 0,
      // disallow function or variable declarations in nested blocks
      "no-inner-declarations": 0,
      // disallow invalid regular expression strings in the RegExp constructor
      "no-invalid-regexp": 0,
      // disallow irregular whitespace outside of strings and comments
      "no-irregular-whitespace": 0,
      // disallow labels that share a name with a variable
      "no-label-var": 0,
      // disallow unnecessary nested blocks
      "no-lone-blocks": 0,
      // disallow creation of functions within loops
      "no-loop-func": 0,
      // disallow negation of the left operand of an in expression
      "no-negated-in-lhs": 0,
      // disallow use of new operator when not part of the assignment or
      // comparison
      "no-new": 0,
      // disallow use of new operator for Function object
      "no-new-func": 0,
      // disallow use of the Object constructor
      "no-new-object": 0,
      // disallows creating new instances of String,Number, and Boolean
      "no-new-wrappers": 0,
      // disallow the use of object properties of the global object (Math and
      // JSON) as functions
      "no-obj-calls": 0,
      // disallow use of octal escape sequences in string literals, such as
      // var foo = "Copyright \251";
      "no-octal-escape": 0,
      // disallow use of undefined when initializing variables
      "no-undef-init": 0,
      // disallow usage of expressions in statement position
      "no-unused-expressions": 0,
      // disallow use of void operator
      "no-void": 0,
      // require assignment operator shorthand where possible or prohibit it
      // entirely
      "operator-assignment": 0,
    },
    settings: {
      jest: {
        // Keep in sync with "jest" version from debugger's package.json
        version: 29,
      },
    },
  },
];
