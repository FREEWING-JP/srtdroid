plugins {
    id(libs.plugins.android.library.get().pluginId)
    alias(libs.plugins.kotlin.android)
}

description = "Secure Reliable Transport (SRT) Protocol for Android"

configurePublication()

android {
    namespace = "io.github.thibaultbee.srtdroid.core"
    compileSdk = AndroidVersions.COMPILE_SDK
    ndkVersion = "29.0.14206865"

    defaultConfig {
        minSdk = AndroidVersions.MIN_SDK

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        consumerProguardFiles("consumer-rules.pro")

    externalNativeBuild {
        cmake {
            // 不要なコンパイルをスキップするフラグを注入
            // 既存の設定（例: -DANDROID_STL=c++_shared など）の後ろに追加
            arguments.addAll(listOf(
                "-DENABLE_APPS=OFF",          // 1. srt-live-transmit などのPC用アプリをビルドしない（超重要）
                "-DENABLE_TESTING=OFF",       // 2. テスト用プログラムのビルドをすべてスキップ
                "-DENABLE_EXAMPLES=OFF",      // 3. SRT公式のC++サンプルプログラムをスキップ
                "-DENABLE_CODE_COVERAGE=OFF", // 4. コードカバレッジ計測用の無駄なバイナリ埋め込みを排除
                "-DENABLE_STDCXX_SYNC=ON",    // 5. Android環境に最適なC++11標準同期の有効化
                "-DCMAKE_BUILD_TYPE=Release"  // 6. デバッグ情報の削除とコンパイラ最適化(-O3)の強制
            ))
        }
    }

        ndk {
            abiFilters.add("arm64-v8a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )

            ndk {
                debugSymbolLevel = 'NONE'
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = File("src/main/cpp/CMakeLists.txt")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    publishing {
        singleVariant("release") {
            withJavadocJar()
            withSourcesJar()
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_1_8)
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)

    testImplementation(libs.junit)

    androidTestImplementation(libs.junit)
    androidTestImplementation(libs.androidx.runner)
    androidTestImplementation(libs.androidx.rules)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(libs.guava)
}
