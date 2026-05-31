/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Declarations for the amalgamated pq-crystals Dilithium reference
 * implementation in dilithium-pqcrystals-ref.c, monomorphized to ML-DSA-87
 * (Dilithium mode 5). Only the entry points freebl needs are exposed here. */

#ifndef DILITHIUM_PQCRYSTALS_REF_H
#define DILITHIUM_PQCRYSTALS_REF_H

#include <stddef.h>
#include <stdint.h>

#define DILITHIUM5_PUBLICKEYBYTES 2592
#define DILITHIUM5_SECRETKEYBYTES 4896
#define DILITHIUM5_SIGNATUREBYTES 4627
#define DILITHIUM_SEEDBYTES 32
#define DILITHIUM_RNDBYTES 32

/* Deterministic key generation from a SEEDBYTES-byte seed. */
int pqcrystals_dilithium5_ref_keypair_internal(uint8_t *pk, uint8_t *sk,
                                               const uint8_t coins[DILITHIUM_SEEDBYTES]);

/* Random key generation (draws the seed from randombytes()). */
int pqcrystals_dilithium5_ref_keypair(uint8_t *pk, uint8_t *sk);

/* Signs m with the prefix pre = (0x00, ctxlen, ctx). rnd is the per-signature
 * randomizer (zeros for deterministic, random bytes for hedged). */
int pqcrystals_dilithium5_ref_signature_internal(uint8_t *sig, size_t *siglen,
                                                 const uint8_t *m, size_t mlen,
                                                 const uint8_t *pre, size_t prelen,
                                                 const uint8_t rnd[DILITHIUM_RNDBYTES],
                                                 const uint8_t *sk);

/* Verifies sig over m with the prefix pre = (0x00, ctxlen, ctx). Returns 0 on
 * success, nonzero on failure. */
int pqcrystals_dilithium5_ref_verify_internal(const uint8_t *sig, size_t siglen,
                                              const uint8_t *m, size_t mlen,
                                              const uint8_t *pre, size_t prelen,
                                              const uint8_t *pk);

#endif /* DILITHIUM_PQCRYSTALS_REF_H */
