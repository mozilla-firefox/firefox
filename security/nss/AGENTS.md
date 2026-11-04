This file provides guidance to LLMs and agents when working with the code in this repository.

## Build

NSS uses GYP + Ninja as its primary build system, driven by `build.sh` (or the `mach` wrapper):

**Always build with `--dist`.** The default output directory, `../dist`, is outside the checkout, so
checkouts that sit side by side share it and silently overwrite each other's libraries and binaries.
Give each checkout its own directory and export it as `DIST`, which `tests/all.sh` and every `mach` subcommand
also read:

```sh
export DIST=$PWD/../dist-$(basename $PWD)
./build.sh --dist=$DIST          # debug build → $DIST/Debug/
./build.sh --dist=$DIST -o       # optimized build → $DIST/Release/
./build.sh --dist=$DIST -c       # clean + build
./mach build                     # equivalent wrapper; passes --dist=$DIST itself
```

Common flags:
- `--asan` / `--msan` / `--ubsan` — sanitizer builds
- `--fuzz` / `--fuzz=tls` — fuzzing builds
- `--enable-fips` — FIPS-140 mode
- `--disable-tests` — skip building test binaries
- `-t <arch>` — cross-compile (x64, ia32, aarch64, …)

Output lands in `out/[Debug|Release]/` (build artifacts) and `$DIST/[Debug|Release]/` (headers, libs, binaries).

A legacy Make-based build (`make nss_build_all`) also exists via `coreconf/` but GYP is preferred for new work.

## Tests

```sh
cd tests && ./all.sh                          # full test suite
NSS_TESTS=ssl_gtests ./all.sh                 # single suite
NSS_TESTS=ssl_gtests NSS_CYCLES=standard ./all.sh  # skip stress cycles
./mach tests ssl_gtests                       # mach wrapper
```

Available suites: `cipher`, `ssl`, `ssl_gtests`, `gtests`, `cert`, `smime`, `fips`, `ec`, `bogo`, `interop`, `policy`, and others. See `tests/all.sh` for the full list.

GTest binaries require a certificate database. Helper scripts create one, then you run the binary.
Set `LD_LIBRARY_PATH` first so the binaries (and `certutil`) load the freshly built libraries
rather than a system NSS:
```sh
export LD_LIBRARY_PATH=$DIST/Debug/lib

# SSL gtests
./tests/ssl_gtests/ssl_gtest_db.sh ./ssl_gtest_certdb $DIST/Debug/bin/certutil
$DIST/Debug/bin/ssl_gtest -d ./ssl_gtest_certdb

# Other gtests (pk11_gtest, freebl_gtest, der_gtest, ...)
./tests/gtests/gtest_db.sh ./gtest_certdb $DIST/Debug/bin/certutil
$DIST/Debug/bin/pk11_gtest -d ./gtest_certdb -s $PWD/gtests/pk11_gtest -w
```

`tests/gtests/gtests.sh` passes two extra flags to every gtest binary except `ssl_gtest`; do the same
when running them by hand:
- `-s <source>/gtests/<binary>`: test-vector (e.g. Wycheproof) lookups are relative to this. Without it
  those tests fail with "error opening vectors".
- `-w`: open the database read-write. Tests that create token objects fail without it.

`ssl_gtest` has its own `main()` and takes only `-d`.

## Code formatting and linting

```sh
./mach clang-format                # format changed files
./mach clang-format path/to/file.c # format specific file
./mach clang-tidy                  # static analysis
./mach clang-tidy --fix            # auto-fix where possible
```

## Code style

Limit the amount of comments you put in the code to a strict minimum. You should almost never add comments, except sometimes on non-trivial code, function definitions if the arguments aren't self-explanatory, and class definitions and their members.

When addressing a task list pertaining to the code (that might arise from a code review), don't add accompanying comments describing what changed or which item a change addresses.

Do not remove existing comments unless they are directly related to what you are changing.

## Architecture

NSS is a layered cryptographic library. The dependency flows roughly bottom-up:

**`lib/freebl/`** — standalone cryptographic primitives (ciphers, hashes, RNG, EC). No NSS dependencies; can be linked independently. Hardware acceleration (AES-NI, CLMUL, AVX, etc.) is selected at this layer.

**`lib/softoken/`** — software PKCS#11 token built on freebl. This is the default cryptographic "device" NSS uses. Legacy database support is in `lib/softoken/legacydb/`.

**`lib/pk11wrap/`** — PKCS#11 abstraction layer. All cryptographic operations above freebl go through here, allowing hardware tokens and HSMs to be swapped in transparently.

