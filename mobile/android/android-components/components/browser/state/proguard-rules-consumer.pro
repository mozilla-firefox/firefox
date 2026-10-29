# ProGuard rules for consumers of this library.

# Mockito's @DoNotMock annotation is used in production code to signal test behavior.
# Since Mockito is a 'compileOnly' dependency, R8 fails to find the class during
# minification. We tell R8 to ignore this missing reference as it is not needed at runtime.
-dontwarn org.mockito.DoNotMock
-keep class org.mockito.DoNotMock

# PermissionHighlightsState is used as an AndroidX Navigation argument type, which names it
# as a fully-qualified string in a consumer's navigation graph XML and resolves it with
# Class.forName. Obfuscation must therefore not rename it. androidx.navigation's own
# consumer rules only cover NavArgs.fromBundle and Navigator subclasses, so argument types
# are not protected by anything else. Members do not need keeping; the CREATOR field of
# every Parcelable is preserved by proguard-android-optimize.txt.
-keep class mozilla.components.browser.state.state.content.PermissionHighlightsState
