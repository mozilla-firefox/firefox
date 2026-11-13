/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This is a mock implementation of Android's `Context` object to allow us to run tests of the generated
 * code against the real `FeatureVariables` code.
 */
@file:Suppress("InvalidPackageDeclaration")

package android.content

import android.graphics.drawable.Drawable

@Suppress("UNUSED_PARAMETER", "PACKAGE_OR_CLASSIFIER_REDECLARATION", "FunctionOnlyReturningConstant")
class Context {
    val resources = Resources

    val applicationContext = this

    val packageName = "dummy.package.name"

    val theme = "a theme"

    fun getDrawable(res: Int): Drawable = Drawable(res)

    fun getString(res: Int): String = "res:$res"
}

@Suppress("UNUSED_PARAMETER", "PACKAGE_OR_CLASSIFIER_REDECLARATION", "FunctionOnlyReturningConstant")
object Resources {
    fun getIdentifier(resName: String, defType: String, packageName: String): Int? = null

    fun getDrawable(resId: Int, theme: String) = Drawable(resId)

    fun getResourceName(resId: Int) = "res:$resId"
}

@Suppress("PACKAGE_OR_CLASSIFIER_REDECLARATION")
class SharedPreferences {
    // Minimal interface used by generated code.
    fun contains(key: String): Boolean = map.containsKey(key)
    fun getBoolean(key: String, def: Boolean): Boolean = (map[key] as? Boolean) ?: def
    fun getString(key: String, def: String): String = (map[key] as? String) ?: def
    fun getInt(key: String, def: Int): Int = (map[key] as? Int) ?: def

    // For testing
    val map = mutableMapOf<String, Any>()
    fun put(key: String, value: Boolean) = map.put(key, value)
    fun put(key: String, value: String) = map.put(key, value)
    fun put(key: String, value: Int) = map.put(key, value)
    fun clear() = map.clear()
    fun remove(key: String) = map.remove(key)
}