**`lib/certdb/` + `lib/certhigh/`** — certificate database (SQLite via `lib/sqlite/`, or legacy DBM via `lib/dbm/`) and high-level certificate operations.

**`lib/cryptohi/`** — high-level signing, verification, and hashing APIs layered over pk11wrap.

**`lib/ssl/`** — TLS 1.2, TLS 1.3, and DTLS implementation. Depends on cryptohi, certdb, and pk11wrap.

**`lib/nss/`** — top-level initialization and the public NSS API surface.

**`lib/smime/`, `lib/pkcs7/`, `lib/pkcs12/`** — higher-level protocol/format support built on the certificate and crypto layers.

**`lib/mozpkix/`** — Mozilla's standalone C++ PKIX certificate chain validation library (also used by Firefox directly).

**`lib/ckfw/`** — Cryptoki Framework: infrastructure for building PKCS#11 modules.

The `cmd/` directory contains command-line tools (`certutil`, `modutil`, `bltest`, etc.) that exercise the public API and are also used by the test suite.

GTests live in `gtests/` (unit tests per module) and `tests/ssl_gtests/` (SSL integration gtests). Shell-based integration tests are in `tests/`.

## mach

`./mach` is a Python 3 script that wraps common tasks:

```sh
./mach commands          # list all available commands
./mach coverage ssl_gtests   # source coverage report for a suite
./mach fuzz-coverage     # coverage for fuzzing targets
```

## Try server

`./mach try` pushes the current changeset to the `nss-try` Taskcluster branch to
validate a change against CI before landing. Run it with no arguments to print the
current set of valid platform/test/tool tokens; pass try syntax after `--`:

```sh
./mach try                                   # print help + valid tokens
./mach try -- -b do -p all -u all -t all     # everything: both build types, all platforms/tests/tools
./mach try -- -p linux-x64 -u ssl,gtest      # a targeted subset
```

Flags: `-b` build type (`d`/`o`/`do`), `-p` platforms, `-u` unit tests, `-t` tools,
`-e all` extra builds. See `doc/src/try.md` for the full flag reference and how the
try syntax is implemented.

**Working from a git checkout — install git-cinnabar:** NSS's canonical repository
is Mercurial, and its `mach try` drives `hg` directly (`hg status`, `hg push
nss-try`, `hg strip`, …) — unlike Firefox's `mach try`, which supports git/git-cinnabar
natively. If you work from a git checkout, install
[git-cinnabar](https://github.com/glandium/git-cinnabar), the git⇆Mercurial bridge
Mozilla uses, so git can talk to the `hg.mozilla.org` servers:

```sh
pip install git-cinnabar     # other install methods: see the project README
git cinnabar download        # fetch the prebuilt helper binary
```

Either way, the working tree must be clean (uncommitted changes are refused) and
the `nss-try` push path must be configured.

## Bugzilla and Phabricator

NSS tracks bugs in Bugzilla and uses Phabricator for code review. The `moz` MCP server gives direct access to both. The server is defined in `.mcp.json`; to enable it, create `.claude/settings.local.json` if it doesn't exist:

```json
{
  "enableAllProjectMcpServers": true,
  "enabledMcpjsonServers": ["moz"]
}
```

- `@moz:bugzilla://bug/{bug_id}` — retrieve a bug and its comments
- `@moz:phabricator://revision/D{revision_id}` — retrieve a Phabricator revision and its review comments

To find the Phabricator revision ID for a local commit, look for a `Differential Revision: https://phabricator.services.mozilla.com/D<N>` line in the commit message:
```sh
git log -v -l 10   # git
hg log -l 10       # mercurial
```

Never submit or update a Phabricator revision without explicit user approval.

## NSPR

NSS depends on NSPR (Netscape Portable Runtime) for OS abstraction. By default the build looks for it at `../nspr/`. Alternatives:
- `--system-nspr` — use the system-installed NSPR
- `--with-nspr=<path>` — specify a path explicitly

## Downstream users

Firefox vendors NSS at `security/nss/`. Thunderbird also makes use of NSS. Before changing a public API or its behaviour, check callers in those repositories with `searchfox-cli`, filtering by path so the vendored copy doesn't show up. Thunderbird is the `comm-central` repository, which nests mozilla-central under `mozilla/`:

```sh
searchfox-cli -q SSL_OptionSet -p '^security/manager' --cpp
searchfox-cli -R comm-central -q NSS_CMS -p '^mailnews' --cpp
```

`searchfox-cli --help` will show the other options.
