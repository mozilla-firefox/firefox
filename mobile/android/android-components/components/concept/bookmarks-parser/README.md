# [android-components](../../../README.md) > Concept > Bookmark File Parser

An abstract definition of a bookmarks-parser component.

## Usage

### Setting up the dependency

Use Gradle to download the library from maven.mozilla.org:

```Groovy
implementation "org.mozilla.components:concept-bookmarks-parser:{latest-version}"
```

### BookmarksParseResult

`BookmarksParseResult` is the result of parsing a bookmark file with a `FileParser<BookmarksParseResult>`: a tree of
`InsertableBookmarkTreeNode`s, plus the number of bookmarks and folders found. Parse failures are reported as a
`FileParserException` carrying a `BookmarksParseError`.

```kotlin
class HtmlBookmarksFileParser : FileParser<BookmarksParseResult> {
    override suspend fun parse(inputStream: InputStream): Result<BookmarksParseResult> {
        // Parse the HTML bookmark file and return the result.
    }
}
```

## License

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/
