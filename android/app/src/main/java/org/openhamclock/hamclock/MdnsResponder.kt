package org.openhamclock.hamclock

import android.content.Context
import android.net.wifi.WifiManager
import android.util.Log
import java.io.ByteArrayOutputStream
import java.io.DataOutputStream
import java.net.DatagramPacket
import java.net.InetAddress
import java.net.MulticastSocket
import java.net.NetworkInterface
import java.util.concurrent.atomic.AtomicBoolean

class MdnsResponder(private val context: Context) {

    private val TAG = "MdnsResponder"
    private val MDNS_GROUP = "224.0.0.251"
    private val MDNS_PORT = 5353

    private var multicastSocket: MulticastSocket? = null
    private var multicastLock: WifiManager.MulticastLock? = null
    private var workerThread: Thread? = null
    private val isRunning = AtomicBoolean(false)
    private var targetHostName: String = "hamclock"

    fun start(hostName: String) {
        stop()

        targetHostName = if (hostName.trim().isNotEmpty()) {
            hostName.trim().lowercase()
        } else {
            "hamclock"
        }

        val wifiManager = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
        multicastLock = wifiManager?.createMulticastLock("HamClock:MdnsLock")?.apply {
            setReferenceCounted(false)
            acquire()
            Log.i(TAG, "Acquired MulticastLock for mDNS")
        }

        isRunning.set(true)
        workerThread = Thread({
            runResponder()
        }, "MdnsResponderThread").apply {
            isDaemon = true
            start()
        }
        Log.i(TAG, "Started mDNS A-record responder for $targetHostName.local")
    }

    fun stop() {
        isRunning.set(false)
        try {
            multicastSocket?.close()
        } catch (e: Exception) {
            // Ignore close exceptions
        }
        multicastSocket = null

        workerThread?.interrupt()
        workerThread = null

        multicastLock?.let {
            if (it.isHeld) {
                it.release()
                Log.i(TAG, "Released MulticastLock")
            }
        }
        multicastLock = null
    }

    private fun getLocalIpv4Address(): InetAddress? {
        try {
            val interfaces = NetworkInterface.getNetworkInterfaces() ?: return null
            for (iface in interfaces) {
                if (iface.isLoopback || !iface.isUp) continue
                for (addr in iface.inetAddresses) {
                    if (!addr.isLoopbackAddress && addr is java.net.Inet4Address) {
                        return addr
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Error getting local IP: ${e.message}")
        }
        return null
    }

    private fun runResponder() {
        val group = InetAddress.getByName(MDNS_GROUP)
        val buf = ByteArray(1500)

        try {
            val socket = MulticastSocket(MDNS_PORT).apply {
                reuseAddress = true
                joinGroup(group)
                timeToLive = 255
            }
            multicastSocket = socket

            // Announce presence immediately on start
            val localIp = getLocalIpv4Address()
            if (localIp != null) {
                val announcePacket = buildResponsePacket(targetHostName, localIp.address)
                val dgram = DatagramPacket(announcePacket, announcePacket.size, group, MDNS_PORT)
                socket.send(dgram)
                Log.i(TAG, "Sent gratuitous mDNS announcement for $targetHostName.local -> ${localIp.hostAddress}")
            }

            while (isRunning.get() && !socket.isClosed) {
                val packet = DatagramPacket(buf, buf.size)
                try {
                    socket.receive(packet)
                } catch (e: Exception) {
                    if (!isRunning.get() || socket.isClosed) break
                    continue
                }

                if (packet.length < 12) continue

                // Check if this is a query (QR bit = 0 in flags)
                val flags = ((buf[2].toInt() and 0xFF) shl 8) or (buf[3].toInt() and 0xFF)
                val isQuery = (flags and 0x8000) == 0
                if (!isQuery) continue

                val qdCount = ((buf[4].toInt() and 0xFF) shl 8) or (buf[5].toInt() and 0xFF)
                if (qdCount < 1) continue

                val queriedName = parseQueriedName(buf, 12, packet.length) ?: continue
                val matchLocal = "$targetHostName.local"
                if (queriedName.equals(matchLocal, ignoreCase = true) || queriedName.equals("hamclock.local", ignoreCase = true)) {
                    val currentIp = getLocalIpv4Address() ?: continue
                    val responseBytes = buildResponsePacket(queriedName, currentIp.address)
                    val responsePacket = DatagramPacket(responseBytes, responseBytes.size, group, MDNS_PORT)
                    socket.send(responsePacket)
                    Log.i(TAG, "Answered mDNS query for $queriedName with IP ${currentIp.hostAddress}")
                }
            }
        } catch (e: Exception) {
            if (isRunning.get()) {
                Log.w(TAG, "mDNS socket loop exited: ${e.message}")
            }
        }
    }

    private fun parseQueriedName(buf: ByteArray, startOffset: Int, totalLen: Int): String? {
        var offset = startOffset
        val sb = java.lang.StringBuilder()
        while (offset < totalLen) {
            val len = buf[offset].toInt() and 0xFF
            if (len == 0) break
            // Pointers (compression) not expected at start of question
            if ((len and 0xC0) == 0xC0) return null
            offset++
            if (offset + len > totalLen) return null
            val part = String(buf, offset, len, Charsets.UTF_8)
            if (sb.isNotEmpty()) sb.append(".")
            sb.append(part)
            offset += len
        }
        return if (sb.isNotEmpty()) sb.toString() else null
    }

    private fun buildResponsePacket(hostFqdn: String, ipBytes: ByteArray): ByteArray {
        val baos = ByteArrayOutputStream()
        val dos = DataOutputStream(baos)

        // Header
        dos.writeShort(0x0000) // Transaction ID (0 for mDNS)
        dos.writeShort(0x8400) // Flags: Response + Authoritative (AA)
        dos.writeShort(0x0000) // QDCOUNT (0 questions)
        dos.writeShort(0x0001) // ANCOUNT (1 answer)
        dos.writeShort(0x0000) // NSCOUNT (0 authority)
        dos.writeShort(0x0000) // ARCOUNT (0 additional)

        // Answer Record: Name
        val labels = hostFqdn.split(".")
        for (label in labels) {
            val bytes = label.toByteArray(Charsets.UTF_8)
            dos.writeByte(bytes.size)
            dos.write(bytes)
        }
        dos.writeByte(0x00) // Root null label

        dos.writeShort(0x0001) // Type: A (Host address)
        dos.writeShort(0x8001) // Class: IN with Cache-Flush bit set (0x8000 | 0x0001)
        dos.writeInt(120)      // TTL: 120 seconds
        dos.writeShort(4)      // RDLENGTH: 4 bytes for IPv4
        dos.write(ipBytes)     // RDATA: IPv4 bytes

        dos.flush()
        return baos.toByteArray()
    }
}
