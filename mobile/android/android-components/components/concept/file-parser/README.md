# [android-components](../../../README.md) > Concept > File Parser

An abstract definition of a file-parser component.

## Usage

### Setting up the dependency

Use Gradle to download the library from maven.mozilla.org:

```Groovy
implementation "org.mozilla.components:concept-file-parser:{latest-version}"
```

### FileParser

`FileParser` is a functional interface for parsing files into a result of type `T`.

```kotlin
class CsvFileParser : FileParser<ParseResult> {
    override suspend fun parse(inputStream: InputStream): Result<ParseResult> {
        // Parse the file and return the result, or a failed Result with a FileParserException
        // carrying a FileParserError that describes the problem.
    }
}
```

## License

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/
