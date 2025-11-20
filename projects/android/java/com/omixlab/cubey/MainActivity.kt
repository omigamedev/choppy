package com.omixlab.cubey

import android.Manifest
import android.view.View
import androidx.activity.result.contract.ActivityResultContracts
import com.google.androidgamesdk.GameActivity

class MainActivity : GameActivity() {
    //companion object {
    //    init {
    //        System.loadLibrary("ce_android")
    //    }
    //}

    external fun onRequestPermissionsResultNative(
        record_audio: Boolean,
    )

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted)
            {
                onRequestPermissionsResultNative(true);
            }
            else
            {
                onRequestPermissionsResultNative(false);
            }
        }

    fun requestPermissions()
    {
        permissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemUi()
        }
    }

    private fun hideSystemUi() {
        val decorView = window.decorView
        decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN)
    }
}
