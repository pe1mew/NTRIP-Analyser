// Top-level build file. Plugins are declared here without applying them,
// so the versions in gradle/libs.versions.toml are the single source.
plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.kotlin.android) apply false
    alias(libs.plugins.kotlin.compose) apply false
    alias(libs.plugins.kotlin.serialization) apply false
}
