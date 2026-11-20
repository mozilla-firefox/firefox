/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if defined(__ELF__) && defined(__x86_64__)
#include <cet.h>
#endif

#if defined(_WIN32) && defined(__i386__)
// Mark the object as SAFESEH-enabled.
.def @feat.00;
.scl 3;
.type 0;
.endef
.global @feat.00
.set @feat.00, 1
#endif

.global ICU_DATA_SYMBOL
#if defined(_WIN32)
.section .rodata, "dr"
#elif defined(__APPLE__)
.data
.const
#elif defined(__wasi__)
.section .rodata,"",@
#else
.section .rodata
#endif
.ascii ICU_DATA_HASH
.balign 16
ICU_DATA_SYMBOL:
.incbin ICU_DATA_FILE
#ifdef __wasi__
.size ICU_DATA_SYMBOL, . - ICU_DATA_SYMBOL
#endif
