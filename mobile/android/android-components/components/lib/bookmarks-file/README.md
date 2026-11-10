# [android-components](../../../README.md) > Lib > Bookmarks File

A concrete implementation of [concept-file-importer](../../concept/file-importer/README.md) that imports bookmarks
from Netscape HTML bookmark files.

## Usage

### Setting up the dependency

Use Gradle to download the library from maven.mozilla.org:

```Groovy
implementation "org.mozilla.components:lib-bookmarks-file:{latest-version}"
```

### Importing bookmarks

```kotlin
val importer = FileImporter.htmlImporter(
    context = context,
    parentGuid = BookmarkRoot.Mobile.id,
    parser = FileParser.jsoupParser(rootFolderName = "Imported"),
    inserter = bookmarksStorage,
)

importer.importFromUri(uri)
    .onSuccess { println("Imported ${it.count} bookmarks") }
    .onFailure { println("Import failed: ${(it as? FileImporterException)?.errorType}") }
```

## License

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/
