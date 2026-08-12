plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
}

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
        versionCode = 1
        // Kept in step with src/core/version.h by hand; see android/readme.md.
        versionName = "3.3.0"

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

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions { jvmTarget = "17" }

    buildFeatures { compose = true }
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
}
