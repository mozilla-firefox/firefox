# [android-components](../../../README.md) > Lib > Bookmarks File Importer

A concrete implementation of [concept-bookmarks-file-importer](../../concept/bookmarks-file-importer/README.md) that parses bookmarks from Netscape HTML bookmark files.

## Usage

### Setting up the dependency

Use Gradle to download the library from maven.mozilla.org:

```Groovy
implementation "org.mozilla.components:lib-bookmarks-file-importer:{latest-version}"
```

### HtmlBookmarksFileImporter

```kotlin
val importer = HtmlBookmarksFileImporter(contentResolver = context.contentResolver)

when (val result = importer.importBookmarksFromUri(uri)) {
    is BookmarksFileImporter.ImportResult.Success -> println("Imported ${result.bookmarksImported} bookmarks")
    is BookmarksFileImporter.ImportResult.Failure -> println("Import failed: ${result.exception}")
}
```

## License

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/
