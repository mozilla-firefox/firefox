# Enabling Rules and Adding Plugins to ESLint

This guide is intended to give helpful pointers on how to enable rules and add
plugins to our ESLint configuration.

```{contents}
:local: true
```

## General Notes

- Enabling of new rules and adding plugins should happen in agreement with the
  [JavaScript Usage, Tools and Style module owner and peers](/mots/index.md#javascript-usage-tools-and-style).

- Enabling of rules for sub-components should also be discussed with the owner
  and peers.

  > - Generally we wish to harmonize rules across the entire code base, and so
  >   would prefer to avoid specialisms for different sub-components.
  > - Exceptions may be made on a sub-component basis.

## Enabling a New Rule

The general process for enabling new rules is to file a bug under the
`Developer Infrastructure` product in the `Lint and Formatting` component.

The rule should then be added to the relevant configurations and existing issues
fixed. For large amounts of existing issues, we may do a staged roll-out
as discussed below.

### Options for Roll-Outs

For rolling out new rules, we prefer that there is a plan and owner for ensuring
the existing failures are resolved over time. They do not always need to be fixed
immediately, but there should be some agreement as to how existing failures
are addressed, so that we do not end up with a large, potentially complicated
set of exclusions, or significant amounts of warnings that never get addressed.

This is not to say the developer adding the rule needs to be the owner of the
plan, but they should ensure that there is an agreed way forward.

There are several options available for roll-outs, depending on how many
errors are found and how much work it is to fix existing issues.

- Fix any issues and enable the rule everywhere

  > - This is most suited to cases where there are a small amount of errors which
  >   are easy to fix up-front

- Enable the rule everywhere, selectively disabling the rule on existing failures

  > - This may be appropriate for cases where fixing the failures may take
  >   a bit longer.

- Enable the rule as a warning

  > - This will raise issues as warnings, which will not prevent patches from
  >   landing with issues, but should at least highlight them during code review.
  > - This may be more appropriate in situations where there are a large amount
  >   of issues that are non-critical, such as preferring use of one method over
  >   another.

- Enable the rule as an error on passing code, but a warning on code with failures

  > - This is a hybrid approach which is suited to cases where there is an issue
  >   that is more critical, and we want to stop new cases making it into the tree,
  >   and highlight the existing cases if the code gets touched.

The options here are not firmly set, the list should be used as a guide.

### Where to Add

New rules should be added in one of the configurations in
{searchfox}`eslint-plugin-mozilla <tools/lint/eslint/eslint-plugin-mozilla/lib/configs>`.

These will then automatically be applied to the relevant places.
eslint-plugin-mozilla is used by a few projects outside of the Firefox repository,
so they will pick up the rule addition when eslint-plugin-mozilla is next released.

Where existing failures are disabled/turned to warnings, these should be handled
in the {searchfox}`top-level .eslintrc-rollouts.js file <..eslintrc-rollouts.js>`,
and follow-up bugs must be filed before landing and referenced in the appropriate
sections. The follow-up bugs should block
[bug 1596191](https://bugzilla.mozilla.org/show_bug.cgi?id=1596191)

## Adding a New ESLint Plugin

### License checks

When a new plugin is proposed, it should be checked to ensure that the licenses
of the node module and all dependent node modules are compatible with the Mozilla
code base. Mozilla employees can consult the
[Licensing & Contributor Agreements Runbook](https://mozilla-hub.atlassian.net/l/cp/bgfp6Be7)
for more details.

A site such as [npmgraph](https://npmgraph.js.org/) can help with checking
licenses.

When filing the bug or reviewing a patch, it should be stated if the module
has passed the license checks.

### Adding to the Repository

If the new plugin is going to have rules defined within a configuration within
eslint-plugin-mozilla, then the module should be referenced in the peer
dependencies of
{searchfox}`eslint-plugin-mozilla's package.json<tools/lint/eslint/eslint-plugin-mozilla/package.json>`
file.

To add the new module to the node system, run:

```shell
./mach npm install --save-exact --save-dev packagename
```

We use exact version matching to make it explicit about the version we are using
and when we upgrade the versions.

The plugin can then be used with ESLint in the
[normal way](https://eslint.org/docs/latest/use/configure/plugins).
