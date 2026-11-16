####################################################################################################
# Keep Focus names readable in stack traces, while still allowing R8 to shrink and optimize Focus
####################################################################################################

-keep,allowshrinking,allowoptimization class org.mozilla.focus.** { *; }


##### Default proguard settings:

# Add project specific ProGuard rules here.
# By default, the flags in this file are appended to flags specified
# in /Users/sebastian/Library/Android/sdk/tools/proguard/proguard-android.txt
# You can edit the include path and order by changing the proguardFiles
# directive in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

####################################################################################################
# Android architecture components
####################################################################################################

-dontwarn org.mozilla.geckoview.**
-dontwarn mozilla.components.**

####################################################################################################
# Mozilla Application Services
####################################################################################################

-keep class mozilla.appservices.** { *; }

####################################################################################################
# REMOVE all Log messages except warnings and errors
####################################################################################################
-assumenosideeffects class android.util.Log {
    public static boolean isLoggable(java.lang.String, int);
    public static int v(...);
    public static int i(...);
    public static int d(...);
}
