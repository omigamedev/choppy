plugins {
	id("com.android.application") version "8.12.1"
    kotlin("android") version "2.1.10"
}

android {
    namespace = "com.omixlab.choppyengine"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.omixlab.choppyengine"
        minSdk = 32
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName("debug")
        }
    }
    buildFeatures {
        prefab = true
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    kotlinOptions {
        jvmTarget = "1.8"
    }
    sourceSets["main"].manifest.srcFile("AndroidManifest.xml")
    sourceSets["main"].kotlin.srcDir("java")
    sourceSets["main"].assets.srcDirs(listOf("assets", "assets_gen"))
    externalNativeBuild {
        cmake {
            path = file("CMakeLists.txt")
            version = "4.0.2"
        }
    }
    ndkVersion = "29.0.13599879"
}

dependencies {
    implementation("androidx.appcompat:appcompat:1.7.1")
    implementation("androidx.games:games-activity:4.0.0")
    implementation("androidx.games:games-controller:2.0.2")
    //implementation("org.khronos.openxr:openxr_loader_for_android:1.1.47")
}