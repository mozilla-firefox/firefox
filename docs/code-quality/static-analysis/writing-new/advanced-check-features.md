(advanced-check-features)=

# Advanced Check Features

This page covers additional ways to improve and extend the check you've added to build/clang-plugin.

## Adding Tests

No doubt you've seen the tests for existing checks in {searchfox}`build/clang-plugin/tests`. Adding tests is straightforward; and your reviewer should insist you do so. Simply copying the existing format of any test and how diagnostics are marked as expected.

One wrinkle - all clang plugin checks are applied to all tests. We try to write tests so that only one check applies to it. If you write a check that triggers on an existing test, try to fix the existing test slightly so the new check does not trigger on it.

## Using Bind To Output More Useful Information

You've probably been wondering what the heck `.bind()` is for. You've been seeing it all over the place but never has it actually been explained what it's for and when to use it.

`.bind()` is used to give a name to part of the AST discovered through your matcher, so you can use it later. Let's go back to our sample matcher:

```
AstMatcher->addMatcher(
  traverse(TK_IgnoreUnlessSpelledInSource,
    ifStmt(allOf(
            has(
                 binaryOperator(
                     has(
                         declRefExpr(hasType(enumDecl()))
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

Now the `.bind("node")` makes more sense. We're naming the If statement we matched, so we can refer to it later when we call `Result.Nodes.getNodeAs<IfStmt>("node")`.

Let's say we want to provide the *type* of the enum in our warning message. There are two enums we end up seeing in our matcher - the enum in the first if statement, and the enum in the second. We're going to arbitrarily pick the first and give it the name `enumType`:

```
AstMatcher->addMatcher(
  traverse(TK_IgnoreUnlessSpelledInSource,
    ifStmt(allOf(
            has(
                 binaryOperator(
                     has(
                         declRefExpr(hasType(enumDecl().bind("enumType")))
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

And in our check() function, we can use it like so:

```
void MissingElseInEnumComparisons::check(
    const MatchFinder::MatchResult &Result) {
  const auto *MatchedDecl = Result.Nodes.getNodeAs<IfStmt>("node");
  const auto *EnumType = Result.Nodes.getNodeAs<EnumDecl>("enumType");

  diag(MatchedDecl->getIfLoc(),
       "Enum comparisons to %0 in an if/else if block without a trailing else.",
       DiagnosticIDs::Warning) << EnumType->getName();
}
```

## Repeated matcher calls

If you find yourself repeating the same several matchers in several spots, you can turn it into a variable to use.

```
auto isTemporaryLifetimeBoundCall =
    cxxMemberCallExpr(
        onImplicitObjectArgument(anyOf(has(cxxTemporaryObjectExpr()),
                                       has(materializeTemporaryExpr()))),
        callee(functionDecl(isMozTemporaryLifetimeBound())));

auto hasTemporaryLifetimeBoundCall =
    anyOf(isTemporaryLifetimeBoundCall,
          conditionalOperator(
              anyOf(hasFalseExpression(isTemporaryLifetimeBoundCall),
                    hasTrueExpression(isTemporaryLifetimeBoundCall))));
```

The above example is parameter-less, but if you need to supply a parameter that changes, you can turn it into a lambda:

```
auto hasConstCharPtrParam = [](const unsigned int Position) {
  return hasParameter(
      Position, hasType(hasCanonicalType(pointsTo(asString("const char")))));
};

auto hasParamOfType = [](const unsigned int Position, const char *Name) {
  return hasParameter(Position, hasType(asString(Name)));
};

auto hasIntegerParam = [](const unsigned int Position) {
  return hasParameter(Position, hasType(isInteger()));
};

AstMatcher->addMatcher(
    callExpr(
      hasName("fopen"),
      hasConstCharPtrParam(0))
        .bind("funcCall"),
    this);
```

## Allow-listing existing callsites

While it's not a great situation, you can set up an allow-list of existing callsites if you need to. A simple allow-list is demonstrated in [NoGetPrincipalURI](https://github.com/mozilla-firefox/firefox/commit/474f063d6d83c11180800ecd86484f2dcf25af87). The [NoNewThreadsChecker](https://github.com/mozilla-firefox/firefox/commit/403fbc5757af3ed47226928b66ce1d41ba36b148) is an example of a more sophisticated way of setting up a larger allow-list.

## Custom Annotations

It's possible to create custom annotations that will be a no-op when compiled, but can be used by a static analysis check. These can be used to annotate special types of sources and sinks (for example). We have some examples of this in-tree presently (such as `MOZ_CAN_RUN_SCRIPT`) but currently don't have a detailed walkthrough in this documentation of how to set these up and use them. (Patches welcome.)
