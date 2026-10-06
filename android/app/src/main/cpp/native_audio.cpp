#include <jni.h>
#include <android/log.h>
#include <aaudio/AAudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>


#define LOG_TAG "HiResAudio"

#define LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)


// ============================================================
// AAudio
// ============================================================

static AAudioStream* audioStream = nullptr;


// ============================================================
// WAV Metadata
// ============================================================

static std::string wavFilePath;

static int32_t wavSampleRate = 0;
static int32_t wavChannels = 0;
static int32_t wavBitsPerSample = 0;
static int32_t wavAudioFormat = 0;

static long wavDataOffset = 0;

static uint32_t wavDataSize = 0;

static uint64_t wavTotalFrames = 0;


// ============================================================
// Lock-Free SPSC Ring Buffer
// ============================================================
//
// SPSC = Single Producer, Single Consumer
//
// Producer:
//     Reader Thread
//
// Consumer:
//     AAudio Callback
//
// Tidak menggunakan mutex.
//
// ============================================================

// 65536 sample
//
// Karena PCM 16-bit:
//
// 65536 * 2 byte
// = 131072 byte
// = 128 KiB
//
static constexpr size_t RING_BUFFER_CAPACITY = 65536;


// Buffer PCM
static std::vector<int16_t> ringBuffer(
    RING_BUFFER_CAPACITY
);


// ------------------------------------------------------------
// Atomic index
// ------------------------------------------------------------
//
// writeIndex:
// hanya ditulis Reader Thread
//
// readIndex:
// hanya ditulis AAudio Callback
//
// Keduanya dibaca oleh thread lainnya.
//

static std::atomic<size_t> ringWriteIndex(0);

static std::atomic<size_t> ringReadIndex(0);


// ============================================================
// Reader Thread State
// ============================================================

static std::thread readerThread;

static std::atomic<bool> readerRunning(false);

static std::atomic<bool> readerStopRequested(false);

static std::atomic<bool> endOfFileReached(false);


// ============================================================
// Playback State
// ============================================================

static std::atomic<bool> playbackFinished(false);


// ============================================================
// WAV Helper
// ============================================================

uint32_t readUint32(FILE* file)
{
    uint8_t buffer[4];

    if (
        fread(
            buffer,
            1,
            4,
            file
        ) != 4
    )
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

    if (
        fread(
            buffer,
            1,
            2,
            file
        ) != 2
    )
    {
        return 0;
    }

    return
        static_cast<uint16_t>(buffer[0]) |
        (static_cast<uint16_t>(buffer[1]) << 8);
}


// ============================================================
// Ring Buffer Reset
// ============================================================

void resetRingBuffer()
{
    ringReadIndex.store(
        0,
        std::memory_order_relaxed
    );

    ringWriteIndex.store(
        0,
        std::memory_order_relaxed
    );
}


// ============================================================
// Ring Buffer - Available Samples
// ============================================================

size_t getAvailableSamples()
{
    size_t writeIndex =
        ringWriteIndex.load(
            std::memory_order_acquire
        );

    size_t readIndex =
        ringReadIndex.load(
            std::memory_order_acquire
        );


    if (writeIndex >= readIndex)
    {
        return writeIndex - readIndex;
    }


    return
        RING_BUFFER_CAPACITY
        -
        readIndex
        +
        writeIndex;
}


// ============================================================
// Ring Buffer - Free Space
// ============================================================
//
// Satu slot sengaja dikosongkan agar kondisi:
//
// write == read
//
// selalu berarti EMPTY.
//
// ============================================================

size_t getFreeSamples()
{
    return
        (RING_BUFFER_CAPACITY - 1)
        -
        getAvailableSamples();
}


// ============================================================
// Ring Buffer - Write
// ============================================================
//
// Hanya dipanggil Reader Thread.
//
// ============================================================

