/* Simple Plugin API */
/* SPDX-FileCopyrightText: Copyright © 2018 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#ifndef SPA_DEBUG_MEM_H
#define SPA_DEBUG_MEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

/**
 * \addtogroup spa_debug
 * \{
 */

#include <spa/debug/context.h>

#ifndef SPA_API_DEBUG_MEM
 #ifdef SPA_API_IMPL
  #define SPA_API_DEBUG_MEM SPA_API_IMPL
 #else
  #define SPA_API_DEBUG_MEM static inline
 #endif
#endif

SPA_API_DEBUG_MEM int spa_debugc_mem(struct spa_debug_context *ctx, int indent, const void *data, size_t size)
{
	const uint8_t *t = (const uint8_t*)data;
	char buffer[512];
	size_t i;
	int pos = 0;

	for (i = 0; i < size; i++) {
		if (i % 16 == 0)
			pos = sprintf(buffer, "%p: ", &t[i]);
		pos += sprintf(buffer + pos, "%02x ", t[i]);
		if (i % 16 == 15 || i == size - 1) {
			spa_debugc(ctx, "%*s" "%s", indent, "", buffer);
		}
	}
	return 0;
}

SPA_API_DEBUG_MEM int spa_debug_mem(int indent, const void *data, size_t size)
{
	return spa_debugc_mem(NULL, indent, data, size);
}
/**
 * \}
 */

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* SPA_DEBUG_MEM_H */
