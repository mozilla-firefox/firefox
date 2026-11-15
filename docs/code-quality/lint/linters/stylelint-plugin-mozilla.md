# Mozilla Stylelint Plugin

This is the documentation of Mozilla Stylelint Plugin.

## Rules

The plugin implements the following rules:

```{toctree}
:glob: true
:maxdepth: 1

stylelint-plugin-mozilla/rules/*
```

## Tests

The tests for stylelint-plugin-mozilla are written using
[stylelint-test-rule-node] as recommended by the [Stylelint docs].

### Running Tests

The stylelint plugin has some self tests, these can be run via:

```shell
cd tools/lint/stylelint/stylelint-plugin-mozilla
npm ci
npm run test
```

The tests include a check that the `use-design-tokens` rule's
[checked properties](stylelint-plugin-mozilla/rules/use-design-tokens/checked-properties.md)
page matches `config.mjs`. After changing `config.mjs`, regenerate the page with
`npm run update-docs`.

[stylelint docs]: https://stylelint.io/developer-guide/plugins#testing
[stylelint-test-rule-node]: https://www.npmjs.com/package/stylelint-test-rule-node
