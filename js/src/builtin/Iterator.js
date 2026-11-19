/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

function IteratorIdentity() {
  return this;
}

/* ECMA262 7.2.7 */
function IteratorNext(iteratorRecord, value) {
  // Steps 1-2.
  var result =
    ArgumentsLength() < 2
      ? callContentFunction(iteratorRecord.nextMethod, iteratorRecord.iterator)
      : callContentFunction(
        iteratorRecord.nextMethod,
        iteratorRecord.iterator,
        value
      );
  // Step 3.
  if (!IsObject(result)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, result);
  }
  // Step 4.
  return result;
}

// https://tc39.es/ecma262/#sec-getiterator
function GetIterator(obj, isAsync, method) {
  // Step 1. If hint is not present, set hint to sync.
  // Step 2. If method is not present, then
  if (!method) {
    // Step 2.a. If hint is async, then
    if (isAsync) {
      // Step 2.a.i. Set method to ? GetMethod(obj, @@asyncIterator).
      method = GetMethod(obj, GetBuiltinSymbol("asyncIterator"));

      // Step 2.a.ii. If method is undefined, then
      if (!method) {
        // Step 2.a.ii.1. Let syncMethod be ? GetMethod(obj, @@iterator).
        var syncMethod = GetMethod(obj, GetBuiltinSymbol("iterator"));

        // Step 2.a.ii.2. Let syncIteratorRecord be ? GetIterator(obj, sync, syncMethod).
        var syncIteratorRecord = GetIterator(obj, false, syncMethod);

        // Step 2.a.ii.2. Return CreateAsyncFromSyncIterator(syncIteratorRecord).
        return CreateAsyncFromSyncIterator(syncIteratorRecord.iterator, syncIteratorRecord.nextMethod);
      }
    } else {
      // Step 2.b. Otherwise, set method to ? GetMethod(obj, @@iterator).
      method = GetMethod(obj, GetBuiltinSymbol("iterator"));
    }
  }

  // Step 3. Let iterator be ? Call(method, obj).
  var iterator = callContentFunction(method, obj);

  // Step 4. If Type(iterator) is not Object, throw a TypeError exception.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_NOT_ITERABLE, obj === null ? "null" : typeof obj);
  }

  // Step 5. Let nextMethod be ? GetV(iterator, "next").
  var nextMethod = iterator.next;

  // Step 6. Let iteratorRecord be the Record { [[Iterator]]: iterator, [[NextMethod]]: nextMethod, [[Done]]: false }.
  var iteratorRecord = {
    __proto__: null,
    iterator,
    nextMethod,
    done: false,
  };

  // Step 7. Return iteratorRecord.
  return iteratorRecord;
}

/**
 * GetIteratorFlattenable ( obj, stringHandling )
 *
 * https://tc39.es/ecma262/#sec-getiteratorflattenable
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function GetIteratorFlattenable(obj, rejectStrings) {
  assert(typeof rejectStrings === "boolean", "rejectStrings is a boolean");

  // Step 1.
  if (!IsObject(obj)) {
    // Steps 1.a-c.
    if (rejectStrings || typeof obj !== "string") {
      ThrowTypeError(JSMSG_OBJECT_REQUIRED, obj === null ? "null" : typeof obj);
    }
  }

  // Step 2.
  var method = obj[GetBuiltinSymbol("iterator")];

  // Steps 3-4.
  var iterator;
  if (IsNullOrUndefined(method)) {
    iterator = obj;
  } else {
    iterator = callContentFunction(method, obj);
  }

  // Step 5.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Step 6. (Caller must call GetIteratorDirect.)
  return iterator;
}

/**
 * Iterator.from ( O )
 *
 * https://tc39.es/ecma262/#sec-iterator.from
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function IteratorFrom(O) {
  // Step 1. (Inlined call to GetIteratorDirect.)
  var iterator = GetIteratorFlattenable(O, /* rejectStrings= */ false);
  var nextMethod = iterator.next;

  // Step 2.
  //
  // Calls |isPrototypeOf| instead of |instanceof| to avoid looking up the
  // `@@hasInstance` property.
  var hasInstance = callFunction(
    std_Object_isPrototypeOf,
    GetBuiltinPrototype("Iterator"),
    iterator
  );

  // Step 3.
  if (hasInstance) {
    return iterator;
  }

  // Step 4.
  var wrapper = NewWrapForValidIterator();

  // Step 5.
  UnsafeSetReservedSlot(
    wrapper,
    WRAP_FOR_VALID_ITERATOR_ITERATOR_SLOT,
    iterator
  );
  UnsafeSetReservedSlot(
    wrapper,
    WRAP_FOR_VALID_ITERATOR_NEXT_METHOD_SLOT,
    nextMethod
  );

  // Step 6.
  return wrapper;
}

/**
 * %WrapForValidIteratorPrototype%.next ( )
 *
 * https://tc39.es/ecma262/#sec-%wrapforvaliditeratorprototype%.next
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function WrapForValidIteratorNext() {
  // Steps 1-2.
  var O = this;
  if (!IsObject(O) || (O = GuardToWrapForValidIterator(O)) === null) {
    return callFunction(
      CallWrapForValidIteratorMethodIfWrapped,
      this,
      "WrapForValidIteratorNext"
    );
  }

  // Step 3.
  var iterator = UnsafeGetReservedSlot(O, WRAP_FOR_VALID_ITERATOR_ITERATOR_SLOT);
  var nextMethod = UnsafeGetReservedSlot(O, WRAP_FOR_VALID_ITERATOR_NEXT_METHOD_SLOT);

  // Step 4.
  return callContentFunction(nextMethod, iterator);
}

/**
 * %WrapForValidIteratorPrototype%.return ( )
 *
 * https://tc39.es/ecma262/#sec-%wrapforvaliditeratorprototype%.return
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function WrapForValidIteratorReturn() {
  // Steps 1-2.
  var O = this;
  if (!IsObject(O) || (O = GuardToWrapForValidIterator(O)) === null) {
    return callFunction(
      CallWrapForValidIteratorMethodIfWrapped,
      this,
      "WrapForValidIteratorReturn"
    );
  }

  // Step 3.
  var iterator = UnsafeGetReservedSlot(O, WRAP_FOR_VALID_ITERATOR_ITERATOR_SLOT);

  // Step 4.
  assert(IsObject(iterator), "iterator is an object");

  // Step 5.
  var returnMethod = iterator.return;

  // Step 6.
  if (IsNullOrUndefined(returnMethod)) {
    return {
      value: undefined,
      done: true,
    };
  }

  // Step 7.
  return callContentFunction(returnMethod, iterator);
}

/**
 * Explicit Resource Management Proposal
 * 27.1.2.1 %IteratorPrototype% [ @@dispose ] ( )
 * https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-%25iteratorprototype%25-%40%40dispose
 */
function IteratorDispose() {
  // Step 1. Let O be the this value.
  var O = this;

  // Step 2. Let return be ? GetMethod(O, "return").
  var returnMethod = GetMethod(O, "return");

  // Step 3. If return is not undefined, then
  if (returnMethod !== undefined) {
    // Step 3.a. Perform ? Call(return, O, « »).
    callContentFunction(returnMethod, O);
  }

  // Step 4. Return NormalCompletion(empty). (implicit)
}

