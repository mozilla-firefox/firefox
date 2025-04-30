/* Simple Plugin API */
/* SPDX-FileCopyrightText: Copyright © 2021 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#ifndef SPA_I18N_H
#define SPA_I18N_H

#ifdef __cplusplus
extern "C" {
#endif

#include <spa/utils/hook.h>
#include <spa/utils/defs.h>

#ifndef SPA_API_I18N
 #ifdef SPA_API_IMPL
  #define SPA_API_I18N SPA_API_IMPL
 #else
  #define SPA_API_I18N static inline
 #endif
#endif

/** \defgroup spa_i18n I18N
 * Gettext interface
 */

/**
 * \addtogroup spa_i18n
 * \{
 */

#define SPA_TYPE_INTERFACE_I18N		SPA_TYPE_INFO_INTERFACE_BASE "I18N"

#define SPA_VERSION_I18N		0
struct spa_i18n { struct spa_interface iface; };

struct spa_i18n_methods {
#define SPA_VERSION_I18N_METHODS	0
        uint32_t version;

	/**
	 * Translate a message
         *
         * \param object the i18n interface
         * \param msgid the message
         * \return a translated message
	 */
	const char *(*text) (void *object, const char *msgid);

	/**
	 * Translate a message for a number
         *
         * \param object the i18n interface
         * \param msgid the message to translate
         * \param msgid_plural the plural form of \a msgid
         * \param n a number
         * \return a translated message for the number \a n
	 */
	const char *(*ntext) (void *object, const char *msgid,
			const char *msgid_plural, unsigned long int n);
};

SPA_FORMAT_ARG_FUNC(2)
SPA_API_I18N const char *
spa_i18n_text(struct spa_i18n *i18n, const char *msgid)
{
	return spa_api_method_null_r(const char *, msgid, spa_i18n, i18n, &i18n->iface,
			text, 0, msgid);
}

SPA_API_I18N const char *
spa_i18n_ntext(struct spa_i18n *i18n, const char *msgid,
		const char *msgid_plural, unsigned long int n)
{
	return spa_api_method_null_r(const char *, n == 1 ? msgid : msgid_plural,
			spa_i18n, i18n, &i18n->iface, ntext, 0, msgid, msgid_plural, n);
}

/**
 * \}
 */

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* SPA_I18N_H */
