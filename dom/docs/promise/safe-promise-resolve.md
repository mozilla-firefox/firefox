# Microtask Reordering

On some pages you may see the warning

> **The execution of microtasks on this page was reordered to avoid unsafe execution.**

## What the warning means

This warning is issued if a promise was resolved by a web API with an object which could run user-code. In this situation rather than synchronously resolving the promise, the promise resolution is deferred by [queueing a new microtask](https://developer.mozilla.org/en-US/docs/Web/API/HTML_DOM_API/Microtask_guide/In_depth). This is a security mitigation, done to avoid running user-code at an unsafe point in a browser engine. It will not be issued by regular JS manipulation of promises as they will never defer.

For more details see the TC39 Proposal ["Curtailing the Power of Thenables"](https://github.com/tc39/proposal-thenable-curtailment) and discussion on the [WebIDL specification](https://github.com/whatwg/webidl/issues/1584).

## Does this need action?

This warning is issued only to allow website owners to know that a reordering has happened. It is extremely unlikely that a website depends strictly on the ordering of this promise resolution, but we issue the warning to help highlight that it is a _possible_ (though implausible) source of site breakage.