/**
 * %IteratorHelperPrototype%.next ( )
 *
 * https://tc39.es/ecma262/#sec-%iteratorhelperprototype%.next
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function IteratorHelperNext() {
  // Step 1.
  var O = this;
  if (!IsObject(O) || (O = GuardToIteratorHelper(O)) === null) {
    return callFunction(
      CallIteratorHelperMethodIfWrapped,
      this,
      "IteratorHelperNext"
    );
  }
  var generator = UnsafeGetReservedSlot(O, ITERATOR_HELPER_GENERATOR_SLOT);
  return callFunction(GeneratorNext, generator, undefined);
}

/**
 * %IteratorHelperPrototype%.return ( )
 *
 * https://tc39.es/ecma262/#sec-%iteratorhelperprototype%.return
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function IteratorHelperReturn() {
  // Step 1.
  var O = this;

  // Step 2.
  if (!IsObject(O) || (O = GuardToIteratorHelper(O)) === null) {
    return callFunction(
      CallIteratorHelperMethodIfWrapped,
      this,
      "IteratorHelperReturn"
    );
  }

  // Step 3. (Implicit)

  // Step 4 (Partial). If O.[[GeneratorState]] is suspended-start, then
  //
  // Retrieve the current resume index before calling GeneratorReturn.
  var generator = UnsafeGetReservedSlot(O, ITERATOR_HELPER_GENERATOR_SLOT);
  var resumeIndex = UnsafeGetReservedSlot(generator, GENERATOR_RESUME_INDEX_SLOT);
  assert(
    resumeIndex === undefined ||
      resumeIndex === null ||
      typeof resumeIndex === "number",
    "unexpected resumeIndex value"
  );

  // If the generator was suspended at the initial yield, then the generator
  // state is "suspended-start".
  var isSuspendedStart = resumeIndex === GENERATOR_RESUME_INDEX_INITIAL_YIELD;
  assert(
    !isSuspendedStart || IsSuspendedGenerator(generator),
    "unexpected 'suspended-start' state for non-suspended generator"
  );

  // Step 4.a. Set O.[[GeneratorState]] to completed.
  // Step 4.b. NOTE: (elided)
  // Step 4.d. Return CreateIteratorResultObject(undefined, true).
  // Step 5. Let C be ReturnCompletion(undefined).
  // Step 6. Return ? GeneratorResumeAbrupt(O, C, "Iterator Helper").
  var result = callFunction(GeneratorReturn, generator, undefined);

  // Step 4 (Cont'ed). If O.[[GeneratorState]] is suspended-start, then
  //
  // Performed after GeneratorReturn, so even if IteratorClose throws an error,
  // it's not possible to re-enter the generator.
  if (isSuspendedStart) {
    var underlyingIterator = UnsafeGetReservedSlot(O, ITERATOR_HELPER_UNDERLYING_ITERATOR_SLOT);
    assert(
      underlyingIterator === undefined || IsObject(underlyingIterator),
      "underlyingIterator is undefined or an object"
    );

    // Step 4.c. Perform ? IteratorClose(O.[[UnderlyingIterator]], NormalCompletion(unused)).
    //
    // NB: |underlyingIterator| can be `undefined` for IteratorConcat.
    if (IsObject(underlyingIterator)) {
      IteratorClose(underlyingIterator);
    }
  }

  return result;
}

// Lazy %Iterator.prototype% methods
//
// In order to match the semantics of the built-in generator objects, we use a
// reserved slot on the IteratorHelper objects to store a regular generator that
// is called from the %IteratorHelper.prototype% methods.
//
// Each of the lazy methods is divided into a prelude and a body, with the
// eager prelude steps being contained in the corresponding IteratorX method
// and the lazy body steps inside the IteratorXGenerator generator functions.
//
// Each prelude method initializes and returns a new IteratorHelper object.
// As part of this initialization process, the appropriate generator function
// is called and stored in the IteratorHelper object, alongside the underlying
// iterator object.

/**
 * Iterator.prototype.map ( mapper )
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.map
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function IteratorMap(mapper) {
  // Step 1.
  var iterator = this;

  // Step 2.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Steps 3-4.
  if (!IsCallable(mapper)) {
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, mapper));
  }

  // Step 5. (Inlined call to GetIteratorDirect.)
  var nextMethod = iterator.next;

  // Steps 6-8.
  var result = NewIteratorHelper();
  var generator = IteratorMapGenerator(iterator, nextMethod, mapper);
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_GENERATOR_SLOT,
    generator
  );
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_UNDERLYING_ITERATOR_SLOT,
    iterator
  );

  // Step 9.
  return result;
}

/**
 * Iterator.prototype.map ( mapper )
 *
 * Abstract closure definition.
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.map
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function* IteratorMapGenerator(iterator, nextMethod, mapper) {
  // Step 6.a.
  var counter = 0;

  // Step 6.b.
  for (var value of allowContentIterWithNext(iterator, nextMethod)) {
    // Steps 6.b.i-ii. (Implicit through for-of loop)

    // Step 6.b.iii.
    var mapped = callContentFunction(mapper, undefined, value, counter);

    // Step 6.b.iv. (Implicit through for-of loop)

    // Step 6.b.v.
    yield mapped;

    // Step 6.b.vi. (Implicit through for-of loop)

    // Step 6.b.vii.
    counter += 1;
  }
}

/**
 * Iterator.prototype.filter ( predicate )
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.filter
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function IteratorFilter(predicate) {
  // Step 1.
  var iterator = this;

  // Step 2.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Steps 3-4.
  if (!IsCallable(predicate)) {
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, predicate));
  }

  // Step 5. (Inlined call to GetIteratorDirect.)
  var nextMethod = iterator.next;

  // Steps 6-8.
  var result = NewIteratorHelper();
  var generator = IteratorFilterGenerator(iterator, nextMethod, predicate);
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_GENERATOR_SLOT,
    generator
  );
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_UNDERLYING_ITERATOR_SLOT,
    iterator
  );

  // Step 9.
  return result;
}

/**
 * Iterator.prototype.filter ( predicate )
 *
 * Abstract closure definition.
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.filter
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function* IteratorFilterGenerator(iterator, nextMethod, predicate) {
  // Step 6.a.
  var counter = 0;

  // Step 6.b.
  for (var value of allowContentIterWithNext(iterator, nextMethod)) {
    // Steps 6.b.i-ii. (Implicit through for-of loop)

    // Step 6.b.iii.
    var selected = callContentFunction(predicate, undefined, value, counter);

    // Step 6.b.iv. (Implicit through for-of loop)

    // Step 6.b.v.
    if (selected) {
      // Step 6.b.v.1.
      yield value;

      // Step 6.b.v.2. (Implicit through for-of loop)
    }

    // Step 6.b.vi.
    counter += 1;
  }
}

/**
 * Iterator.prototype.take ( limit )
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.take
 */
function IteratorTake(limit) {
  // Step 1.
  var iterator = this;

  // Step 2.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Steps 3-5.
  var numLimit;
  try {
    numLimit = +limit;
  } catch (e) {
    try {
      IteratorClose(iterator);
    } catch {}
    throw e;
  }

  // Steps 6-7.
  if (
    Number_isNaN(numLimit) ||
    (Number_isFinite(numLimit) && numLimit > (2 ** 53) - 1)
  ) {
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowRangeError(JSMSG_LIMIT_TOO_LARGE);
  }

  // Step 8.
  var integerLimit = std_Math_trunc(numLimit);

  // Step 9.
  if (!(integerLimit >= 0)) {
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowRangeError(JSMSG_NEGATIVE_LIMIT);
  }

  // Step 10. (Inlined call to GetIteratorDirect.)
  var nextMethod = iterator.next;

  // Steps 11-13.
  var result = NewIteratorHelper();
  var generator = IteratorTakeGenerator(iterator, nextMethod, integerLimit);
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_GENERATOR_SLOT,
    generator
  );
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_UNDERLYING_ITERATOR_SLOT,
    iterator
  );

  // Step 14.
  return result;
}

/**
 * Iterator.prototype.take ( limit )
 *
 * Abstract closure definition.
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.take
 */
function* IteratorTakeGenerator(iterator, nextMethod, remaining) {
  // Step 11.a. (Implicit)

  // Steps 11.b.i and 11.b.i.1. (Reordered before for-of loop entry)
  if (remaining === 0) {
    IteratorClose(iterator);
    return;
  }

  // Step 11.b.
  for (var value of allowContentIterWithNext(iterator, nextMethod)) {
    // Steps 11.b.iii-iv. (Implicit through for-of loop)

    // Step 11.b.v.
    yield value;

    // Step 11.b.vi. (Implicit through for-of loop)

    // Steps 11.b.ii, 11.b.i, and 11.b.i.1. (Reordered)
    if (--remaining === 0) {
      // |break| implicitly calls IteratorClose.
      break;
    }
  }
}

/**
 * Iterator.prototype.drop ( limit )
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.drop
 */
function IteratorDrop(limit) {
  // Step 1.
  var iterator = this;

  // Step 2.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Steps 3-5.
  var numLimit;
  try {
    numLimit = +limit;
  } catch (e) {
    try {
      IteratorClose(iterator);
    } catch {}
    throw e;
  }

  // Steps 6-7.
  if (
    Number_isNaN(numLimit) ||
    (Number_isFinite(numLimit) && numLimit > (2 ** 53) - 1)
  ) {
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowRangeError(JSMSG_LIMIT_TOO_LARGE);
  }

  // Step 8.
  var integerLimit = std_Math_trunc(numLimit);

  // Step 9.
  if (!(integerLimit >= 0)) {
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowRangeError(JSMSG_NEGATIVE_LIMIT);
  }

  // Step 10. (Inlined call to GetIteratorDirect.)
  var nextMethod = iterator.next;

  // Steps 11-13.
  var result = NewIteratorHelper();
  var generator = IteratorDropGenerator(iterator, nextMethod, integerLimit);
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_GENERATOR_SLOT,
    generator
  );
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_UNDERLYING_ITERATOR_SLOT,
    iterator
  );

  // Step 14.
  return result;
}

