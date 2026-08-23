package org.openhamclock

import android.annotation.SuppressLint
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
import androidx.appcompat.app.AppCompatActivity
import org.openhamclock.HamClockNative
import java.io.File
import java.net.Socket
import java.util.concurrent.Executors

class MainActivity : AppCompatActivity() {

    private val TAG = "HamClockActivity"
    private val RW_PORT = 8080
    private val RO_PORT = 8081
    private val REST_PORT = 8082

    private lateinit var webView: WebView
    private lateinit var progressBar: ProgressBar
    private lateinit var statusText: TextView

    private val executor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        hideSystemUI()

        webView = findViewById(R.id.hamclock_webview)
        progressBar = findViewById(R.id.progress_bar)
        statusText = findViewById(R.id.status_text)

        setupWebView()
        startNativeBackend()
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
                val backendHost = getString(R.string.backend_host)
                Log.i(TAG, "Launching native HamClock in ${dataDir.absolutePath} with backend $backendHost")
                HamClockNative.startDaemon(
                    dataDir = dataDir.absolutePath,
                    rwPort = RW_PORT,
                    roPort = RO_PORT,
                    restPort = REST_PORT,
                    backendHost = backendHost
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
