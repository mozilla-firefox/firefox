(add-a-check)=

# Adding a check

After you've completed a matcher using clang-query, it's time to take it to the next step and turn it into C++ and run it on the whole m-c codebase and see what happens.

Clang plugins live in {searchfox}`build/clang-plugin` and here we'll cover what is needed to add one. To see how the most recent check was added, you can look at the log for [Checks.inc](https://github.com/mozilla-firefox/firefox/commits/main/build/clang-plugin/Checks.inc) which is one of the necessary files to edit. That's also what we'll be covering next.

## Boilerplate Steps to Add a New Check

First pick a name. Pick something that makes sense without punctuation, in no more than 8 words or so. For this example we'll call it "Missing Else In Enum Comparisons".

1. Add it alphabetically in build/clang-plugin/Checks.inc, ChecksIncludes.inc, and moz.build
2. `cd build/clang-plugin && touch MissingElseInEnumComparisons.h MissingElseInEnumComparisons.cpp`
3. Copy the contents of an existing, simple .h file (e.g. {searchfox}`build/clang-plugin/ScopeChecker.h`) and edit the class name and header guards.
4. Create the following boilerplate for your implementation:

```
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MissingElseInEnumComparisons.h"
#include "CustomMatchers.h"

void MissingElseInEnumComparisons::registerMatchers(MatchFinder *AstMatcher) {

}

void MissingElseInEnumComparisons::check(const MatchFinder::MatchResult &Result) {

}
```

## Converting your matcher to C++

With the boilerplate out of the way, now we can focus on converting the matcher over to C++. Once it's in C++ you'll also be able to take advantage of techniques that will make your matcher easier to read and understand.

The gist of converting your matcher is to take the following pseudo-code and paste your entire matcher in where 'foo' is; keeping the `.bind("node")` there:

```
AstMatcher->addMatcher(
    traverse(TK_IgnoreUnlessSpelledInSource,
        foo
        .bind("node")),
    this);
```

It honest to god is usually that easy. Here's a working example where I pasted in straight from Compiler Explorer:

```
AstMatcher->addMatcher(
  traverse(TK_IgnoreUnlessSpelledInSource,
    ifStmt(allOf(
            has(
                 binaryOperator(
                     has(
                         declRefExpr(hasType(enumDecl().bind("enum")))
                     )
                 )
             ),
             hasElse(
                 ifStmt(allOf(
                    unless(hasElse(anything())),
                    has(
                         binaryOperator(
                             has(
                                 declRefExpr(hasType(enumDecl()))
                             )
                         )
                     )
                 ))
            )
         ))
        .bind("node")),
    this);
```

If for some reason you're not using the `IgnoreUnlessSpelledInSource` Traversal Mode, remove the call to traverse and the corresponding closing paren. (Also, if you're comparing this code to existing source code, know that because this traversal mode is a new clang feature, most historical clang checks do not use it.)

## Wiring up Warnings and Errors

To get started with a some simple output, just take the boilerplate warning here and stick it in:

```
const auto *MatchedDecl = Result.Nodes.getNodeAs<IfStmt>("node");
  diag(MatchedDecl->getIfLoc(),
      "Enum comparisons in an if/else if block without a trailing else.",
      DiagnosticIDs::Warning);
```

You'll need to edit two things:

1. Make sure "node" matches whatever you put in `.bind()` up above.
2. `getNodeAs<IfStmt>` needs to be changed to whatever type of element "node" is. Above, we bind "node" to an ifStmt so that's what we need to cast it to. Doing this step incorrectly will cause clang to crash during compilation as if there was some internal compiler error.

## Running it on Central

After this, the next thing to do is to add `ac_add_options --enable-clang-plugin` to your .mozconfig and do a build. Your plugin will be automatically compiled and used across the entire codebase. I suggest using `./mach build | tee output.txt` and then `grep "Enum comparisons" output.txt | cut -d " " -f 3- | sort | uniq`. (The `cut` is there to get rid of the timestamp in the line.)
