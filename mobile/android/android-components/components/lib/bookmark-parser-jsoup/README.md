# [android-components](../../../README.md) > Lib > Bookmark Parser Jsoup

A jsoup-based implementation of [concept-file-parser](../../concept/file-parser/README.md) that parses HTML bookmark files in the Netscape Bookmark format.

## Usage

### Setting up the dependency

Use Gradle to download the library from maven.mozilla.org:

```Groovy
implementation "org.mozilla.components:lib-bookmark-parser-jsoup:{latest-version}"
```

### JsoupBookmarksFileParser

`JsoupBookmarksFileParser` parses a bookmark file URI into a tree of `InsertableBookmarkNode`s using
Jsoup.

```kotlin
val parser = FileParser.jsoupParser(...)

lifecycleScope.launch {
    context.contentResolver.openInputStream(uri) { stream ->
        parser.parse(stream)
            .onSuccess { parseResult: BookmarksParseResult ->
                // use like:
                // parseResult.bookmarksCount
                // parseResult.foldersCount
                // parseResult.tree
            }
    }
}
```

## License

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/