/**
 * Iterator.prototype.drop ( limit )
 *
 * Abstract closure definition.
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.drop
 */
function* IteratorDropGenerator(iterator, nextMethod, remaining) {
  // Step 11.a. (Implicit)

  // Step 11.b.
  while (remaining > 0) {
    // Step 11.b.i.
    remaining--;

    // Step 11.b.ii.
    var next = callContentFunction(nextMethod, iterator);
    if (!IsObject(next)) {
      ThrowTypeError(JSMSG_ITER_METHOD_RETURNED_PRIMITIVE, "next");
    }

    // Step 11.b.iii.
    if (next.done) {
      return;
    }
  }

  // Steps 11.c.i-ii. (Implicit through for-of loop)
  for (var value of allowContentIterWithNext(iterator, nextMethod)) {
    // Step 11.c.iii.
    yield value;

    // Step 11.c.iv. (Implicit through for-of loop)
  }
}

/**
 * Iterator.prototype.flatMap ( mapper )
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.flatmap
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function IteratorFlatMap(mapper) {
  // Step 1.
  var iterator = this;

  // Step 2.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Steps 3-4.
  if (!IsCallable(mapper)) {
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, mapper));
  }

  // Step 5. (Inlined call to GetIteratorDirect.)
  var nextMethod = iterator.next;

  // Steps 6-8.
  var result = NewIteratorHelper();
  var generator = IteratorFlatMapGenerator(iterator, nextMethod, mapper);
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_GENERATOR_SLOT,
    generator
  );
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_UNDERLYING_ITERATOR_SLOT,
    iterator
  );

  // Step 9.
  return result;
}

/**
 * Iterator.prototype.flatMap ( mapper )
 *
 * Abstract closure definition.
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.flatmap
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function* IteratorFlatMapGenerator(iterator, nextMethod, mapper) {
  // Step 6.a.
  var counter = 0;

  // Step 6.b.
  for (var value of allowContentIterWithNext(iterator, nextMethod)) {
    // Steps 6.b.i-ii. (Implicit through for-of loop)

    // Step 6.b.iii.
    var mapped = callContentFunction(mapper, undefined, value, counter);

    // Step 6.b.iv. (Implicit through for-of loop)

    // Steps 6.b.v.
    var innerIterator = GetIteratorFlattenable(mapped, /* rejectStrings= */ true);
    var innerIteratorNextMethod = innerIterator.next;

    // Step 6.b.vi. (Implicit through for-of loop)

    // Steps 6.b.vii-viii.
    for (var innerValue of allowContentIterWithNext(innerIterator, innerIteratorNextMethod)) {
      // Steps 6.b.viii.1-3. (Implicit through for-of loop)

      // Step 6.b.viii.4.a.
      yield innerValue;

      // Step 6.b.viii.4.b. (Implicit through for-of loop)
    }

    // Step 6.b.ix.
    counter += 1;
  }
}

/**
 * Iterator.prototype.reduce ( reducer [ , initialValue ] )
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.reduce
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function IteratorReduce(reducer /*, initialValue*/) {
  // Step 1.
  var iterator = this;

  // Step 2.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Steps 3-4.
  if (!IsCallable(reducer)) {
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, reducer));
  }

  // Step 5. (Inlined call to GetIteratorDirect.)
  var nextMethod = iterator.next;

  // Steps 6-7.
  var accumulator;
  var counter;
  if (ArgumentsLength() === 1) {
    // Steps 6.a-d. (Moved below.)
    counter = -1;
  } else {
    // Step 7.a.
    accumulator = GetArgument(1);

    // Step 7.b.
    counter = 0;
  }

  // Step 8.
  for (var value of allowContentIterWithNext(iterator, nextMethod)) {
    if (counter < 0) {
      // Step 6. (Reordered steps to compute initial accumulator.)

      // Step 6.c.
      accumulator = value;

      // Step 6.d.
      counter = 1;
    } else {
      // Steps 8.a-b and 8.d. (Implicit through for-of loop)

      // Steps 8.c and 8.e-f.
      accumulator = callContentFunction(reducer, undefined, accumulator, value, counter++);
    }
  }

  // Step 6.b.
  if (counter < 0) {
    ThrowTypeError(JSMSG_EMPTY_ITERATOR_REDUCE);
  }

  // Step 8.b.
  return accumulator;
}

/**
 * Iterator.prototype.toArray ( )
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.toarray
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function IteratorToArray() {
  // Step 1.
  var iterator = this;

  // Step 2.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Step 3. (Inlined call to GetIteratorDirect.)
  var nextMethod = iterator.next;

  // Steps 4-5.
  return [...allowContentIterWithNext(iterator, nextMethod)];
}

/**
 * Iterator.prototype.forEach ( fn )
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.foreach
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function IteratorForEach(fn) {
  // Step 1.
  var iterator = this;

  // Step 2.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Steps 3-4.
  if (!IsCallable(fn)) {
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, fn));
  }

  // Step 5. (Inlined call to GetIteratorDirect.)
  var nextMethod = iterator.next;

  // Step 6.
  var counter = 0;

  // Step 7.
  for (var value of allowContentIterWithNext(iterator, nextMethod)) {
    // Steps 7.a-b. (Implicit through for-of loop)

    // Steps 7.c and 7.e.
    callContentFunction(fn, undefined, value, counter++);

    // Step 7.d. (Implicit through for-of loop)
  }
}

/**
 * Iterator.prototype.some ( predicate )
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.some
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function IteratorSome(predicate) {
  // Step 1.
  var iterator = this;

  // Step 2.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Steps 3-4.
  if (!IsCallable(predicate)) {
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, predicate));
  }

  // Step 5. (Inlined call to GetIteratorDirect.)
  var nextMethod = iterator.next;

  // Step 6.
  var counter = 0;

  // Step 7.
  for (var value of allowContentIterWithNext(iterator, nextMethod)) {
    // Steps 7.a-b. (Implicit through for-of loop)

    // Steps 7.c-f.
    if (callContentFunction(predicate, undefined, value, counter++)) {
      return true;
    }
  }

  // Step 7.b.
  return false;
}

/**
 * Iterator.prototype.every ( predicate )
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.every
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function IteratorEvery(predicate) {
  // Step 1.
  var iterator = this;

  // Step 2.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Steps 3-4.
  if (!IsCallable(predicate)) {
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, predicate));
  }

  // Step 5. (Inlined call to GetIteratorDirect.)
  var nextMethod = iterator.next;

  // Step 6.
  var counter = 0;

  // Step 7.
  for (var value of allowContentIterWithNext(iterator, nextMethod)) {
    // Steps 7.a-b. (Implicit through for-of loop)

    // Steps 7.c-f.
    if (!callContentFunction(predicate, undefined, value, counter++)) {
      return false;
    }
  }

  // Step 7.b.
  return true;
}

/**
 * Iterator.prototype.find ( predicate )
 *
 * https://tc39.es/ecma262/#sec-iterator.prototype.find
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 */
function IteratorFind(predicate) {
  // Step 1.
  var iterator = this;

  // Step 2.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Steps 3-4.
  if (!IsCallable(predicate)) {
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, predicate));
  }

  // Step 5. (Inlined call to GetIteratorDirect.)
  var nextMethod = iterator.next;

  // Step 6.
  var counter = 0;

  // Step 7.
  for (var value of allowContentIterWithNext(iterator, nextMethod)) {
    // Steps 7.a-b. (Implicit through for-of loop)

    // Steps 7.c-f.
    if (callContentFunction(predicate, undefined, value, counter++)) {
      return value;
    }
  }
}

/**
 * Iterator.concat ( ...items )
 *
 * https://tc39.es/proposal-iterator-sequencing/
 */
