# Android components

[![chat.mozilla.org](https://img.shields.io/badge/chat-on%20matrix-51bb9c)](https://chat.mozilla.org/#/room/#android-components:mozilla.org)

_A collection of Android libraries to build browsers or browser-like applications._

ℹ️ For more information **[see the website](https://mozac.org/)**.

A fully-featured reference browser implementation based on the components can be found in the [reference-browser repository](https://github.com/mozilla-mobile/reference-browser).

# Getting Involved

We encourage you to participate in this open source project. We love pull requests, bug reports, ideas, (security) code reviews or any kind of positive contribution.

Before you attempt to make a contribution please read the [Community Participation Guidelines](https://www.mozilla.org/en-US/about/governance/policies/participation/).

* Matrix: [android-components:mozilla.org chat room](https://chat.mozilla.org/#/room/#android-components:mozilla.org) ([How to connect](https://wiki.mozilla.org/Matrix#Connect_to_Matrix)).

* Localization happens on [Pontoon](https://pontoon.mozilla.org/projects/firefox-for-android/). Please get in touch with delphine (at) mozilla (dot) com directly for more information.

# Maven repository

All components are getting published on [maven.mozilla.org](https://maven.mozilla.org/).
To use them, you need to add the following to your project's top-level build file, in the `allprojects` block (see e.g. the [reference-browser](https://github.com/mozilla-mobile/reference-browser/blob/main/build.gradle)):

```groovy
repositories {
    maven {
       url "https://maven.mozilla.org/maven2"
    }
}
```

Each module that uses a component needs to specify it in its build file, in the `dependencies` block.  For example, to use the `Base` component (in the `support`) collection, you need:

```groovy
dependencies {
    implementation 'org.mozilla.components:support-base:+'
}
```

## Nightly builds

Nightly builds are created every day from the `main` branch and published on [nightly.maven.mozilla.org](https://nightly.maven.mozilla.org).

# Components

* 🔴 **In Development** - Not ready to be used in shipping products.
* ⚪ **Preview** - This component is almost/partially ready and can be tested in products.
* 🔵 **Production ready** - Used by shipping products.

## Browser

High-level components for building browser(-like) apps.

* 🔵 [**Domains**](components/browser/domains/README.md) Localized and customizable domain lists for auto-completion in browsers.

* 🔵 [**Engine-Gecko**](components/browser/engine-gecko/README.md) - *Engine* implementation based on [GeckoView](https://wiki.mozilla.org/Mobile/GeckoView).

* 🔵 [**Engine-System**](components/browser/engine-system/README.md) - *Engine* implementation based on the system's WebView.

* 🔵 [**Errorpages**](components/browser/errorpages/README.md) - Responsive browser error pages for Android apps.

* 🔵 [**Icons**](components/browser/icons/README.md) - A component for loading and storing website icons (like [Favicons](https://en.wikipedia.org/wiki/Favicon)).

* 🔵 [**Menu**](components/browser/menu/README.md) - A generic menu with customizable items primarily for browser toolbars.

* ⚪ [**Menu 2**](components/browser/menu2/README.md) - A generic menu with customizable items primarily for browser toolbars.

* 🔵 [**Session-Storage**](components/browser/session-storage/README.md) - Component for saving and restoring the browser state.

* 🔵 [**State**](components/browser/state/README.md) - Component for maintaining the centralized state of the browser and its components.

* 🔵 [**Storage-Sync**](components/browser/storage-sync/README.md) - A syncable implementation of browser storage backed by [application-services' Places lib](https://github.com/mozilla/application-services).

* 🔵 [**Tabstray**](components/browser/tabstray/README.md) - A customizable tabs tray for browsers.

* 🔵 [**Thumbnails**](components/browser/thumbnails/README.md) - A component for loading and storing website thumbnails (screenshot of the website).

* 🔵 [**Toolbar**](components/browser/toolbar/README.md) - A customizable toolbar for browsers.

## Concept

_API contracts and abstraction layers for browser components._

* 🔵 [**Awesomebar**](components/concept/awesomebar/README.md) - An abstract definition of an awesome bar component.

* 🔵 [**Engine**](components/concept/engine/README.md) - Abstraction layer that allows hiding the actual browser engine implementation.

* 🔵 [**Fetch**](components/concept/fetch/README.md) - An abstract definition of an HTTP client for fetching resources.

* 🔵 [**Push**](components/concept/push/README.md) - An abstract definition of a push service component.

* 🔵 [**Storage**](components/concept/storage/README.md) - Abstract definition of a browser storage component.

* 🔵 [**Tabstray**](components/concept/tabstray/README.md) - Abstract definition of a tabs tray component.

* 🔵 [**Toolbar**](components/concept/toolbar/README.md) - Abstract definition of a browser toolbar component.

## Feature

_Combined components to implement feature-specific use cases._

* 🔵 [**Accounts**](components/feature/accounts/README.md) - A component that connects an FxaAccountManager from [service-firefox-accounts](components/service/firefox-accounts/README.md) with [feature-tabs](components/feature/tabs/README.md) in order to facilitate authentication flows.

* 🔵 [**Accounts Push**](components/feature/accounts-push/README.md) - Feature of use cases for FxA Account that work with push support.

* 🔵 [**Autofill**](components/feature/autofill/README.md) - A component that provides support for Android's Autofill framework.

* 🔵 [**Awesomebar**](components/feature/awesomebar/README.md) - A component that connects a [concept-awesomebar](components/concept/awesomebar/README.md) implementation to a [concept-toolbar](components/concept/toolbar/README.md) implementation and provides implementations of various suggestion providers.

* 🔴 [**Containers**](components/feature/containers/README.md) - A component for working with contextual identities also known as containers.

* 🔵 [**Context Menu**](components/feature/contextmenu/README.md) - A component for displaying context menus when *long-pressing* web content.

* 🔵 [**Custom Tabs**](components/feature/customtabs/README.md) - A component for providing [Custom Tabs](https://developer.chrome.com/multidevice/android/customtabs) functionality in browsers.

* 🔵 [**Downloads**](components/feature/downloads/README.md) - A component to perform downloads using the [Android downloads manager](https://developer.android.com/reference/android/app/DownloadManager).

* 🔵 [**Intent**](components/feature/intent/README.md) - A component that provides intent processing functionality by combining various other feature modules.

* ⚪ [**Progressive Web Apps (PWA)**](components/feature/pwa/README.md) - A component that provides functionality for supporting Progressive Web Apps (PWA).

* 🔵 [**Reader View**](components/feature/readerview/README.md) - A component that provides Reader View functionality.

* 🔵 [**QR**](components/feature/qr/README.md) - A component that provides functionality for scanning QR codes.

* 🔵 [**Search**](components/feature/search/README.md) - A component that connects an (concept) engine implementation with the browser search module.

* 🔵 [**Session**](components/feature/session/README.md) - A component that connects an (concept) engine implementation with the browser session and storage modules.

* 🔵 [**Share**](components/feature/share/README.md) - Feature implementation for saving and sorting recent apps used for sharing.

* 🔵 [**Tabs**](components/feature/tabs/README.md) - A component that connects a tabs tray implementation with the session and toolbar modules.

* 🔵 [**Tab Collections**](components/feature/tab-collections/README.md) - Feature implementation for saving, restoring and organizing collections of tabs.

* 🔵 [**Toolbar**](components/feature/toolbar/README.md) - A component that connects a (concept) toolbar implementation with the browser session module.

* 🔵 [**Top Sites**](components/feature/top-sites/README.md) - Feature implementation for saving and removing top sites.

* 🔵 [**Prompts**](components/feature/prompts/README.md) - A component that will handle all the common prompt dialogs from web content.

* 🔵 [**Push**](components/feature/push/README.md) - A component that provides Autopush messages with help from a supported push service.

* 🔵 [**Find In Page**](components/feature/findinpage/README.md) - A component that provides an UI widget for [find in page functionality](https://support.mozilla.org/en-US/kb/search-contents-current-page-text-or-links).

* 🔵 [**Synced Tabs**](components/feature/syncedtabs/README.md) - Feature that provides access to other device's tabs in the same account.

* 🔵 [**Site Permissions**](components/feature/sitepermissions/README.md) - A feature for showing site permission request prompts.

* 🔵 [**WebAuthn**](components/feature/webauthn/README.md) - A feature that provides WebAuthn functionality for supported engines.

* 🔵 [**Web Notifications**](components/feature/webnotifications/README.md) - A component for displaying web notifications.

* 🔵 [**WebCompat**](components/feature/webcompat/README.md) - A feature to enable website-hotfixing via the Web Compatibility System-Addon.

* 🔵 [**WebCompat Reporter**](components/feature/webcompat-reporter/README.md) - A feature that enables users to report site issues to Mozilla's Web Compatibility team for further diagnosis.

* 🔵 [**Web Add-ons**](components/feature/addons/README.md) - A feature that provides functionality for managing add-ons.

## UI

_Generic low-level UI components for building apps._

* 🔵 [**Autocomplete**](components/ui/autocomplete/README.md) - A set of components to provide autocomplete functionality.

* 🔵 [**Colors**](components/ui/colors/README.md) - The standard set of [Photon](https://design.firefox.com/photon/) colors.

* 🔵 [**Fonts**](components/ui/fonts/README.md) - The standard set of fonts used by Mozilla Android products.

* 🔵 [**Icons**](components/ui/icons/README.md) - A collection of often used browser icons.

* 🔵 [**Tabcounter**](components/ui/tabcounter/README.md) - A button that shows the current tab count and can animate state changes.

## Compose

Compose UI components for building apps.

* 🔵 [**Awesomebar**](components/compose/awesomebar/README.md) - A customizable [Awesome Bar](https://support.mozilla.org/en-US/kb/awesome-bar-search-firefox-bookmarks-history-tabs) implementation for browsers using Jetpack Compose.

* 🔵 [**Base**](components/compose/base/README.md) - Base component containing Composable components based on [Acorn Design System](https://acorn.firefox.com/) that are used as building blocks for building UI.

## Service

_Components and libraries to interact with backend services._

* 🔵 [**Firefox Accounts (FxA)**](components/service/firefox-accounts/README.md) - A library for integrating with Firefox Accounts.

* 🔵 [**Firefox Sync - Logins**](components/service/sync-logins/README.md) - A library for integrating with Firefox Sync - Logins.

* 🔵 [**Firefox Sync - Autofill**](components/service/sync-autofill/README.md) - A library for integrating with Firefox Sync - Autofill.

* 🔵 [**Glean**](components/service/glean/README.md) - A client-side telemetry SDK for collecting metrics and sending them to Mozilla's telemetry service.

* 🔵 [**Location**](components/service/location/README.md) - A library for accessing Mozilla's and other location services.

* 🔴 [**Nimbus**](components/service/nimbus/README.md) - A wrapper for the Nimbus SDK.

* 🔵 [**Pocket**](components/service/pocket/README.md) - A library for communicating with the Pocket API.

* 🔵 [**MARS**](components/service/mars/README.md) - A library for communicating with the MARS API.

## Support

_Supporting components with generic helper code._

* 🔵 [**Android Test**](components/support/android-test/README.md) - A collection of helpers for testing components in instrumented (on device) tests (`src/androidTest`).

* 🔵 [**Base**](components/support/base/README.md) - Base component containing building blocks for components.

* 🔵 [**Ktx**](components/support/ktx/README.md) - A set of Kotlin extensions on top of the Android framework and Kotlin standard library.

* 🔵 [**Test**](components/support/test/README.md) - A collection of helpers for testing components in local unit tests (`src/test`).

* 🔵 [**Test Appservices**](components/support/test-appservices/README.md) - A component for synchronizing Application Services' unit testing dependencies used in Android Components.

* 🔵 [**Test LibState**](components/support/test-libstate/README.md) - A collection of helpers for testing functionality that relies on the lib-state component in local unit tests (`src/test`).

* 🔵 [**Utils**](components/support/utils/README.md) - Generic utility classes to be shared between projects.

* 🔵 [**Webextensions**](components/support/webextensions/README.md) - A component containing building blocks for features implemented as web extensions.

## Standalone libraries

* 🔵 [**Crash**](components/lib/crash/README.md) - A generic crash reporter component that can report crashes to multiple services.

* 🔵 [**Dataprotect**](components/lib/dataprotect/README.md) - A component using AndroidKeyStore to protect user data.

* 🔵 [**Fetch-HttpURLConnection**](components/lib/fetch-httpurlconnection/README.md) - A [concept-fetch](components/concept/fetch/README.md) implementation using [HttpURLConnection](https://developer.android.com/reference/java/net/HttpURLConnection.html).

* 🔵 [**Fetch-OkHttp**](components/lib/fetch-okhttp/README.md) - A [concept-fetch](components/concept/fetch/README.md) implementation using [OkHttp](https://github.com/square/okhttp).

* ⚪ [**JEXL**](components/lib/jexl/README.md) - Javascript Expression Language: Context-based expression parser and evaluator.

* 🔵 [**Public Suffix List**](components/lib/publicsuffixlist/README.md) - A library for reading and using the [public suffix list](https://publicsuffix.org/).

* 🔵 [**Push-Firebase**](components/lib/push-firebase/README.md) - A [concept-push](components/concept/push/README.md) implementation using [Firebase Cloud Messaging](https://firebase.google.com/products/cloud-messaging/).

* 🔵 [**State**](components/lib/state/README.md) - A library for maintaining application state.

## Tooling

* 🔵 [**Fetch-Tests**](components/tooling/fetch-tests/README.md) - A generic test suite for components that implement [concept-fetch](components/concept/fetch/README.md).

* 🔵 [**Lint**](components/tooling/lint/README.md) - Custom Lint rules for the components repository.

# Sample apps

_Sample apps using various components._

* [**Browser**](samples/browser) - A simple browser composed from browser components. This sample application is only a very basic browser. For a full-featured reference browser implementation see the **[reference-browser repository](https://github.com/mozilla-mobile/reference-browser)**.

* [**Crash**](samples/crash) - An app showing the integration of the `lib-crash` component.

* [**Firefox Accounts (FxA)**](samples/firefox-accounts) - A simple app demoing Firefox Accounts integration.

* [**Firefox Sync**](samples/sync) - A simple app demoing general Firefox Sync integration, with bookmarks and history.

* [**Firefox Sync - Logins**](samples/sync-logins) - A simple app demoing Firefox Sync (Logins) integration.

* [**DataProtect**](samples/dataprotect) - An app demoing how to use the [**Dataprotect**](components/lib/dataprotect/README.md) component to load and store encrypted data in `SharedPreferences`.

* [**Glean**](samples/glean) - An app demoing how to use the [**Glean**](components/service/glean/README.md) library to collect and send telemetry data.

* [**Toolbar**](samples/toolbar) - An app demoing multiple customized toolbars using the [**browser-toolbar**](components/browser/toolbar/README.md) component.

# Building #

## Command line ##

```
$ git clone https://github.com/mozilla-firefox/firefox.git
$ cd firefox/mobile/android/android-components
$ ./gradlew assemble
```

## Android Studio ##

If the environment variable `JAVA_HOME` is not defined, you will need to set it. If you would like to use the JDK installed by Android Studio, here's how to find it:

1. Open Android Studio.
2. Select "Configure".
3. Select "Default Project Structure". You should now see the Android JDK location.
4. Set the environment variable `JAVA_HOME` to the location. (How you set an environment variable depends on your OS.)
5. Restart Android Studio.

Once the environment variable is set, you can import the project into Android Studio with the default wizard options.

If your build fails, you may find you get more instructive error messages by attempting the build at the command line.

# Coding Standards #

## Style ##
We follow the style enforced by [ktfmt](https://github.com/Kotlin/ktfmt) and [detekt](https://github.com/detekt/detekt). See [how to configure Android Studio appropriately](https://github.com/Kotlin/ktfmt#intellij-android-studio-and-other-jetbrains-ides).

To check your style, run:

```
./gradlew ktfmtCheck
./gradlew detekt
```

## Documentation ##
We use `README.md` files for each component.

If you fix a bug or change an API, you should update [docs/changelog.md](docs/changelog.md).

## Testing ##
You are expected to both add tests for code that you write and make sure that your changes do not
cause existing tests to fail. You may find these command lines helpful:

```
./gradlew test                             # Run all tests
./gradlew :support-ktx:testdebugunittest   # Run unit tests for a specified module
```

See also [how to measure code coverage](https://mozac.org/contributing/code-coverage).

## Accessibility ##
If your code has user-facing changes, follow [Android accessibility best practices](../docs/shared/android/accessibility_guide.md).

# License

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/
