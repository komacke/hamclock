import java.io.BufferedReader
import java.io.InputStreamReader

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
}

fun getGitVersion(): Pair<String, Boolean> {
    return try {
        val process = ProcessBuilder("git", "describe", "--exact-match", "--tags")
            .directory(rootDir)
            .redirectErrorStream(true)
            .start()
        val tag = process.inputStream.bufferedReader().readText().trim()
        val exitCode = process.waitFor()
        if (exitCode == 0 && tag.isNotEmpty() && !tag.startsWith("fatal")) {
            Pair(tag, true)
        } else {
            Pair("edge", false)
        }
    } catch (e: Exception) {
        Pair("edge", false)
    }
}

fun getGitCommitCount(): Int {
    return try {
        val process = ProcessBuilder("git", "rev-list", "--count", "HEAD")
            .directory(rootDir)
            .redirectErrorStream(true)
            .start()
        val output = process.inputStream.bufferedReader().readText().trim()
        val exitCode = process.waitFor()
        if (exitCode == 0 && output.isNotEmpty()) {
            output.toInt()
        } else {
            1
        }
    } catch (e: Exception) {
        1
    }
}

fun hasLocalChanges(): Boolean {
    return try {
        val process = ProcessBuilder("git", "diff-index", "--quiet", "HEAD", "--")
            .directory(rootDir)
            .start()
        process.waitFor() != 0
    } catch (e: Exception) {
        false
    }
}

val (gitTag, onTag) = getGitVersion()
val appVersion = if (onTag) gitTag else "edge"

// Refuse to build on a tag if there are local uncommitted changes
if (onTag && hasLocalChanges()) {
    throw GradleException(
        """
        
        ========================================================================
        ERROR: Cannot build release tag '$gitTag' with uncommitted local changes.
        Git tags must be built from a clean repository state matching the tag.
        Please commit, stash, or revert local changes before building tag '$gitTag'.
        ========================================================================
        """.trimIndent()
    )
}

// If on a tag, update ESPHamClock/version.cpp with stripped version (e.g. v4.02.1 -> 4.02)
if (onTag) {
    var hcTag = gitTag.removePrefix("v").removePrefix("V")
    if (hcTag.contains(".")) {
        hcTag = hcTag.substringBeforeLast(".")
    }
    val versionFile = file("${rootDir}/../ESPHamClock/version.cpp")
    if (versionFile.exists()) {
        val content = versionFile.readText()
        val updated = content.replace(Regex("""(hc_version\s*=\s*")[^"]+(")"""), "$1$hcTag$2")
        if (content != updated) {
            versionFile.writeText(updated)
        }
    }
}


// Automatically restore ESPHamClock/version.cpp at the end of the build so no git artifacts remain
val restoreVersionCppTask = tasks.register("restoreVersionCpp") {
    doLast {
        val versionFile = file("${rootDir}/../ESPHamClock/version.cpp")
        if (versionFile.exists()) {
            ProcessBuilder("git", "restore", versionFile.absolutePath)
                .directory(rootDir)
                .start()
                .waitFor()
        }
    }
}

tasks.matching {
    it.name.startsWith("assemble") ||
    it.name.startsWith("bundle") ||
    it.name.startsWith("buildCMake")
}.configureEach {
    finalizedBy(restoreVersionCppTask)
}

android {

    namespace = "org.openhamclock.hamclock"
    compileSdk = 36
    ndkVersion = "27.1.12297006"

    defaultConfig {
        applicationId = "org.openhamclock.hamclock"
        minSdk = 24
        targetSdk = 36
        versionCode = getGitCommitCount()
        versionName = appVersion

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++17", "-pthread")
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
    }

fun resolvePath(path: String): File {
    val expanded = if (path.startsWith("~/") || path == "~") {
        path.replaceFirst("~", System.getProperty("user.home"))
    } else {
        path
    }
    return file(expanded)
}

fun promptSecure(prompt: String): String? {
    val console = System.console()
    if (console != null) {
        val chars = console.readPassword("$prompt: ")
        return chars?.let { String(it) }?.ifEmpty { null }
    }
    print("$prompt: ")
    System.out.flush()
    val reader = BufferedReader(InputStreamReader(System.`in`))
    return reader.readLine()?.ifEmpty { null }
}

fun promptPlain(prompt: String): String? {
    val console = System.console()
    if (console != null) {
        return console.readLine("$prompt: ")?.ifEmpty { null }
    }
    print("$prompt: ")
    System.out.flush()
    val reader = BufferedReader(InputStreamReader(System.`in`))
    return reader.readLine()?.ifEmpty { null }
}

    signingConfigs {
        create("release") {
            val storeFilePath = System.getenv("ANDROID_KEYSTORE_FILE")
                ?: (project.findProperty("android.injected.signing.store.file") as? String)

            if (!storeFilePath.isNullOrEmpty()) {
                val keystoreFile = resolvePath(storeFilePath)
                if (keystoreFile.exists()) {
                    storeFile = keystoreFile

                    val keyAliasStr = System.getenv("ANDROID_KEY_ALIAS")
                        ?: (project.findProperty("android.injected.signing.key.alias") as? String)
                        ?: promptPlain("Enter Key Alias")?.trim()

                    val storePass = System.getenv("ANDROID_KEYSTORE_PASSWORD")
                        ?: (project.findProperty("android.injected.signing.store.password") as? String)
                        ?: promptSecure("Enter Keystore Password")

                    val keyPassEnv = System.getenv("ANDROID_KEY_PASSWORD")
                        ?: (project.findProperty("android.injected.signing.key.password") as? String)
                    val keyPass = if (!keyPassEnv.isNullOrEmpty()) {
                        keyPassEnv
                    } else if (!storePass.isNullOrEmpty()) {
                        val prompted = promptSecure("Enter Key Password (press Enter if same as keystore)")
                        if (prompted.isNullOrEmpty()) storePass else prompted
                    } else {
                        null
                    }

                    if (!keyAliasStr.isNullOrEmpty() && !storePass.isNullOrEmpty()) {
                        keyAlias = keyAliasStr
                        storePassword = storePass
                        keyPassword = keyPass ?: storePass
                    } else {
                        throw GradleException("Release signing error: keystore alias or password was not provided.")
                    }
                } else {
                    throw GradleException("Release keystore file not found at: ${keystoreFile.absolutePath}")
                }
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            val releaseSigning = signingConfigs.getByName("release")
            signingConfig = if (releaseSigning.storeFile != null && releaseSigning.storeFile!!.exists()) {
                releaseSigning
            } else {
                signingConfigs.getByName("debug")
            }
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }


    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    kotlinOptions {
        jvmTarget = "1.8"
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildFeatures {
        viewBinding = true
    }

    applicationVariants.all {
        outputs.all {
            val outputImpl = this as? com.android.build.gradle.internal.api.BaseVariantOutputImpl
            outputImpl?.outputFileName = "org.openhamclock.hamclock-${versionName}-${name}.apk"
        }
    }
}




dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.material)
    implementation(libs.androidx.webkit)
}
