/* PipeWire */
/* SPDX-FileCopyrightText: Copyright © 2021 Wim Taymans <wim.taymans@gmail.com> */
/* SPDX-License-Identifier: MIT */

#include <spa/pod/builder.h>
#include <spa/utils/dict.h>
#include <spa/utils/hook.h>
#include <spa/param/audio/raw.h>

#ifndef SPA_AUDIO_AEC_H
#define SPA_AUDIO_AEC_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SPA_API_AUDIO_AEC
 #ifdef SPA_API_IMPL
  #define SPA_API_AUDIO_AEC SPA_API_IMPL
 #else
  #define SPA_API_AUDIO_AEC static inline
 #endif
#endif

#define SPA_TYPE_INTERFACE_AUDIO_AEC SPA_TYPE_INFO_INTERFACE_BASE "Audio:AEC"

#define SPA_VERSION_AUDIO_AEC   1
struct spa_audio_aec {
	struct spa_interface iface;
	const char *name;
	const struct spa_dict *info;
	const char *latency;
};

struct spa_audio_aec_info {
#define SPA_AUDIO_AEC_CHANGE_MASK_PROPS	(1u<<0)
        uint64_t change_mask;

	const struct spa_dict *props;
};

struct spa_audio_aec_events {
#define SPA_VERSION_AUDIO_AEC_EVENTS	0
        uint32_t version;       /**< version of this structure */

	/** Emitted when info changes */
	void (*info) (void *data, const struct spa_audio_aec_info *info);
};

struct spa_audio_aec_methods {
#define SPA_VERSION_AUDIO_AEC_METHODS	3
        uint32_t version;

	int (*add_listener) (void *object,
			struct spa_hook *listener,
			const struct spa_audio_aec_events *events,
			void *data);

	int (*init) (void *object, const struct spa_dict *args, const struct spa_audio_info_raw *info);
	int (*run) (void *object, const float *rec[], const float *play[], float *out[], uint32_t n_samples);
	int (*set_props) (void *object, const struct spa_dict *args);
	/* since 0.3.58, version 1:1 */
	int (*activate) (void *object);
	/* since 0.3.58, version 1:1 */
	int (*deactivate) (void *object);

	/* version 1:2 */
	int (*enum_props) (void* object, int index, struct spa_pod_builder* builder);
	int (*get_params) (void* object, struct spa_pod_builder* builder);
	int (*set_params) (void *object, const struct spa_pod *args);

	/* version 1:3 */
	int (*init2) (void *object, const struct spa_dict *args,
			struct spa_audio_info_raw *play_info,
			struct spa_audio_info_raw *rec_info,
			struct spa_audio_info_raw *out_info);
};

SPA_API_AUDIO_AEC int spa_audio_aec_add_listener(struct spa_audio_aec *object,
			struct spa_hook *listener,
			const struct spa_audio_aec_events *events,
			void *data)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_audio_aec, &object->iface, add_listener, 0, listener, events, data);
}

SPA_API_AUDIO_AEC int spa_audio_aec_init(struct spa_audio_aec *object,
		const struct spa_dict *args, const struct spa_audio_info_raw *info)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_audio_aec, &object->iface, init, 0, args, info);
}
SPA_API_AUDIO_AEC int spa_audio_aec_run(struct spa_audio_aec *object,
		const float *rec[], const float *play[], float *out[], uint32_t n_samples)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_audio_aec, &object->iface, run, 0, rec, play, out, n_samples);
}
SPA_API_AUDIO_AEC int spa_audio_aec_set_props(struct spa_audio_aec *object, const struct spa_dict *args)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_audio_aec, &object->iface, set_props, 0, args);
}
SPA_API_AUDIO_AEC int spa_audio_aec_activate(struct spa_audio_aec *object)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_audio_aec, &object->iface, activate, 1);
}
SPA_API_AUDIO_AEC int spa_audio_aec_deactivate(struct spa_audio_aec *object)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_audio_aec, &object->iface, deactivate, 1);
}
SPA_API_AUDIO_AEC int spa_audio_aec_enum_props(struct spa_audio_aec *object,
		int index, struct spa_pod_builder* builder)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_audio_aec, &object->iface, enum_props, 2, index, builder);
}
SPA_API_AUDIO_AEC int spa_audio_aec_get_params(struct spa_audio_aec *object,
		struct spa_pod_builder* builder)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_audio_aec, &object->iface, get_params, 2, builder);
}
SPA_API_AUDIO_AEC int spa_audio_aec_set_params(struct spa_audio_aec *object,
		const struct spa_pod *args)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_audio_aec, &object->iface, set_params, 2, args);
}
SPA_API_AUDIO_AEC int spa_audio_aec_init2(struct spa_audio_aec *object,
		const struct spa_dict *args,
		struct spa_audio_info_raw *play_info,
		struct spa_audio_info_raw *rec_info,
		struct spa_audio_info_raw *out_info)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_audio_aec, &object->iface, init2, 3, args, play_info, rec_info, out_info);
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* SPA_AUDIO_AEC_H */
