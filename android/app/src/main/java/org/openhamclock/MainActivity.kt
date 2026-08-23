package org.openhamclock

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.ProgressBar
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import android.widget.EditText
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import org.openhamclock.HamClockNative
import java.io.File
import java.net.Socket
import java.util.concurrent.Executors

class MainActivity : AppCompatActivity() {

    private val TAG = "HamClockActivity"
    private val PREFS_NAME = "hamclock_prefs"
    private val PREF_BACKEND_HOST = "backend_host"
    private val PREF_CUSTOM_HOST = "custom_backend_host"

    private val RW_PORT = 8080
    private val RO_PORT = 8081
    private val REST_PORT = 8082

    private lateinit var webView: WebView
    private lateinit var progressBar: ProgressBar
    private lateinit var statusText: TextView
    private lateinit var btnSettings: ImageButton

    private val executor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    private val locationPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        startNativeBackend()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        hideSystemUI()

        webView = findViewById(R.id.hamclock_webview)
        progressBar = findViewById(R.id.progress_bar)
        statusText = findViewById(R.id.status_text)
        btnSettings = findViewById(R.id.btn_settings)

        btnSettings.setOnClickListener {
            showBackendSettingsDialog()
        }

        setupWebView()

        // Check or request location permissions to obtain host coordinates
        if (hasLocationPermission()) {
            startNativeBackend()
        } else {
            locationPermissionLauncher.launch(
                arrayOf(
                    Manifest.permission.ACCESS_FINE_LOCATION,
                    Manifest.permission.ACCESS_COARSE_LOCATION
                )
            )
        }
    }

    private fun getSelectedBackendHost(): String {

        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return prefs.getString(PREF_BACKEND_HOST, getString(R.string.backend_default))
            ?: getString(R.string.backend_default)
    }

    private fun showBackendSettingsDialog() {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val currentHost = getSelectedBackendHost()
        val customHostSaved = prefs.getString(PREF_CUSTOM_HOST, "") ?: ""

        val ohbHost = getString(R.string.backend_ohb)
        val hcHost = getString(R.string.backend_hamclock_com)

        val dialogView = layoutInflater.inflate(R.layout.dialog_backend_settings, null)
        val rg = dialogView.findViewById<RadioGroup>(R.id.rg_backends)
        val rbOhb = dialogView.findViewById<RadioButton>(R.id.rb_ohb)
        val rbHc = dialogView.findViewById<RadioButton>(R.id.rb_hamclock)
        val rbCustom = dialogView.findViewById<RadioButton>(R.id.rb_custom)
        val etCustom = dialogView.findViewById<EditText>(R.id.et_custom_backend)

        when (currentHost) {
            ohbHost -> rbOhb.isChecked = true
            hcHost -> rbHc.isChecked = true
            else -> {
                rbCustom.isChecked = true
                etCustom.setText(currentHost)
                etCustom.visibility = View.VISIBLE
            }
        }

        rg.setOnCheckedChangeListener { _, checkedId ->
            if (checkedId == R.id.rb_custom) {
                etCustom.visibility = View.VISIBLE
                if (etCustom.text.isEmpty() && customHostSaved.isNotEmpty()) {
                    etCustom.setText(customHostSaved)
                }
                etCustom.requestFocus()
            } else {
                etCustom.visibility = View.GONE
            }
        }

        AlertDialog.Builder(this, androidx.appcompat.R.style.Theme_AppCompat_Dialog_Alert)
            .setTitle(getString(R.string.settings_title))
            .setView(dialogView)
            .setPositiveButton(getString(R.string.save_and_restart)) { _, _ ->
                val newHost = when {
                    rbOhb.isChecked -> ohbHost
                    rbHc.isChecked -> hcHost
                    else -> {
                        val entered = etCustom.text.toString().trim()
                        if (entered.isNotEmpty()) entered else ohbHost
                    }
                }

                prefs.edit().apply {
                    putString(PREF_BACKEND_HOST, newHost)
                    if (rbCustom.isChecked) {
                        putString(PREF_CUSTOM_HOST, newHost)
                    }
                    apply()
                }

                // Restart activity to apply updated daemon arguments
                finish()
                startActivity(intent)
            }
            .setNegativeButton(getString(R.string.cancel), null)
            .show()
    }



    private fun hasLocationPermission(): Boolean {
        return ContextCompat.checkSelfPermission(
            this, Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED || ContextCompat.checkSelfPermission(
            this, Manifest.permission.ACCESS_COARSE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
    }

    private fun getHostLocation(): Pair<Double, Double>? {
        if (!hasLocationPermission()) return null

        val lm = getSystemService(Context.LOCATION_SERVICE) as? LocationManager ?: return null
        var bestLocation: Location? = null

        for (provider in lm.getProviders(true)) {
            try {
                val loc = lm.getLastKnownLocation(provider) ?: continue
                if (bestLocation == null || loc.accuracy < bestLocation.accuracy) {
                    bestLocation = loc
                }
            } catch (e: SecurityException) {
                Log.w(TAG, "Location permission error: ${e.message}")
            }
        }

        return bestLocation?.let { Pair(it.latitude, it.longitude) }
    }


    private fun hideSystemUI() {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let { controller ->
                controller.hide(WindowInsets.Type.statusBars() or WindowInsets.Type.navigationBars())
                controller.systemBarsBehavior =
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                            or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            or View.SYSTEM_UI_FLAG_FULLSCREEN
                    )
        }
    }

    @SuppressLint("SetJavaScriptEnabled")
    private fun setupWebView() {
        webView.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            databaseEnabled = true
            useWideViewPort = true
            loadWithOverviewMode = true
            cacheMode = WebSettings.LOAD_NO_CACHE
            mediaPlaybackRequiresUserGesture = false
            setSupportZoom(false)
        }

        webView.setLayerType(View.LAYER_TYPE_HARDWARE, null)
        webView.setBackgroundColor(0xFF000000.toInt())

        webView.webViewClient = object : WebViewClient() {
            override fun onPageFinished(view: WebView?, url: String?) {
                super.onPageFinished(view, url)
                progressBar.visibility = View.GONE
                statusText.visibility = View.GONE
                webView.visibility = View.VISIBLE
            }

            override fun onReceivedError(
                view: WebView?,
                request: WebResourceRequest?,
                error: WebResourceError?
            ) {
                super.onReceivedError(view, request, error)
                Log.w(TAG, "WebView error: ${error?.description}")
            }
        }
    }

    private fun startNativeBackend() {
        val dataDir = File(filesDir, "hamclock_data")
        if (!dataDir.exists()) {
            dataDir.mkdirs()
        }

        statusText.text = getString(R.string.starting_engine)

        executor.execute {
            if (!HamClockNative.isDaemonRunning()) {
                val backendHost = getSelectedBackendHost()
                val location = getHostLocation()

                val hasLocation = location != null
                val lat = location?.first ?: 0.0
                val lng = location?.second ?: 0.0

                Log.i(TAG, "Launching native HamClock in ${dataDir.absolutePath} (hasLocation=$hasLocation, lat=$lat, lng=$lng, backend=$backendHost)")
                HamClockNative.startDaemon(
                    dataDir = dataDir.absolutePath,
                    rwPort = RW_PORT,
                    roPort = RO_PORT,
                    restPort = REST_PORT,
                    backendHost = backendHost,
                    hasLocation = hasLocation,
                    lat = lat,
                    lng = lng
                )
            }


            // Wait for local HTTP/WebSocket port to become available
            waitForServerReady()
        }
    }


    private fun waitForServerReady() {
        var attempts = 0
        val maxAttempts = 60
        var isReady = false

        while (attempts < maxAttempts && !isReady) {
            attempts++
            try {
                Socket("127.0.0.1", RW_PORT).use { socket ->
                    if (socket.isConnected) {
                        isReady = true
                        Log.i(TAG, "HamClock server ready on port $RW_PORT after $attempts attempts")
                    }
                }
            } catch (e: Exception) {
                Thread.sleep(250)
            }
        }

        mainHandler.post {
            if (isReady) {
                statusText.text = getString(R.string.loading_interface)
                val targetUrl = "http://127.0.0.1:$RW_PORT/live.html"
                Log.i(TAG, "Loading URL: $targetUrl")
                webView.loadUrl(targetUrl)
            } else {
                statusText.text = getString(R.string.start_failed)
                progressBar.visibility = View.GONE
            }
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemUI()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        executor.shutdown()
    }
}
