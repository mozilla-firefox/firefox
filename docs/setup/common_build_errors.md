# Common Build Errors

When setting up Firefox, you may encounter some other build errors or
warnings that are not fatal. This document is to help you determine
if the error you're running into is a fatal one or not.

## Watchman unavailable

This is a warning and can be ignored.
[Watchman is a file watching service](https://facebook.github.io/watchman/)
that can speed up some interactions with Git.

## VSCode Java extension

If you happen to have the Java extension installed in VSCode, there's
a chance that the "Problems" tab in the integrated terminal will
display an error about `Cannot run program [...]/firefox/mach`.
This is because the extension does not know how to parse the mozilla-central
repository and specifically the `mach` command runner.
This will not prevent you from building and running Firefox.

## ERROR glean_core

This is a non-fatal error and will not prevent you from building and
running Firefox. You might see this text in the console after running
`./mach run`. The specific text might appear as:
`Error setting metrics feature config: [...]`.

## Region.sys.mjs

This is a non-fatal error, this will not prevent you from building and
running Firefox. You might see this text in the console after running
`./mach run` and the specific text might look like:
`console.error: Region.sys.mjs: Error fetching region`.
