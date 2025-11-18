plugins {
	id("com.android.application") version "8.13.1"
    kotlin("android") version "2.1.10"
}

val ndkHome = System.getenv("ANDROID_NDK_HOME") as String
val vcpkgHome = System.getenv("VCPKG_ROOT") as String
val versionCodeProp: String? = project.findProperty("versionCode") as String?

android {
    namespace = "com.omixlab.cubey"
    compileSdk = 34
    defaultConfig {
        applicationId = "com.omixlab.cubey"
        minSdk = 32
        targetSdk = 34
        versionCode = versionCodeProp?.toIntOrNull() ?: 1
        versionName = "0.1"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_shared" // ?? is this needed, AI generated
                // requires env ANDROID_NDK_HOME
                // NOTE: if it fails read the file logs, it contains more info that console
                arguments += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgHome/scripts/buildsystems/vcpkg.cmake"
                arguments += "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$ndkHome/build/cmake/android.toolchain.cmake"
                arguments += "-DVCPKG_TARGET_TRIPLET=arm64-android"
                targets += "main"
            }
        }
    }
    signingConfigs {
        create("release") {
            storeFile = file(property("CUBEY_STORE_FILE") as String)
            storePassword = property("CUBEY_STORE_PASSWORD") as String
            keyAlias = property("CUBEY_KEY_ALIAS") as String
            keyPassword = property("CUBEY_KEY_PASSWORD") as String
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName("release")
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
            version = "4.1.2"
        }
    }
    ndkVersion = "29.0.14206865"
}

dependencies {
    implementation("androidx.appcompat:appcompat:1.7.1")
    implementation("androidx.games:games-activity:4.0.0")
    implementation("androidx.games:games-controller:2.0.2")
    //implementation("org.khronos.openxr:openxr_loader_for_android:1.1.47")
}

tasks.register<Exec>("deployAlpha") {
    group = "deployment"
    description = "Uploads release APK to Meta Quest"

    dependsOn("assembleRelease")

    doFirst {
        val apkPath = project.layout.buildDirectory
            .file("outputs/apk/release/android-release.apk")
            .get()
            .asFile
            .absolutePath

        val cli = project.findProperty("OVR_PLATFORM_UTIL") as? String
            ?: throw GradleException("OVR_PLATFORM_UTIL not defined")
        val appId = project.findProperty("CUBEY_APP_ID") as? String
            ?: throw GradleException("CUBEY_APP_ID not defined")
        val appSecret = project.findProperty("CUBEY_APP_SECRET") as? String
            ?: throw GradleException("CUBEY_APP_SECRET not defined")

        commandLine(
            cli,
            "upload-quest-build",
            "--app-id", appId,
            "--app-secret", appSecret,
            "--apk", apkPath,
            "--channel", "ALPHA"
        )
    }
}
