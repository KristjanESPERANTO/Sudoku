// SPDX-License-Identifier: GPL-3.0-or-later

import org.gradle.api.file.DuplicatesStrategy
import java.io.File

plugins {
    id("com.android.application")
}

val sudokuRepoRoot = rootDir.parentFile.absolutePath
val defaultBaselineRoot = run {
    val root = File(sudokuRepoRoot)
    val candidates = listOf(
        File(root, ".a0-baseline"),
        File(root.parentFile, ".a0-baseline"),
        root.parentFile?.parentFile?.let { File(it, ".a0-baseline") }
    ).filterNotNull()
    val existing = candidates.firstOrNull { File(it, "install/lib").exists() }
    (existing ?: candidates.first()).absolutePath
}
val a0BaselineRoot = providers.gradleProperty("a0BaselineRoot").orNull
    ?: System.getenv("A0_BASELINE_ROOT")
    ?: defaultBaselineRoot
val a0BaselinePrefix = providers.gradleProperty("a0BaselinePrefix").orNull
    ?: System.getenv("A0_BASELINE_PREFIX")
    ?: "$a0BaselineRoot/install"
val a0C1OverlayPrefix = providers.gradleProperty("a0C1OverlayPrefix").orNull
    ?: System.getenv("A0_C1_OVERLAY_PREFIX")
    ?: "$a0BaselineRoot/install-c1-overlay"
val sudokuEnableGtk4 = providers.gradleProperty("sudokuEnableGtk4").orNull?.toBoolean()
    ?: true
val sudokuBundleGtkRuntime = providers.gradleProperty("sudokuBundleGtkRuntime").orNull?.toBoolean()
    ?: sudokuEnableGtk4
val sudokuRuntimeLibsOutDir = layout.buildDirectory.dir("generated/sudoku-jniLibs").get().asFile
val sudokuAssetsOutDir = layout.buildDirectory.dir("generated/sudoku-assets").get().asFile

val compileSudokuGschemas by tasks.registering(Exec::class) {
    val schemaDir = "$a0C1OverlayPrefix/share/glib-2.0/schemas"
    val outputFile = file("$schemaDir/gschemas.compiled")
    inputs.dir(schemaDir)
    outputs.file(outputFile)
    commandLine("glib-compile-schemas", schemaDir)
}

val syncSudokuAssets by tasks.registering(Sync::class) {
    dependsOn(compileSudokuGschemas)
    from("$a0C1OverlayPrefix/share/glib-2.0/schemas") {
        include("gschemas.compiled")
        into("glib-2.0/schemas")
    }
    from("$sudokuRepoRoot/src") {
        include("**/*.py")
        into("python/sudoku-src")
    }
    from("$a0C1OverlayPrefix/share/python-stdlib") {
        include("*.zip")
        into("python/stdlib")
    }
    from("$a0C1OverlayPrefix/lib") {
        include("libpython*.so*")
        include("librubicon.so*")
        include("libffi.so*")
        include("libsqlite3.so*")
        include("libbz2.so*")
        include("liblzma.so*")
        include("libssl*.so*")
        include("libcrypto*.so*")
        into("python/libs")
    }
    into(sudokuAssetsOutDir)
}

