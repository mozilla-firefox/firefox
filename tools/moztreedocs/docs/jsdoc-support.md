# jsdoc support

## ESLint support

If you are looking to enable JSDoc generation for your documents, please also
enable the ESLint plugin for JSDoc if it is not already enabled.

In the top-level {searchfox}`eslint-rollouts.config.mjs file <eslint-rollouts.config.mjs>`
there are currently two sections where the `valid-jsdoc` and `require-jsdoc`
rules are enabled. Please check that your component is not excluded from these
sections. If it is, you should remove the exclusion and fix any instances that
are raised by running

```shell
./mach eslint path/to/component/
```

## Enabling JSDoc generation

Here is a quick example, for the public AddonManager {doc}`API </toolkit/mozapps/extensions/addon-manager/AddonManager>`

To use it for your own code:

1. Check that JSDoc generates the output you expect (you may need to use a @class annotation on "object initializer"-style class definitions for instance)
2. Create an `.md` file, which may contain explanatory text as well as the API docs. The minimum will look something like
   {searchfox}`this <toolkit/mozapps/extensions/docs/AddonManager.md>`.
3. Ensure your component is on the js_source_path here in the sphinx
   config: <https://github.com/mozilla-firefox/firefox/blob/966b5519aeda8c361b13c6b2d3a8021ea412bcc5/tools/docs/conf.py#L46>
4. Run `mach doc` locally to generate the output and confirm that it looks correct.
