####################################################################################################
# Keep Fenix names readable in stack traces, while still allowing R8 to shrink and optimize Fenix
####################################################################################################

-keep,allowshrinking,allowoptimization class org.mozilla.fenix.** { *; }

####################################################################################################
# Protobuf-lite reads and writes message fields through Unsafe, using the types declared in the
# .proto, so R8 must not change them
####################################################################################################

-keepclassmembers class * extends com.google.protobuf.GeneratedMessageLite {
    <fields>;
}

####################################################################################################
# GeckoView built-ins
####################################################################################################

-dontwarn org.mozilla.geckoview.**

# Raptor now writes a *-config.yaml file to specify Gecko runtime settings (e.g. the profile dir). This
# file gets deserialized into a DebugConfig object, which is why we need to keep this class
# and its members.
-keep class org.mozilla.gecko.util.DebugConfig { *; }

####################################################################################################
# Remove debug logs from release builds
####################################################################################################
-assumenosideeffects class android.util.Log {
    public static boolean isLoggable(java.lang.String, int);
    public static int v(...);
    public static int d(...);
}

####################################################################################################
# Mozilla Application Services
####################################################################################################

-keep class mozilla.appservices.** { *; }

# Keep code generated from Glean Metrics
-keep class org.mozilla.fenix.GleanMetrics.** {  *; }