function IteratorConcat() {
  // Step 1.
  //
  // Stored in reversed order to simplify removing processed items.
  var index = ArgumentsLength() * 2;
  var iterables = std_Array(index);

  // Step 2.
  for (var i = 0; i < ArgumentsLength(); i++) {
    var item = GetArgument(i);

    // Step 2.a.
    if (!IsObject(item)) {
      ThrowTypeError(JSMSG_OBJECT_REQUIRED, typeof item);
    }

    // Step 2.b. (Inlined GetMethod)
    var method = item[GetBuiltinSymbol("iterator")];

    // Step 2.c.
    if (!IsCallable(method)) {
      ThrowTypeError(JSMSG_NOT_ITERABLE, ToSource(item));
    }

    // Step 2.d.
    DefineDataProperty(iterables, --index, item);
    DefineDataProperty(iterables, --index, method);
  }
  assert(index === 0, "all items stored");

  // Steps 3-5.
  var result = NewIteratorHelper();
  var generator = IteratorConcatGenerator(iterables);
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_GENERATOR_SLOT,
    generator
  );
  // ITERATOR_HELPER_UNDERLYING_ITERATOR_SLOT is unused because IteratorConcat
  // doesn't have an underlying iterator.

  // Step 6.
  return result;
}

/**
 * Iterator.concat ( ...items )
 *
 * https://tc39.es/proposal-iterator-sequencing/
 */
function* IteratorConcatGenerator(iterables) {
  assert(IsArray(iterables), "iterables is an array");
  assert(iterables.length % 2 === 0, "iterables contains pairs (item, method)");

  // Step 3.a.
  for (var i = iterables.length; i > 0;) {
    var item = iterables[--i];
    var method = iterables[--i];

    // Remove processed items to avoid keeping them alive.
    iterables.length -= 2;

    // Steps 3.a.i-v.
    for (var innerValue of allowContentIterWith(item, method)) {
      // Steps 3.a.v.1-3. (Implicit through for-of loop)

      yield innerValue;
    }
  }
}

/**
 * Iterator.zip (iterables [, options])
 *
 * https://tc39.es/proposal-joint-iteration/#sec-iterator.zip
 */
function IteratorZip(iterables, options = undefined) {
  // Step 1.
  if (!IsObject(iterables)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterables === null ? "null" : typeof iterables);
  }

  // Steps 2-7.
  if (options !== undefined) {
    // Step 2. (Inlined GetOptionsObject)
    if (!IsObject(options)) {
      ThrowTypeError(
        JSMSG_OBJECT_REQUIRED_ARG, "options", "Iterator.zip", ToSource(options)
      );
    }

    // Step 3.
    var mode = options.mode;

    // Step 4.
    if (mode === undefined) {
      mode = "shortest";
    }

    // Step 5.
    if (mode !== "shortest" && mode !== "longest" && mode !== "strict") {
      if (typeof mode !== "string") {
        ThrowTypeError(
          JSMSG_ITERATOR_ZIP_INVALID_OPTION_TYPE,
          "mode",
          mode === null ? "null" : typeof mode
        );
      }
      ThrowTypeError(
        JSMSG_ITERATOR_ZIP_INVALID_OPTION_VALUE, "mode", ToSource(mode)
      );
    }

    // Step 6.
    var paddingOption = undefined;

    // Step 7.
    if (mode === "longest") {
      // Step 7.a.
      paddingOption = options.padding;

      // Step 7.b.
      if (paddingOption !== undefined && !IsObject(paddingOption)) {
        ThrowTypeError(
          JSMSG_ITERATOR_ZIP_INVALID_OPTION_TYPE,
          "padding",
          paddingOption === null ? "null" : typeof paddingOption
        );
      }
    }
  } else {
    // Step 4.
    var mode = "shortest";
  }

  // Step 8.
  var iters = [];
  var nextMethods = [];

  // Steps 10-12.
  try {
    var closedIterators = false;
    for (var iter of allowContentIter(iterables)) {
      // Step 12.a. (Implicit)

      // Step 12.c.i.
      try {
        iter = GetIteratorFlattenable(iter, /* rejectStrings= */ true);
        var nextMethod = iter.next;
      } catch (e) {
        // Step 12.c.ii.
        closedIterators = true;
        IteratorCloseAllForException(iters);
        throw e;
      }

      // Step 12.c.iii.
      DefineDataProperty(iters, iters.length, iter);
      DefineDataProperty(nextMethods, nextMethods.length, nextMethod);
    }
  } catch (e) {
    // Step 12.b.
    if (!closedIterators) {
      IteratorCloseAllForException(iters);
    }
    throw e;
  }

  // Step 14.
  if (mode === "longest") {
    // Step 9. (Reordered)
    var padding = [];

    // Step 13. (Reordered)
    var iterCount = iters.length;

    // Steps 14.b.
    if (paddingOption !== undefined) {
      // Steps 14.b.i-v.
      try {
        // Take care to not execute IteratorStepValue when |iterCount| is zero.
        if (iterCount > 0) {
          for (var paddingValue of allowContentIter(paddingOption)) {
            DefineDataProperty(padding, padding.length, paddingValue);

            // |break| statement to perform IteratorClose.
            if (padding.length === iterCount) {
              break;
            }
          }
        } else {
          // Empty array destructuring performs GetIterator + IteratorClose.
          // eslint-disable-next-line no-empty-pattern
          var [] = allowContentIter(paddingOption);
        }
      } catch (e) {
        // Steps 14.b.ii, 14.b.iv.1.b, 14.b.v.2.
        IteratorCloseAllForException(iters);
        throw e;
      }
    }

    // Steps 14.a.i and 14.b.iv.2.
    //
    // Fill with |undefined| up to |iterCount|.
    for (var i = padding.length; i < iterCount; i++) {
      DefineDataProperty(padding, i, undefined);
    }
  }

  // Steps 15-16.
  var result = NewIteratorHelper();
  var generator = IteratorZipGenerator(iters, nextMethods, mode, padding);
  var closeIterator = {
    return() {
      IteratorCloseAllForReturn(iters);
      return {};
    }
  };
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_GENERATOR_SLOT,
    generator
  );
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_UNDERLYING_ITERATOR_SLOT,
    closeIterator
  );

  return result;
}

/**
 * Iterator.zipKeyed ( iterables [, options] )
 *
 * https://tc39.es/proposal-joint-iteration/#sec-iterator.zipkeyed
 */
function IteratorZipKeyed(iterables, options = undefined) {
  // Step 1.
  if (!IsObject(iterables)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterables === null ? "null" : typeof iterables);
  }

  // Steps 2-7.
  if (options !== undefined) {
    // Step 2. (Inlined GetOptionsObject)
    if (!IsObject(options)) {
      ThrowTypeError(
        JSMSG_OBJECT_REQUIRED_ARG, "options", "Iterator.zipKeyed", ToSource(options)
      );
    }

    // Step 3.
    var mode = options.mode;

    // Step 4.
    if (mode === undefined) {
      mode = "shortest";
    }

    // Step 5.
    if (mode !== "shortest" && mode !== "longest" && mode !== "strict") {
      if (typeof mode !== "string") {
        ThrowTypeError(
          JSMSG_ITERATOR_ZIP_INVALID_OPTION_TYPE,
          "mode",
          mode === null ? "null" : typeof mode
        );
      }
      ThrowTypeError(
        JSMSG_ITERATOR_ZIP_INVALID_OPTION_VALUE, "mode", ToSource(mode)
      );
    }

    // Step 6.
    var paddingOption = undefined;

    // Step 7.
    if (mode === "longest") {
      // Step 7.a.
      paddingOption = options.padding;

      // Step 7.b.
      if (paddingOption !== undefined && !IsObject(paddingOption)) {
        ThrowTypeError(
          JSMSG_ITERATOR_ZIP_INVALID_OPTION_TYPE,
          "padding",
          paddingOption === null ? "null" : typeof paddingOption
        );
      }
    }
  } else {
    // Step 4.
    var mode = "shortest";
  }

  // Step 8.
  var iters = [];
  var nextMethods = [];

  // Step 10.
  var allKeys = std_Reflect_ownKeys(iterables);

  // Step 11.
  var keys = [];

  // Step 12.
  try {
    for (var i = 0; i < allKeys.length; i++) {
      var key = allKeys[i];

      // Step 12.a.
      var desc = ObjectGetOwnPropertyDescriptor(iterables, key);

      // Step 12.c.
      if (desc && desc.enumerable) {
        // Step 12.c.i.
        var value = iterables[key];

        // Step 12.c.iii.
        if (value !== undefined) {
          // Step 12.c.iii.1.
          DefineDataProperty(keys, keys.length, key);

          // Step 12.c.iii.2.
          var iter = GetIteratorFlattenable(value, /* rejectStrings= */ true);
          var nextMethod = iter.next;

          // Step 12.c.iii.4.
          DefineDataProperty(iters, iters.length, iter);
          DefineDataProperty(nextMethods, nextMethods.length, nextMethod);
        }
      }
    }
  } catch (e) {
    // Steps 12.b, 12.c.ii, and 12.c.iii.3.
    IteratorCloseAllForException(iters);
    throw e;
  }

  // Step 14.
  if (mode === "longest") {
    // Step 9. (Reordered)
    var padding = [];

    // Steps 14.a-b.
    if (paddingOption === undefined) {
      // Step 13. (Reordered)
      var iterCount = iters.length;

      // Step 14.1.i.
      for (var i = 0; i < iterCount; i++) {
        DefineDataProperty(padding, i, undefined);
      }
    } else {
      try {
        // Step 14.b.i.
        for (var i = 0; i < keys.length; i++) {
          DefineDataProperty(padding, i, paddingOption[keys[i]]);
        }
      } catch (e) {
        // Step 14.b.i.2.
        IteratorCloseAllForException(iters);
        throw e;
      }
    }
  }

  // Steps 15-16.
  var result = NewIteratorHelper();
  var generator = IteratorZipGenerator(iters, nextMethods, mode, padding, keys);
  var closeIterator = {
    return() {
      IteratorCloseAllForReturn(iters);
      return {};
    }
  };
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_GENERATOR_SLOT,
    generator
  );
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_UNDERLYING_ITERATOR_SLOT,
    closeIterator
  );

  return result;
}

