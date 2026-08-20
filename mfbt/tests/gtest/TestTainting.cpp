/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <array>
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"
#include "mozilla/Array.h"
#include "mozilla/Tainting.h"
#include "nsHashKeys.h"
#include "nsTArray.h"
#include "nsTHashtable.h"

using mozilla::Tainted;

#define EXPECTED_INT 10
#define EXPECTED_CHAR 'z'

static bool externalFunction(int arg) { return arg > 2; }

// ==================================================================
// MOZ_VALIDATE_AND_GET =============================================
TEST(Tainting, moz_validate_and_get)
{
  int bar;
  int comparisonVariable = 20;
  Tainted<int> foo = Tainted<int>(EXPECTED_INT);

  bar = MOZ_VALIDATE_AND_GET(foo, foo < 20);
  ASSERT_EQ(bar, EXPECTED_INT);

  // This test is for comparison to an external variable, testing the
  // default capture mode of the lambda used inside the macro.
  bar = MOZ_VALIDATE_AND_GET(foo, foo < comparisonVariable);
  ASSERT_EQ(bar, EXPECTED_INT);

  bar = MOZ_VALIDATE_AND_GET(
      foo, foo < 20,
      "foo must be less than 20 because higher values represent decibel"
      "levels greater than a a jet engine inside your ear.");
  ASSERT_EQ(bar, EXPECTED_INT);

  // Test an external variable with a comment.
  bar = MOZ_VALIDATE_AND_GET(foo, foo < comparisonVariable, "Test comment");
  ASSERT_EQ(bar, EXPECTED_INT);

  // Test an external function with a comment.
  bar = MOZ_VALIDATE_AND_GET(foo, externalFunction(foo), "Test comment");
  ASSERT_EQ(bar, EXPECTED_INT);

  // Lambda Tests
  bar =
      MOZ_VALIDATE_AND_GET(foo, ([&foo]() { return externalFunction(foo); }()));
  ASSERT_EQ(bar, EXPECTED_INT);

  // This test is for the lambda variant with a supplied assertion
  // string.
  bar =
      MOZ_VALIDATE_AND_GET(foo, ([&foo]() { return externalFunction(foo); }()),
                           "This tests a comment");
  ASSERT_EQ(bar, EXPECTED_INT);

  // This test is for the lambda variant with a captured variable
  bar = MOZ_VALIDATE_AND_GET(foo, ([&foo, &comparisonVariable] {
                               bool intermediateResult = externalFunction(foo);
                               return intermediateResult ||
                                      comparisonVariable < 4;
                             }()),
                             "This tests a comment");
  ASSERT_EQ(bar, EXPECTED_INT);

  // This test is for the lambda variant with full capture mode
  bar = MOZ_VALIDATE_AND_GET(foo, ([&] {
                               bool intermediateResult = externalFunction(foo);
                               return intermediateResult ||
                                      comparisonVariable < 4;
                             }()),
                             "This tests a comment");
  ASSERT_EQ(bar, EXPECTED_INT);

  // External lambdas
  auto lambda1 = [](int foo) { return externalFunction(foo); };

  auto lambda2 = [&](int foo) {
    bool intermediateResult = externalFunction(foo);
    return intermediateResult || comparisonVariable < 4;
  };

  // Test with an explicit capture
  auto lambda3 = [&comparisonVariable](int foo) {
    bool intermediateResult = externalFunction(foo);
    return intermediateResult || comparisonVariable < 4;
  };

  bar = MOZ_VALIDATE_AND_GET(foo, lambda1(foo));
  ASSERT_EQ(bar, EXPECTED_INT);

  // Test with a comment
  bar = MOZ_VALIDATE_AND_GET(foo, lambda1(foo), "Test comment.");
  ASSERT_EQ(bar, EXPECTED_INT);

  // Test with a default capture mode
  bar = MOZ_VALIDATE_AND_GET(foo, lambda2(foo), "Test comment.");
  ASSERT_EQ(bar, EXPECTED_INT);

  bar = MOZ_VALIDATE_AND_GET(foo, lambda3(foo), "Test comment.");
  ASSERT_EQ(bar, EXPECTED_INT);

  // We can't test MOZ_VALIDATE_AND_GET failing, because that triggers
  // a release assert.
}

