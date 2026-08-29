package org.openhamclock.hamclock

import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.util.Log
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

object HamClockNative {
    init {
        System.loadLibrary("hamclock")
    }

    interface AppControlListener {
        fun onExitRequested()
        fun onRestartRequested()
        fun onOpenUrlRequested(url: String)
        fun getClipboardText(): String
    }

    @Volatile
    private var appControlListener: AppControlListener? = null

    private val httpExecutor = Executors.newCachedThreadPool()

    fun setAppControlListener(listener: AppControlListener?) {
        appControlListener = listener
    }

    @JvmStatic
    fun notifyExitRequested() {
        appControlListener?.onExitRequested()
    }

    @JvmStatic
    fun notifyRestartRequested() {
        appControlListener?.onRestartRequested()
    }

    @JvmStatic
    fun notifyOpenUrl(url: String) {
        appControlListener?.onOpenUrlRequested(url)
    }

    @JvmStatic
    fun getClipboardText(): String {
        var text = ""
        val latch = CountDownLatch(1)
        val handler = Handler(Looper.getMainLooper())
        handler.post {
            try {
                text = appControlListener?.getClipboardText() ?: ""
            } catch (e: Exception) {
                Log.w("HamClockNative", "Error reading clipboard: ${e.message}")
            } finally {
                latch.countDown()
            }
        }
        try {
            latch.await(1, TimeUnit.SECONDS)
        } catch (e: Exception) {
            Log.w("HamClockNative", "Timeout waiting for clipboard text")
        }
        return text
    }

    @JvmStatic
    fun fetchHttpsUrlToFd(urlString: String, userAgent: String?, header: String?, writeFdInt: Int) {
        val pfd = ParcelFileDescriptor.adoptFd(writeFdInt)
        httpExecutor.execute {
            try {
                ParcelFileDescriptor.AutoCloseOutputStream(pfd).use { outStream ->
                    val url = URL(urlString)
                    val conn = url.openConnection() as HttpURLConnection
                    conn.connectTimeout = 15000
                    conn.readTimeout = 15000
                    conn.instanceFollowRedirects = true
                    if (!userAgent.isNullOrEmpty()) {
                        conn.setRequestProperty("User-Agent", userAgent)
                    }
                    if (!header.isNullOrEmpty()) {
                        val colon = header.indexOf(':')
                        if (colon > 0) {
                            val name = header.substring(0, colon).trim()
                            val value = header.substring(colon + 1).trim()
                            conn.setRequestProperty(name, value)
                        }
                    }
                    conn.connect()
                    val code = conn.responseCode
                    val inStream = if (code in 200..299) conn.inputStream else conn.errorStream
                    val bytesCopied = inStream?.use { input ->
                        input.copyTo(outStream)
                    } ?: 0L
                    Log.i("HamClockHttp", "Successfully streamed $bytesCopied bytes (HTTP $code) for $urlString")
                    conn.disconnect()
                }
            } catch (e: Exception) {
                Log.w("HamClockHttp", "Error streaming HTTPS $urlString: ${e.message}")
            }
        }
    }

    external fun startDaemon(
        dataDir: String,
        rwPort: Int,
        roPort: Int,
        restPort: Int,
        backendHost: String = "",
        hasLocation: Boolean = false,
        lat: Double = 0.0,
        lng: Double = 0.0,
        forceSetup: Boolean = false
    ): Boolean

    external fun isDaemonRunning(): Boolean
    external fun setAllowExternalAccess(allow: Boolean)
}