/**
 * IteratorZip ( iters, mode, padding, finishResults )
 *
 * https://tc39.es/proposal-joint-iteration/#sec-IteratorZip
 */
function* IteratorZipGenerator(iters, nextMethods, mode, padding, keys) {
  assert(
    iters.length === nextMethods.length,
    "iters and nextMethods have the same number of entries"
  );
  assert(
    mode === "shortest" || mode === "longest" || mode === "strict",
    "invalid mode"
  );
  assert(
    mode !== "longest" || (IsArray(padding) && padding.length === iters.length),
    "iters and padding have the same number of entries"
  );
  assert(
    keys === undefined || (IsArray(keys) && keys.length === iters.length),
    "keys is undefined or an array with iters.length entries"
  );

  // Step 1.
  var iterCount = iters.length;

  // Step 2.
  //
  // Our implementation reuses |iters| instead of using another list. This
  // counter is the number of non-null entries in |iters|.
  var openIterCount = iterCount;

  // Step 3.a.
  if (iterCount === 0) {
    return;
  }

  // Step 3.b.
  while (true) {
    // Step 3.b.i.
    var results = [];

    // Step 3.b.ii.
    assert(openIterCount > 0, "at least one open iterator");

    // Step 3.b.iii.
    for (var i = 0; i < iterCount; i++) {
      // Step 3.b.iii.1.
      var iter = iters[i];
      var nextMethod = nextMethods[i];

      // Steps 3.b.iii.2-3.
      var result;
      if (iter === null) {
        // Step 3.b.iii.2.a.
        assert(mode === "longest", "padding only applied when mode is longest");

        // Step 3.b.iii.2.b.
        result = padding[i];
      } else {
        // Steps 3.b.iii.3.a-c.
        try {
          // Step 3.b.iii.3.a.
          var iterResult = callContentFunction(nextMethod, iter);
          if (!IsObject(iterResult)) {
            ThrowTypeError(JSMSG_ITER_METHOD_RETURNED_PRIMITIVE, "next");
          }
          var done = !!iterResult.done;
          if (!done) {
            result = iterResult.value;
          }
        } catch (e) {
          // Step 3.b.iii.3.b.i.
          iters[i] = null;

          // Step 3.b.iii.3.b.ii.
          IteratorCloseAllForException(iters);
          throw e;
        }

        // Step 3.b.iii.3.d.
        if (done) {
          // Step 3.b.iii.3.d.i.
          //
          // Set to null to mark the iterator as closed.
          iters[i] = null;

          // Step 3.b.iii.3.d.ii.
          if (mode === "shortest") {
            // Step 3.b.iii.3.d.ii.i.
            IteratorCloseAllForReturn(iters);
            return;
          }

          // Step 3.b.iii.3.d.iii.
          if (mode === "strict") {
            // Step 3.b.iii.3.d.iii.i.
            if (i !== 0) {
              IteratorCloseAllForException(iters);
              ThrowTypeError(JSMSG_ITERATOR_ZIP_STRICT_OPEN_ITERATOR);
            }

            // Step 3.b.iii.3.d.iii.ii.
            for (var k = 1; k < iterCount; k++) {
              // Step 3.b.iii.3.d.iii.ii.i.
              assert(iters[k] !== null, "unexpected closed iterator");

              // Steps 3.b.iii.3.d.iii.ii.ii-iv.
              var done;
              try {
                // Step 3.b.iii.3.d.iii.ii.ii.
                var iterResult = callContentFunction(nextMethods[k], iters[k]);
                if (!IsObject(iterResult)) {
                  ThrowTypeError(JSMSG_ITER_METHOD_RETURNED_PRIMITIVE, "next");
                }
                done = !!iterResult.done;
              } catch (e) {
                // // Step 3.b.iii.3.d.iii.ii.iii.i.
                iters[k] = null;

                // Step 3.b.iii.3.d.iii.ii.iii.ii.
                IteratorCloseAllForException(iters);
                throw e;
              }

              // Steps 3.b.iii.3.d.iii.ii.v-vi.
              if (done) {
                // Steps 3.b.iii.3.d.iii.ii.v.i.
                iters[k] = null;
              } else {
                // Steps 3.b.iii.3.d.iii.ii.vi.i.
                IteratorCloseAllForException(iters);
                ThrowTypeError(JSMSG_ITERATOR_ZIP_STRICT_OPEN_ITERATOR);
              }
            }

            // Step 3.b.iii.3.d.iii.iii.
            return;
          }

          // Step 3.b.iii.3.d.iii.iv.i.
          assert(mode === "longest", "unexpected mode");

          // Step 3.b.iii.3.d.iii.iv.ii.
          assert(openIterCount > 0, "at least one open iterator");
          if (--openIterCount === 0) {
            return;
          }

          // Step 3.b.iii.3.d.iii.iv.iii.
          iters[i] = null;

          // Step 3.b.iii.3.d.iii.iv.iv.
          result = padding[i];
        }
      }

      // Step 3.b.iii.4.
      DefineDataProperty(results, results.length, result);
    }

    // Step 3.b.iv.
    if (keys) {
      // Iterator.zipKeyed, step 15.a.
      var obj = std_Object_create(null);

      // Iterator.zipKeyed, step 15.b.
      for (var i = 0; i < keys.length; i++) {
        DefineDataProperty(obj, keys[i], results[i]);
      }

      // Iterator.zipKeyed, step 15.c.
      results = obj;
    }

    // Steps 3.b.v-vi.
    var returnCompletion = true;
    try {
      // Step 3.b.v.
      yield results;

      returnCompletion = false;
    } finally {
      // Step 3.b.vi.
      //
      // IteratorHelper iterators can't continue execution with a Throw
      // completion, so this must be a Return completion.
      if (returnCompletion) {
        IteratorCloseAllForReturn(iters);
      }
    }
  }
}

/**
 * IteratorCloseAll ( iters, completion )
 *
 * When |completion| is a Return completion.
 *
 * https://tc39.es/proposal-joint-iteration/#sec-closeall
 */
function IteratorCloseAllForReturn(iters) {
  assert(IsArray(iters), "iters is an array");

  var exception;
  var hasException = false;

  // Step 1.
  for (var i = iters.length - 1; i >= 0; i--) {
    var iter = iters[i];
    assert(IsObject(iter) || iter === null, "iter is an object or null");

    if (IsObject(iter)) {
      try {
        IteratorClose(iter);
      } catch (e) {
        // Store the first exception and then ignore any later exceptions.
        if (!hasException) {
          hasException = true;
          exception = e;
        }
      }
    }
  }

  // Step 2.
  if (hasException) {
    throw exception;
  }
}

/**
 * IteratorCloseAll ( iters, completion )
 *
 * When |completion| is a Throw completion.
 *
 * https://tc39.es/proposal-joint-iteration/#sec-closeall
 */
function IteratorCloseAllForException(iters) {
  assert(IsArray(iters), "iters is an array");

  // Step 1.
  for (var i = iters.length - 1; i >= 0; i--) {
    var iter = iters[i];
    assert(IsObject(iter) || iter === null, "iter is an object or null");

    if (IsObject(iter)) {
      try {
        IteratorClose(iter);
      } catch {
        // Ignore any inner exceptions.
      }
    }
  }

  // Step 2. (Performed in caller)
}

