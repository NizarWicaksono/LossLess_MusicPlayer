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


// ============================================================
// WAV Metadata
// ============================================================

// Path file WAV yang sedang digunakan
static std::string wavFilePath;

// Sample rate WAV
static int32_t wavSampleRate = 0;

// Jumlah channel WAV
static int32_t wavChannels = 0;

// Bit depth WAV
static int32_t wavBitsPerSample = 0;

// Format audio
// PCM = 1
static int32_t wavAudioFormat = 0;

// Posisi awal chunk "data" di dalam file
static long wavDataOffset = 0;

// Ukuran data PCM
static uint32_t wavDataSize = 0;

// Jumlah frame audio
static uint64_t wavTotalFrames = 0;


// ============================================================
// WAV Helper
// ============================================================

uint32_t readUint32(FILE* file)
{
    uint8_t buffer[4];

    if (fread(buffer, 1, 4, file) != 4)
    {
        return 0;
    }

    return
        static_cast<uint32_t>(buffer[0]) |
        (static_cast<uint32_t>(buffer[1]) << 8) |
        (static_cast<uint32_t>(buffer[2]) << 16) |
        (static_cast<uint32_t>(buffer[3]) << 24);
}


uint16_t readUint16(FILE* file)
{
    uint8_t buffer[2];

    if (fread(buffer, 1, 2, file) != 2)
    {
        return 0;
    }

    return
        static_cast<uint16_t>(buffer[0]) |
        (static_cast<uint16_t>(buffer[1]) << 8);
}


// ============================================================
// Reset WAV State
// ============================================================

void resetWavState()
{
    wavFilePath.clear();

    wavSampleRate = 0;

    wavChannels = 0;

    wavBitsPerSample = 0;

    wavAudioFormat = 0;

    wavDataOffset = 0;

    wavDataSize = 0;

    wavTotalFrames = 0;

    playbackPosition = 0;
}


// ============================================================
// WAV Parser
// ============================================================

