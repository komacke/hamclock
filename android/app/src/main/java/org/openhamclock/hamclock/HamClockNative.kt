package org.openhamclock.hamclock

object HamClockNative {
    init {
        System.loadLibrary("hamclock")
    }

    interface AppControlListener {
        fun onExitRequested()
        fun onRestartRequested()
        fun onOpenUrlRequested(url: String)
    }

    @Volatile
    private var appControlListener: AppControlListener? = null

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
