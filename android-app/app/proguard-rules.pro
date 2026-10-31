-keep class com.invisiblex.loader.** { *; }
-keepclassmembers class * {
    @android.webkit.JavascriptInterface <methods>;
}
-keepattributes Signature
-keepattributes *Annotation*

# Networking + JSON now run natively (ix_auth.cpp / libixauth.so) — no
# JNI-exported Java classes to keep beyond com.invisiblex.loader.** above.

# androidx.security:security-crypto pulls in Google Tink, which
# references optional javax.annotation (JSR-305) classes that aren't on
# the Android classpath at all; they're compile-time-only annotations,
# never actually needed at runtime, so just tell R8 not to worry about
# their absence rather than keeping fake stubs for them.
-dontwarn javax.annotation.**
-dontwarn javax.annotation.concurrent.**
