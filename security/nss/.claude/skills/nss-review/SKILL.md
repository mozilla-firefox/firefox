---
name: nss-review
description: Review guidance for patches to Network Security Services (NSS).
---

# Reviewing NSS patches

NSS is a security library. Prioritise memory safety, cryptographic correctness,
secret handling, and compatibility. Apply the checks below where relevant to
the patch.

Based on the
[NSS team review checklist](https://github.com/mozilla/nss-tools/blob/master/nss-code-review-checklist.yaml).
The guidance below is self-contained.

## Review approach

- Report actionable defects introduced or made reachable by the change. Explain
  the triggering input or execution path and the resulting incorrect behaviour.
- Check the affected function's contract, callers, and cleanup paths when that
  context is available. Code missing from the diff is not evidence that a check
  is missing from the program.
- Distinguish a demonstrated defect from an assumption needing more context.
  Do not turn each checklist item into a review comment.
- Avoid style-only findings, speculative refactoring, and blanket requests for
  tests.

## Inputs and failure handling

- Validate untrusted lengths, offsets, counts, and algorithm parameters (from
  DER, TLS records, PKCS#11 templates, and similar) before use. Check arithmetic
  before allocation, indexing, copying, or narrowing a value. Account for
  signedness and bytes-versus-bits conversions.
- Assertions do not replace runtime checks for malformed external input.
  Public functions must handle NULL and zero-length arguments as their contract
  documents; internal preconditions may be asserted instead.
- Read variables only after they are initialised on every path, including
  early exits to cleanup labels.
- Interpret return values according to the API: `SECStatus`, pointers, integer
  lengths, NSPR status, and PKCS#11 `CK_RV` have different failure conventions.
  Preserve intentional retry, `SECWouldBlock`, and fallback behaviour.
- Where the API reports an NSS/NSPR error, ensure failure leaves an appropriate
  error set by this function (`PORT_SetError()`) or a callee. Preserve useful
  errors through cleanup. PKCS#11 entry points must return appropriate `CKR_*`
  values; use the existing mapping at layer boundaries.
- On failure, out-parameters and partially modified objects must follow their
  contracts. Some APIs clear outputs or return required buffer sizes on failure;
  do not require all outputs to remain untouched. Callers must not consume
  outputs that are valid only on success.

## Ownership, lifetime, and concurrency

- Trace ownership through success and every error exit, including `goto loser`
  cleanup. Check allocations, reference counts, arena lifetimes, borrowed
  pointers, and ownership transfers.
- Match allocations and references with their proper release operations.
  Do not independently free arena-owned or borrowed objects. Check for leaks,
  double frees, use after free, and cleanup of partially initialised objects.
- For `SECItem`, distinguish ownership of the item from ownership of its data;
  the destructor's `freeit` argument must match how the item itself was allocated.
- For shared state changes, preserve locking, lock order, reference lifetimes,
  and callback/re-entrancy contracts, including on failure paths.

## Secrets and cryptographic behaviour

- Clear owned secret buffers and temporary copies when no longer needed,
  including stack buffers and failure paths. Use the applicable NSS facility,
  such as `PORT_SafeZero`, `PORT_ZFree`, or `SECITEM_ZfreeItem`; ordinary
  `memset` is insufficient for erasure that must survive compiler optimisation.
- Preserve constant-time behaviour where required. Examine new secret-dependent
  branches, memory accesses, loop bounds, and early exits. Use
  `NSS_SecureMemcmp` for equality tests requiring constant time; comparisons of
  public data do not automatically need it.
- For changed cryptographic operations, check randomness failures, nonce/IV
  requirements, parameter validation, key usage, and authentication before
  accepting plaintext. Do not introduce new secret logging.
- For TLS/DTLS changes, check message/state ordering, transcript updates,
  negotiated versions and algorithms, and rejection behaviour. For certificate
  changes, check trust, name, usage, and constraint enforcement. For PKCS#11
  changes, check mechanism parameters, attributes, permissions, and token/session
  state. Apply these checks only to affected behaviour.
- Preserve relevant FIPS mode, self-test, and algorithm-policy restrictions.

## Layering and compatibility

- Preserve freebl/softoken dependency boundaries, including `FREEBL_NO_DEPEND`
  configurations. Do not introduce dependencies on higher NSS layers.
- Production code above softoken should access cryptographic operations through
  the established PKCS#11 interfaces in `lib/pk11wrap/`. Investigate new direct
  freebl calls in that context; direct calls in freebl tests are expected.
- Preserve public signatures, exported symbol versions, public structure
  layouts, and externally visible behaviour. Never remove or incompatibly change
  an existing export.
- Add newly exported APIs to the relevant library's `.def` file (for example
  `lib/nss/nss.def`), in the section for the release they first ship in
  (`;+NSS_3.NNN { ... global: ...`). Do not guess the release number.
- Where a freebl interface changes, check dispatch-vector layout and
  `FREEBL_VERSION` in `lib/freebl/loader.h`.
- Check whether accepted API additions or changes require updates to
  `automation/abi-check/expected-report-*.txt`. An updated expectation does not
  justify an incompatible change.
- When a patch changes something Firefox or Thunderbird can depend on, such as
  exported functions, experimental APIs in `lib/ssl/sslexp.h`, public headers,
  mozpkix, or externally visible behaviour and defaults, check downstream
  callers with `searchfox-cli`, as described under "Downstream users" in
  `AGENTS.md`.

## Tests and build integration

- Look for coverage of the changed behaviour in the appropriate existing suite:
  GTests, shell tests, test vectors, or fuzz tests. For a bug fix, prefer a
  regression test that fails before the fix and passes afterwards.
- Identify specific missing cases when relevant: truncated or malformed inputs,
  boundary lengths, rejected parameters, and reachable failure/cleanup paths.
  Reuse existing coverage where it already exercises the behaviour.
- Check that new sources and tests are registered in both the Make
  (`manifest.mn`) and GYP (`.gyp`) files. A new GTest executable also needs
  runner integration, such as the `GTESTS` list in `tests/gtests/gtests.sh`.
- For platform-specific or accelerated code, consider affected architectures,
  portable fallbacks, feature guards, and both build systems.
- Leave routine formatting to clang-format and CI. Do not claim tests or
  formatting checks ran without evidence.

## Restricted security work

Respect the issue's existing disclosure restrictions. Avoid adding vulnerability
or exploit details to suggested public commit messages or summaries. Continue
to report actionable security defects within the authorised review.
