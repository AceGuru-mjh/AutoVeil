import com.google.protobuf.gradle.id
import java.io.File
import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.compose.compiler)
    alias(libs.plugins.protobuf)
}

android {
    namespace = "com.nexus.manager"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.nexus.manager"
        minSdk = 34
        targetSdk = 35
        versionCode = 1
        versionName = "1.0.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        vectorDrawables { useSupportLibrary = true }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            // 整改 B18：原 release 用 debug key 签名，daemon 端的签名校验失效，
            // 且 release APK 与 debug APK 签名相同导致安装冲突。
            // 改为：通过 local.properties 读取 release keystore 路径与密码（不入库）；
            //       未配置时回退到 debug key 并打 warning，方便 CI 与本地未配置开发者继续构建。
            signingConfig = run {
                val props = Properties()
                val localProps = rootProject.file("local.properties")
                if (localProps.exists()) {
                    localProps.inputStream().use { props.load(it) }
                }
                val storeFile = props.getProperty("release.storeFile")?.let(::File)
                val storePass = props.getProperty("release.storePassword")
                val keyAlias = props.getProperty("release.keyAlias")
                val keyPass = props.getProperty("release.keyPassword")
                if (storeFile != null && storeFile.exists() && storePass != null &&
                    keyAlias != null && keyPass != null) {
                    signingConfigs.create("release") {
                        this.storeFile = storeFile
                        this.storePassword = storePass
                        this.keyAlias = keyAlias
                        this.keyPassword = keyPass
                    }
                } else {
                    println("WARNING: release keystore not configured in local.properties; " +
                        "falling back to debug key. Configure: release.storeFile / release.storePassword / " +
                        "release.keyAlias / release.keyPassword")
                    signingConfigs.getByName("debug")
                }
            }
        }
        debug {
            isMinifyEnabled = false
            applicationIdSuffix = ".debug"
            versionNameSuffix = "-debug"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    packaging {
        resources {
            excludes += setOf(
                "/META-INF/{AL2.0,LGPL2.1}",
                "META-INF/DEPENDENCIES",
                "META-INF/*.kotlin_module",
                "META-INF/INDEX.LIST"
            )
        }
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.androidx.datastore.preferences)
    implementation(libs.androidx.work.runtime.ktx)
    implementation(libs.androidx.biometric)

    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.ui.graphics)
    implementation(libs.compose.ui.tooling.preview)
    implementation(libs.compose.material3)
    implementation(libs.compose.material.icons.extended)
    implementation(libs.compose.animation)
    debugImplementation(libs.compose.ui.tooling)

    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.protobuf.javalite)

    // ============ 单元测试 ============
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.9.0")
    testImplementation("com.google.truth:truth:1.4.4")
    testImplementation("org.mockito:mockito-core:5.14.2")
    testImplementation("org.mockito.kotlin:mockito-kotlin:5.4.0")
    testImplementation("androidx.arch.core:core-testing:2.2.0")

    // Android 仪器测试
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.6.1")
    androidTestImplementation(platform(libs.compose.bom))
    androidTestImplementation("androidx.compose.ui:ui-test-junit4")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}

protobuf {
    protoc {
        artifact = "com.google.protobuf:protoc:${libs.versions.protobuf.get()}"
    }
    generateProtoTasks {
        all().forEach { task ->
            task.builtins {
                id("java") {
                    option("lite")
                }
            }
        }
    }
}
