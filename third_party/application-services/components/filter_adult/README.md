# Filter Adult

The Filter Adult module will compare the base domain string of a URL against a
static list of base domains for what are considered "adult sites".

A "base domain" in this context is the same base domain that is returned by
[nsIEffectiveTLDService.getBaseDomain](https://searchfox.org/mozilla-central/rev/85d6bf1b521040c79ed72f3966274a25a2f987c7/netwerk/dns/nsIEffectiveTLDService.idl#54-94).

## Updating the list

`src/adult_set.bin` contains the bytes for the hash set of adult sites. To
update it, you need a local Firefox build.

1. Format the list of domains as a text file with a single hostname per line.
   The list itself is not checked in.

2. Map the hostnames to base domains with `scripts/map-base-domains.js`, which
   runs in `xpcshell` and uses the same lookup as Firefox. See the script for
   usage.

3. Review the `Rewrote` lines in the output to make sure each transformed
   domain should be blocked as a whole. If you change the list, run step 2
   again.

4. From the root of this repository, regenerate `adult_set.bin` from the output
   of step 2:

   ```shell
   cargo run -p filter_adult --features build-binary --bin import-site-list -- path/to/domains.txt components/filter_adult/src/adult_set.bin
   ```

## Tests

Tests are run with

```shell
cargo test -p filter_adult --features build-binary
```

## Bugs

We use Bugzilla to track bugs and feature work. You can use [this link](bugzilla.mozilla.org/enter_bug.cgi?product=Firefox&component=New Tab Page) to file bugs in the `Firefox :: New Tab Page` bug component.
