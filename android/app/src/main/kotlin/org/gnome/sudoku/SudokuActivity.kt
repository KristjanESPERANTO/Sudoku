// SPDX-License-Identifier: GPL-3.0-or-later

package org.gnome.sudoku

import android.app.Activity
import android.content.res.Configuration
import android.os.Bundle
import android.system.Os
import android.util.Log
import android.view.MotionEvent
import android.view.ViewGroup
import java.io.File

class SudokuActivity : Activity() {

    companion object {
        private const val TAG = "SudokuActivity"
        @Volatile private var nativeLibsLoaded = false
    }

    private lateinit var boardOverlay: SudokuBoardOverlayView
    private var pythonBootstrapPending = false
    private lateinit var pythonRoot: File

    private fun ensureNativeLibrariesLoaded() {
        if (nativeLibsLoaded) {
            return
        }

        synchronized(SudokuActivity::class.java) {
            if (nativeLibsLoaded) {
                return
            }

            try {
                System.loadLibrary("z")
            } catch (t: Throwable) {
                Log.w(TAG, "zlib preload failed, continuing", t)
            }

            System.loadLibrary("sudoku_bootstrap")
            nativeLibsLoaded = true
            Log.i(TAG, "sudoku_bootstrap loaded")
        }
    }

    private fun prepareGnomeEnv() {
        try {
            val schemaDir = File(filesDir, "glib-2.0/schemas")
            schemaDir.mkdirs()
            val compiled = File(schemaDir, "gschemas.compiled")
            assets.open("glib-2.0/schemas/gschemas.compiled").use { input ->
                compiled.outputStream().use { output -> input.copyTo(output) }
            }
            Os.setenv("GSETTINGS_SCHEMA_DIR", schemaDir.absolutePath, true)
            Os.setenv("GDK_BACKEND", "android", true)
        } catch (e: Exception) {
            Log.w(TAG, "prepareGnomeEnv: could not extract gschemas.compiled: ${e.message}")
        }
    }

    private fun extractAssetTree(assetPath: String, outputDir: File) {
        val entries = assets.list(assetPath) ?: return
        if (entries.isEmpty()) {
            val outFile = outputDir
            outFile.parentFile?.mkdirs()
            assets.open(assetPath).use { input ->
                outFile.outputStream().use { output -> input.copyTo(output) }
            }
            return
        }

        outputDir.mkdirs()
        for (entry in entries) {
            val childAssetPath = "$assetPath/$entry"
            val childOut = File(outputDir, entry)
            extractAssetTree(childAssetPath, childOut)
        }
    }

    private fun preparePythonAssets(): File {
        val pythonRoot = File(filesDir, "python")
        val sudokuSrc = File(pythonRoot, "sudoku-src")
        val stdlibDir = File(pythonRoot, "stdlib")
        val libsDir = File(pythonRoot, "libs")
        if (!sudokuSrc.exists()) {
            extractAssetTree("python/sudoku-src", sudokuSrc)
        }
        if (!stdlibDir.exists()) {
            extractAssetTree("python/stdlib", stdlibDir)
        }
        if (!libsDir.exists()) {
            extractAssetTree("python/libs", libsDir)
        }
        return pythonRoot
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        prepareGnomeEnv()
        ensureNativeLibrariesLoaded()
        nativeOnCreate(this)
        pythonRoot = preparePythonAssets()
        pythonBootstrapPending = true
        super.onCreate(savedInstanceState)

        boardOverlay = SudokuBoardOverlayView(this).apply {
            isClickable = true
            isFocusable = true
            setOnTouchListener { v, event ->
                if (event.action == MotionEvent.ACTION_UP) {
                    nativeOnTap(event.x, event.y, v.width, v.height)
                    true
                } else {
                    true
                }
            }
        }
        addContentView(
            boardOverlay,
            ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )

        nativeRequestBoardSync()

        Log.i(TAG, "SudokuActivity.onCreate")
    }

    override fun onResume() {
        super.onResume()
        Log.i(TAG, "SudokuActivity.onResume")
        nativeOnResume()
        // Python bootstrap remains disabled in-process: both BeeWare 3.9 and
        // Termux 3.13 runtimes crash after Py_InitializeEx on API 36 (HWUI mutex abort).
        // if (pythonBootstrapPending) {
        //     pythonBootstrapPending = false
        //     Thread {
        //         val pyOk = nativeTryPythonBootstrap(pythonRoot.absolutePath)
        //         Log.i(TAG, "nativeTryPythonBootstrap result=$pyOk root=${pythonRoot.absolutePath}")
        //     }.start()
        // }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        Log.i(TAG, "SudokuActivity.onConfigurationChanged orientation=${newConfig.orientation}")
    }

    override fun onPause() {
        Log.i(TAG, "SudokuActivity.onPause")
        super.onPause()
        nativeOnPause()
    }

    override fun onDestroy() {
        Log.i(TAG, "SudokuActivity.onDestroy")
        super.onDestroy()
        nativeOnDestroy()
    }

    @Suppress("unused")
    fun onNativeBoardChanged(moveCount: Int, selected0: Int, selected1: Int, tileCount: Int) {
        Log.i(
            TAG,
            "onNativeBoardChanged moveCount=$moveCount selected0=$selected0 selected1=$selected1 tileCount=$tileCount"
        )

        runOnUiThread {
            if (::boardOverlay.isInitialized) {
                boardOverlay.updateBoardState(moveCount, selected0, selected1, tileCount)
            }
        }
    }

    private external fun nativeOnCreate(activity: Activity)
    private external fun nativeRequestBoardSync()
    private external fun nativeOnTap(x: Float, y: Float, width: Int, height: Int): Boolean
    private external fun nativeTryPythonBootstrap(pythonRootDir: String): Boolean
    private external fun nativeOnPause()
    private external fun nativeOnResume()
    private external fun nativeOnDestroy()
}
