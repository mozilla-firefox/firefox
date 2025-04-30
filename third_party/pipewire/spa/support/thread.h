/* Simple Plugin API */
/* SPDX-FileCopyrightText: Copyright © 2021 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#ifndef SPA_THREAD_H
#define SPA_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <errno.h>

#include <spa/utils/defs.h>
#include <spa/utils/hook.h>
#include <spa/utils/dict.h>

#ifndef SPA_API_THREAD
 #ifdef SPA_API_IMPL
  #define SPA_API_THREAD SPA_API_IMPL
 #else
  #define SPA_API_THREAD static inline
 #endif
#endif

/** \defgroup spa_thread Thread
 * Threading utility interfaces
 */

/**
 * \addtogroup spa_thread
 * \{
 */

/** a thread object.
 * This can be cast to a platform native thread, like pthread on posix systems
 */
#define SPA_TYPE_INFO_Thread		SPA_TYPE_INFO_POINTER_BASE "Thread"
struct spa_thread;

#define SPA_TYPE_INTERFACE_ThreadUtils	SPA_TYPE_INFO_INTERFACE_BASE "ThreadUtils"
#define SPA_VERSION_THREAD_UTILS		0
struct spa_thread_utils { struct spa_interface iface; };

/** thread utils */
struct spa_thread_utils_methods {
#define SPA_VERSION_THREAD_UTILS_METHODS	0
	uint32_t version;

	/** create a new thread that runs \a start with \a arg */
	struct spa_thread * (*create) (void *object, const struct spa_dict *props,
			void *(*start)(void*), void *arg);
	/** stop and join a thread */
	int (*join)(void *object, struct spa_thread *thread, void **retval);

	/** get realtime priority range for threads created with \a props */
	int (*get_rt_range) (void *object, const struct spa_dict *props, int *min, int *max);
	/** acquire realtime priority, a priority of -1 refers to the priority
	 * configured in the realtime module
	 */
	int (*acquire_rt) (void *object, struct spa_thread *thread, int priority);
	/** drop realtime priority */
	int (*drop_rt) (void *object, struct spa_thread *thread);
};

/** \copydoc spa_thread_utils_methods.create
 * \sa spa_thread_utils_methods.create */
SPA_API_THREAD struct spa_thread *spa_thread_utils_create(struct spa_thread_utils *o,
		const struct spa_dict *props, void *(*start_routine)(void*), void *arg)
{
	return spa_api_method_r(struct spa_thread *, NULL,
			spa_thread_utils, &o->iface, create, 0,
			props, start_routine, arg);
}

/** \copydoc spa_thread_utils_methods.join
 * \sa spa_thread_utils_methods.join */
SPA_API_THREAD int spa_thread_utils_join(struct spa_thread_utils *o,
		struct spa_thread *thread, void **retval)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_thread_utils, &o->iface, join, 0,
			thread, retval);
}

/** \copydoc spa_thread_utils_methods.get_rt_range
 * \sa spa_thread_utils_methods.get_rt_range */
SPA_API_THREAD int spa_thread_utils_get_rt_range(struct spa_thread_utils *o,
		const struct spa_dict *props, int *min, int *max)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_thread_utils, &o->iface, get_rt_range, 0,
			props, min, max);
}

/** \copydoc spa_thread_utils_methods.acquire_rt
 * \sa spa_thread_utils_methods.acquire_rt */
SPA_API_THREAD int spa_thread_utils_acquire_rt(struct spa_thread_utils *o,
		struct spa_thread *thread, int priority)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_thread_utils, &o->iface, acquire_rt, 0,
			thread, priority);
}

/** \copydoc spa_thread_utils_methods.drop_rt
 * \sa spa_thread_utils_methods.drop_rt */
SPA_API_THREAD int spa_thread_utils_drop_rt(struct spa_thread_utils *o,
		struct spa_thread *thread)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_thread_utils, &o->iface, drop_rt, 0, thread);
}

#define SPA_KEY_THREAD_NAME		"thread.name"		/* the thread name */
#define SPA_KEY_THREAD_STACK_SIZE	"thread.stack-size"	/* the stack size of the thread */
#define SPA_KEY_THREAD_AFFINITY		"thread.affinity"	/* array of CPUs for this thread */
#define SPA_KEY_THREAD_CREATOR		"thread.creator"	/* platform specific thread creator function */

/**
 * \}
 */

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* SPA_THREAD_H */
