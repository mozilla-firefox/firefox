# [Android Components](../../../README.md) > Feature > Tab Data Coordinator

A component that gathers tab data from the various places it is produced and exposes it from a
single location, so that consumers do not have to know which source a given piece of data came
from.

For the complete documentation of this module, please refer to [the source docs](https://firefox-source-docs.mozilla.org/mobile/android/fenix/tab-data-coordinator.html).

### Setting up the dependency

Use Gradle to download the library from [maven.mozilla.org](https://maven.mozilla.org/) ([Setup repository](../../../README.md#maven-repository)):

```Groovy
implementation "org.mozilla.components:feature-tabdata-coordinator:{latest-version}"
```

## License

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/