size_t writeToRingBuffer(
    const int16_t* source,
    size_t sampleCount
)
{
    size_t writeIndex =
        ringWriteIndex.load(
            std::memory_order_relaxed
        );


    size_t readIndex =
        ringReadIndex.load(
            std::memory_order_acquire
        );


    size_t availableSpace;


    if (writeIndex >= readIndex)
    {
        availableSpace =
            RING_BUFFER_CAPACITY
            -
            writeIndex
            +
            readIndex
            -
            1;
    }
    else
    {
        availableSpace =
            readIndex
            -
            writeIndex
            -
            1;
    }


    size_t samplesToWrite =
        std::min(
            sampleCount,
            availableSpace
        );


    for (
        size_t i = 0;
        i < samplesToWrite;
        i++
    )
    {
        ringBuffer[writeIndex] =
            source[i];


        writeIndex++;


        if (
            writeIndex
            >=
            RING_BUFFER_CAPACITY
        )
        {
            writeIndex = 0;
        }
    }


    // Publish data setelah semua sample selesai ditulis.
    ringWriteIndex.store(
        writeIndex,
        std::memory_order_release
    );


    return samplesToWrite;
}


// ============================================================
// Ring Buffer - Read
// ============================================================
//
// Hanya dipanggil AAudio Callback.
//
// Tidak menggunakan mutex.
//
// Tidak melakukan allocation.
//
// Tidak melakukan fread().
//
// ============================================================

size_t readFromRingBuffer(
    int16_t* destination,
    size_t sampleCount
)
{
    size_t readIndex =
        ringReadIndex.load(
            std::memory_order_relaxed
        );


    size_t writeIndex =
        ringWriteIndex.load(
            std::memory_order_acquire
        );


    size_t availableSamples;


    if (writeIndex >= readIndex)
    {
        availableSamples =
            writeIndex
            -
            readIndex;
    }
    else
    {
        availableSamples =
            RING_BUFFER_CAPACITY
            -
            readIndex
            +
            writeIndex;
    }


    size_t samplesToRead =
        std::min(
            sampleCount,
            availableSamples
        );


    for (
        size_t i = 0;
        i < samplesToRead;
        i++
    )
    {
        destination[i] =
            ringBuffer[readIndex];


        readIndex++;


        if (
            readIndex
            >=
            RING_BUFFER_CAPACITY
        )
        {
            readIndex = 0;
        }
    }


    // Publish posisi read baru.
    ringReadIndex.store(
        readIndex,
        std::memory_order_release
    );


    return samplesToRead;
}


// ============================================================
// Stop Reader Thread
// ============================================================

void stopReaderThread()
{
    readerStopRequested.store(
        true,
        std::memory_order_release
    );


    if (
        readerThread.joinable()
    )
    {
        readerThread.join();
    }


    readerRunning.store(
        false,
        std::memory_order_release
    );
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
}


// ============================================================
// Reader Thread
// ============================================================

void readerThreadFunction()
{
    LOGI(
        "SPSC Reader Thread dimulai."
    );


    readerRunning.store(
        true,
        std::memory_order_release
    );


    // --------------------------------------------------------
    // Buka file WAV
    // --------------------------------------------------------

    FILE* file =
        fopen(
            wavFilePath.c_str(),
            "rb"
        );


    if (file == nullptr)
    {
        LOGE(
            "Reader gagal membuka WAV."
        );


        endOfFileReached.store(
            true,
            std::memory_order_release
        );


        readerRunning.store(
            false,
            std::memory_order_release
        );


        return;
    }


    // --------------------------------------------------------
    // Menuju awal PCM
    // --------------------------------------------------------

    if (
        fseek(
            file,
            wavDataOffset,
            SEEK_SET
        ) != 0
    )
    {
        LOGE(
            "Reader gagal menuju data PCM."
        );


        fclose(file);


        endOfFileReached.store(
            true,
            std::memory_order_release
        );


        readerRunning.store(
            false,
            std::memory_order_release
        );


        return;
    }


    // --------------------------------------------------------
    // Temporary read buffer
    // --------------------------------------------------------

    constexpr size_t READ_CHUNK_SAMPLES =
        4096;


    std::vector<int16_t> readBuffer(
        READ_CHUNK_SAMPLES
    );


    uint64_t totalSamplesRead = 0;


    // ========================================================
    // Streaming loop
    // ========================================================

    while (
        !readerStopRequested.load(
            std::memory_order_acquire
        )
    )
    {
        // ----------------------------------------------------
        // Cek ruang ring buffer
        // ----------------------------------------------------

        size_t freeSamples =
            getFreeSamples();


        if (freeSamples == 0)
        {
            // Buffer penuh.
            //
            // Reader bukan melakukan busy-loop
            // terus menerus.
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1)
            );

            continue;
        }


        size_t samplesToRead =
            std::min(
                freeSamples,
                READ_CHUNK_SAMPLES
            );


        // ----------------------------------------------------
        // Jangan membaca melewati data WAV
        // ----------------------------------------------------

        uint64_t bytesAlreadyRead =
            totalSamplesRead
            *
            sizeof(int16_t);


        if (
            bytesAlreadyRead
            >=
            wavDataSize
        )
        {
            break;
        }


        uint64_t bytesRemaining =
            wavDataSize
            -
            bytesAlreadyRead;


        size_t maxSamplesByFile =
            static_cast<size_t>(
                bytesRemaining
                /
                sizeof(int16_t)
            );


        if (
            samplesToRead
            >
            maxSamplesByFile
        )
        {
            samplesToRead =
                maxSamplesByFile;
        }


        if (samplesToRead == 0)
        {
            break;
        }


        // ----------------------------------------------------
        // Baca dari file
        // ----------------------------------------------------

        size_t samplesRead =
            fread(
                readBuffer.data(),
                sizeof(int16_t),
                samplesToRead,
                file
            );


        if (samplesRead == 0)
        {
            LOGE(
                "Reader gagal membaca PCM."
            );

            break;
        }


        // ----------------------------------------------------
        // Masukkan ke ring buffer
        // ----------------------------------------------------

        size_t offset = 0;


        while (
            offset < samplesRead
            &&
            !readerStopRequested.load(
                std::memory_order_acquire
            )
        )
        {
            size_t written =
                writeToRingBuffer(
                    readBuffer.data() + offset,
                    samplesRead - offset
                );


            offset += written;


            if (written == 0)
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1)
                );
            }
        }


        totalSamplesRead +=
            offset;
    }


    fclose(file);


    // ========================================================
    // Reader selesai
    // ========================================================

    endOfFileReached.store(
        true,
        std::memory_order_release
    );


    readerRunning.store(
        false,
        std::memory_order_release
    );


    LOGI(
        "SPSC Reader Thread selesai."
    );


    LOGI(
        "Total samples: %llu",
        static_cast<unsigned long long>(
            totalSamplesRead
        )
    );
}


