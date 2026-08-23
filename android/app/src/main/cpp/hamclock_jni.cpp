#include <jni.h>
#include <string>
#include <vector>
#include <pthread.h>
#include <unistd.h>
#include <android/log.h>

#define TAG "HamClockNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

extern int hamclock_main(int ac, char *av[]);

struct DaemonArgs {
    std::string dataDir;
    int rwPort;
    int roPort;
    int restPort;
};

static pthread_t daemon_thread;
static bool daemon_running = false;

static void *daemon_worker(void *arg) {
    DaemonArgs *dargs = static_cast<DaemonArgs *>(arg);

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
    std::string throtVal = "0.8";

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
    argv.push_back(nullptr);

    int argc = static_cast<int>(argv.size() - 1);

    LOGI("Starting HamClock daemon with argc=%d in dir=%s on rw_port=%s", argc, dirVal.c_str(), rwVal.c_str());
    hamclock_main(argc, argv.data());

    LOGI("HamClock daemon exited");
    daemon_running = false;
    return nullptr;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_hamclock_HamClockNative_startDaemon(
        JNIEnv *env,
        jobject /* this */,
        jstring dataDir,
        jint rwPort,
        jint roPort,
        jint restPort) {

    if (daemon_running) {
        LOGI("HamClock daemon already running");
        return JNI_TRUE;
    }

    const char *dataDirChars = env->GetStringUTFChars(dataDir, nullptr);
    if (!dataDirChars) {
        return JNI_FALSE;
    }

    DaemonArgs *dargs = new DaemonArgs();
    dargs->dataDir = dataDirChars;
    dargs->rwPort = rwPort;
    dargs->roPort = roPort;
    dargs->restPort = restPort;

    env->ReleaseStringUTFChars(dataDir, dataDirChars);

    daemon_running = true;
    int err = pthread_create(&daemon_thread, nullptr, daemon_worker, dargs);
    if (err != 0) {
        LOGE("Failed to create daemon pthread: %d", err);
        daemon_running = false;
        delete dargs;
        return JNI_FALSE;
    }

    pthread_detach(daemon_thread);
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_hamclock_HamClockNative_isDaemonRunning(
        JNIEnv * /* env */,
        jobject /* this */) {
    return daemon_running ? JNI_TRUE : JNI_FALSE;
}
