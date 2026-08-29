# ProGuard / R8 Rules for HamClock Android

# Keep line numbers and source file names for readable stack traces in Play Console / crash logs
-keepattributes SourceFile,LineNumberTable

# Preserve JNI / Native interface classes and methods
-keep class org.openhamclock.hamclock.HamClockNative { *; }
-keepclasseswithmembernames class * {
    native <methods>;
}

# Preserve Android Activities and ViewBinding classes
-keep class org.openhamclock.hamclock.MainActivity { *; }
-keep class org.openhamclock.hamclock.databinding.** { *; }

# WebView JavaScript interface rules (if any JS interfaces are used)
-keepclassmembers class * {
    @android.webkit.JavascriptInterface <methods>;
}

# Suppress warnings from common libraries if any
-dontwarn okhttp3.**
-dontwarn javax.annotation.**
