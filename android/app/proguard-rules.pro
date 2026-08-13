# R8 rules for the release build.
#
# Everything here exists because something reaches Kotlin from outside
# Kotlin -- the JNI linker by symbol name, or a serializer by annotation.
# R8 cannot see either, and what it cannot see it renames or removes.
# Each rule below says which of the two it is.

# ── JNI ──────────────────────────────────────────────────────────────
# The native library binds statically: libntrip_android.so exports
# Java_nl_pe1mew_ntripanalyser_NtripBridge_nativePump and the runtime
# finds it by that exact name. Rename the class or the method and the
# link fails at the first call, in the release build only -- an
# UnsatisfiedLinkError on Start, with a clean debug build to compare
# against.
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}

# ── kotlinx.serialization ────────────────────────────────────────────
# The library ships its own R8 rules and they cover the general case;
# these pin this app's own model, which is decoded from JSON the C side
# writes (Model.kt) and encoded into the saved profile store
# (Settings.kt). A stripped Companion or $$serializer surfaces as a
# decode failure -- and a decode failure publishes nothing, so the
# screen would sit at READY while a run worked perfectly.
-keepattributes *Annotation*, InnerClasses
-keepclassmembers class nl.pe1mew.ntripanalyser.** {
    *** Companion;
}
-keepclasseswithmembers class nl.pe1mew.ntripanalyser.** {
    kotlinx.serialization.KSerializer serializer(...);
}
-keep,includedescriptorclasses class nl.pe1mew.ntripanalyser.**$$serializer {
    *;
}

# ── Tink, under androidx.security-crypto ──────────────────────────
# The credential store's crypto library is annotated for build-time
# analysers -- @Immutable, @GuardedBy -- whose annotation classes are not
# on the runtime classpath and are not meant to be. They document
# intent; nothing reads them at run time. R8 reports them as missing
# references, and the silence is what these two lines buy.
-dontwarn com.google.errorprone.annotations.Immutable
-dontwarn javax.annotation.concurrent.GuardedBy

# ── Diagnosis ────────────────────────────────────────────────────────
# Keep line numbers in stack traces, and rename the source file so the
# original names are not shipped. Without this a crash report from a
# user names neither the file nor the line, and Play's vitals -- the
# only crash information this project collects at all
# (design/telemetry.md) -- become unreadable.
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile
