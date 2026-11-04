# ProGuard rules for consumers of this library.

# These classes are used as AndroidX Navigation argument types, which name them as
# fully-qualified strings in a consumer's navigation graph XML and resolve them with
# Class.forName. Obfuscation must therefore not rename them. androidx.navigation's own
# consumer rules only cover NavArgs.fromBundle and Navigator subclasses, so argument types
# are not protected by anything else. Members do not need keeping; the CREATOR field of
# every Parcelable is preserved by proguard-android-optimize.txt.
-keep class mozilla.components.concept.storage.Address
-keep class mozilla.components.concept.storage.CreditCard
