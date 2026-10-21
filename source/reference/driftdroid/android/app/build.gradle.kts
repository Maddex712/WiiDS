import java.io.File
import java.util.Properties

plugins {
    id("com.android.application")
    kotlin("android")
}

val thorBuild = providers.gradleProperty("thorBuild").orNull == "true"
val thorRetroRewind = thorBuild && providers.gradleProperty("thorRetroRewind").orNull == "true"
// WiiDS release signing: a private key described outside every repository. Without it (any other
// machine) release builds fall back to the debug key so they still build and install.
val wiidsKeystore: Properties? = File(System.getProperty("user.home"), ".wiids-release/keystore.properties")
    .takeIf { it.isFile }?.let { f -> Properties().apply { f.inputStream().use { stream -> load(stream) } } }
// One WiiDS version for every app, passed by the packaging scripts (release/version.properties).
val wiidsVersionName = providers.gradleProperty("wiidsVersionName").orNull
val wiidsVersionCode = providers.gradleProperty("wiidsVersionCode").orNull?.toInt()
// The public WiiDS app: every game in one APK under its own id, so it installs beside the
// private standalone builds.
val wiidsApp = thorBuild && providers.gradleProperty("wiidsApp").orNull == "true"

android {
    namespace = "com.driftdroid.android"
    compileSdk = 34
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = when {
            wiidsApp -> "io.github.maddex712.wiids"
            thorBuild -> "com.driftdroid.android.thor"
            else -> "com.driftdroid.android"
        }
        // NSMB Wii's guest memory uses memfd_create (Android 11, API 30).
        minSdk = 30
        targetSdk = 34
        versionCode = wiidsVersionCode ?: 6
        versionName = wiidsVersionName ?: if (thorBuild) "1.3.0-thor-dev" else "1.3.0"
        buildConfigField("boolean", "THOR_BUILD", thorBuild.toString())
        buildConfigField("boolean", "THOR_RETRO_REWIND", thorRetroRewind.toString())
        manifestPlaceholders["appLabel"] = when {
            wiidsApp -> "WiiDS"
            thorBuild -> "DriftDroid Thor"
            else -> "@string/app_name"
        }
        // Thor launches only through ModePicker's runtime/update gate.
        manifestPlaceholders["gameActivityExported"] = (!thorBuild).toString()

        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_shared"
                cppFlags += "-std=c++20"
            }
        }
    }

    buildFeatures {
        buildConfig = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.28.3"
        }
    }

    sourceSets {
        getByName("main") {
            // cacert.pem: the TLS root bundle network_ssl.cpp needs; same file desktop builds copy
            // next to the executable (runtime/cmake/PublicProducts.cmake).
            assets.srcDir("../../runtime/assets/certs")
            // initial_pipeline_cache.db: shader recipes imported on first launch
            // (aurora-main/lib/gfx/pipeline_cache.cpp).
            assets.srcDir("../../runtime/assets/pipeline")
            // runtime-nsmbw/ and runtime-msmbwii/: staged by experiments/release/stage_merged.py.
            assets.srcDir("../../../../build/wiids-merge/assets")
        }
    }

    androidResources {
        // SQLite reads the pipeline seed with random access through SDL's asset stream.
        noCompress += "db"
    }

    packaging {
        jniLibs {
            // Compressed in the APK (about 330 MB of game libraries), extracted at install.
            useLegacyPackaging = true
            if (thorBuild) {
                // Thor uses standalone products. Stale staged libraries must not enable RR.
                excludes += "**/libGameCombined.so"
                if (!thorRetroRewind) excludes += "**/libRetroRewind.so"
            }
        }
    }

    signingConfigs {
        if (wiidsKeystore != null) {
            create("wiids") {
                storeFile = file(wiidsKeystore.getProperty("storeFile"))
                storePassword = wiidsKeystore.getProperty("storePassword")
                keyAlias = wiidsKeystore.getProperty("keyAlias")
                keyPassword = wiidsKeystore.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Signed with the debug key so release-type builds can be installed and measured
            // directly. Android only enables CheckJNI (and its per-call class/field validation,
            // ~10% of the UI thread in a debug profile) for debuggable builds, so performance
            // testing has to happen here, not on the debug variant. WiiDS releases use the private
            // WiiDS key when present (see wiidsKeystore above).
            signingConfig = signingConfigs.findByName("wiids") ?: signingConfigs.getByName("debug")
        }
        debug {
            isJniDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.documentfile:documentfile:1.0.1")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.recyclerview:recyclerview:1.3.2")
}
