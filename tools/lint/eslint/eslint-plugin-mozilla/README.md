# eslint-plugin-mozilla

A collection of rules that help enforce JavaScript coding standard in the Mozilla project.

These are primarily developed and used within the Firefox build system ([mozilla-central](https://github.com/mozilla-firefox/firefox)), but are made available for other related projects to use as well.

## Usage

### Within mozilla-central:

```sh
$ ./mach eslint --setup
```

### Outside mozilla-central:

Install ESLint [ESLint](http://eslint.org):

```sh
$ npm i eslint --save-dev
```

Next, install `eslint-plugin-mozilla`:

```sh
$ npm install eslint-plugin-mozilla --save-dev
```

#### Flat config

```js
import mozilla from "eslint-plugin-mozilla"

export default [
  ...mozilla.configs["flat/recommended"];
]
```

The recommended configuration does not set up globals for all files. It
only sets the globals in the environment for Mozilla specific files, e.g. system
modules, sjs files and workers.

If you use some of the other configurations, note that they are objects rather
than arrays.

#### Legacy Configuration

```js
{
  "extends": ["plugin:mozilla/recommended"]
}
```

#### Notes

If you use prettier in your setup, you may need to extend from eslint-config-prettier
to ensure that any rules that conflict with prettier are disabled.
See [here for more information](https://github.com/prettier/eslint-config-prettier?tab=readme-ov-file#installation).

## Documentation

For details about the rules, please see the [firefox documentation page](http://firefox-source-docs.mozilla.org/tools/lint/linters/eslint-plugin-mozilla.html).

## Source Code

The sources can be found at:

* Code: https://searchfox.org/firefox-main/source/tools/lint/eslint/eslint-plugin-mozilla
* Documentation: https://searchfox.org/firefox-main/source/docs/code-quality/lint/linters

## Bugs

Please file bugs in Bugzilla in the Lint and Formatting component of the Developer Infrastructure product.

* [Existing bugs](https://bugzilla.mozilla.org/buglist.cgi?resolution=---&list_id=17273428&product=Developer%20Infrastructure&query_format=advanced&component=Lint%20and%20Formatting)
* [New bugs](https://bugzilla.mozilla.org/enter_bug.cgi?component=Lint%20and%20Formatting&product=Developer%20Infrastructure)

## Tests

The tests can only be run from within mozilla-central. To run the tests:

```sh
$ ./mach npm --prefix tools/lint/eslint/eslint-plugin-mozilla ci
$ ./mach npm --prefix tools/lint/eslint/eslint-plugin-mozilla test
```
