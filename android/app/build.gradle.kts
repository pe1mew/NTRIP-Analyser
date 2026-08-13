import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
}

/*
 * The version comes from src/core/version.h, the one place every artefact
 * in this repository reads it -- the CLI, the GUI, the daemon and this
 * app. Kept by hand, the phone eventually reports a version no other
 * program was built from, and a bug report stops identifying the source
 * it came from.
 *
 * The header is deliberately dependency-free so that it can be parsed
 * textually; see its own comment.
 */
val versionHeader: String = rootProject.file("../src/core/version.h").readText()

fun versionPart(name: String): Int =
    Regex("#define\\s+NTRIP_VERSION_" + name + "\\s+(\\d+)")
        .find(versionHeader)?.groupValues?.get(1)?.toInt()
        ?: throw GradleException("NTRIP_VERSION_$name not found in src/core/version.h")

val verMajor = versionPart("MAJOR")
val verMinor = versionPart("MINOR")
val verPatch = versionPart("PATCH")

/*
 * MMmmpp: 3.3.0 becomes 30300, and the number rises with the version it
 * is derived from. Play refuses an upload whose versionCode it has seen
 * before, so a second upload of the same version means bumping the patch
 * in version.h -- which is correct, because it is a different binary.
 *
 * Each edition has its own applicationId and therefore its own
 * versionCode series; free and pro need not move together.
 */
val verCode = verMajor * 10000 + verMinor * 100 + verPatch

/*
 * Release signing, from a keystore this repository never contains.
 * android/keystore.properties is git-ignored and names a .jks kept
 * outside the tree; keystore.properties.example documents the four keys
 * and how to create one.
 *
 * Absent, a release build falls back to the debug key, so it can still be
 * assembled and tested. That artefact is deliberately not uploadable:
 * Play rejects a debug-signed APK, which is the right failure -- loud, at
 * submission, rather than something that merely looks releasable.
 */
val keystoreProperties = Properties().apply {
    val f = rootProject.file("keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}
val haveKeystore = keystoreProperties.getProperty("storeFile") != null

android {
    namespace = "nl.pe1mew.ntripanalyser"
    compileSdk = 35

    // Pinned rather than left to AGP's default: the default version
    // changes with the plugin, and a mismatch fails at configuration
    // time with an unhelpful path error rather than "NDK not found".
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "nl.pe1mew.ntripanalyser"
        minSdk = 26          // startForegroundService and notification channels
        targetSdk = 35
        versionCode = verCode
        versionName = "$verMajor.$verMinor.$verPatch"

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=none")   // pure C, no C++ runtime
                cFlags += listOf("-Wall")
            }
        }

        ndk {
            // 64-bit only: Play has required a 64-bit binary since 2019,
            // and every device this app targets is arm64.  x86_64 is here
            // for the emulator.
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    // Two Play listings from one source tree (android/design/editions.md).
    // Each flavor supplies its own Features object, so gating is
    // compile-time: the free APK does not contain the paid screens and
    // cannot be unlocked by flipping a boolean.
    flavorDimensions += "edition"
    productFlavors {
        create("free") {
            dimension = "edition"
            applicationIdSuffix = ".free"
            versionNameSuffix = "-free"
        }
        create("pro") {
            dimension = "edition"
            applicationIdSuffix = ".pro"
        }
    }

    signingConfigs {
        if (haveKeystore) {
            create("release") {
                storeFile = rootProject.file(keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.findByName("release")
                ?: signingConfigs.getByName("debug")

            // R8 on, with the rules in proguard-rules.pro. Enabled before
            // the first release rather than after it: what it breaks -- a
            // class reached only through JNI or reflection -- fails at run
            // time in the release build alone, and that is better met on a
            // bench than in the field.
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions { jvmTarget = "17" }

    buildFeatures {
        compose = true
        buildConfig = true   // the About box shows the version
    }
}

if (!haveKeystore) {
    gradle.projectsEvaluated {
        logger.lifecycle(
            "NTRIP-Analyser: android/keystore.properties not found -- release " +
                "builds are signed with the debug key and cannot be uploaded " +
                "to Play. See android/keystore.properties.example."
        )
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.ui)
    implementation(libs.androidx.material3)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.androidx.security.crypto)
}
