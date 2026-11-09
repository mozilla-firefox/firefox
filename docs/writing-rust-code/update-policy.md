# Rust Update Policy

We document here the decision making and planning around when we update the
Rust toolchains used and required to build Firefox.

This allows contributors to know when new features will be usable, and
downstream packagers to know what toolchain will be required for each Firefox
release. Both benefit from the predictability of a schedule.

## Policy

### Official builds

_We ship official stable Firefox with a stable Rust._

As a general rule, we update the Rust version used to build Firefox Nightly
soon after its release, unless we're in a toolchain freeze period, which
starts 7 days before a merge day. In that case, we wait for the next Nightly
train.

We don't upgrade the Rust version in the beta or release branches of Firefox.

The following exceptions apply:

- We may use a Rust version from the Rust beta or nightly channels for new
  platforms (e.g. we did so for Android, arm64 Windows and arm64 macOS), and
  later upgrade when that Rust version becomes stable (we may even do so on the
  Firefox beta branch).

- We may skip the update (or backout the update) if major problems are
  encountered (typically, we've had to do so because of build problems, crash
  reporting bustage, or performance issues).

### Developer builds

_Local developer builds use whatever Rust toolchain is available on the
system._

Someone building Firefox can maintain the latest stable Rust with the `rustup`
or `mach bootstrap` tools, or try other variations.

### Minimum Supported Rust Version

_We will update the Minimum Supported Rust Version (MSRV) when required._

The MSRV will generally remain unchanged, until a newer version is required
by some code.

When that happens, we'll prefer to update the MSRV to the strict minimum
required at that moment (e.g. if we require version 1.47.0, the currently used
Rust version is 1.51.0, and a crate needs 1.50.0, we'll prefer to update the
MSRV to 1.50.0 rather than 1.51.0).

The MSRV won't be updated to a version of Rust that hasn't been used for
Firefox Nightly for at least 14 days.

We expect ESR releases will keep their MSRV, so backporting security fixes may
require Rust compatibility work.

### Rationale

Historically, the Rust ecosystem quickly required new features provided by new
Rust compilers, which made it necessary to update the minimum supported version
quite often, and as such, a scheduled update was deemed a better trade-off.

Fast-forward several years, and new Rust compiler releases more rarely sport
ground-breaking new features, which has reduced the necessity to update quite
significantly.

On the flip side, in some instances, we have had to stick to specific versions
of the Rust compiler for extended periods of time because of e.g. regressions,
going against the schedule.

## Schedule

Here are the Rust versions for each Firefox version.

- The "Uses" column indicates the version of Rust used to build
  releases shipped to users.

- The "MSRV" column indicates the minimum supported Rust version to build
  the sources.

| Firefox Version | Uses | MSRV | Rust "Uses" release date | Toolchain Freeze | Firefox release date
|-----------------|------|----------|--------------------------|---------------------|----------------------
| Firefox 115 | Rust 1.69.0 | 1.66.0 | 2023 April 20 | | 2023 July 4
| Firefox 140 | Rust 1.86.0 | 1.82.0 | 2025 April 3 | | 2025 June 24
| Firefox 153 | Rust 1.94.0 | 1.90.0 | 2026 March 5 | | 2026 July 21
| Firefox 154 | Rust 1.94.0 | 1.90.0 | 2026 March 5 | | 2026 August 18
| Firefox 155 | Rust 1.95.0 | 1.90.0 | 2026 April 16 | 2026 August 6 | 2026 September 1
| Firefox 156 | Rust 1.95.0 | 1.90.0 | 2026 April 16 | 2026 August 20 | 2026 September 15
| Firefox 157 | Rust 1.95.0 | 1.90.0 | 2026 April 16 | 2026 September 3 | 2026 September 29
| Firefox 158 | Rust 1.95.0 | 1.90.0 | 2026 April 16 | 2026 September 17 | 2026 October 13
| Firefox 159 | Rust 1.95.0 | 1.90.0 | 2026 April 16 | 2026 October 1 | 2026 October 27
| Firefox 160 | Rust 1.95.0 | 1.90.0 | 2026 April 16 | 2026 October 15 | 2026 November 10
| **Estimated** | &nbsp; | &nbsp; | &nbsp; | &nbsp; | &nbsp;
| Firefox 161 | Rust 1.99.0 | 1.95.0 | 2026 April 16 | 2026 October 29 | 2026 November 24
| Firefox 162 | Rust 1.99.0 | ? | 2026 October 1 | 2026 November 12 | 2026 December 8
| Firefox 163 | Rust 1.100.0 | ? | 2026 November 12 | 2026 November 26 | 2027 January 12
