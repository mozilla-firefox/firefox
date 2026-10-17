# Rules for the androidTest APK, used only when running UI tests against a minified build type.

# Optional integrations in test-only libraries, for classes neither the app nor the tests ship.
-dontwarn android.support.v4.app.**
-dontwarn androidx.work.multiprocess.**
-dontwarn com.google.protobuf.GeneratedMessageLite$MergeFromVisitor
-dontwarn com.google.protobuf.GeneratedMessageLite$Visitor
