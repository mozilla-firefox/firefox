# [android-components](../../../README.md) > Concept > Bookmark File

An abstract definition of a component dealing with bookmark files.

## Usage

### Setting up the dependency

Use Gradle to download the library from maven.mozilla.org:

```Groovy
implementation "org.mozilla.components:concept-bookmark-file:{latest-version}"
```

### BookmarkFileImporter

Implement the `BookmarkFileImporter` interface to provide a concrete bookmark import strategy:

```kotlin
class HtmlBookmarksFileImporter : BookmarksFileImporter {
    override suspend fun importBookmarksFromUri(uri: Uri) = runCatching {
        
    }
}

val importer: BookmarksFileImporter = HtmlBookmarkFileImporter()
val result = importer.importBookmarksFromUri(uri).getOrThrow()
println("Imported ${result.bookmarksImported} bookmarks")
```

## License

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/