// ==================================================================
// MOZ_IS_VALID =====================================================
TEST(Tainting, moz_is_valid)
{
  int comparisonVariable = 20;
  Tainted<int> foo = Tainted<int>(EXPECTED_INT);

  ASSERT_TRUE(MOZ_IS_VALID(foo, foo < 20));

  ASSERT_FALSE(MOZ_IS_VALID(foo, foo > 20));

  ASSERT_TRUE(MOZ_IS_VALID(foo, foo < comparisonVariable));

  ASSERT_TRUE(
      MOZ_IS_VALID(foo, ([&foo]() { return externalFunction(foo); }())));

  ASSERT_TRUE(MOZ_IS_VALID(foo, ([&foo, &comparisonVariable]() {
                             bool intermediateResult = externalFunction(foo);
                             return intermediateResult ||
                                    comparisonVariable < 4;
                           }())));

  // External lambdas
  auto lambda1 = [](int foo) { return externalFunction(foo); };

  auto lambda2 = [&](int foo) {
    bool intermediateResult = externalFunction(foo);
    return intermediateResult || comparisonVariable < 4;
  };

  // Test with an explicit capture
  auto lambda3 = [&comparisonVariable](int foo) {
    bool intermediateResult = externalFunction(foo);
    return intermediateResult || comparisonVariable < 4;
  };

  ASSERT_TRUE(MOZ_IS_VALID(foo, lambda1(foo)));

  ASSERT_TRUE(MOZ_IS_VALID(foo, lambda2(foo)));

  ASSERT_TRUE(MOZ_IS_VALID(foo, lambda3(foo)));
}

// ==================================================================
// MOZ_VALIDATE_OR ==================================================
TEST(Tainting, moz_validate_or)
{
  int result;
  int comparisonVariable = 20;
  Tainted<int> foo = Tainted<int>(EXPECTED_INT);

  result = MOZ_VALIDATE_OR(foo, foo < 20, 100);
  ASSERT_EQ(result, EXPECTED_INT);

  result = MOZ_VALIDATE_OR(foo, foo > 20, 100);
  ASSERT_EQ(result, 100);

  result = MOZ_VALIDATE_OR(foo, foo < comparisonVariable, 100);
  ASSERT_EQ(result, EXPECTED_INT);

  // External lambdas
  auto lambda1 = [](int foo) { return externalFunction(foo); };

  auto lambda2 = [&](int foo) {
    bool intermediateResult = externalFunction(foo);
    return intermediateResult || comparisonVariable < 4;
  };

  // Test with an explicit capture
  auto lambda3 = [&comparisonVariable](int foo) {
    bool intermediateResult = externalFunction(foo);
    return intermediateResult || comparisonVariable < 4;
  };

  result = MOZ_VALIDATE_OR(foo, lambda1(foo), 100);
  ASSERT_EQ(result, EXPECTED_INT);

  result = MOZ_VALIDATE_OR(foo, lambda2(foo), 100);
  ASSERT_EQ(result, EXPECTED_INT);

  result = MOZ_VALIDATE_OR(foo, lambda3(foo), 100);
  ASSERT_EQ(result, EXPECTED_INT);

  result =
      MOZ_VALIDATE_OR(foo, ([&foo]() { return externalFunction(foo); }()), 100);
  ASSERT_EQ(result, EXPECTED_INT);

  // This test is for the lambda variant with a supplied assertion
  // string.
  result =
      MOZ_VALIDATE_OR(foo, ([&foo] { return externalFunction(foo); }()), 100);
  ASSERT_EQ(result, EXPECTED_INT);

  // This test is for the lambda variant with a captured variable
  result =
      MOZ_VALIDATE_OR(foo, ([&foo, &comparisonVariable] {
                        bool intermediateResult = externalFunction(foo);
                        return intermediateResult || comparisonVariable < 4;
                      }()),
                      100);
  ASSERT_EQ(result, EXPECTED_INT);

  // This test is for the lambda variant with full capture mode
  result =
      MOZ_VALIDATE_OR(foo, ([&] {
                        bool intermediateResult = externalFunction(foo);
                        return intermediateResult || comparisonVariable < 4;
                      }()),
                      100);
  ASSERT_EQ(result, EXPECTED_INT);
}

