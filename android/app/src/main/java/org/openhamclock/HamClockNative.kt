package org.openhamclock

object HamClockNative {
    init {
        System.loadLibrary("hamclock")
    }

    external fun startDaemon(
        dataDir: String,
        rwPort: Int,
        roPort: Int,
        restPort: Int,
        backendHost: String = ""
    ): Boolean


    external fun isDaemonRunning(): Boolean
}
