# ProGuard rules for consumers of this library.

# Addon is used as an AndroidX Navigation argument type, which names it as a
# fully-qualified string in a consumer's navigation graph XML and resolves it with
# Class.forName. Obfuscation must therefore not rename it. androidx.navigation's own
# consumer rules only cover NavArgs.fromBundle and Navigator subclasses, so argument types
# are not protected by anything else. Members do not need keeping; the CREATOR field of
# every Parcelable is preserved by proguard-android-optimize.txt.
-keep class mozilla.components.feature.addons.Addon
