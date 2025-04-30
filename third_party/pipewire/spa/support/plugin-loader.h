/* Simple Plugin API */
/* SPDX-FileCopyrightText: Copyright © 2021 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#ifndef SPA_PLUGIN_LOADER_H
#define SPA_PLUGIN_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <spa/utils/hook.h>
#include <spa/utils/dict.h>

#ifndef SPA_API_PLUGIN_LOADER
 #ifdef SPA_API_IMPL
  #define SPA_API_PLUGIN_LOADER SPA_API_IMPL
 #else
  #define SPA_API_PLUGIN_LOADER static inline
 #endif
#endif

/** \defgroup spa_plugin_loader Plugin Loader
 * SPA plugin loader
 */

/**
 * \addtogroup spa_plugin_loader
 * \{
 */

#define SPA_TYPE_INTERFACE_PluginLoader	SPA_TYPE_INFO_INTERFACE_BASE "PluginLoader"

#define SPA_VERSION_PLUGIN_LOADER		0
struct spa_plugin_loader { struct spa_interface iface; };

struct spa_plugin_loader_methods {
#define SPA_VERSION_PLUGIN_LOADER_METHODS	0
        uint32_t version;

	/**
	 * Load a SPA plugin.
         *
         * \param factory_name Plugin factory name
         * \param info Info dictionary for plugin. NULL if none.
         * \return plugin handle, or NULL on error
	 */
	struct spa_handle *(*load) (void *object, const char *factory_name, const struct spa_dict *info);

	/**
	 * Unload a SPA plugin.
         *
         * \param handle Plugin handle.
         * \return 0 on success, < 0 on error
	 */
	int (*unload)(void *object, struct spa_handle *handle);
};

SPA_API_PLUGIN_LOADER struct spa_handle *
spa_plugin_loader_load(struct spa_plugin_loader *loader, const char *factory_name, const struct spa_dict *info)
{
	return spa_api_method_null_r(struct spa_handle *, NULL, spa_plugin_loader, loader, &loader->iface,
			load, 0, factory_name, info);
}

SPA_API_PLUGIN_LOADER int
spa_plugin_loader_unload(struct spa_plugin_loader *loader, struct spa_handle *handle)
{
	return spa_api_method_null_r(int, -1, spa_plugin_loader, loader, &loader->iface,
			unload, 0, handle);
}

/**
 * \}
 */

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* SPA_PLUGIN_LOADER_H */
