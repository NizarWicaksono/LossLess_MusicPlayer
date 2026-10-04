#include <jni.h>
#include <android/log.h>

#define LOG_TAG "HiResAudio"

#define LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_hires_1player_MainActivity_nativeGetEngineStatus(
        JNIEnv* env,
        jobject /* this */
) {
    LOGI("=================================");
    LOGI("Native Audio Engine dipanggil!");
    LOGI("C++ JNI berhasil bekerja!");
    LOGI("=================================");

    return env->NewStringUTF(
        "Native Audio Engine Connected"
    );
}