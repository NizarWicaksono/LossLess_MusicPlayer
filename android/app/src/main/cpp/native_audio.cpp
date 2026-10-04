#include <jni.h>
#include <android/log.h>
#include <aaudio/AAudio.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>


#define LOG_TAG "HiResAudio"

#define LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)


// ============================================================
// Audio
// ============================================================

static AAudioStream* audioStream = nullptr;

static std::vector<int16_t> pcmData;

static size_t playbackPosition = 0;

static int32_t wavSampleRate = 0;

static int32_t wavChannels = 0;


// ============================================================
// WAV helper
// ============================================================

uint32_t readUint32(FILE* file) {

    uint8_t buffer[4];

    fread(buffer, 1, 4, file);

    return
        static_cast<uint32_t>(buffer[0]) |
        (static_cast<uint32_t>(buffer[1]) << 8) |
        (static_cast<uint32_t>(buffer[2]) << 16) |
        (static_cast<uint32_t>(buffer[3]) << 24);
}


uint16_t readUint16(FILE* file) {

    uint8_t buffer[2];

    fread(buffer, 1, 2, file);

    return
        static_cast<uint16_t>(buffer[0]) |
        (static_cast<uint16_t>(buffer[1]) << 8);
}


// ============================================================
// WAV parser
// ============================================================

bool loadWav(const char* path) {

    LOGI("Membuka WAV:");
    LOGI("%s", path);


    FILE* file = fopen(path, "rb");

    if (file == nullptr) {

        LOGE("Tidak bisa membuka file WAV");

        return false;
    }


    // --------------------------------------------------------
    // RIFF
    // --------------------------------------------------------

    char riff[4];

    fread(riff, 1, 4, file);

    if (memcmp(riff, "RIFF", 4) != 0) {

        LOGE("Bukan file RIFF");

        fclose(file);

        return false;
    }


    uint32_t fileSize =
        readUint32(file);

    (void)fileSize;


    // --------------------------------------------------------
    // WAVE
    // --------------------------------------------------------

    char wave[4];

    fread(wave, 1, 4, file);

    if (memcmp(wave, "WAVE", 4) != 0) {

        LOGE("Bukan format WAVE");

        fclose(file);

        return false;
    }


    bool foundFmt = false;

    bool foundData = false;


    uint16_t audioFormat = 0;

    uint16_t bitsPerSample = 0;

    uint32_t sampleRate = 0;

    uint16_t channels = 0;

    uint32_t dataSize = 0;

    long dataOffset = 0;


    // --------------------------------------------------------
    // Cari chunk
    // --------------------------------------------------------

    while (!feof(file)) {

        char chunkId[4];

        if (fread(chunkId, 1, 4, file) != 4) {
            break;
        }


        uint32_t chunkSize =
            readUint32(file);


        // ----------------------------------------------------
        // fmt
        // ----------------------------------------------------

        if (memcmp(chunkId, "fmt ", 4) == 0) {

            audioFormat =
                readUint16(file);

            channels =
                readUint16(file);

            sampleRate =
                readUint32(file);


            // byte rate
            readUint32(file);

            // block align
            readUint16(file);

            bitsPerSample =
                readUint16(file);


            // Kalau ada data tambahan
            if (chunkSize > 16) {

                fseek(
                    file,
                    chunkSize - 16,
                    SEEK_CUR
                );
            }


            foundFmt = true;
        }


        // ----------------------------------------------------
        // data
        // ----------------------------------------------------

        else if (memcmp(chunkId, "data", 4) == 0) {

            dataSize = chunkSize;

            dataOffset = ftell(file);

            fseek(
                file,
                chunkSize,
                SEEK_CUR
            );

            foundData = true;
        }


        // ----------------------------------------------------
        // Chunk lain
        // ----------------------------------------------------

        else {

            fseek(
                file,
                chunkSize,
                SEEK_CUR
            );
        }


        if (foundFmt && foundData) {
            break;
        }
    }


    if (!foundFmt) {

        LOGE("Chunk fmt tidak ditemukan");

        fclose(file);

        return false;
    }


    if (!foundData) {

        LOGE("Chunk data tidak ditemukan");

        fclose(file);

        return false;
    }


    // ========================================================
    // Validasi
    // ========================================================

    LOGI("==============================");
    LOGI("WAV INFO");
    LOGI("Sample Rate : %u Hz", sampleRate);
    LOGI("Channels    : %u", channels);
    LOGI("Bit Depth   : %u bit", bitsPerSample);
    LOGI("Audio Format: %u", audioFormat);
    LOGI("Data Size   : %u bytes", dataSize);
    LOGI("==============================");


    // PCM = 1
    if (audioFormat != 1) {

        LOGE(
            "Format audio bukan PCM."
        );

        fclose(file);

        return false;
    }


    if (bitsPerSample != 16) {

        LOGE(
            "Untuk tahap ini hanya mendukung 16-bit."
        );

        fclose(file);

        return false;
    }


    if (channels < 1 || channels > 2) {

        LOGE(
            "Untuk tahap ini hanya mendukung 1 atau 2 channel."
        );

        fclose(file);

        return false;
    }


    // ========================================================
    // Baca PCM
    // ========================================================

    pcmData.resize(
        dataSize / sizeof(int16_t)
    );


    fseek(
        file,
        dataOffset,
        SEEK_SET
    );


    size_t samplesRead =
        fread(
            pcmData.data(),
            sizeof(int16_t),
            pcmData.size(),
            file
        );


    fclose(file);


    if (samplesRead != pcmData.size()) {

        LOGE(
            "Jumlah PCM yang terbaca tidak sesuai."
        );

        pcmData.clear();

        return false;
    }


    wavSampleRate =
        static_cast<int32_t>(sampleRate);

    wavChannels =
        static_cast<int32_t>(channels);


    playbackPosition = 0;


    LOGI(
        "PCM berhasil dimuat: %zu samples",
        pcmData.size()
    );


    return true;
}


