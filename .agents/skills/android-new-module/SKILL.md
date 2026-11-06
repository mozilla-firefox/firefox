---
name: android-new-module
description: Guide for creating new Android gradle modules in the android-components project.
---

# Creating a new Android module

## Overview

This skill helps you create new Android gradle modules following Mozilla's conventions and structure. Components are organized into categories: `browser`, `concept`, `feature`, `lib`, `service`, `support`, `ui`, and `compose`.

## Component Categories

- **browser** - High-level browser components (engine, menu, state, etc.)
- **compose** - Jetpack Compose components
- **concept** - Abstract API contracts and interfaces
- **feature** - Feature modules for browser functionality
- **lib** - Standalone libraries (utilities, tools)
- **service** - Services (push, sync, etc.)
- **support** - Support/utility modules
- **tooling** - Support modules for the build or tools.
- **ui** - UI components; has overlap with Compose components.

## Steps to Create a New Component
Create a plan to execute the steps. The plan should include all eight steps below.

### 1. Determine Component Type and Name

Ask the user:
- What category? (feature, lib, browser, etc.)
- What should it be named? (lowercase with hyphens, e.g., `my-component`)
- What will it do? (for description)
- Will it be published to Maven? (most are `publish: true`, examples are `publish: false`)

### 2. Create Directory Structure

```bash
mkdir -p mobile/android/android-components/components/{category}/{name}/src/main/java/mozilla/components/{category}/{name}
```

### 3. Create build.gradle

Reference the example at `mobile/android/android-components/components/feature/example/build.gradle` for the standard structure.

Key elements:
- Apply the `com.android.library` plugin in the `plugins {}` block
- Set namespace: `mozilla.components.{category}.{name}`
- Include common dependencies: `androidx.core`, `kotlinx.coroutines`
- Apply common config and publish scripts

**Add dependencies as needed:**
- Support base (for LifecycleAwareFeature): `implementation project(':components:support-base')`
- Lifecycle: `libs.androidx.lifecycle.runtime`
- Compose: Add `platform(libs.androidx.compose.bom)` and compose dependencies
- State management: `implementation project(':components:lib-state')`
- Browser state: `implementation project(':components:browser-state')`
- Other components: `implementation project(':components:{category}:{component-name}')`

### 4. Create AndroidManifest.xml

File: `src/main/AndroidManifest.xml`

```xml
<!-- This Source Code Form is subject to the terms of the Mozilla Public
   - License, v. 2.0. If a copy of the MPL was not distributed with this
   - file, You can obtain one at http://mozilla.org/MPL/2.0/. -->
<manifest />
```

### 5. (Optional) Create Main Kotlin Source

File: `src/main/java/mozilla/components/{category}/{name}/{ComponentName}.kt`

Reference the example at `mobile/android/android-components/components/feature/example/src/main/java/mozilla/components/feature/example/ExampleFeature.kt`.

Key elements for a feature component:
- MPL 2.0 license header
- Package: `mozilla.components.{category}.{name}`
- Implement `LifecycleAwareFeature` for lifecycle-aware features
- Use dependency injection for testability (e.g., inject `CoroutineDispatcher`)
- Include coroutine scope for async operations
- Override lifecycle methods (`start()`, `stop()`)

For simpler library components, a basic class with the necessary methods is sufficient.

### 6. (Optional) Create Test File

File: `src/test/java/mozilla/components/{category}/{name}/{ComponentName}Test.kt`

Reference the example at `mobile/android/android-components/components/feature/example/src/test/java/mozilla/components/feature/example/ExampleFeatureTest.kt`.

Key elements:
- Add test dependencies `junit.bom` and `junit4`, plus `kotlinx.coroutines.test` for coroutine tests
- MPL 2.0 license header
- Use `StandardTestDispatcher` from `kotlinx.coroutines.test` for testing coroutines
- Inject the test dispatcher into the component being tested
- Use `runTest(testDispatcher)` to run tests with coroutine support
- Use `testDispatcher.scheduler.advanceUntilIdle()` to advance virtual time
- Track actual callback invocations instead of using mocks
- Write descriptive test names using backticks (e.g., `` `start triggers onUpdate callback` ``)
- Use JUnit assertions to verify behavior
- Robolectric tests: see the `android` skill

### 7. Create README.md

File: `README.md`

Reference the example at `mobile/android/android-components/components/feature/example/README.md`.

Key elements:
- Title with breadcrumb navigation: `# [android-components](../../../README.md) > {Category} > {Name}`
- Brief description of the component
- Usage section with dependency setup
- Code examples showing how to use the component
- MPL 2.0 license footer

### 8. Register in .buildconfig.yml

Add the component to `.buildconfig.yml` in alphabetical order within its category:

```yaml
  components:{category}-{name}:
    description: {Brief description}
    path: components/{category}/{name}
    publish: true
```

Find the correct insertion point by searching for the category (e.g., `components:feature-` for feature modules) and inserting in alphabetical order.

Then update the upstream_dependencies for the new buildconfig with this mach command:

```bash
./mach android update-buildconfig android-components
```

Also update `taskcluster/config.yml` to add the new module's display name to `treeherder.group-names`. Without this step, CI will fail with a buildconfig mismatch error.

## Common Patterns

### Package Naming
- Pattern: `mozilla.components.{category}.{name}`
- Example: `mozilla.components.feature.example`

### Class Naming
- Convert kebab-case to PascalCase
- Example: `my-component` → `MyComponent`

### Dependency Types
- `api` - For interfaces and contracts (concept modules)
- `implementation` - For internal dependencies
- `testImplementation` - For testing dependencies
- `androidTestImplementation` - For instrumented tests

### Code Style
- Use MPL 2.0 license headers on all files
- Minimal comments (only for non-obvious code)
- Use `@VisibleForTesting` for test-only visibility
- Follow Kotlin coding conventions

## Verification

After creating the component, verify it builds with the debug variant:

```bash
./mach gradle :components:{category}-{name}:buildDebug
```

Run tests:

```bash
./mach gradle :components:{category}-{name}:testDebug
```

Format and lint:

```bash
./mach lint --fix mobile/android/android-components/components/{category}/{name}
```

## Notes

- Always use `./mach gradle` instead of `gradlew` to ensure the build system is executed in its entirety.
- Component names use kebab-case (hyphens).
- Package names use lowercase without separators.
- Class names use PascalCase
- Set `publish: false` for example/sample components because we do not want them to be distributed through the maven mirrors.
- The module will be automatically included in the build after registering in `.buildconfig.yml`.