#ifdef NIGHTLY_BUILD
/**
 * CreateNumericRangeIterator (start, end, optionOrStep, type)
 * Step 18
 *
 * https://tc39.es/proposal-iterator.range/#sec-create-numeric-range-iterator
 */
function IteratorRangeNext() {
  var obj = this;
  // Step 18. Let closure be a new Abstract Closure with no parameters
  // that captures start, end, step, inclusiveEnd, zero, one and performs the following steps when called:

  if (!IsObject(obj) || (obj = GuardToIteratorRange(obj)) === null) {
    return callFunction(
      CallIteratorRangeMethodIfWrapped,
      this,
      "IteratorRangeNext"
    );
  }

  // Retrieve values from reserved slots
  var start = UnsafeGetReservedSlot(obj, ITERATOR_RANGE_SLOT_START);
  var end = UnsafeGetReservedSlot(obj, ITERATOR_RANGE_SLOT_END);
  var step = UnsafeGetReservedSlot(obj, ITERATOR_RANGE_SLOT_STEP);
  var inclusiveEnd = UnsafeGetReservedSlot(obj, ITERATOR_RANGE_SLOT_INCLUSIVE_END);
  var zero = UnsafeGetReservedSlot(obj, ITERATOR_RANGE_SLOT_ZERO);
  var one = UnsafeGetReservedSlot(obj, ITERATOR_RANGE_SLOT_ONE);
  var currentCount = UnsafeGetReservedSlot(obj, ITERATOR_RANGE_SLOT_CURRENT_COUNT);

  // Step 18.a: If end > start, let ifIncrease be true
  // Step 18.b: Else let ifIncrease be false
  var ifIncrease = end > start;

  // Step 18.c: If step > zero, let ifStepIncrease be true
  // Step 18.d: Else let ifStepIncrease be false
  var ifStepIncrease = step > zero;

  // Step 18.e: If ifIncrease is not ifStepIncrease, return undeﬁned.
  if (ifIncrease !== ifStepIncrease) {
    return { value: undefined, done: true };
  }

  // Step 18.f: Let hitsEnd be false
  var hitsEnd = false;

  // Step 18.g: Let currentCount be zero (already handled via slots)

  // Step 18.i.i: Let currentYieldingValue be start + (step × currentCount)
  var currentYieldingValue = start + (step * currentCount);

  // Step 18.i.ii: If currentYieldingValue is equal to end, set hitsEnd to true
  hitsEnd = currentYieldingValue === end && !inclusiveEnd;


  // Step 18.i.iii: Set currentCount to currentCount + one
  currentCount = currentCount + one;

  // Step 18.i.iv: If ifIncrease is true, then
  if (ifIncrease) {
    // Step 18.i.iv.1: If inclusiveEnd is true, then
    if (inclusiveEnd) {
      // Step 18.i.iv.1.a: If currentYieldingValue > end, return undefined.
      if (currentYieldingValue > end) {
        return { value: undefined, done: true };
      }
    } else {
      // Step 18.i.iv.2: If currentYieldingValue >= end, return undefined
      if (currentYieldingValue >= end) {
        return { value: undefined, done: true };
      }
    }
  } else {
    // Step 18.i.v: Else
    // Step 18.i.v.1: If inclusiveEnd is true, then
    if (inclusiveEnd) {
      //Step 18.i.v.1.a.a. If end > currentYieldingValue, return undefined.
      if (end > currentYieldingValue) {
        return { value: undefined, done: true };
      }
    } else {
      // Step 18.i.v.2: Else
      if (end >= currentYieldingValue) {
        // Step 18i.v.2.a: If end >= currentYieldingValue, return undefined
        return { value: undefined, done: true };
      }
    }
  }

  // Step 18.i.vi: Yield currentYieldingValue
  UnsafeSetReservedSlot(obj, ITERATOR_RANGE_SLOT_CURRENT_COUNT, currentCount);

  // Step 18.j: Return undefined if the loop completes
  if (hitsEnd) {
    return { value: undefined, done: true };
  }

  // Return the current value
  return { value: currentYieldingValue, done: false };
}



/**
 * CreateNumericRangeIterator (start, end, optionOrStep, type)
 *
 * https://tc39.es/proposal-iterator.range/#sec-create-numeric-range-iterator
 */
function CreateNumericRangeIterator(start, end, optionOrStep, isNumberRange) {

  // Step 1: If start is NaN, throw a RangeError exception.
  if (isNumberRange && Number_isNaN(start)) {
    ThrowRangeError(JSMSG_ITERATOR_RANGE_INVALID_START_RANGEERR);
  }

  // Step 2: If end is NaN, throw a RangeError exception.
  if (isNumberRange && Number_isNaN(end)) {
    ThrowRangeError(JSMSG_ITERATOR_RANGE_INVALID_END_RANGEERR);
  }

  // Step 3: If type is NUMBER-RANGE, then
  if (isNumberRange) {
    // Step 3.a. Assert: start is a Number.
    assert(typeof start === 'number', "The 'start' argument must be a number");

    // Step 3.b. If end is not a Number, throw a TypeError exception.
    if (typeof end !== 'number') {
      ThrowTypeError(JSMSG_ITERATOR_RANGE_INVALID_END);
    }

    // Step 3.c. Let zero be 0ℤ.
    var zero = 0;

    // Step 3.d. Let one be 1ℤ.
    var one = 1;
    // 4: Else,
  } else {
    // 4.a. Assert: start is a BigInt.
    assert(typeof start === 'bigint', "The 'start' argument must be a bigint");

    // 4.b. If end is not +∞𝔽 or -∞𝔽 and end is not a BigInt, throw a TypeError exception.
    if (typeof end !== 'bigint' && !(Number_isFinite(end))) {
      ThrowTypeError(JSMSG_ITERATOR_RANGE_INVALID_END);
    }

    // 4.c. Let zero be 0𝔽.
    var zero = 0n;

    // 4.d. Let one be 1𝔽.
    var one = 1n;
  }
  // Step 5: If start is +∞ or -∞, throw a RangeError exception.
  if (typeof start === 'number' && !Number_isFinite(start)) {
    ThrowRangeError(JSMSG_ITERATOR_RANGE_START_INFINITY);
  }
  // Step 6: Let inclusiveEnd be false.
  var inclusiveEnd = false;

  // Step 7: If optionOrStep is undefined or null, then
  // Step 7.a. Let step be undefined.
  var step;

  // Step 8: Else if optionOrStep is an Object, then
  if (optionOrStep !== null && typeof optionOrStep === 'object') {
    // Step 8.a. Let step be ? Get(optionOrStep, "step").
    step = optionOrStep.step;

    // Step 8.b. Set inclusiveEnd to ToBoolean(? Get(optionOrStep, "inclusive")).
    inclusiveEnd = TO_BOOLEAN(optionOrStep.inclusiveEnd);
  }
  // Step 9: Else if type is NUMBER-RANGE and optionOrStep is a Number, then
  else if (isNumberRange && typeof optionOrStep === 'number') {
    // Step 9.a. Let step be optionOrStep.
    step = optionOrStep;
  }

  // Step 10: Else if type is BIGINT-RANGE and optionOrStep is a BigInt, then
  // Step 10.a. Let step be optionOrStep.
  else if (!isNumberRange && typeof optionOrStep === 'bigint') {
    step = optionOrStep;
  }
  // Step 11: Else, throw a TypeError exception.
  else if (optionOrStep !== undefined && optionOrStep !== null) {
    ThrowTypeError(JSMSG_ITERATOR_RANGE_INVALID_STEP);
  }

  // Step 12: If step is undefined or null, then
  if (step === undefined || step === null) {
    // Step 12.a. If end > start, let step be one.
    // Step 12.b. Else let step be -one.
    step = end > start ? one : -one;
  }

  // Step 13: If step is NaN, throw a RangeError exception.
  if (typeof step === "number" && Number_isNaN(step)) {
    ThrowRangeError(JSMSG_ITERATOR_RANGE_STEP_NAN);
  }

  // Step 14: If type is NUMBER-RANGE and step is not a Number, throw a TypeError exception.
  if (isNumberRange && typeof step !== 'number') {
    ThrowTypeError(JSMSG_ITERATOR_RANGE_STEP_NOT_NUMBER);
  }

  // Step 15: Else if type is BIGINT-RANGE and step is not a BigInt, throw a TypeError exception
  else if (!isNumberRange && typeof step !== 'bigint') {
    ThrowTypeError(JSMSG_ITERATOR_RANGE_STEP_NOT_BIGINT);
  }

  // Step 16: If step is +∞ or -∞, throw a RangeError exception.
  if (typeof step === 'number' && !Number_isFinite(step)) {
    ThrowRangeError(JSMSG_ITERATOR_RANGE_STEP_NOT_FINITE);
  }

  // Step 17: If step is zero and start is not end, throw a RangeError exception.
  if (step === zero && start !== end) {
    ThrowRangeError(JSMSG_ITERATOR_RANGE_STEP_ZERO);
  }
  // Step 19: Return CreateIteratorFromClosure(closure, "%NumericRangeIteratorPrototype%", %NumericRangeIteratorPrototype%).

  var obj = NewIteratorRange();
  UnsafeSetReservedSlot(obj, ITERATOR_RANGE_SLOT_START, start);
  UnsafeSetReservedSlot(obj, ITERATOR_RANGE_SLOT_END, end);
  UnsafeSetReservedSlot(obj, ITERATOR_RANGE_SLOT_STEP, step);
  UnsafeSetReservedSlot(obj, ITERATOR_RANGE_SLOT_INCLUSIVE_END, inclusiveEnd);
  UnsafeSetReservedSlot(obj, ITERATOR_RANGE_SLOT_ZERO, zero);
  UnsafeSetReservedSlot(obj, ITERATOR_RANGE_SLOT_ONE, one);
  UnsafeSetReservedSlot(obj, ITERATOR_RANGE_SLOT_CURRENT_COUNT, zero);

  return obj;
}