// ==================================================================
// MOZ_FIND_AND_VALIDATE ============================================
TEST(Tainting, moz_find_and_validate)
{
  Tainted<int> foo = Tainted<int>(EXPECTED_INT);
  Tainted<char> baz = Tainted<char>(EXPECTED_CHAR);

  //-------------------------------
  const mozilla::Array<int, 6> mozarrayWithFoo(0, 5, EXPECTED_INT, 15, 20, 25);
  const mozilla::Array<int, 5> mozarrayWithoutFoo(0, 5, 15, 20, 25);

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(foo, list_item == foo, mozarrayWithFoo) ==
              mozarrayWithFoo[2]);

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(foo, list_item == foo,
                                    mozarrayWithoutFoo) == nullptr);

  //-------------------------------
  class TestClass {
   public:
    int a;
    int b;

    TestClass(int a, int b) {
      this->a = a;
      this->b = b;
    }

    bool operator==(const TestClass& other) const = default;
  };

  const mozilla::Array<TestClass, 5> mozarrayOfClassesWithFoo(
      TestClass(0, 1), TestClass(2, 3), TestClass(EXPECTED_INT, EXPECTED_INT),
      TestClass(4, 5), TestClass(6, 7));

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(
                  foo, foo == list_item.a && foo == list_item.b,
                  mozarrayOfClassesWithFoo) == mozarrayOfClassesWithFoo[2]);

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(
                  foo, (foo == list_item.a && foo == list_item.b),
                  mozarrayOfClassesWithFoo) == mozarrayOfClassesWithFoo[2]);

  ASSERT_TRUE(
      *MOZ_FIND_AND_VALIDATE(
          foo,
          (foo == list_item.a && foo == list_item.b && externalFunction(foo)),
          mozarrayOfClassesWithFoo) == mozarrayOfClassesWithFoo[2]);

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(
                  foo, ([](int tainted_val, TestClass list_item) {
                    return tainted_val == list_item.a &&
                           tainted_val == list_item.b;
                  }(foo, list_item)),
                  mozarrayOfClassesWithFoo) == mozarrayOfClassesWithFoo[2]);

  auto lambda4 = [](int tainted_val, TestClass list_item) {
    return tainted_val == list_item.a && tainted_val == list_item.b;
  };

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(foo, lambda4(foo, list_item),
                                     mozarrayOfClassesWithFoo) ==
              mozarrayOfClassesWithFoo[2]);

  //-------------------------------
  const char m[] = "m";
  const char o[] = "o";
  const char z[] = {EXPECTED_CHAR, '\0'};
  const char l[] = "l";
  const char a[] = "a";

  nsTHashtable<nsCharPtrHashKey> hashtableWithBaz;
  hashtableWithBaz.PutEntry(m);
  hashtableWithBaz.PutEntry(o);
  hashtableWithBaz.PutEntry(z);
  hashtableWithBaz.PutEntry(l);
  hashtableWithBaz.PutEntry(a);
  nsTHashtable<nsCharPtrHashKey> hashtableWithoutBaz;
  hashtableWithoutBaz.PutEntry(m);
  hashtableWithoutBaz.PutEntry(o);
  hashtableWithoutBaz.PutEntry(l);
  hashtableWithoutBaz.PutEntry(a);

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(baz, *list_item.GetKey() == baz,
                                    hashtableWithBaz) ==
              hashtableWithBaz.GetEntry(z));

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(baz, *list_item.GetKey() == baz,
                                    hashtableWithoutBaz) == nullptr);

  //-------------------------------
  const nsTArray<int> nsTArrayWithFoo = {0, 5, EXPECTED_INT, 15, 20, 25};
  const nsTArray<int> nsTArrayWithoutFoo = {0, 5, 15, 20, 25};

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(foo, list_item == foo, nsTArrayWithFoo) ==
              nsTArrayWithFoo[2]);

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(foo, list_item == foo,
                                    nsTArrayWithoutFoo) == nullptr);

  //-------------------------------
  const std::array<int, 6> arrayWithFoo{0, 5, EXPECTED_INT, 15, 20, 25};
  const std::array<int, 5> arrayWithoutFoo{0, 5, 15, 20, 25};

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(foo, list_item == foo, arrayWithFoo) ==
              arrayWithFoo[2]);

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(foo, list_item == foo, arrayWithoutFoo) ==
              nullptr);

  //-------------------------------
  const std::deque<int> dequeWithFoo{0, 5, EXPECTED_INT, 15, 20, 25};
  const std::deque<int> dequeWithoutFoo{0, 5, 15, 20, 25};

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(foo, list_item == foo, dequeWithFoo) ==
              dequeWithFoo[2]);

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(foo, list_item == foo, dequeWithoutFoo) ==
              nullptr);

  //-------------------------------
  const std::forward_list<int> forwardWithFoo{0, 5, EXPECTED_INT, 15, 20, 25};
  const std::forward_list<int> forwardWithoutFoo{0, 5, 15, 20, 25};

  auto forwardListIt = forwardWithFoo.begin();
  std::advance(forwardListIt, 2);

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(foo, list_item == foo, forwardWithFoo) ==
              *forwardListIt);

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(foo, list_item == foo, forwardWithoutFoo) ==
              nullptr);

  //-------------------------------
  const std::list<int> listWithFoo{0, 5, EXPECTED_INT, 15, 20, 25};
  const std::list<int> listWithoutFoo{0, 5, 15, 20, 25};

  auto listIt = listWithFoo.begin();
  std::advance(listIt, 2);

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(foo, list_item == foo, listWithFoo) ==
              *listIt);

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(foo, list_item == foo, listWithoutFoo) ==
              nullptr);

  //-------------------------------
  const std::map<std::string, int> mapWithFoo{{
      {"zero", 0},
      {"five", 5},
      {"ten", EXPECTED_INT},
      {"fifteen", 15},
      {"twenty", 20},
      {"twenty-five", 25},
  }};
  const std::map<std::string, int> mapWithoutFoo{{
      {"zero", 0},
      {"five", 5},
      {"fifteen", 15},
      {"twenty", 20},
      {"twenty-five", 25},
  }};

  const auto map_it = mapWithFoo.find("ten");

  ASSERT_TRUE(
      MOZ_FIND_AND_VALIDATE(foo, list_item.second == foo, mapWithFoo)->second ==
      map_it->second);

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(foo, list_item.second == foo,
                                    mapWithoutFoo) == nullptr);

  //-------------------------------
  const std::set<int> setWithFoo{0, 5, EXPECTED_INT, 15, 20, 25};
  const std::set<int> setWithoutFoo{0, 5, 15, 20, 25};

  auto setIt = setWithFoo.find(EXPECTED_INT);

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(foo, list_item == foo, setWithFoo) ==
              *setIt);

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(foo, list_item == foo, setWithoutFoo) ==
              nullptr);

  //-------------------------------
  const std::unordered_map<std::string, int> unordermapWithFoo = {
      {"zero", 0},     {"five", 5},    {"ten", EXPECTED_INT},
      {"fifteen", 15}, {"twenty", 20}, {"twenty-five", 25},
  };
  const std::unordered_map<std::string, int> unordermapWithoutFoo{{
      {"zero", 0},
      {"five", 5},
      {"fifteen", 15},
      {"twenty", 20},
      {"twenty-five", 25},
  }};

  auto unorderedMapIt = unordermapWithFoo.find("ten");

  ASSERT_TRUE(
      MOZ_FIND_AND_VALIDATE(foo, list_item.second == foo, unordermapWithFoo)
          ->second == unorderedMapIt->second);

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(foo, list_item.second == foo,
                                    unordermapWithoutFoo) == nullptr);

  //-------------------------------
  const std::unordered_set<int> unorderedsetWithFoo{0,  5,  EXPECTED_INT,
                                                    15, 20, 25};
  const std::unordered_set<int> unorderedsetWithoutFoo{0, 5, 15, 20, 25};

  auto unorderedSetIt = unorderedsetWithFoo.find(EXPECTED_INT);

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(foo, list_item == foo,
                                     unorderedsetWithFoo) == *unorderedSetIt);

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(foo, list_item == foo,
                                    unorderedsetWithoutFoo) == nullptr);

  //-------------------------------
  const std::vector<int> vectorWithFoo{0, 5, EXPECTED_INT, 15, 20, 25};
  const std::vector<int> vectorWithoutFoo{0, 5, 15, 20, 25};

  ASSERT_TRUE(*MOZ_FIND_AND_VALIDATE(foo, list_item == foo, vectorWithFoo) ==
              vectorWithFoo[2]);

  ASSERT_TRUE(MOZ_FIND_AND_VALIDATE(foo, list_item == foo, vectorWithoutFoo) ==
              nullptr);
}

// ==================================================================
// MOZ_NO_VALIDATE ==================================================
TEST(Tainting, moz_no_validate)
{
  int result;
  Tainted<int> foo = Tainted<int>(EXPECTED_INT);

  result = MOZ_NO_VALIDATE(
      foo,
      "Value is used to match against a dictionary key in the parent."
      "If there's no key present, there won't be a match."
      "There is no risk of grabbing a cross-origin value from the dictionary,"
      "because the IPC actor is instatiated per-content-process and the "
      "dictionary is not shared between actors.");
  ASSERT_TRUE(result == EXPECTED_INT);
}
