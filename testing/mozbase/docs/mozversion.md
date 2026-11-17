# {mod}`mozversion` --- Get application information

[mozversion](https://searchfox.org/firefox-main/source/testing/mozbase/mozversion)
provides version information such as the application name and the changesets
that it has been built from. This is commonly used in reporting or for
conditional logic based on the application under test.

## API Usage

```{eval-rst}
.. automodule:: mozversion
    :members: get_version
```

### Examples

Firefox:

```
import mozversion

version = mozversion.get_version(binary='/path/to/firefox')
for (key, value) in sorted(version.items()):
    if value:
        print '%s: %s' % (key, value)
```

Firefox for Android:

```
version = mozversion.get_version(binary='path/to/firefox.apk')
print version['application_changeset'] # gets source revision of build
```

## Command Line Usage

mozversion comes with a command line program, `mozversion` which may be used to
get version information from an application.

Usage:

```
mozversion [options]
```

### Options

#### ---binary

This is the path to the target application binary or .apk. If this is omitted
then the current directory is checked for the existence of an
application.ini file. If not found, then it is assumed the target
application is a remote Firefox OS instance.

### Examples

Firefox:

```
$ mozversion --binary=/path/to/firefox-bin
application_buildid: 20131205075310
application_changeset: 39faf812aaec
application_name: Firefox
application_repository: http://hg.mozilla.org/releases/mozilla-release
application_version: 26.0
platform_buildid: 20131205075310
platform_changeset: 39faf812aaec
platform_repository: http://hg.mozilla.org/releases/mozilla-release
```

Firefox for Android:

```
$ mozversion --binary=/path/to/firefox.apk
```
