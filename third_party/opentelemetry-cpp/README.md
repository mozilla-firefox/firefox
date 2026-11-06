# OpenTelemetry C++

[![Slack](https://img.shields.io/badge/slack-@cncf/otel/cpp-brightgreen.svg?logo=slack)](https://cloud-native.slack.com/archives/C01N3AT62SJ)
[![codecov.io](https://codecov.io/gh/open-telemetry/opentelemetry-cpp/branch/main/graphs/badge.svg?)](https://codecov.io/gh/open-telemetry/opentelemetry-cpp/)
[![Build
Status](https://github.com/open-telemetry/opentelemetry-cpp/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/open-telemetry/opentelemetry-cpp/actions)
[![Release](https://img.shields.io/github/v/release/open-telemetry/opentelemetry-cpp?include_prereleases&style=)](https://github.com/open-telemetry/opentelemetry-cpp/releases/)
[![FOSSA License Status](https://app.fossa.com/api/projects/custom%2B162%2Fgithub.com%2Fopen-telemetry%2Fopentelemetry-cpp.svg?type=shield&issueType=license)](https://app.fossa.com/projects/custom%2B162%2Fgithub.com%2Fopen-telemetry%2Fopentelemetry-cpp?ref=badge_shield&issueType=license)
[![FOSSA Security Status](https://app.fossa.com/api/projects/custom%2B162%2Fgithub.com%2Fopen-telemetry%2Fopentelemetry-cpp.svg?type=shield&issueType=security)](https://app.fossa.com/projects/custom%2B162%2Fgithub.com%2Fopen-telemetry%2Fopentelemetry-cpp?ref=badge_shield&issueType=security)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/open-telemetry/opentelemetry-cpp/badge)](https://scorecard.dev/viewer/?uri=github.com/open-telemetry/opentelemetry-cpp)

The C++ [OpenTelemetry](https://opentelemetry.io/) client.

## Project Status

**Stable** across all 3 signals i.e. `Logs`, `Metrics`, and `Traces`.

See [Spec Compliance
Matrix](https://github.com/open-telemetry/opentelemetry-specification/blob/main/spec-compliance-matrix.md)
to understand which portions of the specification has been implemented in this
repo.

## Supported C++ Versions

Code shipped from this repository generally supports the following versions of
C++ standards:

* ISO/IEC 14882:2014 (C++14)
* ISO/IEC 14882:2017 (C++17)
* ISO/IEC 14882:2020 (C++20)

Any exceptions to this are noted in the individual `README.md` files.

Please note that supporting the [C Programming
Language](https://en.wikipedia.org/wiki/C_(programming_language)) is not a goal
of the current project.

## Supported Development Platforms

 Our CI pipeline builds and tests on following `x86-64` platforms:

| Platform                                                            |   Build type  |
|---------------------------------------------------------------------|---------------|
| ubuntu-22.04 (GCC 10, GCC 12, Clang 14)                             | CMake, Bazel  |
| ubuntu-20.04 (GCC 9.4.0 - default compiler)                         | CMake, Bazel  |
| ubuntu-20.04 (GCC 9.4.0 with -std=c++14/17/20 flags)                | CMake, Bazel  |
| macOS 12.7 (Xcode 14.2)                                             | Bazel         |
| Windows Server 2019 (Visual Studio Enterprise 2019)                 | CMake, Bazel  |
| Windows Server 2022 (Visual Studio Enterprise 2022)                 | CMake         |

In general, the code shipped from this repository should build on all platforms
having C++ compiler with [supported C++ standards](#supported-c-versions).

## Dependencies

Please refer to [Dependencies.md](docs/dependencies.md) for OSS Dependencies and
license requirements.

## Installation

Please refer to [INSTALL.md](./INSTALL.md).

### Bazel Central Registry

This project is available in the [Bazel Central Registry (BCR)](https://github.com/bazelbuild/bazel-central-registry/tree/main/modules/opentelemetry-cpp).
To use it with Bazel, add the following to your `MODULE.bazel` file:

```python
bazel_dep(name = "opentelemetry-cpp", version = "x.y.z")
```

For the latest version, see [BCR: opentelemetry-cpp](https://github.com/bazelbuild/bazel-central-registry/tree/main/modules/opentelemetry-cpp).

## Getting Started

As an application owner or the library author, you can find the getting started
guide and reference documentation on
[opentelemetry-cpp.readthedocs.io](https://opentelemetry-cpp.readthedocs.io/en/latest/)

The `examples/simple` directory contains a minimal program demonstrating how to
instrument a small library using a simple `processor` and console `exporter`,
along with build files for CMake and Bazel.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md)

We meet weekly, and the time of the meeting alternates between Monday at 13:00
PT and Wednesday at 9:00 PT. The meeting is subject to change depending on
contributors' availability. Check the [OpenTelemetry community
calendar](https://github.com/open-telemetry/community#calendar)
for specific dates and Zoom meeting links.

Meeting notes are available as a public
[Google doc](https://docs.google.com/document/d/1rdF6GZcCqH9huo3oqur5x_McsPS_qQ9OPSRFjIqndm8/edit?usp=sharing).

For edit access, get in touch on the
[#otel-cpp](https://cloud-native.slack.com/archives/C01N3AT62SJ)
channel on CNCF Slack. If you are new to the CNCF Slack community,
you can [create an account](https://slack.cncf.io/).

The meeting is open for all to join. We invite everyone to join our meeting,
regardless of your experience level. Whether you're a seasoned OpenTelemetry
developer, just starting your journey, or simply curious about the work we do,
you're more than welcome to participate!

### Maintainers

* [Doug Barker](https://github.com/dbarker)
* [Ehsan Saei](https://github.com/esigo)
* [Lalit Kumar Bhasin](https://github.com/lalitb), Microsoft
* [Marc Alff](https://github.com/marcalff), Oracle
* [Tom Tan](https://github.com/ThomsonTan), Microsoft

For more information about the maintainer role, see the [community repository](https://github.com/open-telemetry/community/blob/main/guides/contributor/membership.md#maintainer).

### Approvers

* [WenTao Ou](https://github.com/owent)

For more information about the approver role, see the [community repository](https://github.com/open-telemetry/community/blob/main/guides/contributor/membership.md#approver).

### Emeritus

* [Alolita Sharma](https://github.com/alolita), Triager
* [Emil Mikulic](https://github.com/g-easy), Maintainer
* [Jodee Varney](https://github.com/jodeev), Triager
* [Johannes Tax](https://github.com/pyohannes), Approver
* [Josh Suereth](https://github.com/jsuereth), Approver
* [Max Golovanov](https://github.com/maxgolov), Approver
* [Pranav Sharma](https://github.com/psx95), Approver
* [Reiley Yang](https://github.com/reyang), Maintainer
* [Ryan Burn](https://github.com/rnburn), Approver

For more information about the emeritus role, see the [community repository](https://github.com/open-telemetry/community/blob/main/guides/contributor/membership.md#emeritus-maintainerapprovertriager).

### Thanks to all the people who have contributed

[![contributors](https://contributors-img.web.app/image?repo=open-telemetry/opentelemetry-cpp)](https://github.com/open-telemetry/opentelemetry-cpp/graphs/contributors)

## Release Schedule

See the [release
notes](https://github.com/open-telemetry/opentelemetry-cpp/releases) for
existing releases.

See the [project
milestones](https://github.com/open-telemetry/opentelemetry-cpp/milestones) for
details on upcoming releases. The dates and features described in issues and
milestones are estimates, and subject to change.