// ============================================================
// WAV Parser
// ============================================================

bool loadWav(
    const char* path
)
{
    LOGI(
        "================================"
    );

    LOGI(
        "Membuka WAV:"
    );

    LOGI(
        "%s",
        path
    );

    LOGI(
        "================================"
    );


    // --------------------------------------------------------
    // Pastikan reader lama berhenti
    // --------------------------------------------------------

    stopReaderThread();


    resetRingBuffer();

    resetWavState();


    endOfFileReached.store(
        false,
        std::memory_order_release
    );


    playbackFinished.store(
        false,
        std::memory_order_release
    );


    // --------------------------------------------------------
    // Buka file
    // --------------------------------------------------------

    FILE* file =
        fopen(
            path,
            "rb"
        );


    if (file == nullptr)
    {
        LOGE(
            "Tidak bisa membuka WAV."
        );

        return false;
    }


    // --------------------------------------------------------
    // RIFF
    // --------------------------------------------------------

    char riff[4];


    if (
        fread(
            riff,
            1,
            4,
            file
        ) != 4
    )
    {
        LOGE(
            "Gagal membaca RIFF."
        );

        fclose(file);

        return false;
    }


    if (
        memcmp(
            riff,
            "RIFF",
            4
        ) != 0
    )
    {
        LOGE(
            "File bukan RIFF."
        );

        fclose(file);

        return false;
    }


    // File size
    uint32_t fileSize =
        readUint32(file);


    (void)fileSize;


    // --------------------------------------------------------
    // WAVE
    // --------------------------------------------------------

    char wave[4];


    if (
        fread(
            wave,
            1,
            4,
            file
        ) != 4
    )
    {
        LOGE(
            "Gagal membaca WAVE."
        );

        fclose(file);

        return false;
    }


    if (
        memcmp(
            wave,
            "WAVE",
            4
        ) != 0
    )
    {
        LOGE(
            "File bukan WAVE."
        );

        fclose(file);

        return false;
    }


    // --------------------------------------------------------
    // Parser state
    // --------------------------------------------------------

    bool foundFmt = false;

    bool foundData = false;


    uint16_t audioFormat = 0;

    uint16_t channels = 0;

    uint32_t sampleRate = 0;

    uint16_t bitsPerSample = 0;

    uint32_t dataSize = 0;

    long dataOffset = 0;


    // ========================================================
    // Cari chunk fmt dan data
    // ========================================================

    while (!feof(file))
    {
        char chunkId[4];


        if (
            fread(
                chunkId,
                1,
                4,
                file
            ) != 4
        )
        {
            break;
        }


        uint32_t chunkSize =
            readUint32(file);


        // ----------------------------------------------------
        // fmt
        // ----------------------------------------------------

        if (
            memcmp(
                chunkId,
                "fmt ",
                4
            ) == 0
        )
        {
            audioFormat =
                readUint16(file);


            channels =
                readUint16(file);


            sampleRate =
                readUint32(file);


            // Byte rate
            readUint32(file);


            // Block align
            readUint16(file);


            bitsPerSample =
                readUint16(file);


            // ------------------------------------------------
            // Extra fmt data
            // ------------------------------------------------

            if (chunkSize > 16)
            {
                long remaining =
                    static_cast<long>(
                        chunkSize - 16
                    );


                fseek(
                    file,
                    remaining,
                    SEEK_CUR
                );
            }


            // ------------------------------------------------
            // WAV chunk padding
            // ------------------------------------------------

            if (
                chunkSize & 1
            )
            {
                fseek(
                    file,
                    1,
                    SEEK_CUR
                );
            }


            foundFmt = true;
        }


        // ----------------------------------------------------
        // data
        // ----------------------------------------------------

        else if (
            memcmp(
                chunkId,
                "data",
                4
            ) == 0
        )
        {
            dataSize =
                chunkSize;


            dataOffset =
                ftell(file);


            // Lewati data sementara parser
            fseek(
                file,
                static_cast<long>(
                    chunkSize
                ),
                SEEK_CUR
            );


            // Padding RIFF
            if (
                chunkSize & 1
            )
            {
                fseek(
                    file,
                    1,
                    SEEK_CUR
                );
            }


            foundData = true;
        }


        // ----------------------------------------------------
        // Chunk lain
        // ----------------------------------------------------

        else
        {
            fseek(
                file,
                static_cast<long>(
                    chunkSize
                ),
                SEEK_CUR
            );


            if (
                chunkSize & 1
            )
            {
                fseek(
                    file,
                    1,
                    SEEK_CUR
                );
            }
        }


        if (
            foundFmt
            &&
            foundData
        )
        {
            break;
        }
    }


    // --------------------------------------------------------
    // Validasi
    // --------------------------------------------------------

    if (!foundFmt)
    {
        LOGE(
            "Chunk fmt tidak ditemukan."
        );

        fclose(file);

        return false;
    }


    if (!foundData)
    {
        LOGE(
            "Chunk data tidak ditemukan."
        );

        fclose(file);

        return false;
    }


    // ========================================================
    // WAV Information
    // ========================================================

    LOGI(
        "=============================="
    );

    LOGI(
        "WAV INFO"
    );

    LOGI(
        "Sample Rate : %u Hz",
        sampleRate
    );

    LOGI(
        "Channels    : %u",
        channels
    );

    LOGI(
        "Bit Depth   : %u bit",
        bitsPerSample
    );

    LOGI(
        "Audio Format: %u",
        audioFormat
    );

    LOGI(
        "Data Size   : %u bytes",
        dataSize
    );

    LOGI(
        "Data Offset : %ld",
        dataOffset
    );

    LOGI(
        "=============================="
    );


    // --------------------------------------------------------
    // PCM
    // --------------------------------------------------------

    if (
        audioFormat != 1
    )
    {
        LOGE(
            "WAV bukan PCM."
        );

        fclose(file);

        return false;
    }


    // --------------------------------------------------------
    // 16-bit
    // --------------------------------------------------------

    if (
        bitsPerSample != 16
    )
    {
        LOGE(
            "Versi ini hanya mendukung PCM 16-bit."
        );

        fclose(file);

        return false;
    }


    // --------------------------------------------------------
    // Channel
    // --------------------------------------------------------

    if (
        channels < 1
        ||
        channels > 2
    )
    {
        LOGE(
            "Versi ini hanya mendukung 1-2 channel."
        );

        fclose(file);

        return false;
    }


    // --------------------------------------------------------
    // Data harus valid
    // --------------------------------------------------------

    uint32_t bytesPerSample =
        bitsPerSample / 8;


    uint32_t bytesPerFrame =
        bytesPerSample
        *
        channels;


    if (
        bytesPerFrame == 0
    )
    {
        LOGE(
            "Bytes per frame tidak valid."
        );

        fclose(file);

        return false;
    }


    // ========================================================
    // Simpan metadata
    // ========================================================

    wavFilePath =
        path;


    wavSampleRate =
        static_cast<int32_t>(
            sampleRate
        );


    wavChannels =
        static_cast<int32_t>(
            channels
        );


    wavBitsPerSample =
        static_cast<int32_t>(
            bitsPerSample
        );


    wavAudioFormat =
        static_cast<int32_t>(
            audioFormat
        );


    wavDataOffset =
        dataOffset;


    wavDataSize =
        dataSize;


    wavTotalFrames =
        dataSize
        /
        bytesPerFrame;


    fclose(file);


    // ========================================================
    // Log metadata
    // ========================================================

    LOGI(
        "================================"
    );

    LOGI(
        "WAV METADATA TERSIMPAN"
    );

    LOGI(
        "Path        : %s",
        wavFilePath.c_str()
    );

    LOGI(
        "Sample Rate : %d Hz",
        wavSampleRate
    );

    LOGI(
        "Channels    : %d",
        wavChannels
    );

    LOGI(
        "Bit Depth   : %d bit",
        wavBitsPerSample
    );

    LOGI(
        "Format      : %d",
        wavAudioFormat
    );

    LOGI(
        "Data Offset : %ld",
        wavDataOffset
    );

    LOGI(
        "Data Size   : %u bytes",
        wavDataSize
    );

    LOGI(
        "Total Frame : %llu",
        static_cast<unsigned long long>(
            wavTotalFrames
        )
    );

    LOGI(
        "Ring Buffer : %zu samples",
        RING_BUFFER_CAPACITY
    );

    LOGI(
        "Ring Buffer : %zu bytes",
        RING_BUFFER_CAPACITY
        *
        sizeof(int16_t)
    );

    LOGI(
        "================================"
    );


    return true;
}


