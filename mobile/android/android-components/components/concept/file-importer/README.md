# [android-components](../../../README.md) > Concept > File Import

An abstract definition of a component for importing data from files.

## Usage

### Setting up the dependency

Use Gradle to download the library from maven.mozilla.org:

```Groovy
implementation "org.mozilla.components:concept-file-importer:{latest-version}"
```

### FileImporter

Implement the `FileImporter` interface to provide a concrete file import strategy, returning a result that implements
`ImportResult`:

```kotlin
data class BookmarksImportResult(override val count: Int) : ImportResult

class HtmlBookmarksFileImporter : FileImporter<BookmarksImportResult> {
    override val mimeType = "text/html"

    override suspend fun importFromUri(uri: Uri) = runCatching {
        // Read, parse and save the file content.
        BookmarksImportResult(count = 0)
    }
}

val importer: FileImporter<BookmarksImportResult> = HtmlBookmarksFileImporter()
val result = importer.importFromUri(uri).getOrThrow()
println("Imported ${result.count} items")
```

## License

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/
