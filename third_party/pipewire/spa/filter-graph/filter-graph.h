/* Simple Plugin API */
/* SPDX-FileCopyrightText: Copyright © 2024 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#ifndef SPA_FILTER_GRAPH_H
#define SPA_FILTER_GRAPH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#include <spa/utils/defs.h>
#include <spa/utils/hook.h>
#include <spa/pod/builder.h>

#ifndef SPA_API_FILTER_GRAPH
 #ifdef SPA_API_IMPL
  #define SPA_API_FILTER_GRAPH SPA_API_IMPL
 #else
  #define SPA_API_FILTER_GRAPH static inline
 #endif
#endif

/** \defgroup spa_filter_graph Filter Graph
 * a graph of filters
 */

/**
 * \addtogroup spa_filter_graph
 * \{
 */

/**
 * A graph of filters
 */
#define SPA_TYPE_INTERFACE_FilterGraph	SPA_TYPE_INFO_INTERFACE_BASE "FilterGraph"

#define SPA_VERSION_FILTER_GRAPH		0
struct spa_filter_graph { struct spa_interface iface; };

struct spa_filter_graph_info {
	uint32_t n_inputs;
	uint32_t n_outputs;

#define SPA_FILTER_GRAPH_CHANGE_MASK_FLAGS		(1u<<0)
#define SPA_FILTER_GRAPH_CHANGE_MASK_PROPS		(1u<<1)
	uint64_t change_mask;

	uint64_t flags;
	struct spa_dict *props;
};

struct spa_filter_graph_events {
#define SPA_VERSION_FILTER_GRAPH_EVENTS	0
	uint32_t version;

	void (*info) (void *object, const struct spa_filter_graph_info *info);

	void (*apply_props) (void *object, enum spa_direction direction, const struct spa_pod *props);

	void (*props_changed) (void *object, enum spa_direction direction);
};

struct spa_filter_graph_methods {
#define SPA_VERSION_FILTER_GRAPH_METHODS	0
	uint32_t version;

	int (*add_listener) (void *object,
			struct spa_hook *listener,
			const struct spa_filter_graph_events *events,
			void *data);

	int (*enum_prop_info) (void *object, uint32_t idx, struct spa_pod_builder *b,
			struct spa_pod **param);
	int (*get_props) (void *object, struct spa_pod_builder *b, struct spa_pod **props);
	int (*set_props) (void *object, enum spa_direction direction, const struct spa_pod *props);

	int (*activate) (void *object, const struct spa_dict *props);
	int (*deactivate) (void *object);

	int (*reset) (void *object);

	int (*process) (void *object, const void *in[], void *out[], uint32_t n_samples);
};

SPA_API_FILTER_GRAPH int spa_filter_graph_add_listener(struct spa_filter_graph *object,
			struct spa_hook *listener,
			const struct spa_filter_graph_events *events, void *data)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_filter_graph, &object->iface, add_listener, 0, listener,
			events, data);
}

SPA_API_FILTER_GRAPH int spa_filter_graph_enum_prop_info(struct spa_filter_graph *object,
		uint32_t idx, struct spa_pod_builder *b, struct spa_pod **param)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_filter_graph, &object->iface, enum_prop_info, 0, idx, b, param);
}
SPA_API_FILTER_GRAPH int spa_filter_graph_get_props(struct spa_filter_graph *object,
		struct spa_pod_builder *b, struct spa_pod **props)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_filter_graph, &object->iface, get_props, 0, b, props);
}

SPA_API_FILTER_GRAPH int spa_filter_graph_set_props(struct spa_filter_graph *object,
		enum spa_direction direction, const struct spa_pod *props)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_filter_graph, &object->iface, set_props, 0, direction, props);
}

SPA_API_FILTER_GRAPH int spa_filter_graph_activate(struct spa_filter_graph *object, const struct spa_dict *props)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_filter_graph, &object->iface, activate, 0, props);
}
SPA_API_FILTER_GRAPH int spa_filter_graph_deactivate(struct spa_filter_graph *object)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_filter_graph, &object->iface, deactivate, 0);
}

SPA_API_FILTER_GRAPH int spa_filter_graph_reset(struct spa_filter_graph *object)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_filter_graph, &object->iface, reset, 0);
}

SPA_API_FILTER_GRAPH int spa_filter_graph_process(struct spa_filter_graph *object,
			const void *in[], void *out[], uint32_t n_samples)
{
	return spa_api_method_r(int, -ENOTSUP,
			spa_filter_graph, &object->iface, process, 0, in, out, n_samples);
}

/**
 * \}
 */

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* SPA_FILTER_GRAPH_H */

