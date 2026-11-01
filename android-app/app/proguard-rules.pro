# [APP PROTECTION HARDENING]
# This used to be a single blanket "-keep class com.invisiblex.loader.** { *; }",
# which told R8 to leave the ENTIRE app package completely unobfuscated and
# unshrunk — every class name, method name, and field name shipped in the
# APK exactly as written, trivially readable with any decompiler. That
# defeated the whole point of minifyEnabled. The only thing that actually
# NEEDS protecting from obfuscation is NativeAuth: its native method names
# are resolved by the JNI linker against the exact symbol names exported
# from libixauth.so (Java_com_invisiblex_loader_NativeAuth_*), so the class
# name and native method signatures must stay exactly as declared. Nothing
# else in the package needs that — everything else is now free to be
# renamed/shrunk by R8 like normal app code.
-keep class com.invisiblex.loader.NativeAuth {
    native <methods>;
}

# Manifest-declared components (Activities/Services declared in
# AndroidManifest.xml, incl. InvisibleXTileService) are kept automatically
# by AGP's own consumer rules — no separate -keep needed for them here.

-keepclassmembers class * {
    @android.webkit.JavascriptInterface <methods>;
}

# Strip all android.util.Log calls from release builds entirely (R8 removes
# the calls AND their string arguments — including TAG constants and
# interpolated debug strings — rather than just no-opping them at runtime).
-assumenosideeffects class android.util.Log {
    public static *** v(...);
    public static *** d(...);
    public static *** i(...);
    public static *** w(...);
    public static *** e(...);
}

# Deeper identifier obfuscation: reuse the same short names across
# unrelated classes/members wherever scoping allows, instead of giving
# every symbol in the whole APK a unique one.
-repackageclasses ''
-allowaccessmodification
-overloadaggressively

# Networking + JSON run natively (ix_auth.cpp / libixauth.so) — no other
# JNI-exported Java classes to keep beyond NativeAuth above.

# androidx.security:security-crypto pulls in Google Tink, which
# references optional javax.annotation (JSR-305) classes that aren't on
# the Android classpath at all; they're compile-time-only annotations,
# never actually needed at runtime, so just tell R8 not to worry about
# their absence rather than keeping fake stubs for them.
-dontwarn javax.annotation.**
-dontwarn javax.annotation.concurrent.**
