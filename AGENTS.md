# Global instructions
Limit the amount of comments you put in the code to a strict minimum. You should almost never add comments, except sometimes on non-trivial code, function definitions if the arguments aren't self-explanatory, and class definitions and their members.

The Firefox repository is very big and so it isn't advised to blindly run grep or rg commands without specifying a narrow set of directories to search. There are tools available to help, see next section.

## Tooling for Firefox work
- Some tools useful for Firefox work are available in the `moz` MCP server
- Firefox is a very large repository, and it isn't efficient to search with usual tooling. When working on Firefox, you MUST use the `searchfox-cli` tool if you want to know about something. Its `--help` flag will show the options, but you probably want:
```
searchfox-cli --define 'AudioContext::AudioContext' # get function impl
searchfox-cli --define 'AudioSink' # get class definition
searchfox-cli --path ipdl -q 'MySearchTerm' # search for a text string, restrict on path
searchfox-cli --id AudioSink -l 150 --cpp # search for identifier audio sink in C++ code, 150 results max
```
- For C++, Rust and Java code, prefer searching for identifiers with `searchfox-cli`. Use text search restricted by path otherwise.
- Do not try to use identifier search for front-end identifiers like JS object or function names, CSS classes or HTML custom element names.
- `searchfox-cli`'s `--path` can only be provided once, but supports globs so you can combine a path with a file extension restriction.
- If you must use regular expressions with `searchfox-cli`, don't forget the `--regexp` flag.
- Use the `searchfox-cli` tool, only using `rg` or usual local tools if you need to find information about something
that has definitely changed locally. If you're unsure, ask.
- If you can't find something quickly, it is better to ask than run local searches.
- `./mach` is the main interface to the Mozilla build system and common developer tasks. Important commands are listed here, and you can run `./mach help` for a full list of commands. If you want additional details for a given command, you can run `./mach COMMAND --help`
- `./mach lint`: Run linters. Run it without additional parameters to lint all the files you have modified
- `./mach format`: Format code. Run it without additional parameters to format all the files you have modified
- `./mach build`: Build the project. Full builds can take a long time, up to tens of minutes.
- `./mach test --auto`: Run tests
- `./mach run`: Run the project
- `./mach doc --no-serve --no-open`: Build the documentation
- `treeherder-check`: Pull CI results for a try push
- Use the MCP resource `@moz:bugzilla://bug/{bug_id}` to retrieve a bug
- Use the MCP resource `@moz:phabricator://revision/D{revision_id}` to retrieve a Phabricator revision

## Fixing review comments
Use `@moz:phabricator://revision/D{revision_id}` to retrieve the revision and its comments.

You can find the review identifier by inspecting the commit log with:

- `jj log -T builtin_log_detailed` if using `jj`
- `git log -v -l 10` if using git

## Code Style
- Our style guide forbids the use of emoji.

## Workflow
- After making code changes, ensure the code is formatted by using `./mach format`, linted by using `./mach lint`, and build it using `./mach build`. If there are no errors, you can use `mach run` to ensure the browser still runs. Occasionally run `./mach clang-format -p path/to/modifiedfile path/to/othermodifiedfile` to format C++, `./mach lint --fix path/to/file` for most other files.
- You can run tests by using `./mach test --auto`. Once you are satisfied with the tests you run locally, use `mach try auto` to run tests in CI
- Ask if you should run a test. If you do, you probably want to run the test with `--headless`
- You may commit and push when asked to do so
- It's better to just run `./mach build` without subdirectory, the build system is well optimized
- Always build normally, never use subdirectories, e.g. `./mach build` is the only command to run
- Never build with a subdirectory. Always simply do `./mach build`
- The only exception is for Android and Desktop front-end-only changes, in which case you can use the special `./mach build faster` to skip all C++/Rust compilation.
