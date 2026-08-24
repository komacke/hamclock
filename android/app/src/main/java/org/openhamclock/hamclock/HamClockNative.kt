package org.openhamclock.hamclock

object HamClockNative {
    init {
        System.loadLibrary("hamclock")
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
}