// ============================================================
// AAudio callback
// ============================================================

aaudio_data_callback_result_t audioCallback(
    AAudioStream* stream,
    void* userData,
    void* audioData,
    int32_t numFrames
) {

    auto* output =
        static_cast<int16_t*>(audioData);


    int32_t channels =
        AAudioStream_getChannelCount(
            stream
        );


    size_t totalSamples =
        pcmData.size();


    for (int32_t frame = 0;
         frame < numFrames;
         frame++) {


        for (int32_t channel = 0;
             channel < channels;
             channel++) {


            size_t index =
                playbackPosition +
                channel;


            if (index < totalSamples) {

                output[
                    frame * channels + channel
                ] = pcmData[index];

            } else {

                output[
                    frame * channels + channel
                ] = 0;
            }
        }


        playbackPosition += channels;


        // ----------------------------------------------------
        // Lagu selesai
        // ----------------------------------------------------

        if (playbackPosition >= totalSamples) {

            for (int32_t remainingFrame = frame + 1;
                 remainingFrame < numFrames;
                 remainingFrame++) {

                for (int32_t channel = 0;
                     channel < channels;
                     channel++) {

                    output[
                        remainingFrame * channels +
                        channel
                    ] = 0;
                }
            }


            return AAUDIO_CALLBACK_RESULT_STOP;
        }
    }


    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}


// ============================================================
// Error callback
// ============================================================

void errorCallback(
    AAudioStream* stream,
    void* userData,
    aaudio_result_t error
) {

    LOGE(
        "AAudio error: %s",
        AAudio_convertResultToText(error)
    );
}


// ============================================================
// Play WAV
// ============================================================

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_hires_1player_MainActivity_nativePlayWav(
    JNIEnv* env,
    jobject /* this */,
    jstring path
) {

    const char* filePath =
        env->GetStringUTFChars(
            path,
            nullptr
        );


    // --------------------------------------------------------
    // Load WAV
    // --------------------------------------------------------

    bool loaded =
        loadWav(filePath);


    env->ReleaseStringUTFChars(
        path,
        filePath
    );


    if (!loaded) {

        return env->NewStringUTF(
            "Gagal membaca WAV"
        );
    }


    // --------------------------------------------------------
    // Tutup stream sebelumnya
    // --------------------------------------------------------

    if (audioStream != nullptr) {

        AAudioStream_requestStop(
            audioStream
        );

        AAudioStream_close(
            audioStream
        );

        audioStream = nullptr;
    }


    // --------------------------------------------------------
    // Builder
    // --------------------------------------------------------

    AAudioStreamBuilder* builder =
        nullptr;


    aaudio_result_t result =
        AAudio_createStreamBuilder(
            &builder
        );


    if (result != AAUDIO_OK) {

        LOGE(
            "Gagal membuat AAudio builder: %s",
            AAudio_convertResultToText(result)
        );

        return env->NewStringUTF(
            "Gagal membuat AAudio builder"
        );
    }


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
        wavChannels
    );


    AAudioStreamBuilder_setSampleRate(
        builder,
        wavSampleRate
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


    // --------------------------------------------------------
    // Open
    // --------------------------------------------------------

    result =
        AAudioStreamBuilder_openStream(
            builder,
            &audioStream
        );


    AAudioStreamBuilder_delete(
        builder
    );


    if (result != AAUDIO_OK) {

        LOGE(
            "Gagal membuka AAudio: %s",
            AAudio_convertResultToText(result)
        );

        audioStream = nullptr;

        return env->NewStringUTF(
            "Gagal membuka AAudio"
        );
    }


    // --------------------------------------------------------
    // Informasi aktual
    // --------------------------------------------------------

    int32_t actualSampleRate =
        AAudioStream_getSampleRate(
            audioStream
        );


    int32_t actualChannels =
        AAudioStream_getChannelCount(
            audioStream
        );


    LOGI(
        "Actual Sample Rate: %d Hz",
        actualSampleRate
    );


    LOGI(
        "Actual Channels: %d",
        actualChannels
    );


    // --------------------------------------------------------
    // Start
    // --------------------------------------------------------

    result =
        AAudioStream_requestStart(
            audioStream
        );


    if (result != AAUDIO_OK) {

        LOGE(
            "Gagal start AAudio: %s",
            AAudio_convertResultToText(result)
        );


        AAudioStream_close(
            audioStream
        );


        audioStream = nullptr;


        return env->NewStringUTF(
            "Gagal menjalankan WAV"
        );
    }


    LOGI(
        "WAV mulai diputar."
    );


    return env->NewStringUTF(
        "WAV sedang diputar"
    );
}


// ============================================================
// Stop
// ============================================================

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_hires_1player_MainActivity_nativeStopAudio(
    JNIEnv* env,
    jobject /* this */
) {

    if (audioStream != nullptr) {

        AAudioStream_requestStop(
            audioStream
        );


        AAudioStream_close(
            audioStream
        );


        audioStream = nullptr;
    }


    pcmData.clear();

    playbackPosition = 0;


    LOGI(
        "Audio dihentikan."
    );


    return env->NewStringUTF(
        "Audio berhenti"
    );
}


// ============================================================
// Engine Status
// ============================================================

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_hires_1player_MainActivity_nativeGetEngineStatus(
    JNIEnv* env,
    jobject /* this */
) {

    return env->NewStringUTF(
        "Native Audio Engine Connected"
    );
}