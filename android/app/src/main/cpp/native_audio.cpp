#include <jni.h>
#include <android/log.h>
#include <aaudio/AAudio.h>

#include <cmath>
#include <cstdint>

#define LOG_TAG "HiResAudio"

#define LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)


// ===============================
// Audio Stream
// ===============================

static AAudioStream* audioStream = nullptr;


// ===============================
// Variabel generator suara
// ===============================

static double phase = 0.0;

static constexpr double FREQUENCY = 440.0;
static constexpr double AMPLITUDE = 0.20;


// ===============================
// Callback audio
// ===============================

aaudio_data_callback_result_t audioCallback(
        AAudioStream* stream,
        void* userData,
        void* audioData,
        int32_t numFrames
) {

    auto* output = static_cast<int16_t*>(audioData);

    const int32_t channelCount =
            AAudioStream_getChannelCount(stream);

    const int32_t sampleRate =
            AAudioStream_getSampleRate(stream);


    const double phaseIncrement =
            (2.0 * M_PI * FREQUENCY) /
            static_cast<double>(sampleRate);


    for (int32_t frame = 0; frame < numFrames; frame++) {

        double sample =
                std::sin(phase) * AMPLITUDE;

        int16_t pcmSample =
                static_cast<int16_t>(
                    sample * 32767.0
                );


        for (int32_t channel = 0;
             channel < channelCount;
             channel++) {

            output[
                frame * channelCount + channel
            ] = pcmSample;
        }


        phase += phaseIncrement;

        if (phase >= 2.0 * M_PI) {
            phase -= 2.0 * M_PI;
        }
    }


    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}


// ===============================
// Error callback
// ===============================

void errorCallback(
        AAudioStream* stream,
        void* userData,
        aaudio_result_t error
) {

    LOGE(
        "AAudio error callback: %s",
        AAudio_convertResultToText(error)
    );
}


// ===============================
// Start Audio
// ===============================

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_hires_1player_MainActivity_nativePlayTestTone(
        JNIEnv* env,
        jobject /* this */
) {

    LOGI("==============================");
    LOGI("Memulai AAudio test tone...");
    LOGI("==============================");


    // Kalau sebelumnya masih berjalan,
    // tutup terlebih dahulu.

    if (audioStream != nullptr) {

        AAudioStream_requestStop(audioStream);
        AAudioStream_close(audioStream);

        audioStream = nullptr;
    }


    phase = 0.0;


    // ===============================
    // Builder
    // ===============================

    AAudioStreamBuilder* builder = nullptr;

    aaudio_result_t result =
            AAudio_createStreamBuilder(&builder);


    if (result != AAUDIO_OK) {

        LOGE(
            "Gagal membuat builder: %s",
            AAudio_convertResultToText(result)
        );

        return env->NewStringUTF(
            "Gagal membuat AAudio builder"
        );
    }


    // ===============================
    // Konfigurasi
    // ===============================

    AAudioStreamBuilder_setDirection(
        builder,
        AAUDIO_DIRECTION_OUTPUT
    );


    AAudioStreamBuilder_setPerformanceMode(
        builder,
        AAUDIO_PERFORMANCE_MODE_LOW_LATENCY
    );


    AAudioStreamBuilder_setSharingMode(
        builder,
        AAUDIO_SHARING_MODE_SHARED
    );


    AAudioStreamBuilder_setFormat(
        builder,
        AAUDIO_FORMAT_PCM_I16
    );


    AAudioStreamBuilder_setChannelCount(
        builder,
        2
    );


    AAudioStreamBuilder_setSampleRate(
        builder,
        48000
    );


    AAudioStreamBuilder_setDataCallback(
        builder,
        audioCallback,
        nullptr
    );


    AAudioStreamBuilder_setErrorCallback(
        builder,
        errorCallback,
        nullptr
    );


    // ===============================
    // Open stream
    // ===============================

    result =
            AAudioStreamBuilder_openStream(
                builder,
                &audioStream
            );


    AAudioStreamBuilder_delete(builder);


    if (result != AAUDIO_OK) {

        LOGE(
            "Gagal membuka stream: %s",
            AAudio_convertResultToText(result)
        );

        audioStream = nullptr;

        return env->NewStringUTF(
            "Gagal membuka AAudio stream"
        );
    }


    // ===============================
    // Cek konfigurasi aktual
    // ===============================

    int32_t actualSampleRate =
            AAudioStream_getSampleRate(
                audioStream
            );

    int32_t actualChannels =
            AAudioStream_getChannelCount(
                audioStream
            );

    aaudio_format_t actualFormat =
            AAudioStream_getFormat(
                audioStream
            );


    LOGI(
        "Sample Rate: %d Hz",
        actualSampleRate
    );

    LOGI(
        "Channels: %d",
        actualChannels
    );

    LOGI(
        "Format: %d",
        actualFormat
    );


    // ===============================
    // Start
    // ===============================

    result =
            AAudioStream_requestStart(
                audioStream
            );


    if (result != AAUDIO_OK) {

        LOGE(
            "Gagal start stream: %s",
            AAudio_convertResultToText(result)
        );

        AAudioStream_close(audioStream);

        audioStream = nullptr;

        return env->NewStringUTF(
            "Gagal menjalankan audio"
        );
    }


    LOGI("AAudio berhasil dimulai!");

    return env->NewStringUTF(
        "AAudio aktif - 440 Hz"
    );
}


// ===============================
// Stop Audio
// ===============================

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_hires_1player_MainActivity_nativeStopAudio(
        JNIEnv* env,
        jobject /* this */
) {

    LOGI("Menghentikan audio...");


    if (audioStream != nullptr) {

        AAudioStream_requestStop(
            audioStream
        );

        AAudioStream_close(
            audioStream
        );

        audioStream = nullptr;
    }


    LOGI("Audio berhenti.");


    return env->NewStringUTF(
        "Audio berhenti"
    );
}


// ===============================
// Engine Status
// ===============================

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_hires_1player_MainActivity_nativeGetEngineStatus(
        JNIEnv* env,
        jobject /* this */
) {

    LOGI("Native Audio Engine dipanggil!");

    return env->NewStringUTF(
        "Native Audio Engine Connected"
    );
}