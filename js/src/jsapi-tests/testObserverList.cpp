/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gc/FinalizationObservers.h"
#include "jsapi-tests/tests.h"
#include "vm/JSContext.h"
#include "vm/NativeObject.h"

#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;
using namespace js::gc;

namespace {

class TestObserverListElement : public ObserverListObject {
 public:
  static const JSClass class_;

  static TestObserverListElement* New(JSContext* cx) {
    return NewObjectWithGivenProto<TestObserverListElement>(cx, nullptr);
  }

 private:
  static const JSClassOps classOps_;

  static void Finalize(JS::GCContext* gcx, JSObject* obj) {
    obj->as<TestObserverListElement>().unlink();
  }
};

const JSClassOps TestObserverListElement::classOps_ = {
    nullptr,   // addProperty
    nullptr,   // delProperty
    nullptr,   // enumerate
    nullptr,   // newEnumerate
    nullptr,   // resolve
    nullptr,   // mayResolve
    Finalize,  // finalize
    nullptr,   // call
    nullptr,   // construct
    nullptr,   // trace
};

const JSClass TestObserverListElement::class_ = {
    "TestObserverListElement",
    JSCLASS_HAS_RESERVED_SLOTS(ObserverListObject::SlotCount) |
        JSCLASS_FOREGROUND_FINALIZE,
    &classOps_, JS_NULL_CLASS_SPEC, &classExtension_};

}  // namespace

BEGIN_TEST(testObserverList_insertAndUnlink) {
  ObserverList list;
  CHECK(list.isEmpty());

  Rooted<TestObserverListElement*> a(cx, TestObserverListElement::New(cx));
  CHECK(a);
  CHECK(!a->isInList());

  list.insertFront(a);
  CHECK(!list.isEmpty());
  CHECK(a->isInList());
  CHECK(list.getFirst() == a);

  Rooted<TestObserverListElement*> b(cx, TestObserverListElement::New(cx));
  CHECK(b);
  CHECK(!b->isInList());

  list.insertFront(b);
  CHECK(!list.isEmpty());
  CHECK(b->isInList());
  CHECK(list.getFirst() == b);

  Rooted<TestObserverListElement*> c(cx, TestObserverListElement::New(cx));
  CHECK(c);
  list.insertFront(c);
  CHECK(list.getFirst() == c);

  // Unlink an element from the middle.
  b->unlink();
  CHECK(!b->isInList());
  CHECK(!list.isEmpty());
  CHECK(list.getFirst() == c);

  // Unlink the front element.
  c->unlink();
  CHECK(!c->isInList());
  CHECK(!list.isEmpty());
  CHECK(list.getFirst() == a);

  // Unlink the last remaining element.
  a->unlink();
  CHECK(!a->isInList());
  CHECK(list.isEmpty());

  return true;
}
END_TEST(testObserverList_insertAndUnlink)

BEGIN_TEST(testObserverList_move) {
  Rooted<TestObserverListElement*> a(cx, TestObserverListElement::New(cx));
  Rooted<TestObserverListElement*> b(cx, TestObserverListElement::New(cx));
  CHECK(a && b);

  // Move construction from an empty list.
  {
    ObserverList src;
    ObserverList dst(std::move(src));
    CHECK(src.isEmpty());
    CHECK(dst.isEmpty());
  }

  // Move construction from a non-empty list.
  {
    ObserverList src;
    src.insertFront(a);
    src.insertFront(b);
    CHECK(src.getFirst() == b);

    ObserverList dst(std::move(src));
    CHECK(src.isEmpty());
    CHECK(!dst.isEmpty());
    CHECK(dst.getFirst() == b);

    b->unlink();
    a->unlink();
    CHECK(dst.isEmpty());
  }

  // Move assignment from a non-empty list.
  {
    ObserverList src;
    src.insertFront(a);
    src.insertFront(b);

    ObserverList dst;
    dst = std::move(src);
    CHECK(src.isEmpty());
    CHECK(dst.getFirst() == b);

    b->unlink();
    a->unlink();
    CHECK(dst.isEmpty());
  }

  return true;
}
END_TEST(testObserverList_move)

BEGIN_TEST(testObserverList_append) {
  ObserverList list1;
  ObserverList list2;
  CHECK(list1.isEmpty());
  CHECK(list2.isEmpty());

  // Append empty to empty.
  list1.append(std::move(list2));
  CHECK(list1.isEmpty());
  CHECK(list2.isEmpty());

  Rooted<TestObserverListElement*> a(cx, TestObserverListElement::New(cx));
  Rooted<TestObserverListElement*> b(cx, TestObserverListElement::New(cx));
  Rooted<TestObserverListElement*> c(cx, TestObserverListElement::New(cx));
  CHECK(a);
  CHECK(b);
  CHECK(c);

  // Append empty to non-empty.
  list1.insertFront(a);
  list1.insertFront(b);
  list1.append(std::move(list2));
  CHECK(!list1.isEmpty());
  CHECK(list2.isEmpty());
  CHECK(list1.getFirst() == b);

  // Append non-empty to empty.
  list2.append(std::move(list1));
  CHECK(list1.isEmpty());
  CHECK(!list2.isEmpty());
  CHECK(list2.getFirst() == b);

  // Build list1 = [c, b], list2 = [a].
  a->unlink();
  b->unlink();
  CHECK(list2.isEmpty());
  list1.insertFront(b);
  list1.insertFront(c);
  list2.insertFront(a);

  // Append non-empty to non-empty: list1 becomes [c, b, a].
  list1.append(std::move(list2));
  CHECK(list2.isEmpty());
  CHECK(!list1.isEmpty());
  CHECK(list1.getFirst() == c);
  c->unlink();
  CHECK(list1.getFirst() == b);
  b->unlink();
  CHECK(list1.getFirst() == a);
  a->unlink();
  CHECK(list1.isEmpty());

  return true;
}
END_TEST(testObserverList_append)