/**
 *  Iterator.range ( start, end, optionOrStep )
 *
 * https://tc39.es/proposal-iterator.range/#sec-iterator.range
 */
function IteratorRange(start, end, optionOrStep) {

  // Step 1. If start is a Number, return ? CreateNumericRangeIterator(start, end, optionOrStep, NUMBER-RANGE)
  if (typeof start === 'number') {
    return CreateNumericRangeIterator(start, end, optionOrStep, true);
  }

  // Step 2. If start is a BigInt, return ? CreateNumericRangeIterator(start, end, optionOrStep, BIGINT-RANGE)
  if (typeof start === 'bigint') {
    return CreateNumericRangeIterator(start, end, optionOrStep, false);
  }

  // Step 3. Throw a TypeError exception.
  ThrowTypeError(JSMSG_ITERATOR_RANGE_INVALID_START);

}

#endif

/**
 *  Iterator.prototype.chunks ( chunkSize )
 *
 *  https://tc39.es/proposal-iterator-chunking/#sec-iterator.prototype.chunks
 */
function IteratorChunks(chunkSize) {
  // Step 1. Let O be the this value.
  var iterator = this;

  // Step 2. If O is not an Object, throw a TypeError exception.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Step 3. Let iterated be the Iterator Record
  //  { [[Iterator]]: O, [[NextMethod]]: undefined, [[Done]]: false }.

  // Step 4. If chunkSize is not an integral Number, then
  if (!Number_isInteger(chunkSize)) {
    // Step 4.a. Let error be ThrowCompletion(a newly created TypeError object).
    // Step 4.b. Return ? IteratorClose(iterated, error).
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowTypeError(JSMSG_CHUNK_SIZE_NOT_INTEGER);
  }

  // Step 5. If chunkSize is not in the inclusive interval from 1𝔽 to
  // 𝔽(2**32 - 1), then
  if (chunkSize < 1 || chunkSize > (2 ** 32) - 1) {
    // Step 5.a. Let error be ThrowCompletion(a newly created RangeError object).
    // Step 5.b. Return ? IteratorClose(iterated, error).
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowRangeError(JSMSG_INVALID_CHUNKSIZE);
  }

  // Step 6. Set iterated to ? GetIteratorDirect(O).
  var nextMethod = iterator.next;

  // Step 7. Let closure be a new Abstract Closure with ...
  // (Handled in IteratorChunksGenerator.)

  // Step 8. Let result be CreateIteratorFromClosure(
  //   closure, "Iterator Helper", %IteratorHelperPrototype%,
  //   « [[UnderlyingIterators]] »
  // ).
  var result = NewIteratorHelper();
  var generator = IteratorChunksGenerator(iterator, nextMethod, chunkSize);

  // Step 9. Set result.[[UnderlyingIterators]] to « iterated ».
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_GENERATOR_SLOT,
    generator
  );
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_UNDERLYING_ITERATOR_SLOT,
    iterator
  );

  // Step 10. Return result.
  return result;
}

/**
 *  Iterator.prototype.chunks ( chunkSize )
 *
 *  Abstract closure definition.
 *
 *  https://tc39.es/proposal-iterator-chunking/#sec-iterator.prototype.chunks
 */
function* IteratorChunksGenerator(iterator, nextMethod, chunkSize) {
  // Step 6. Let closure be a new Abstract Closure
  //         with no parameters that captures iterated and
  //         chunkSize and performs the following steps when called:
  // Step 6.a. Let buffer be a new empty List.
  // This is an optimization that performs the equivalent of CreateArrayFromList
  // at the same time as constructing the buffer.
  // All the operations done on buffer are not affected by prototype pollution,
  // and thus directly using an array here is safe.
  // All the operations done on the buffer are not observable,
  // and thus reordering the operation here is safe.
  var buffer = [];

  // Step 6.b. Repeat,
  // Step 6.b.i. Let value be ? IteratorStepValue(iterated).
  for (var value of allowContentIterWithNext(iterator, nextMethod)) {
    // Step 6.b.iii. Append value to buffer.
    // (Reordered)
    // NOTE: The OOM case is automatically handled by the for-of loop.
    DefineDataProperty(buffer, buffer.length, value);

    // Step 6.b.iv. If the number of elements in buffer is ℝ(chunkSize), then
    if (buffer.length === chunkSize) {
      // Step 6.b.iv.1. Let completion be
      //                Completion(Yield(CreateArrayFromList(buffer))).
      yield buffer;

      // Step 6.b.iv.3. Set buffer to a new empty List.
      // This is an optimization that performs the equivalent of
      // CreateArrayFromList at the same time as constructing the buffer.
      buffer = [];
    }
  }

  // Step 6.b.ii. If value is done, then
  // Step 6.b.ii.1. If buffer is not empty, then
  if (buffer.length) {
    // Step 6.b.ii.1.a. Perform Completion(Yield(CreateArrayFromList(buffer))).
    // Iterator helper doesn't have throw methods, and only "normal" or "return"
    // completion can appear here.
    // Given that this is the last step inside the function, there's no
    // difference between handling and ignoring the completion.
    yield buffer;
  }

  // Step 6.b.ii.2. Return ReturnCompletion(undefined).
  // (implicit)
}

/**
 *  Iterator.prototype.windows ( windowSize, undersized )
 *
 *  https://tc39.es/proposal-iterator-chunking/#sec-iterator.prototype.windows
 */
function IteratorWindows(windowSize, undersized = undefined) {
  // Step 1. Let O be the this value.
  var iterator = this;

  // Step 2. If O is not an Object, throw a TypeError exception.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, iterator === null ? "null" : typeof iterator);
  }

  // Step 3. Let iterated be the Iterator Record
  //         { [[Iterator]]: O, [[NextMethod]]: undefined, [[Done]]: false }.

  // Step 4. If windowSize is not an integral Number, then
  if (!Number_isInteger(windowSize)) {
    // Step 4.a. Let error be ThrowCompletion(a newly created TypeError object).
    // Step 4.b. Return ? IteratorClose(iterated, error).
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowTypeError(JSMSG_WINDOW_SIZE_NOT_INTEGER);
  }

  // Step 5. If windowSize is not in the inclusive interval from 1𝔽 to
  //         𝔽(2**32 - 1), then
  if (windowSize < 1 || windowSize > (2 ** 32) - 1) {
    // Step 5.a. Let error be ThrowCompletion(a newly created RangeError object).
    // Step 5.b. Return ? IteratorClose(iterated, error).
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowRangeError(JSMSG_INVALID_WINDOWSIZE);
  }

  // Step 6. If undersized is undefined, set undersized to "only-full".
  if (undersized === undefined) {
    undersized = "only-full";
  }

  // Step 7. If undersized is not "only-full" or "allow-partial", then
  if (undersized !== "only-full" && undersized !== "allow-partial") {
    // Step 7.a. Let error be ThrowCompletion(a newly created TypeError object).
    // Step 7.b. Return ? IteratorClose(iterated, error).
    try {
      IteratorClose(iterator);
    } catch {}
    ThrowTypeError(
      JSMSG_INVALID_UNDERSIZED_OPTION_VALUE, "undersized", ToSource(undersized)
    );
  }

  // Step 8. Set iterated to ? GetIteratorDirect(O).
  var nextMethod = iterator.next;

  // Step 9. Let closure be a new Abstract Closure with ...
  // (Handled in IteratorWindowsGenerator.)

  // Step 10. Let result be CreateIteratorFromClosure(
  //         closure, "Iterator Helper", %IteratorHelperPrototype%,
  //         « [[UnderlyingIterators]] »).
  var result = NewIteratorHelper();
  var generator = IteratorWindowsGenerator(iterator, nextMethod, windowSize, undersized);

  // Step 11. Set result.[[UnderlyingIterators]] to « iterated ».
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_GENERATOR_SLOT,
    generator
  );
  UnsafeSetReservedSlot(
    result,
    ITERATOR_HELPER_UNDERLYING_ITERATOR_SLOT,
    iterator
  );

  // Step 12. Return result.
  return result;
}

