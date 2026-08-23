#include <jni.h>
#include <string>
#include <vector>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <android/log.h>

#include "HamClock.h"

#define TAG "HamClockNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

extern int hamclock_main(int ac, char *av[]);

struct DaemonArgs {
    std::string dataDir;
    int rwPort;
    int roPort;
    int restPort;
    std::string backendHost;
    bool hasLocation;
    double lat;
    double lng;
};

static pthread_t daemon_thread;
static bool daemon_running = false;

static void configure_fdsan() {
    typedef enum {
        FDSAN_DISABLED,
        FDSAN_WARN_ONCE,
        FDSAN_WARN_ALWAYS,
        FDSAN_FATAL,
    } fdsan_level;
    typedef void (*set_fdsan_fn)(fdsan_level);
    set_fdsan_fn set_fdsan = (set_fdsan_fn) dlsym(RTLD_DEFAULT, "android_fdsan_set_error_level");
    if (set_fdsan) {
        set_fdsan(FDSAN_WARN_ALWAYS);
    }
}


static std::string cached_data_dir;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
extern "C" std::string __wrap__ZN4WiFi10macAddressEv() {
#pragma clang diagnostic pop

    char mac_buf[32] = {0};
    std::string mac_file = cached_data_dir.empty() ? "" : (cached_data_dir + "/.mac_address");

    if (!mac_file.empty()) {
        FILE *fp = fopen(mac_file.c_str(), "r");
        if (fp) {
            if (fgets(mac_buf, sizeof(mac_buf), fp)) {
                char *nl = strchr(mac_buf, '\n');
                if (nl) *nl = '\0';
                unsigned int m1, m2, m3, m4, m5, m6;
                if (sscanf(mac_buf, "%x:%x:%x:%x:%x:%x", &m1, &m2, &m3, &m4, &m5, &m6) == 6) {
                    fclose(fp);
                    return std::string(mac_buf);
                }
            }
            fclose(fp);
        }
    }

    // Generate locally administered unicast MAC (02:xx:xx:xx:xx:xx)
    uint8_t rand_bytes[6] = {0};
    FILE *urand = fopen("/dev/urandom", "re");
    if (urand) {
        fread(rand_bytes, 1, 6, urand);
        fclose(urand);
    }
    rand_bytes[0] = (rand_bytes[0] & 0xFE) | 0x02; // locally administered unicast

    snprintf(mac_buf, sizeof(mac_buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             rand_bytes[0], rand_bytes[1], rand_bytes[2],
             rand_bytes[3], rand_bytes[4], rand_bytes[5]);

    if (!mac_file.empty()) {
        FILE *fp = fopen(mac_file.c_str(), "w");
        if (fp) {
            fprintf(fp, "%s\n", mac_buf);
            fclose(fp);
        }
    }

    LOGI("Generated persistent Android MAC: %s", mac_buf);
    return std::string(mac_buf);
}

static void *daemon_worker(void *arg) {
    DaemonArgs *dargs = static_cast<DaemonArgs *>(arg);
    cached_data_dir = dargs->dataDir;
    our_dir = dargs->dataDir + "/";

    // Preset NTP to Computer (OS) time if unconfigured
    uint8_t ntp_val = 0;
    if (!NVReadUInt8(NV_NTPSET, &ntp_val) || ntp_val == 0) {
        NVWriteUInt8(NV_NTPSET, 2); // NTPSC_OS ("Computer")
        LOGI("Preset NTP to Computer (OS) time");
    }

    // If host GPS location is available, update DE location on every start
    if (dargs->hasLocation) {
        LatLong ll;
        ll.lat_d = dargs->lat;
        ll.lng_d = dargs->lng;
        ll.normalize();

        NVWriteFloat(NV_DE_LAT, ll.lat_d);
        NVWriteFloat(NV_DE_LNG, ll.lng_d);
        setNVMaidenhead(NV_DE_GRID, ll);
        setTZAuto(de_tz);
        NVWriteTZ(NV_DE_TZ, de_tz);

        char grid[MAID_CHARLEN] = {0};
        getNVMaidenhead(NV_DE_GRID, grid);
        LOGI("Updated DE from host GPS: %.4f, %.4f (Grid: %s, TZ: %d min)",
             ll.lat_d, ll.lng_d, grid, de_tz.tz_secs / 60);
    }

    std::string progName = "hamclock-android";
    std::string dirFlag = "-d";
    std::string dirVal = dargs->dataDir;
    std::string rwFlag = "-w";
    std::string rwVal = std::to_string(dargs->rwPort);
    std::string roFlag = "-r";
    std::string roVal = std::to_string(dargs->roPort);
    std::string restFlag = "-e";
    std::string restVal = std::to_string(dargs->restPort);
    std::string throtFlag = "-t";
    std::string throtVal = "80";
    std::string skipFlag = "-k";
    std::string geoFlag = "-g";
    std::string bFlag = "-b";
    std::string bVal = dargs->backendHost;
    bool hasLoc = dargs->hasLocation;

    delete dargs;

    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(progName.c_str()));
    argv.push_back(const_cast<char *>(dirFlag.c_str()));
    argv.push_back(const_cast<char *>(dirVal.c_str()));
    argv.push_back(const_cast<char *>(rwFlag.c_str()));
    argv.push_back(const_cast<char *>(rwVal.c_str()));
    argv.push_back(const_cast<char *>(roFlag.c_str()));
    argv.push_back(const_cast<char *>(roVal.c_str()));
    argv.push_back(const_cast<char *>(restFlag.c_str()));
    argv.push_back(const_cast<char *>(restVal.c_str()));
    argv.push_back(const_cast<char *>(throtFlag.c_str()));
    argv.push_back(const_cast<char *>(throtVal.c_str()));
    argv.push_back(const_cast<char *>(skipFlag.c_str()));
    if (!hasLoc) {
        argv.push_back(const_cast<char *>(geoFlag.c_str()));
        LOGI("Host GPS location not available: falling back to GeoIP (-g)");
    }
    if (!bVal.empty()) {
        argv.push_back(const_cast<char *>(bFlag.c_str()));
        argv.push_back(const_cast<char *>(bVal.c_str()));
    }

    argv.push_back(nullptr);

    int argc = static_cast<int>(argv.size() - 1);

    LOGI("Starting HamClock daemon with argc=%d in dir=%s on rw_port=%s (backend=%s)",
         argc, dirVal.c_str(), rwVal.c_str(), bVal.c_str());
    hamclock_main(argc, argv.data());

    LOGI("HamClock daemon exited");
    daemon_running = false;
    return nullptr;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_openhamclock_HamClockNative_startDaemon(
        JNIEnv *env,
        jobject /* this */,
        jstring dataDir,
        jint rwPort,
        jint roPort,
        jint restPort,
        jstring backendHost,
        jboolean hasLocation,
        jdouble lat,
        jdouble lng) {

    configure_fdsan();

    if (daemon_running) {
        LOGI("HamClock daemon already running");
        return JNI_TRUE;
    }

    const char *dataDirChars = env->GetStringUTFChars(dataDir, nullptr);
    if (!dataDirChars) {
        return JNI_FALSE;
    }

    std::string backendHostStr;
    if (backendHost) {
        const char *backendChars = env->GetStringUTFChars(backendHost, nullptr);
        if (backendChars) {
            backendHostStr = backendChars;
            env->ReleaseStringUTFChars(backendHost, backendChars);
        }
    }

    DaemonArgs *args = new DaemonArgs();
    args->dataDir = dataDirChars;
    args->rwPort = rwPort;
    args->roPort = roPort;
    args->restPort = restPort;
    args->backendHost = backendHostStr;
    args->hasLocation = (hasLocation == JNI_TRUE);
    args->lat = lat;
    args->lng = lng;

    env->ReleaseStringUTFChars(dataDir, dataDirChars);

    daemon_running = true;
    if (pthread_create(&daemon_thread, nullptr, daemon_worker, args) != 0) {
        LOGE("Failed to create daemon thread");
        delete args;
        daemon_running = false;
        return JNI_FALSE;
    }

    pthread_detach(daemon_thread);
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_openhamclock_HamClockNative_isDaemonRunning(
        JNIEnv * /* env */,
        jobject /* this */) {
    return daemon_running ? JNI_TRUE : JNI_FALSE;
}
