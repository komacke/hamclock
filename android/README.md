# HamClock for Android

This directory contains the standalone Android wrapper for HamClock.

## Architecture

* **Core Engine:** The native C++ HamClock engine is compiled via Android NDK using `-D_WEB_ONLY` and `-D_CLOCK_1600x960` (crisp 2x resolution for 1080p and 2K screens).
* **Display & Touch:** Interactive HTML5/WebSocket frontend rendered inside an accelerated, fullscreen Android `WebView`.
* **Zero Source Duplication:** The NDK CMake build references `../../../../ESPHamClock` directly.

## Building

### Option 1: Android Studio (Recommended)
1. Open **Android Studio**.
2. Select **Open** and choose the `android/` directory (`/path/to/hamclock/android`).
3. Allow Gradle to sync and download NDK/SDK dependencies if prompted.
4. Select **Build > Build Bundle(s) / APK(s) > Build APK(s)** or click **Run** on a connected device/emulator.

### Option 2: Command Line (with Android SDK installed)

#### Build Debug APK:
```bash
cd android
./gradlew assembleDebug
```
The resulting APK will be located at:
`android/app/build/outputs/apk/debug/org.openhamclock.hamclock-<version>-debug.apk`

#### Build & Sign Release App Bundle (.aab) for Google Play:
Set the keystore file and key alias in your environment (passwords will be prompted interactively and securely):
```bash
cd android
ANDROID_KEYSTORE_FILE=~/.keystores/hamclock-upload-key.keystore ANDROID_KEY_ALIAS=hamclock ./gradlew bundleRelease
```
*(You will be prompted to enter the keystore and key passwords without echoing them to the terminal).*

The resulting signed release bundle will be located at:
`android/app/build/outputs/bundle/release/org.openhamclock.hamclock-<version>-release.aab`
*(also available as `app-release.aab`)*

## Requirements
* Android SDK 36 (Android 16)
* Android NDK (version 25+ / 26+)
* Minimum supported device: Android 7.0+ (API 24)

