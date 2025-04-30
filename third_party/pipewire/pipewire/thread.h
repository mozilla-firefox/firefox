/* PipeWire */
/* SPDX-FileCopyrightText: Copyright © 2021 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#ifndef PIPEWIRE_THREAD_H
#define PIPEWIRE_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <errno.h>

#include <spa/support/thread.h>

/** \defgroup pw_thread Thread
 *
 * \brief functions to manipulate threads
 */

/**
 * \addtogroup pw_thread
 * \{
 */

SPA_DEPRECATED
void pw_thread_utils_set(struct spa_thread_utils *impl);
struct spa_thread_utils *pw_thread_utils_get(void);
void *pw_thread_fill_attr(const struct spa_dict *props, void *attr);

#ifndef PW_API_THREAD_IMPL
#define PW_API_THREAD_IMPL static inline
#endif

PW_API_THREAD_IMPL struct spa_thread *pw_thread_utils_create(
		const struct spa_dict *props, void *(*start_routine)(void*), void *arg)
{
	return spa_thread_utils_create(pw_thread_utils_get(), props, start_routine, arg);
}
PW_API_THREAD_IMPL int pw_thread_utils_join(struct spa_thread *thread, void **retval)
{
	return spa_thread_utils_join(pw_thread_utils_get(), thread, retval);
}
PW_API_THREAD_IMPL int pw_thread_utils_get_rt_range(const struct spa_dict *props, int *min, int *max)
{
	return spa_thread_utils_get_rt_range(pw_thread_utils_get(), props, min, max);
}
PW_API_THREAD_IMPL int pw_thread_utils_acquire_rt(struct spa_thread *thread, int priority)
{
	return spa_thread_utils_acquire_rt(pw_thread_utils_get(), thread, priority);
}
PW_API_THREAD_IMPL int pw_thread_utils_drop_rt(struct spa_thread *thread)
{
	return spa_thread_utils_drop_rt(pw_thread_utils_get(), thread);
}

/**
 * \}
 */

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* PIPEWIRE_THREAD_H */
