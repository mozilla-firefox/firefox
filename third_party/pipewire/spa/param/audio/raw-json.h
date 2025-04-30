/* Simple Plugin API */
/* SPDX-FileCopyrightText: Copyright © 2024 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#ifndef SPA_AUDIO_RAW_JSON_H
#define SPA_AUDIO_RAW_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \addtogroup spa_param
 * \{
 */

#include <spa/utils/dict.h>
#include <spa/utils/json.h>
#include <spa/param/audio/raw.h>
#include <spa/param/audio/raw-types.h>

#ifndef SPA_API_AUDIO_RAW_JSON
 #ifdef SPA_API_IMPL
  #define SPA_API_AUDIO_RAW_JSON SPA_API_IMPL
 #else
  #define SPA_API_AUDIO_RAW_JSON static inline
 #endif
#endif

SPA_API_AUDIO_RAW_JSON int
spa_audio_parse_position(const char *str, size_t len,
		uint32_t *position, uint32_t *n_channels)
{
	struct spa_json iter;
        char v[256];
	uint32_t channels = 0;

        if (spa_json_begin_array_relax(&iter, str, len) <= 0)
                return 0;

        while (spa_json_get_string(&iter, v, sizeof(v)) > 0 &&
		channels < SPA_AUDIO_MAX_CHANNELS) {
                position[channels++] = spa_type_audio_channel_from_short_name(v);
        }
	*n_channels = channels;
	return channels;
}

SPA_API_AUDIO_RAW_JSON int
spa_audio_info_raw_update(struct spa_audio_info_raw *info, const char *key, const char *val, bool force)
{
	uint32_t v;
	if (spa_streq(key, SPA_KEY_AUDIO_FORMAT)) {
		if (force || info->format == 0)
			info->format = (enum spa_audio_format)spa_type_audio_format_from_short_name(val);
	} else if (spa_streq(key, SPA_KEY_AUDIO_RATE)) {
		if (spa_atou32(val, &v, 0) && (force || info->rate == 0))
			info->rate = v;
	} else if (spa_streq(key, SPA_KEY_AUDIO_CHANNELS)) {
		if (spa_atou32(val, &v, 0) && (force || info->channels == 0))
			info->channels = SPA_MIN(v, SPA_AUDIO_MAX_CHANNELS);
	} else if (spa_streq(key, SPA_KEY_AUDIO_POSITION)) {
		if (force || info->channels == 0) {
			if (spa_audio_parse_position(val, strlen(val), info->position, &info->channels) > 0)
				SPA_FLAG_CLEAR(info->flags, SPA_AUDIO_FLAG_UNPOSITIONED);
		}
	}
	return 0;
}

SPA_API_AUDIO_RAW_JSON int SPA_SENTINEL
spa_audio_info_raw_init_dict_keys(struct spa_audio_info_raw *info,
		const struct spa_dict *defaults,
		const struct spa_dict *dict, ...)
{
	spa_zero(*info);
	SPA_FLAG_SET(info->flags, SPA_AUDIO_FLAG_UNPOSITIONED);
	if (dict) {
		const char *val, *key;
		va_list args;
		va_start(args, dict);
		while ((key = va_arg(args, const char *))) {
			if ((val = spa_dict_lookup(dict, key)) == NULL)
				continue;
			spa_audio_info_raw_update(info, key, val, true);
		}
		va_end(args);
	}
	if (defaults) {
		const struct spa_dict_item *it;
		spa_dict_for_each(it, defaults)
			spa_audio_info_raw_update(info, it->key, it->value, false);
	}
	return 0;
}

/**
 * \}
 */

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* SPA_AUDIO_RAW_JSON_H */
