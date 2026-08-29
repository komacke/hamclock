package org.openhamclock.hamclock

import android.Manifest
import android.annotation.SuppressLint
import android.app.AlarmManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.location.Location
import android.location.LocationManager
import android.os.Build
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
import android.net.Uri
import android.os.Message
import android.webkit.ConsoleMessage
import android.webkit.JavascriptInterface
import android.webkit.WebChromeClient
import android.widget.ProgressBar
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.view.WindowCompat
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.ImageButton
import android.widget.LinearLayout
import android.net.wifi.WifiManager
import android.content.ClipData
import android.content.ClipboardManager
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.text.Editable
import android.text.TextWatcher
import android.widget.Toast
import java.io.File
import java.net.Socket
import java.util.concurrent.Executors

class MainActivity : AppCompatActivity() {

    private val TAG = "HamClockActivity"
    private val PREFS_NAME = "hamclock_prefs"
    private val PREF_BACKEND_HOST = "backend_host"
    private val PREF_FORCE_SETUP = "force_setup"
    private val PREF_RESTART_COUNTDOWN = "restart_countdown"
    private val PREF_START_ON_BOOT = "start_on_boot"
    private val PREF_ALLOW_EXTERNAL = "allow_external_access"
    private val PREF_MDNS_NAME = "mdns_name"

    private val REST_PORT = 8080
    private val RW_PORT = 8081
    private val RO_PORT = 8082

    private var actualRestPort = 8080
    private var restPortConflict = false

    private var wifiLock: WifiManager.WifiLock? = null
    private var nsdManager: NsdManager? = null
    private var nsdRegistrationListener: NsdManager.RegistrationListener? = null
    private var mdnsResponder: MdnsResponder? = null
    private var registeredMdnsName: String? = null

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

        HamClockNative.setAppControlListener(object : HamClockNative.AppControlListener {
            override fun onExitRequested() {
                mainHandler.post {
                    Log.i(TAG, "Exit requested by native HamClock engine")
                    finishAndRemoveTask()
                }
            }

            override fun onRestartRequested(minusK: Boolean) {
                mainHandler.post {
                    Log.i(TAG, "Restart requested by native HamClock engine (minusK=$minusK)")
                    if (!minusK) {
                        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                        prefs.edit().putBoolean(PREF_RESTART_COUNTDOWN, true).commit()
                    }
                    restartApp()
                }
            }

            override fun onOpenUrlRequested(url: String) {
                mainHandler.post {
                    openExternalUrl(url)
                }
            }

            override fun getClipboardText(): String {
                return try {
                    val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
                    val clip = clipboard?.primaryClip
                    if (clip != null && clip.itemCount > 0) {
                        clip.getItemAt(0).coerceToText(this@MainActivity)?.toString() ?: ""
                    } else ""
                } catch (e: Exception) {
                    Log.w(TAG, "Error accessing clipboard: ${e.message}")
                    ""
                }
            }
        })

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
        val currentStartOnBoot = prefs.getBoolean(PREF_START_ON_BOOT, false)
        val currentAllowExternal = prefs.getBoolean(PREF_ALLOW_EXTERNAL, false)
        val currentMdnsName = prefs.getString(PREF_MDNS_NAME, "") ?: ""

        val dialogView = layoutInflater.inflate(R.layout.dialog_backend_settings, null)
        val etBackendHost = dialogView.findViewById<EditText>(R.id.et_backend_host)
        val cbStartOnBoot = dialogView.findViewById<CheckBox>(R.id.cb_start_on_boot)
        val llAutostartHelper = dialogView.findViewById<LinearLayout>(R.id.ll_autostart_helper)
        val tvAutostartNotice = dialogView.findViewById<TextView>(R.id.tv_autostart_notice)
        val btnOpenAutostart = dialogView.findViewById<Button>(R.id.btn_open_autostart)

        val cbAllowExternal = dialogView.findViewById<CheckBox>(R.id.cb_allow_external)
        val llLocalAccessDetails = dialogView.findViewById<LinearLayout>(R.id.ll_local_access_details)
        val etMdnsName = dialogView.findViewById<EditText>(R.id.et_mdns_name)
        val tvLocalUrlValue = dialogView.findViewById<TextView>(R.id.tv_local_url_value)
        val btnCopyLocalUrl = dialogView.findViewById<Button>(R.id.btn_copy_local_url)

        etBackendHost.setText(currentHost)
        etBackendHost.setSelection(etBackendHost.text.length)
        cbStartOnBoot.isChecked = currentStartOnBoot

