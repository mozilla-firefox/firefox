/* Simple Plugin API */
/* SPDX-FileCopyrightText: Copyright © 2018 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#ifndef SPA_UTILS_RESULT_H
#define SPA_UTILS_RESULT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup spa_result Result handling
 * Asynchronous result utilities
 */

/**
 * \addtogroup spa_result
 * \{
 */

#include <errno.h>

#include <spa/utils/defs.h>

#ifndef SPA_API_RESULT
 #ifdef SPA_API_IMPL
  #define SPA_API_RESULT SPA_API_IMPL
 #else
  #define SPA_API_RESULT static inline
 #endif
#endif


#define SPA_ASYNC_BIT			(1 << 30)
#define SPA_ASYNC_SEQ_MASK		(SPA_ASYNC_BIT - 1)
#define SPA_ASYNC_MASK			(~SPA_ASYNC_SEQ_MASK)

#define SPA_RESULT_IS_OK(res)		((res) >= 0)
#define SPA_RESULT_IS_ERROR(res)	((res) < 0)
#define SPA_RESULT_IS_ASYNC(res)	(((res) & SPA_ASYNC_MASK) == SPA_ASYNC_BIT)

#define SPA_RESULT_ASYNC_SEQ(res)	((res) & SPA_ASYNC_SEQ_MASK)
#define SPA_RESULT_RETURN_ASYNC(seq)	(SPA_ASYNC_BIT | SPA_RESULT_ASYNC_SEQ(seq))

SPA_API_RESULT const char *spa_strerror(int err)
{
	int _err = -(err);
	if (SPA_RESULT_IS_ASYNC(err))
		_err = EINPROGRESS;
	return strerror(_err);
}

/**
 * \}
 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPA_UTILS_RESULT_H */