bool loadWav(const char* path)
{
    LOGI("================================");

    LOGI("Membuka WAV:");

    LOGI("%s", path);

    LOGI("================================");


    // --------------------------------------------------------
    // Reset state sebelumnya
    // --------------------------------------------------------

    resetWavState();

    pcmData.clear();


    // --------------------------------------------------------
    // Buka file
    // --------------------------------------------------------

    FILE* file = fopen(path, "rb");

    if (file == nullptr)
    {
        LOGE("Tidak bisa membuka file WAV");

        return false;
    }


    // --------------------------------------------------------
    // RIFF
    // --------------------------------------------------------

    char riff[4];

    if (fread(riff, 1, 4, file) != 4)
    {
        LOGE("Gagal membaca header RIFF");

        fclose(file);

        return false;
    }


    if (memcmp(riff, "RIFF", 4) != 0)
    {
        LOGE("Bukan file RIFF");

        fclose(file);

        return false;
    }


    uint32_t fileSize = readUint32(file);

    (void)fileSize;


    // --------------------------------------------------------
    // WAVE
    // --------------------------------------------------------

    char wave[4];

    if (fread(wave, 1, 4, file) != 4)
    {
        LOGE("Gagal membaca header WAVE");

        fclose(file);

        return false;
    }


    if (memcmp(wave, "WAVE", 4) != 0)
    {
        LOGE("Bukan format WAVE");

        fclose(file);

        return false;
    }


    // --------------------------------------------------------
    // Variabel parser
    // --------------------------------------------------------

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

    while (!feof(file))
    {
        char chunkId[4];


        if (fread(chunkId, 1, 4, file) != 4)
        {
            break;
        }


        uint32_t chunkSize = readUint32(file);


        // ----------------------------------------------------
        // fmt
        // ----------------------------------------------------

        if (memcmp(chunkId, "fmt ", 4) == 0)
        {
            audioFormat = readUint16(file);

            channels = readUint16(file);

            sampleRate = readUint32(file);


            // Byte rate
            readUint32(file);

            // Block align
            readUint16(file);

            bitsPerSample = readUint16(file);


            // Kalau ada data tambahan
            if (chunkSize > 16)
            {
                fseek(
                    file,
                    static_cast<long>(chunkSize - 16),
                    SEEK_CUR
                );
            }


            foundFmt = true;
        }


        // ----------------------------------------------------
        // data
        // ----------------------------------------------------

        else if (memcmp(chunkId, "data", 4) == 0)
        {
            dataSize = chunkSize;

            dataOffset = ftell(file);


            fseek(
                file,
                static_cast<long>(chunkSize),
                SEEK_CUR
            );


            foundData = true;
        }


        // ----------------------------------------------------
        // Chunk lain
        // ----------------------------------------------------

        else
        {
            fseek(
                file,
                static_cast<long>(chunkSize),
                SEEK_CUR
            );
        }


        // ----------------------------------------------------
        // Kalau sudah ketemu dua-duanya
        // ----------------------------------------------------

        if (foundFmt && foundData)
        {
            break;
        }
    }


    // --------------------------------------------------------
    // Validasi chunk
    // --------------------------------------------------------

    if (!foundFmt)
    {
        LOGE("Chunk fmt tidak ditemukan");

        fclose(file);

        return false;
    }


    if (!foundData)
    {
        LOGE("Chunk data tidak ditemukan");

        fclose(file);

        return false;
    }


    // ========================================================
    // Validasi format
    // ========================================================

    LOGI("==============================");

    LOGI("WAV INFO");

    LOGI("Sample Rate : %u Hz", sampleRate);

    LOGI("Channels    : %u", channels);

    LOGI("Bit Depth   : %u bit", bitsPerSample);

    LOGI("Audio Format: %u", audioFormat);

    LOGI("Data Size   : %u bytes", dataSize);

    LOGI("Data Offset : %ld", dataOffset);

    LOGI("==============================");


    // --------------------------------------------------------
    // PCM = 1
    // --------------------------------------------------------

    if (audioFormat != 1)
    {
        LOGE("Format audio bukan PCM.");

        fclose(file);

        return false;
    }


    // --------------------------------------------------------
    // Untuk sekarang masih 16-bit
    // --------------------------------------------------------

    if (bitsPerSample != 16)
    {
        LOGE(
            "Untuk tahap ini hanya mendukung 16-bit."
        );

        fclose(file);

        return false;
    }


    // --------------------------------------------------------
    // Untuk sekarang 1 atau 2 channel
    // --------------------------------------------------------

    if (channels < 1 || channels > 2)
    {
        LOGE(
            "Untuk tahap ini hanya mendukung 1 atau 2 channel."
        );

        fclose(file);

        return false;
    }


    // ========================================================
    // Simpan metadata ke state global
    // ========================================================

    wavFilePath = path;

    wavSampleRate =
        static_cast<int32_t>(sampleRate);

    wavChannels =
        static_cast<int32_t>(channels);

    wavBitsPerSample =
        static_cast<int32_t>(bitsPerSample);

    wavAudioFormat =
        static_cast<int32_t>(audioFormat);

    wavDataOffset = dataOffset;

    wavDataSize = dataSize;


    // Jumlah frame:
    //
    // dataSize
    // --------
    // bytes per sample × jumlah channel
    //
    uint32_t bytesPerSample =
        bitsPerSample / 8;


    uint32_t bytesPerFrame =
        bytesPerSample * channels;


    if (bytesPerFrame > 0)
    {
        wavTotalFrames =
            dataSize / bytesPerFrame;
    }
    else
    {
        wavTotalFrames = 0;
    }


    // ========================================================
    // Untuk 3B-1:
    //
    // Playback masih menggunakan pcmData.
    //
    // Kita belum menggunakan file streaming di callback.
    // ========================================================

    pcmData.resize(
        dataSize / sizeof(int16_t)
    );


    // --------------------------------------------------------
    // Kembali ke awal PCM
    // --------------------------------------------------------

    fseek(
        file,
        dataOffset,
        SEEK_SET
    );


    // --------------------------------------------------------
    // Baca PCM
    // --------------------------------------------------------

    size_t samplesRead =
        fread(
            pcmData.data(),
            sizeof(int16_t),
            pcmData.size(),
            file
        );


    // --------------------------------------------------------
    // Tutup file
    // --------------------------------------------------------

    fclose(file);


    // --------------------------------------------------------
    // Validasi hasil baca
    // --------------------------------------------------------

    if (samplesRead != pcmData.size())
    {
        LOGE(
            "Jumlah PCM yang terbaca tidak sesuai."
        );

        pcmData.clear();

        resetWavState();

        return false;
    }


    playbackPosition = 0;


    // ========================================================
    // Log metadata
    // ========================================================

    LOGI("================================");

    LOGI("WAV METADATA TERSIMPAN");

    LOGI("Path        : %s", wavFilePath.c_str());

    LOGI("Sample Rate : %d Hz", wavSampleRate);

    LOGI("Channels    : %d", wavChannels);

    LOGI("Bit Depth   : %d bit", wavBitsPerSample);

    LOGI("Format      : %d", wavAudioFormat);

    LOGI("Data Offset : %ld", wavDataOffset);

    LOGI("Data Size   : %u bytes", wavDataSize);

    LOGI("Total Frame : %llu",
         static_cast<unsigned long long>(
             wavTotalFrames
         ));

    LOGI("PCM Samples : %zu", pcmData.size());

    LOGI("================================");


    return true;
}