/**
 *  Iterator.prototype.windows ( windowSize, undersized )
 *
 *  Abstract closure definition.
 *
 *  https://tc39.es/proposal-iterator-chunking/#sec-iterator.prototype.windows
 */
function* IteratorWindowsGenerator(iterator, nextMethod, windowSize, undersized) {
  // Step 8. Let closure be a new Abstract Closure with no parameters that captures
  //         iterated, windowSize, and undersized
  //         and performs the following steps when called:

  // Step 8.a. Let buffer be a new empty List.
  var buffer = new_List();

  // Step 8.b. Repeat,
  // Step 8.b.i. Let value be ? IteratorStepValue(iterated).
  for (var value of allowContentIterWithNext(iterator, nextMethod)) {
    // Step 8.b.iii. If the number of elements in buffer is ℝ(windowSize), then
    if (buffer.length === windowSize) {
      // Step 8.b.iii.1. Remove the first element from buffer.
      callFunction(std_Array_shift, buffer);
    }

    // Step 8.b.iv. Append value to buffer.
    DefineDataProperty(buffer, buffer.length, value);

    // Step 8.b.v. If the number of elements in buffer is ℝ(windowSize), then
    if (buffer.length === windowSize) {
      // Step 8.b.v.1. Let completion be Completion(Yield(CreateArrayFromList(buffer))).
      // Step 8.b.v.2. IfAbruptCloseIterator(completion, iterated).
      // NOTE: The abrupt return completion case is automatically handled by the for-of loop.
      yield callFunction(std_Array_slice, buffer);
    }
  }

  // Step 8.b.ii. If value is done, then
  // Step 8.b.ii.1. If undersized is "allow-partial", buffer is not empty,
  //                and the number of elements in buffer < ℝ(windowSize), then
  if (undersized === "allow-partial" && buffer.length && buffer.length < windowSize) {
    // Step 8.b.ii.1.a. Perform Completion(Yield(CreateArrayFromList(buffer))).
    // Iterator helper doesn't have throw methods, and only "normal" or "return"
    // completion can appear here.
    // Given that this is the last step inside the function, there's no
    // difference between handling and ignoring the completion.
    yield callFunction(std_Array_slice, buffer);
  }
  // Step 8.b.ii.2. Return ReturnCompletion(undefined).
  // (implicit)
}

/**
 *  Iterator.prototype.join ( separator )
 *
 *  https://tc39.es/proposal-iterator-join/#sec-iterator.prototype.join
 */
function IteratorJoin(separator) {
  // Step 1. Let O be the this value.
  var O = this;

  // Step 2. If O is not an Object, throw a TypeError exception.
  if (!IsObject(O)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, O === null ? "null" : typeof O);
  }

  // Step 3. Let iterated be the Iterator Record
  //         { [[Iterator]]: O, [[NextMethod]]: undefined, [[Done]]: false }.

  // Step 4. If separator is undefined, then
  var sep;
  if (separator === undefined) {
    // Step 4.a. Let sep be ",".
    sep = ",";
  } else {
    // Step 5. Else,
    // Step 5.a. Let sep be Completion(ToString(separator)).
    // Step 5.b. IfAbruptCloseIterator(sep, iterated).
    try {
      sep = ToString(separator);
    } catch (e) {
      try {
        IteratorClose(O);
      } catch {}
      throw e;
    }
  }

  // Step 6. Set iterated to ? GetIteratorDirect(O).
  // (Inlined call to GetIteratorDirect.)
  var nextMethod = O.next;

  // Step 7. Let R be the empty String.
  var R = "";

  // Step 8. Let first be true.
  var first = true;

  // Step 9. Repeat,
  // Step 9.a. Let value be ? IteratorStepValue(iterated).
  for (var value of allowContentIterWithNext(O, nextMethod)) {
    // Step 9.c. If first is true, then
    if (first) {
      // Step 9.c.i. Set first to false.
      first = false;
    } else {
      // Step 9.d. Else,
      // Step 9.d.i. Set R to the string-concatenation of R and sep.
      R += sep;
    }

    // Step 9.e. If value is neither undefined nor null, then
    if (value !== undefined && value !== null) {
      // Step 9.e.i. Let S be Completion(ToString(value)).
      // Step 9.e.ii. IfAbruptCloseIterator(S, iterated).
      // Step 9.e.iii. Set R to the string-concatenation of R and S.
      R += ToString(value);
    }
  }

  // Step 9.b. If value is done, return R.
  return R;
}

/**
 *  Iterator.prototype.includes ( searchElement [ , skippedElements ] )
 *
 *  https://tc39.es/proposal-iterator-includes
 */
function IteratorIncludes(searchElement, skippedElements = undefined) {
  // Step 1. Let O be the this value.
  var O = this;

  // Step 2. If O is not an Object, throw a TypeError exception.
  if (!IsObject(O)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, O === null ? "null" : typeof O);
  }

  // Step 3. Let iterated be the Iterator Record
  //         { [[Iterator]]: O, [[NextMethod]]: undefined, [[Done]]: false }.

  // Step 4. If skippedElements is undefined, then
  // Step 4.a. Let toSkip be 0.
  var toSkip = 0;
  // Step 5. Else, (skippedElements is not undefined)
  if (skippedElements !== undefined) {
    // Step 5.a. If skippedElements is not one of +∞𝔽, -∞𝔽, or an integral Number, then
    if (!(Number_isInteger(skippedElements) ||
          (typeof skippedElements === "number" &&
           !Number_isFinite(skippedElements) &&
           !Number_isNaN(skippedElements)))) {
      // Step 5.a.i-ii. Let error be ThrowCompletion(...). Return ? IteratorClose(iterated, error).
      try {
        IteratorClose(O);
      } catch {}
      ThrowTypeError(JSMSG_INVALID_SKIP_COUNT);
    }
    // Step 5.b. Let toSkip be the extended mathematical value of skippedElements.
    toSkip = skippedElements;
  }

  // Step 6. If toSkip < 0, then
  // Step 6.a. Let error be ThrowCompletion(a newly created RangeError object).
  // Step 6.b. Return ? IteratorClose(iterated, error).
  if (toSkip < 0) {
    try {
      IteratorClose(O);
    } catch {}
    ThrowRangeError(JSMSG_NEGATIVE_LIMIT);
  }

  // Step 7. If toSkip is finite and toSkip > 𝔽(2**53 - 1), then
  // Step 7.a. Let error be ThrowCompletion(a newly created RangeError object).
  // Step 7.b. Return ? IteratorClose(iterated, error).
  if (Number_isFinite(toSkip) && toSkip > (2 ** 53) - 1) {
    try {
      IteratorClose(O);
    } catch {}
    ThrowRangeError(JSMSG_SKIP_COUNT_TOO_LARGE);
  }

  // Step 8. Let skipped be 0.
  var skipped = 0;

  // Step 9. Set iterated to ? GetIteratorDirect(O).
  // (Inlined call to GetIteratorDirect.)
  var nextMethod = O.next;

  // Step 10. Repeat,
  // Step 10.a. Let value be ? IteratorStepValue(iterated).
  for (var value of allowContentIterWithNext(O, nextMethod)) {
    // Step 10.c. If skipped < toSkip, then
    if (skipped < toSkip) {
      // Step 10.c.i. Set skipped to skipped + 1.
      skipped++;
    // Step 10.d. Else if SameValueZero(value, searchElement) is true, then
    } else if (value === searchElement || (Number_isNaN(value) && Number_isNaN(searchElement))) {
      // Step 10.d.i. Return ? IteratorClose(iterated, NormalCompletion(true)).
      return true;
    }
  }

  // Step 10.b. If value is done, return false.
  return false;
}
