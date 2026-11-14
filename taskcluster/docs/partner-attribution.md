# Partner attribution

(partner-attribution-1)=

In contrast to {doc}`partner-repacks`, attributed builds only differ from the normal Firefox
builds by the adding a string in the dummy windows signing certificate. We support doing this for:

- Windows stub installers,
- Windows full installers,
- macOS DMGs.

The parameters of the string are carried into the telemetry system, tagging an install into
a cohort of users. This a lighter weight process because we don't repackage or re-sign the builds.

## Parameters & Scheduling

Partner attribution uses a number of parameters to control how they work:

- `release_enable_partner_attribution`
- `release_partner_config`
- `release_partner_build_number`
- `release_partners`

The enable parameter is a boolean, a simple on/off switch. We set it in shipit's
[is_partner_enabled()](https://github.com/mozilla-releng/shipit/blob/046826e96999bde08fdc33cce03c4576b89d8904/api/src/shipit_api/admin/release.py#L77)
when starting a release. It's true for Firefox betas >= b5 and releases, but otherwise false, the
same as partner repacks.

`release_partner_config` is a dictionary of configuration data which drives the task generation
logic. It's usually looked up during the release promotion action task, using the Github
GraphQL API in the `gecko_taskgraph.util.partners.get_partner_config_by_url()` function, with the
url defined in [taskcluster/config.yml](https://searchfox.org/mozilla-central/search?q=partner-urls&path=taskcluster%2Fconfig.yml&case=true&regexp=false&redirect=true).

`release_partner_build_number` is an integer used to create unique upload paths in the firefox
candidates directory, while `release_partners` is a list of partners that should be
attributed (i.e. a subset of the whole config). Both are intended for use when respinning a partner after
the regular Firefox has shipped. More information on that can be found in the
[RelEng Docs](https://moz-releng-docs.readthedocs.io/en/latest/procedures/misc-operations/off-cycle-partner-repacks-and-funnelcake.html).

`release_partners` is shared with partner repacks but we don't support doing both at the same time.

## Configuration

This is done using an `attribution_config.yml` file which next lives to the `default.xml` used
for partner repacks. There are no repos for each partner, the whole configuration exists in the one
file because the amount of information to be tracked is much smaller.

An example config looks like this:

```yaml
defaults:
  medium: distribution
  source: mozilla
configs:
- campaign: sample
  content: sample-001
  locales:
    - en-US
    - de
    - ru
  platforms:
    - macosx64-shippable
    - win32-shippable
  upload_to_candidates: true
  publish_to_releases: true
```

The four main parameters are `medium, source, campaign, content`, of which the first two are
common to all attributions. The combination of `campaign` and `content` should be unique
to avoid confusion in telemetry data. They correspond to the repo name and sub-directory in partner repacks,
so avoid any overlap between values in partner repacks and atrribution.
The optional parameters of `variation`, and `experiment` may also be specified.

Non-empty lists of locales and platforms are required parameters (NB the `-shippable` suffix should be used on
the platforms).

The Firefox installers are uploaded into the [candidates directory](https://archive.mozilla.org/pub/firefox/candidates/) when `upload_to_candidates` is set to true and
into the [release directory](https://archive.mozilla.org/pub/firefox/releases/partners/) when
`publish_to_releases` is too.

## Verifying attribution

To verify that a Firefox install was correctly attributed, navigate to
`about:telemetry#search=attribution` in the browser. The following fields should
be populated with values matching the `attribution_config.yml`:

- `attribution.medium`
- `attribution.source`
- `attribution.campaign`
- `attribution.content`

## Repacking process

Attribution only works with these type of tasks:

- attribution - add attribution code to the regular builds
- beetmover - move the files to a partner-specific destination
- bouncer - create partner-attributed links that point to the latest version.

### Attribution

- kind: `release-partner-attribution`

There is one task per OS.

On Windows, [python/mozrelease/mozrelease/attribute_builds.py](https://searchfox.org/firefox-main/source/python/mozrelease/mozrelease/attribute_builds.py)
handles the attribution. Since [bug 1943640](https://bugzilla.mozilla.org/show_bug.cgi?id=1943640),
only the stub installer is attributed: it carries the attribution code and passes it to the
installed Firefox. No attributed full installer is generated. The stub installer is a 32-bit
binary and is therefore shipped under the `win32` folder. The `ATTRIBUTION_CONFIG`
environment variable controls the script. The size of `ATTRIBUTION_CONFIG` variable may grow
large if the number of configurations increases, and it may be necessary to pass the content of
`attribution_config.yml` to the script instead, or via an artifact of the promotion task.

On macOS, the [dmg attribute](https://github.com/mozilla/libdmg-hfsplus/blob/d6287b5afc2406b398de42f74eba432f2123b937/dmg/dmg.c#L133)
binary handles the attribution. Unlike Windows, `ATTRIBUTION_CONFIG` is not used. Instead,
the payload is passed as a CLI argument.

### Beetmover

- kinds: `release-partner-attribution-beetmover`, `release-beetmover-push-to-release`

The first kind moves and renames the artifacts to their public location in the [candidates directory](https://archive.mozilla.org/pub/firefox/candidates/).

Each task will have the `project:releng:beetmover:action:push-to-partner` and
`project:releng:beetmover:bucket:release` scopes. There's a partner-specific
code path in [beetmoverscript](https://github.com/mozilla-releng/scriptworker-scripts/tree/master/beetmoverscript).

The second kind moves it under the release directory. In the case of partner builds, this is
[pub/firefox/releases/partners/](https://archive.mozilla.org/pub/firefox/releases/partners/).

### Bouncer

- kinds: `release-partner-repack-bouncer-sub` and `release-bouncer-aliases`.

`release-partner-repack-bouncer-sub` creates the bouncer entries for both partner repacks
and partner attributed builds. `release-bouncer-aliases` creates/updates the aliases of
all type of builds (standard Firefox, partner repacks, partner attributed builds) to point
to their latest bouncer entry.