        cbAllowExternal.isChecked = currentAllowExternal
        etMdnsName.setText(currentMdnsName)

        fun formatMdnsUrl(name: String): String {
            val trimmed = name.trim()
            val host = if (trimmed.isNotEmpty()) trimmed else (registeredMdnsName ?: "hamclock")
            val ip = getDeviceIpAddress()
            val ipText = if (!ip.isNullOrEmpty()) "\nIP: http://$ip:$RW_PORT/live.html" else ""
            return "http://$host.local:$RW_PORT/live.html$ipText"
        }

        fun updateLocalAccessVisibility(isChecked: Boolean) {
            llLocalAccessDetails.visibility = if (isChecked) View.VISIBLE else View.GONE
            tvLocalUrlValue.text = formatMdnsUrl(etMdnsName.text.toString())
        }

        updateLocalAccessVisibility(currentAllowExternal)
        cbAllowExternal.setOnCheckedChangeListener { _, isChecked ->
            updateLocalAccessVisibility(isChecked)
        }

        etMdnsName.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                tvLocalUrlValue.text = formatMdnsUrl(s?.toString() ?: "")
            }
            override fun afterTextChanged(s: Editable?) {}
        })

        btnCopyLocalUrl.setOnClickListener {
            val url = tvLocalUrlValue.text.toString()
            val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
            val clip = ClipData.newPlainText("HamClock URL", url)
            clipboard?.setPrimaryClip(clip)
            Toast.makeText(this, getString(R.string.copied_to_clipboard), Toast.LENGTH_SHORT).show()
        }

        // Check if the current device OS has specific background or OEM auto-start requirements
        val specialSetting = AutoStartHelper.getSpecialSetting(this)
        fun updateHelperVisibility(isChecked: Boolean) {
            if (isChecked && specialSetting != null) {
                tvAutostartNotice.text = specialSetting.message
                btnOpenAutostart.setOnClickListener {
                    try {
                        startActivity(specialSetting.intent)
                    } catch (e: Exception) {
                        Log.w(TAG, "Failed to launch special setting intent: ${e.message}")
                    }
                }
                llAutostartHelper.visibility = View.VISIBLE
            } else {
                llAutostartHelper.visibility = View.GONE
            }
        }

        updateHelperVisibility(currentStartOnBoot)
        cbStartOnBoot.setOnCheckedChangeListener { _, isChecked ->
            updateHelperVisibility(isChecked)
        }

        val tvPortsInfo = dialogView.findViewById<TextView>(R.id.tv_ports_info)
        val tvRestPortConflict = dialogView.findViewById<TextView>(R.id.tv_rest_port_conflict)

        val restText = if (actualRestPort > 0) "Port $actualRestPort" else getString(R.string.rest_port_disabled)
        tvPortsInfo.text = getString(R.string.server_ports_format, RW_PORT, RO_PORT, restText)

        if (restPortConflict) {
            tvRestPortConflict.visibility = View.VISIBLE
            tvRestPortConflict.text = if (actualRestPort > 0) {
                getString(R.string.rest_port_conflict_warning, actualRestPort)
            } else {
                getString(R.string.rest_port_conflict_disabled)
            }
        } else {
            tvRestPortConflict.visibility = View.GONE
        }

        val dialog = AlertDialog.Builder(this, androidx.appcompat.R.style.Theme_AppCompat_Dialog_Alert)
            .setTitle(getString(R.string.settings_title))
            .setView(dialogView)
            .setPositiveButton(getString(R.string.save)) { _, _ ->
                val entered = etBackendHost.text.toString().trim()
                val newHost = if (entered.isNotEmpty()) entered else getString(R.string.backend_default)
                val newStartOnBoot = cbStartOnBoot.isChecked
                val newAllowExternal = cbAllowExternal.isChecked
                val newMdnsName = etMdnsName.text.toString().trim()

                Log.i(TAG, "Saving settings: backend=$newHost, startOnBoot=$newStartOnBoot, allowExternal=$newAllowExternal, mdnsName=$newMdnsName")
                val hostChanged = newHost != currentHost
                val externalChanged = (newAllowExternal != currentAllowExternal) || (newMdnsName != currentMdnsName)

                prefs.edit()
                    .putString(PREF_BACKEND_HOST, newHost)
                    .putBoolean(PREF_START_ON_BOOT, newStartOnBoot)
                    .putBoolean(PREF_ALLOW_EXTERNAL, newAllowExternal)
                    .putString(PREF_MDNS_NAME, newMdnsName)
                    .commit()

                if (externalChanged) {
                    HamClockNative.setAllowExternalAccess(newAllowExternal)
                    updateMdnsService(newAllowExternal, newMdnsName)
                    if (newAllowExternal) {
                        acquireWifiLock()
                    } else {
                        releaseWifiLock()
                    }
                }

                if (hostChanged) {
                    // Clear cached files while preserving the eeprom file (holds config), configurations, and .mac_address
                    val dataDir = File(filesDir, "hamclock_data")
                    clearHamClockCache(dataDir)

                    // Restart app process cleanly so native daemon restarts with new backend argument
                    restartApp()
                }
            }
            .setNegativeButton(getString(R.string.cancel), null)
            .create()

        val btnOpenSetup = dialogView.findViewById<Button>(R.id.btn_open_setup)
        btnOpenSetup.setOnClickListener {
            Log.i(TAG, "User requested HamClock Setup screen")
            prefs.edit().putBoolean(PREF_FORCE_SETUP, true).commit()
            dialog.dismiss()
            restartApp()
        }

        val btnExitApp = dialogView.findViewById<Button>(R.id.btn_exit_app)
        btnExitApp?.setOnClickListener {
            Log.i(TAG, "User requested Exit HamClock from settings dialog")
            dialog.dismiss()
            finishAndRemoveTask()
        }

        dialog.show()
        @Suppress("DEPRECATION")
        dialog.window?.setSoftInputMode(
            WindowManager.LayoutParams.SOFT_INPUT_STATE_HIDDEN or
            WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE
        )
    }

    private fun clearHamClockCache(dataDir: File) {
        if (!dataDir.exists() || !dataDir.isDirectory) return
        val files = dataDir.listFiles() ?: return
        for (file in files) {
            // Keep eeprom file which holds the config, saved configuration profiles, and persistent MAC address
            if (file.name == "eeprom" || file.name == ".mac_address" || file.name == "configurations") {
                continue
            }
            try {
                if (file.isDirectory) {
                    file.deleteRecursively()
                } else {
                    file.delete()
                }
                Log.i(TAG, "Cleared cache item: ${file.name}")
            } catch (e: Exception) {
                Log.w(TAG, "Failed to delete cache item ${file.name}: ${e.message}")
            }
        }
    }

    private fun restartApp() {
        RestartActivity.restart(this)
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
            WindowCompat.setDecorFitsSystemWindows(window, false)
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

    private fun openExternalUrl(url: String) {
        try {
            val uri = Uri.parse(url)
            val intent = Intent(Intent.ACTION_VIEW, uri).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            startActivity(intent)
            Log.i(TAG, "Opened external URL: $url")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to open URL $url: ${e.message}")
        }
    }

    @SuppressLint("SetJavaScriptEnabled")
    private fun setupWebView() {
        webView.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            useWideViewPort = true
            loadWithOverviewMode = true
            cacheMode = WebSettings.LOAD_NO_CACHE
            mediaPlaybackRequiresUserGesture = false
            setSupportZoom(false)
            javaScriptCanOpenWindowsAutomatically = true
            setSupportMultipleWindows(true)
        }

        webView.setLayerType(View.LAYER_TYPE_HARDWARE, null)
        webView.setBackgroundColor(0xFF000000.toInt())

        webView.addJavascriptInterface(object {
            @JavascriptInterface
            fun openUrl(url: String) {
                mainHandler.post {
                    openExternalUrl(url)
                }
            }

            @JavascriptInterface
            fun getClipboardText(): String {
                var text = ""
                val latch = java.util.concurrent.CountDownLatch(1)
                mainHandler.post {
                    try {
                        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
                        val clip = clipboard?.primaryClip
                        if (clip != null && clip.itemCount > 0) {
                            text = clip.getItemAt(0).coerceToText(this@MainActivity)?.toString() ?: ""
                        }
                    } catch (e: Exception) {
                        Log.w(TAG, "Error accessing clipboard from JS: ${e.message}")
                    } finally {
                        latch.countDown()
                    }
                }
                try {
                    latch.await(1, java.util.concurrent.TimeUnit.SECONDS)
                } catch (e: Exception) {
                    Log.w(TAG, "Timeout waiting for clipboard in JS interface")
                }
                return text
            }
        }, "AndroidApp")

        webView.webChromeClient = object : WebChromeClient() {
            override fun onCreateWindow(
                view: WebView?,
                isDialog: Boolean,
                isUserGesture: Boolean,
                resultMsg: Message?
            ): Boolean {
                val href = view?.handler?.obtainMessage()
                view?.requestFocusNodeHref(href)
                val url = href?.data?.getString("url")
                if (!url.isNullOrEmpty()) {
                    openExternalUrl(url)
                    return true
                }
                return false
            }

            override fun onConsoleMessage(consoleMessage: ConsoleMessage?): Boolean {
                Log.d("HamClockJS", "${consoleMessage?.message()} -- line ${consoleMessage?.lineNumber()}")
                return true
            }
        }

        webView.webViewClient = object : WebViewClient() {
            override fun onPageFinished(view: WebView?, url: String?) {
                super.onPageFinished(view, url)
                progressBar.visibility = View.GONE
                statusText.visibility = View.GONE
                webView.visibility = View.VISIBLE

                view?.evaluateJavascript(
                    """
                    (function() {
                        window.open = function(url, target, features) {
                            if (window.AndroidApp && window.AndroidApp.openUrl) {
                                window.AndroidApp.openUrl(url);
                                return { close: function() {}, closed: false, focus: function() {} };
                            }
                            return null;
                        };
                    })();
                    """.trimIndent(),
                    null
                )
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
                val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                val forceSetup = prefs.getBoolean(PREF_FORCE_SETUP, false)
                if (forceSetup) {
                    prefs.edit().putBoolean(PREF_FORCE_SETUP, false).apply()
                    Log.i(TAG, "Starting native backend with forceSetup=true (direct to Setup)")
                }

                val countdownSetup = prefs.getBoolean(PREF_RESTART_COUNTDOWN, false)
                if (countdownSetup) {
                    prefs.edit().putBoolean(PREF_RESTART_COUNTDOWN, false).apply()
                    Log.i(TAG, "Starting native backend with countdownSetup=true (10s countdown clock)")
                }

                val backendHost = getSelectedBackendHost()
                val location = getHostLocation()

                val hasLocation = location != null
                val lat = location?.first ?: 0.0
                val lng = location?.second ?: 0.0

                val allowExternal = prefs.getBoolean(PREF_ALLOW_EXTERNAL, false)
                HamClockNative.setAllowExternalAccess(allowExternal)
                val mdnsName = prefs.getString(PREF_MDNS_NAME, "")

                mainHandler.post {
                    updateMdnsService(allowExternal, mdnsName)
                    if (allowExternal) {
                        acquireWifiLock()
                    } else {
                        releaseWifiLock()
                    }
                }

                val (selectedRestPort, conflict) = findAvailableRestPort(REST_PORT)
                actualRestPort = selectedRestPort
                restPortConflict = conflict

                if (conflict) {
                    mainHandler.post {
                        val msg = if (actualRestPort > 0) {
                            getString(R.string.rest_port_conflict_toast, actualRestPort)
                        } else {
                            getString(R.string.rest_port_disabled_toast)
                        }
                        Toast.makeText(this@MainActivity, msg, Toast.LENGTH_LONG).show()
                    }
                }

                Log.i(TAG, "Launching native HamClock in ${dataDir.absolutePath} (hasLocation=$hasLocation, lat=$lat, lng=$lng, backend=$backendHost, forceSetup=$forceSetup, countdownSetup=$countdownSetup, allowExternal=$allowExternal, restPort=$actualRestPort, restConflict=$conflict)")
                HamClockNative.startDaemon(
                    dataDir = dataDir.absolutePath,
                    rwPort = RW_PORT,
                    roPort = RO_PORT,
                    restPort = actualRestPort,
                    backendHost = backendHost,
                    hasLocation = hasLocation,
                    lat = lat,
                    lng = lng,
                    forceSetup = forceSetup,
                    countdownSetup = countdownSetup
                )
            }


            // Wait for local HTTP/WebSocket port to become available
            waitForServerReady()
        }
    }

    private fun findAvailableRestPort(preferredPort: Int): Pair<Int, Boolean> {
        if (preferredPort > 0 && isPortAvailable(preferredPort)) {
            return Pair(preferredPort, false)
        }
        val fallbacks = listOf(8088, 8085, 8089, 8090)
        for (port in fallbacks) {
            if (isPortAvailable(port)) {
                Log.w(TAG, "Port $preferredPort was in use. Using fallback port $port")
                return Pair(port, true)
            }
        }
        Log.w(TAG, "Neither port $preferredPort nor fallback ports were available. Disabling REST server (-1).")
        return Pair(-1, true)
    }

    private fun isPortAvailable(port: Int): Boolean {
        return try {
            java.net.ServerSocket().apply {
                reuseAddress = true
                bind(java.net.InetSocketAddress(port))
                close()
            }
            true
        } catch (e: Exception) {
            false
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

    private fun getDeviceIpAddress(): String? {
        try {
            val interfaces = java.net.NetworkInterface.getNetworkInterfaces() ?: return null
            for (iface in interfaces) {
                if (iface.isLoopback || !iface.isUp) continue
                for (addr in iface.inetAddresses) {
                    if (!addr.isLoopbackAddress && addr is java.net.Inet4Address) {
                        return addr.hostAddress
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Error getting device IP: ${e.message}")
        }
        return null
    }

    private fun updateMdnsService(enable: Boolean, customName: String?) {
        unregisterMdnsService()
        if (!enable) return

        val rawName = customName?.trim()
        val serviceName = if (!rawName.isNullOrEmpty()) rawName else "hamclock"

        // 1. Start dedicated mDNS A-record responder for direct hostname queries (e.g. <name>.local)
        mdnsResponder = MdnsResponder(this).apply {
            start(serviceName)
        }

        // 2. Also register DNS-SD service via Android NsdManager
        val serviceInfo = NsdServiceInfo().apply {
            this.serviceName = serviceName
            this.serviceType = "_http._tcp"
            this.port = RW_PORT
        }

        val nsd = getSystemService(Context.NSD_SERVICE) as? NsdManager ?: return
        nsdManager = nsd

        val listener = object : NsdManager.RegistrationListener {
            override fun onServiceRegistered(registeredInfo: NsdServiceInfo) {
                registeredMdnsName = registeredInfo.serviceName
                Log.i(TAG, "mDNS DNS-SD service registered: ${registeredInfo.serviceName}._http._tcp on port $RW_PORT")
            }

            override fun onRegistrationFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                Log.w(TAG, "mDNS DNS-SD registration failed: errorCode=$errorCode")
            }

            override fun onServiceUnregistered(serviceInfo: NsdServiceInfo) {
                Log.i(TAG, "mDNS DNS-SD service unregistered")
                registeredMdnsName = null
            }

            override fun onUnregistrationFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                Log.w(TAG, "mDNS DNS-SD unregistration failed: errorCode=$errorCode")
            }
        }

        try {
            nsd.registerService(serviceInfo, NsdManager.PROTOCOL_DNS_SD, listener)
            nsdRegistrationListener = listener
        } catch (e: Exception) {
            Log.w(TAG, "Error registering mDNS service: ${e.message}")
        }
    }

    private fun unregisterMdnsService() {
        mdnsResponder?.stop()
        mdnsResponder = null

        nsdRegistrationListener?.let { listener ->
            try {
                nsdManager?.unregisterService(listener)
            } catch (e: Exception) {
                Log.w(TAG, "Error unregistering mDNS service: ${e.message}")
            }
        }
        nsdRegistrationListener = null
        registeredMdnsName = null
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        hideSystemUI()
        webView.postDelayed({
            hideSystemUI()
            webView.evaluateJavascript(
                "(function() { if (typeof runSoon === 'function' && typeof getFullImage === 'function') { runSoon(getFullImage); } else { window.dispatchEvent(new Event('resize')); } })();",
                null
            )
        }, 300)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemUI()
        }
    }

    private fun acquireWifiLock() {
        if (wifiLock == null) {
            val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
            @Suppress("DEPRECATION")
            wifiLock = wifiManager?.createWifiLock(
                WifiManager.WIFI_MODE_FULL_HIGH_PERF,
                "HamClock:WifiLock"
            )?.apply {
                setReferenceCounted(false)
                acquire()
                Log.i(TAG, "Acquired high-performance WifiLock")
            }
        }
    }

    private fun releaseWifiLock() {
        wifiLock?.let {
            if (it.isHeld) {
                it.release()
                Log.i(TAG, "Released WifiLock")
            }
        }
        wifiLock = null
    }

    override fun onDestroy() {
        super.onDestroy()
        HamClockNative.setAppControlListener(null)
        unregisterMdnsService()
        releaseWifiLock()
        executor.shutdown()
        android.os.Process.killProcess(android.os.Process.myPid())
    }
}
