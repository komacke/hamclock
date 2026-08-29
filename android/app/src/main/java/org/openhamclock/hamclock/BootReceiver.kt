package org.openhamclock.hamclock

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

class BootReceiver : BroadcastReceiver() {

    private val TAG = "HamClockBootReceiver"
    private val PREFS_NAME = "hamclock_prefs"
    private val PREF_START_ON_BOOT = "start_on_boot"

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action
        Log.i(TAG, "Received broadcast intent action: $action")

        if (Intent.ACTION_BOOT_COMPLETED == action || "android.intent.action.QUICKBOOT_POWERON" == action) {
            val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            val startOnBoot = prefs.getBoolean(PREF_START_ON_BOOT, false)

            if (startOnBoot) {
                Log.i(TAG, "Start on boot is enabled, launching HamClock...")
                val launchIntent = context.packageManager.getLaunchIntentForPackage(context.packageName)?.apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
                } ?: Intent(context, MainActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
                }
                context.startActivity(launchIntent)
            } else {
                Log.i(TAG, "Start on boot is disabled; skipping launch.")
            }
        }
    }
}