// ============================================================
// AAudio Callback
// ============================================================

aaudio_data_callback_result_t audioCallback(
    AAudioStream* stream,
    void* userData,
    void* audioData,
    int32_t numFrames
)
{
    auto* output =
        static_cast<int16_t*>(audioData);


    int32_t channels =
        AAudioStream_getChannelCount(
            stream
        );


    size_t totalSamples =
        pcmData.size();


    // --------------------------------------------------------
    // Isi buffer output
    // --------------------------------------------------------

    for (
        int32_t frame = 0;
        frame < numFrames;
        frame++
    )
    {
        for (
            int32_t channel = 0;
            channel < channels;
            channel++
        )
        {
            size_t index =
                playbackPosition +
                channel;


            if (index < totalSamples)
            {
                output[
                    frame * channels + channel
                ] = pcmData[index];
            }
            else
            {
                output[
                    frame * channels + channel
                ] = 0;
            }
        }


        playbackPosition += channels;


        // ----------------------------------------------------
        // Lagu selesai
        // ----------------------------------------------------

        if (playbackPosition >= totalSamples)
        {
            for (
                int32_t remainingFrame = frame + 1;
                remainingFrame < numFrames;
                remainingFrame++
            )
            {
                for (
                    int32_t channel = 0;
                    channel < channels;
                    channel++
                )
                {
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
// Error Callback
// ============================================================

void errorCallback(
    AAudioStream* stream,
    void* userData,
    aaudio_result_t error
)
{
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
)
{
    // --------------------------------------------------------
    // Ambil path dari Kotlin
    // --------------------------------------------------------

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


    if (!loaded)
    {
        return env->NewStringUTF(
            "Gagal membaca WAV"
        );
    }


    // --------------------------------------------------------
    // Tutup stream sebelumnya
    // --------------------------------------------------------

    if (audioStream != nullptr)
    {
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


    if (result != AAUDIO_OK)
    {
        LOGE(
            "Gagal membuat AAudio builder: %s",
            AAudio_convertResultToText(result)
        );


        return env->NewStringUTF(
            "Gagal membuat AAudio builder"
        );
    }


    // --------------------------------------------------------
    // Konfigurasi AAudio
    // --------------------------------------------------------

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


    if (result != AAUDIO_OK)
    {
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


    if (result != AAUDIO_OK)
    {
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
)
{
    if (audioStream != nullptr)
    {
        AAudioStream_requestStop(
            audioStream
        );


        AAudioStream_close(
            audioStream
        );


        audioStream = nullptr;
    }


    pcmData.clear();


    resetWavState();


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
)
{
    return env->NewStringUTF(
        "Native Audio Engine Connected"
    );
}