// ============================================================
// AAudio Callback
// ============================================================
//
// PENTING:
//
// Callback ini harus ringan.
//
// Tidak ada:
// - fread()
// - mutex
// - condition_variable
// - sleep
// - memory allocation
//
// ============================================================

aaudio_data_callback_result_t audioCallback(
    AAudioStream* stream,
    void* userData,
    void* audioData,
    int32_t numFrames
)
{
    auto* output =
        static_cast<int16_t*>(
            audioData
        );


    int32_t outputChannels =
        AAudioStream_getChannelCount(
            stream
        );


    size_t requestedSamples =
        static_cast<size_t>(
            numFrames
        )
        *
        static_cast<size_t>(
            outputChannels
        );


    // --------------------------------------------------------
    // Baca dari ring buffer
    // --------------------------------------------------------

    size_t samplesRead =
        readFromRingBuffer(
            output,
            requestedSamples
        );


    // --------------------------------------------------------
    // Underrun
    // --------------------------------------------------------

    if (
        samplesRead
        <
        requestedSamples
    )
    {
        std::memset(
            output + samplesRead,
            0,
            (
                requestedSamples
                -
                samplesRead
            )
            *
            sizeof(int16_t)
        );


        bool finished =
            endOfFileReached.load(
                std::memory_order_acquire
            );


        bool readerStillRunning =
            readerRunning.load(
                std::memory_order_acquire
            );


        size_t remainingSamples =
            getAvailableSamples();


        // ----------------------------------------------------
        // Kalau file sudah habis dan ring buffer kosong
        // ----------------------------------------------------

        if (
            finished
            &&
            !readerStillRunning
            &&
            remainingSamples == 0
        )
        {
            playbackFinished.store(
                true,
                std::memory_order_release
            );


            return AAUDIO_CALLBACK_RESULT_STOP;
        }
    }


    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}