val syncSudokuNativeLibs by tasks.registering(Sync::class) {
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    if (sudokuBundleGtkRuntime) {
        from(fileTree("$a0BaselinePrefix/lib") {
            include("*.so*")
            exclude("libz.so*")
            exclude("liblog.so*")
            exclude("libandroid.so*")
            exclude("libEGL.so*")
            exclude("libGLESv2.so*")
            exclude("libGLESv3.so*")
            exclude("libm.so*")
            exclude("libc.so*")
            exclude("libdl.so*")
        })
        from(fileTree("$a0C1OverlayPrefix/lib") {
            include("*.so*")
            exclude("libz.so*")
            exclude("liblog.so*")
            exclude("libandroid.so*")
            exclude("libEGL.so*")
            exclude("libGLESv2.so*")
            exclude("libGLESv3.so*")
            exclude("libm.so*")
            exclude("libc.so*")
            exclude("libdl.so*")
            exclude("libpython*.so*")
            exclude("librubicon.so*")
            exclude("libsqlite3.so*")
            exclude("libbz2.so*")
            exclude("liblzma.so*")
            exclude("libssl*.so*")
            exclude("libcrypto*.so*")
        })
        from("${rootDir.absolutePath}/tools/libappstream-stub") {
            include("libappstream.so")
        }
    }
    into(file("$sudokuRuntimeLibsOutDir/x86_64"))

    doLast {
        if (sudokuBundleGtkRuntime) {
            val outDir = file("$sudokuRuntimeLibsOutDir/x86_64")
            val elfLibs = outDir.listFiles { f ->
                f.isFile && f.name.endsWith(".so")
            }?.toList() ?: emptyList()
            val availableNames = elfLibs.map { it.name }.toSet()
            val rewriteNeededMap = mapOf(
                "libz.so.1" to "libz.so",
                "libappstream.so.4" to "libappstream.so"
            )

            fun rewriteNeededName(file: java.io.File, from: String, to: String): Boolean {
                if (to.length > from.length) {
                    return false
                }
                val bytes = file.readBytes()
                val fromBytes = (from + "\u0000").toByteArray(Charsets.ISO_8859_1)
                val toBytes = ByteArray(fromBytes.size)
                val plainTo = to.toByteArray(Charsets.ISO_8859_1)
                System.arraycopy(plainTo, 0, toBytes, 0, plainTo.size)

                var replaced = false
                var i = 0
                while (i <= bytes.size - fromBytes.size) {
                    var match = true
                    var j = 0
                    while (j < fromBytes.size) {
                        if (bytes[i + j] != fromBytes[j]) {
                            match = false
                            break
                        }
                        j++
                    }
                    if (match) {
                        System.arraycopy(toBytes, 0, bytes, i, toBytes.size)
                        replaced = true
                        i += fromBytes.size
                    } else {
                        i++
                    }
                }

                if (replaced) {
                    file.writeBytes(bytes)
                }
                return replaced
            }

            val alwaysRewriteForSystem = setOf("libz.so")

            elfLibs.forEach { lib ->
                rewriteNeededMap.forEach { (versionedName, unversionedName) ->
                    if (availableNames.contains(unversionedName) || alwaysRewriteForSystem.contains(unversionedName)) {
                        rewriteNeededName(lib, versionedName, unversionedName)
                    }
                }
            }
        }
    }
}

android {
    namespace = "org.gnome.sudoku"
    compileSdk = 36

    ndkVersion = libs.versions.ndkVersion.get()

    defaultConfig {
        applicationId = "org.gnome.sudoku"
        minSdk = 21
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters.add("x86_64")
        }

        externalNativeBuild {
            cmake {
                arguments(
                    "-DANDROID_STL=c++_shared",
                    "-DA0_BASELINE_ROOT=$a0BaselineRoot",
                    "-DA0_BASELINE_PREFIX=$a0BaselinePrefix",
                    "-DA0_C1_OVERLAY_PREFIX=$a0C1OverlayPrefix",
                    "-DSUDOKU_ENABLE_GTK4=${if (sudokuEnableGtk4) "ON" else "OFF"}"
                )
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/jni/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets {
        getByName("main") {
            jniLibs.directories.clear()
            jniLibs.directories.add(sudokuRuntimeLibsOutDir.path)
            assets.directories.add(sudokuAssetsOutDir.path)
        }
    }

    buildFeatures {
        aidl = false
        viewBinding = false
    }
}

dependencies {
    implementation("androidx.appcompat:appcompat:1.6.1")
}

tasks.matching { it.name == "mergeDebugNativeLibs" || it.name == "mergeReleaseNativeLibs" }
    .configureEach {
        dependsOn(syncSudokuNativeLibs)
    }

tasks.matching { it.name == "mergeDebugJniLibFolders" || it.name == "mergeReleaseJniLibFolders" }
    .configureEach {
        dependsOn(syncSudokuNativeLibs)
    }

tasks.matching { it.name.startsWith("merge") && it.name.endsWith("Assets") }
    .configureEach {
        dependsOn(syncSudokuAssets)
    }
