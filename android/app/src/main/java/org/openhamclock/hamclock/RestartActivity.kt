package org.openhamclock.hamclock

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.Process

class RestartActivity : Activity() {

    companion object {
        private const val EXTRA_MAIN_PID = "extra_main_pid"

        fun restart(context: Context) {
            val intent = Intent(context, RestartActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                putExtra(EXTRA_MAIN_PID, Process.myPid())
            }
            context.startActivity(intent)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // 1. Relaunch the main activity while this activity is in foreground
        val launchIntent = packageManager.getLaunchIntentForPackage(packageName)?.apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
        }
        if (launchIntent != null) {
            startActivity(launchIntent)
        }

        // 2. Kill previous main process to wipe native memory, threads, and daemon state
        val mainPid = intent.getIntExtra(EXTRA_MAIN_PID, -1)
        if (mainPid > 0) {
            Process.killProcess(mainPid)
        }

        // 3. Finish this activity cleanly
        finish()
    }

    override fun onDestroy() {
        super.onDestroy()
        // Terminate the restart helper process once lifecycle transition completes
        Runtime.getRuntime().exit(0)
    }
}