// ============================================================
// AAudio Error Callback
// ============================================================

void errorCallback(
    AAudioStream* stream,
    void* userData,
    aaudio_result_t error
)
{
    LOGE(
        "AAudio error: %s",
        AAudio_convertResultToText(
            error
        )
    );
}


// ============================================================
// JNI - Play WAV
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
    // Pastikan playback lama berhenti
    // --------------------------------------------------------

    if (
        audioStream != nullptr
    )
    {
        AAudioStream_requestStop(
            audioStream
        );


        AAudioStream_close(
            audioStream
        );


        audioStream = nullptr;
    }


    stopReaderThread();


    resetRingBuffer();


    // --------------------------------------------------------
    // Ambil path
    // --------------------------------------------------------

    const char* filePath =
        env->GetStringUTFChars(
            path,
            nullptr
        );


    // --------------------------------------------------------
    // Parse WAV
    // --------------------------------------------------------

    bool loaded =
        loadWav(
            filePath
        );


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
    // State
    // --------------------------------------------------------

    readerStopRequested.store(
        false,
        std::memory_order_release
    );


    endOfFileReached.store(
        false,
        std::memory_order_release
    );


    playbackFinished.store(
        false,
        std::memory_order_release
    );


    resetRingBuffer();


    // ========================================================
    // AAudio Builder
    // ========================================================

    AAudioStreamBuilder* builder =
        nullptr;


    aaudio_result_t result =
        AAudio_createStreamBuilder(
            &builder
        );


    if (
        result != AAUDIO_OK
    )
    {
        LOGE(
            "Gagal membuat AAudio builder: %s",
            AAudio_convertResultToText(
                result
            )
        );


        return env->NewStringUTF(
            "Gagal membuat AAudio builder"
        );
    }


    // --------------------------------------------------------
    // Konfigurasi output
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


    // ========================================================
    // Open stream
    // ========================================================

    result =
        AAudioStreamBuilder_openStream(
            builder,
            &audioStream
        );


    AAudioStreamBuilder_delete(
        builder
    );


    if (
        result != AAUDIO_OK
    )
    {
        LOGE(
            "Gagal membuka AAudio: %s",
            AAudio_convertResultToText(
                result
            )
        );


        audioStream = nullptr;


        return env->NewStringUTF(
            "Gagal membuka AAudio"
        );
    }


    // ========================================================
    // Actual Audio Configuration
    // ========================================================

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
    // Cek channel
    // --------------------------------------------------------

    if (
        actualChannels != wavChannels
    )
    {
        LOGE(
            "Channel mismatch! WAV=%d Actual=%d",
            wavChannels,
            actualChannels
        );


        AAudioStream_close(
            audioStream
        );


        audioStream = nullptr;


        return env->NewStringUTF(
            "Jumlah channel tidak cocok"
        );
    }


    // ========================================================
    // Start Reader Thread
    // ========================================================

    readerThread =
        std::thread(
            readerThreadFunction
        );


    // ========================================================
    // Start AAudio
    // ========================================================

    result =
        AAudioStream_requestStart(
            audioStream
        );


    if (
        result != AAUDIO_OK
    )
    {
        LOGE(
            "Gagal start AAudio: %s",
            AAudio_convertResultToText(
                result
            )
        );


        readerStopRequested.store(
            true,
            std::memory_order_release
        );


        if (
            readerThread.joinable()
        )
        {
            readerThread.join();
        }


        AAudioStream_close(
            audioStream
        );


        audioStream = nullptr;


        return env->NewStringUTF(
            "Gagal menjalankan WAV"
        );
    }


    // ========================================================
    // Success
    // ========================================================

    LOGI(
        "================================"
    );

    LOGI(
        "SPSC RING BUFFER AKTIF"
    );

    LOGI(
        "Reader Thread : ON"
    );

    LOGI(
        "AAudio Callback: ON"
    );

    LOGI(
        "Lock-Free Path: ON"
    );

    LOGI(
        "WAV Playback  : ON"
    );

    LOGI(
        "================================"
    );


    return env->NewStringUTF(
        "WAV sedang diputar"
    );
}


// ============================================================
// JNI - Stop
// ============================================================

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_hires_1player_MainActivity_nativeStopAudio(
    JNIEnv* env,
    jobject /* this */
)
{
    // --------------------------------------------------------
    // Hentikan AAudio terlebih dahulu
    // --------------------------------------------------------

    if (
        audioStream != nullptr
    )
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
    // Hentikan reader
    // --------------------------------------------------------

    stopReaderThread();


    // --------------------------------------------------------
    // Reset
    // --------------------------------------------------------

    resetRingBuffer();


    resetWavState();


    readerStopRequested.store(
        false,
        std::memory_order_release
    );


    endOfFileReached.store(
        false,
        std::memory_order_release
    );


    playbackFinished.store(
        false,
        std::memory_order_release
    );


    LOGI(
        "Audio dihentikan."
    );


    return env->NewStringUTF(
        "Audio berhenti"
    );
}


// ============================================================
// JNI - Engine Status
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