/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/audio_stream.h"
#include "core/audio_codec.h"
#include <pthread.h>
#include "core/threads.h"
// codec2 system library has a weird include prefix
#if defined(PLATFORM_LINUX)
#include <codec2/codec2.h>
#else
#include "codec2.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "core/dsp.h"
#include "core/utils.h"
#include "hwconfig.h"
#ifdef CONFIG_DSTAR
#include "core/dstar_vocoder.h"
#endif
#if defined(CONFIG_DMR) || defined(CONFIG_YSF)
#include "core/dmr_vocoder.h"
#endif
#if defined(PLATFORM_C62) \
    && (defined(CONFIG_DSTAR) || defined(CONFIG_DMR) || defined(CONFIG_YSF))
#include "mbe_heap.h"
#endif

#if defined(CONFIG_DSTAR) || defined(CONFIG_DMR) || defined(CONFIG_YSF)
/*
 * Codec thread stack for the AMBE codecs: the mbelib-neo encoders need about
 * 35 KB on Cortex-M (20 KB for the parameter quantisation frame alone).
 */
#define AMBE_THREAD_STKSIZE 49152
#endif

/*
 * Frame queue capacity. DMR voice comes in bursts of three frames every
 * 60 ms: its decoder keeps a deeper queue as a jitter buffer.
 */
#define QUEUE_SIZE 4
#define DMR_QUEUE_SIZE 8
#define DMR_PREFILL 4 /* Frames queued before DMR playback starts */

/* YSF voice comes in frames of five every 100 ms */
#define YSF_QUEUE_SIZE 12
#define YSF_PREFILL 6 /* Frames queued before YSF playback starts */

#if defined(CONFIG_YSF)
#define BUF_SIZE YSF_QUEUE_SIZE
#elif defined(CONFIG_DMR)
#define BUF_SIZE DMR_QUEUE_SIZE
#else
#define BUF_SIZE QUEUE_SIZE
#endif

#define CODEC2_FRAME_SAMPLES 160 /* Samples per codec2 3200-mode frame */
#define DMA_BUF_SAMPLES (CODEC2_FRAME_SAMPLES * 2) /* Double-buffered DMA */

#if defined(CONFIG_DSTAR) || defined(CONFIG_DMR) || defined(CONFIG_YSF)
#define MAX_FRAME_SIZE 9 /* D-STAR AMBE and DMR AMBE+2 frames */
#else
#define MAX_FRAME_SIZE 8
#endif

#ifndef CONFIG_MIC_GAIN
#define CONFIG_MIC_GAIN 1
#endif

#ifndef CONFIG_MIC_OVERSAMPLE
#define CONFIG_MIC_OVERSAMPLE 1
#endif

static pathId audioPath;

static uint8_t initCnt = 0;
static bool running;

static bool reqStop;
static pthread_t codecThread;
static pthread_attr_t codecAttr;
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wakeup_cond = PTHREAD_COND_INITIALIZER;

static uint8_t readPos;
static uint8_t writePos;
static uint8_t numElements;
static uint8_t dataBuffer[BUF_SIZE][MAX_FRAME_SIZE];
static enum codecType curCodec;
static size_t frameSize;
static uint8_t queueSize;
#if defined(PLATFORM_C62) \
    && (defined(CONFIG_DSTAR) || defined(CONFIG_DMR) || defined(CONFIG_YSF))
static bool stackInPsram; // Codec thread stack allocated in PSRAM
#endif

static void *encodeFunc(void *arg);
static void *decodeFunc(void *arg);
static bool startThread(const pathId path, const enum codecType type,
                        void *(*func)(void *));
static void stopThread();

void codec_init()
{
    pthread_mutex_lock(&init_mutex);
    initCnt += 1;
    pthread_mutex_unlock(&init_mutex);

    if (initCnt > 0)
        return;

    running = false;
    readPos = 0;
    writePos = 0;
    numElements = 0;
    queueSize = QUEUE_SIZE;
}

void codec_terminate()
{
    pthread_mutex_lock(&init_mutex);
    initCnt -= 1;
    pthread_mutex_unlock(&init_mutex);

    if (initCnt > 0)
        return;

    if (running)
        stopThread();
}

bool codec_startEncode(const pathId path)
{
    return startThread(path, CODEC_CODEC2_3200, encodeFunc);
}

bool codec_startDecode(const pathId path)
{
    return startThread(path, CODEC_CODEC2_3200, decodeFunc);
}

bool codec_startEncodeType(const pathId path, const enum codecType type)
{
    return startThread(path, type, encodeFunc);
}

bool codec_startDecodeType(const pathId path, const enum codecType type)
{
    return startThread(path, type, decodeFunc);
}

void codec_stop(const pathId path)
{
    if (running == false)
        return;

    if (audioPath != path)
        return;

    stopThread();
}

bool codec_running()
{
    return running;
}

bool codec_queueEmpty(const pathId path)
{
    if ((running == false) || (audioPath != path))
        return true;

    return numElements == 0;
}

uint8_t codec_queuedFrames()
{
    pthread_mutex_lock(&data_mutex);
    uint8_t count = numElements;
    pthread_mutex_unlock(&data_mutex);

    return count;
}

int codec_popFrame(uint8_t *frame, const bool blocking)
{
    if (running == false)
        return -EPERM;

    uint8_t element[MAX_FRAME_SIZE];

    // No data available and non-blocking call: just return false.
    if ((numElements == 0) && (blocking == false))
        return -EAGAIN;

    // Blocking call: wait until some data is pushed
    pthread_mutex_lock(&data_mutex);
    while (numElements == 0) {
        pthread_cond_wait(&wakeup_cond, &data_mutex);
    }

    memcpy(element, dataBuffer[readPos], frameSize);
    readPos = (readPos + 1) % queueSize;
    numElements -= 1;
    pthread_mutex_unlock(&data_mutex);

    // Do memcpy after mutex unlock to reduce execution time spent inside the
    // critical section
    memcpy(frame, element, frameSize);

    return 0;
}

int codec_pushFrame(const uint8_t *frame, const bool blocking)
{
    if (running == false)
        return -EPERM;

    // Copy data to a temporary variable before mutex lock to reduce execution
    // time spent inside the critical section
    uint8_t element[MAX_FRAME_SIZE];
    memcpy(element, frame, frameSize);

    // No space available and non-blocking call: return
    if ((numElements >= queueSize) && (blocking == false))
        return -EAGAIN;

    // Blocking call: wait until there is some free space
    pthread_mutex_lock(&data_mutex);
    while (numElements >= queueSize) {
        pthread_cond_wait(&wakeup_cond, &data_mutex);
    }

    // There is free space, push data into the queue
    memcpy(dataBuffer[writePos], element, frameSize);
    writePos = (writePos + 1) % queueSize;
    numElements += 1;

    pthread_mutex_unlock(&data_mutex);
    return 0;
}

static void *encodeFunc(void *arg)
{
    streamId iStream;
    pathId iPath = *((pathId *)arg);
    struct CODEC2 *codec2 = NULL;
#ifdef CONFIG_DSTAR
    dstarEncoder_t *dstar = NULL;
#endif
#if defined(CONFIG_DMR) || defined(CONFIG_YSF)
    dmrEncoder_t *dmr = NULL;
#endif
    struct dcBlock dcBlock;
    struct voiceBand voiceBand;
    struct decimatorState decimator;

    // Allocate on the heap, as the stack isn't big enough for oversampling
    // above 2 or so.
    stream_sample_t *adcBuf = malloc(DMA_BUF_SAMPLES * CONFIG_MIC_OVERSAMPLE
                                     * sizeof(stream_sample_t));
    if (adcBuf == NULL)
        goto exit;

    iStream = audioStream_start(iPath, adcBuf,
                                DMA_BUF_SAMPLES * CONFIG_MIC_OVERSAMPLE,
                                8000 * CONFIG_MIC_OVERSAMPLE,
                                STREAM_INPUT | BUF_CIRC_DOUBLE);
    if (iStream < 0)
        goto exit;

    dsp_resetState(dcBlock);
    dsp_resetState(voiceBand);
    dsp_resetState(decimator);

    // Codec2 renders little of the upper band by itself, the AMBE codecs
    // all of it: band-limit their input as radios do
    bool bandLimit = (curCodec != CODEC_CODEC2_3200);
#ifdef CONFIG_DSTAR
    if (curCodec == CODEC_DSTAR_AMBE)
        dstar = dstarEncoder_create();
    else
#endif
#ifdef CONFIG_DMR
    if (curCodec == CODEC_DMR_AMBE)
        dmr = dmrEncoder_create();
    else
#endif
#ifdef CONFIG_YSF
    if (curCodec == CODEC_YSF_AMBE)
        dmr = dmrEncoder_create();
    else
#endif
        codec2 = codec2_create(CODEC2_MODE_3200);

    while (reqStop == false) {
        // Invalid audio path, quit
        if (audioPath_getStatus(iPath) != PATH_OPEN)
            break;

        dataBlock_t audio = inputStream_getData(iStream);
        if (audio.data == NULL || audio.len == 0)
            break;

        // Each inputStream_getData call returns exactly
        // CODEC2_FRAME_SAMPLES * CONFIG_MIC_OVERSAMPLE raw samples
        // (one half of the double-buffered DMA transfer).  Decimate
        // by averaging each group of CONFIG_MIC_OVERSAMPLE samples.
        size_t outIdx = 0;
        for (size_t i = 0; i < audio.len; i++) {
            int16_t sample = audio.data[i];

#if CONFIG_MIC_OVERSAMPLE > 1
            if (!dsp_decimator(&decimator, &sample, CONFIG_MIC_OVERSAMPLE))
                continue;
#endif

            sample = dsp_dcBlockFilter(&dcBlock, sample);
            if (bandLimit)
                sample = dsp_voiceBandFilter(&voiceBand, sample);
            audio.data[outIdx++] = s16_mulSat(sample, CONFIG_MIC_GAIN);
        }

        // Encode 160 speech samples (20 ms) into a frame: 8 bytes for CODEC2,
        // 9 bytes for D-STAR AMBE and DMR AMBE+2.
        uint8_t frame[MAX_FRAME_SIZE] = { 0 };
#ifdef CONFIG_DSTAR
        if (dstar != NULL)
            dstarEncoder_encode(dstar, audio.data, frame);
        else
#endif
#ifdef CONFIG_YSF
        if ((dmr != NULL) && (curCodec == CODEC_YSF_AMBE))
            dmrEncoder_encodeParams(dmr, audio.data, frame);
        else
#endif
#ifdef CONFIG_DMR
        if (dmr != NULL)
            dmrEncoder_encode(dmr, audio.data, frame);
        else
#endif
        if (codec2 != NULL)
            codec2_encode(codec2, frame, audio.data);

        pthread_mutex_lock(&data_mutex);

        // If buffer is full erase the oldest frame
        if (numElements >= queueSize) {
            readPos = (readPos + 1) % queueSize;
        }

        memcpy(dataBuffer[writePos], frame, frameSize);
        writePos = (writePos + 1) % queueSize;

        if (numElements == 0)
            pthread_cond_signal(&wakeup_cond);

        if (numElements < queueSize)
            numElements += 1;

        pthread_mutex_unlock(&data_mutex);
    }

    audioStream_terminate(iStream);
    if (codec2 != NULL)
        codec2_destroy(codec2);
#ifdef CONFIG_DSTAR
    dstarEncoder_destroy(dstar);
#endif
#if defined(CONFIG_DMR) || defined(CONFIG_YSF)
    dmrEncoder_destroy(dmr);
#endif

exit:
    free(adcBuf);

    // In case thread terminates due to invalid path or stream error, detach it
    // to ensure that its memory gets freed by the OS.
    if (reqStop == false)
        pthread_detach(pthread_self());

    running = false;
    return NULL;
}

static void *decodeFunc(void *arg)
{
    streamId oStream;
    pathId oPath = *((pathId *)arg);
    int16_t audioBuf[DMA_BUF_SAMPLES];
    struct CODEC2 *codec2 = NULL;
#ifdef CONFIG_DSTAR
    dstarDecoder_t *dstar = NULL;
#endif
#if defined(CONFIG_DMR) || defined(CONFIG_YSF)
    dmrDecoder_t *dmr = NULL;
#endif

    // Open output stream
    memset(audioBuf, 0x00, DMA_BUF_SAMPLES * sizeof(stream_sample_t));
    oStream = audioStream_start(oPath, audioBuf, DMA_BUF_SAMPLES, 8000,
                                STREAM_OUTPUT | BUF_CIRC_DOUBLE);
    if (oStream < 0) {
        pthread_detach(pthread_self());
        running = false;
        return NULL;
    }

#ifdef CONFIG_DSTAR
    if (curCodec == CODEC_DSTAR_AMBE)
        dstar = dstarDecoder_create();
    else
#endif
#ifdef CONFIG_DMR
    if (curCodec == CODEC_DMR_AMBE)
        dmr = dmrDecoder_create();
    else
#endif
#ifdef CONFIG_YSF
    if (curCodec == CODEC_YSF_AMBE)
        dmr = dmrDecoder_create();
    else
#endif
        codec2 = codec2_create(CODEC2_MODE_3200);

    // Ensure that thread start is correctly synchronized with the output
    // stream to avoid having the decode function writing in a memory area
    // being read at the same time by the output stream system causing cracking
    // noises at speaker output. Behaviour observed on both Module17 and MD-UV380
    outputStream_sync(oStream, false);

    // DMR and YSF playback waits for a few queued frames, see DMR_PREFILL
    // and YSF_PREFILL
    size_t prefill = 0;
#ifdef CONFIG_DMR
    if (curCodec == CODEC_DMR_AMBE)
        prefill = DMR_PREFILL;
#endif
#ifdef CONFIG_YSF
    if (curCodec == CODEC_YSF_AMBE)
        prefill = YSF_PREFILL;
#endif
    bool playing = (prefill == 0);

    while (reqStop == false) {
        // Invalid path, quit
        if (audioPath_getStatus(oPath) != PATH_OPEN)
            break;

        // Try popping data from the queue
        uint8_t frame[MAX_FRAME_SIZE];
        bool newData = false;

        pthread_mutex_lock(&data_mutex);

        // Restart the jitter buffer when it runs empty
        if (prefill != 0) {
            if (numElements == 0)
                playing = false;
            else if (numElements >= prefill)
                playing = true;
        }

        if (playing && (numElements != 0)) {
            memcpy(frame, dataBuffer[readPos], frameSize);
            readPos = (readPos + 1) % queueSize;
            if (numElements >= queueSize)
                pthread_cond_signal(&wakeup_cond);

            numElements -= 1;
            newData = true;
        }

        pthread_mutex_unlock(&data_mutex);

        stream_sample_t *audioBuf = outputStream_getIdleBuffer(oStream);
        if (audioBuf == NULL)
            break;

        if (newData) {
#ifdef CONFIG_DSTAR
            if (dstar != NULL)
                dstarDecoder_decode(dstar, frame, audioBuf);
            else
#endif
#ifdef CONFIG_YSF
            if ((dmr != NULL) && (curCodec == CODEC_YSF_AMBE))
                dmrDecoder_decodeParams(dmr, frame, audioBuf);
            else
#endif
#ifdef CONFIG_DMR
            if (dmr != NULL)
                dmrDecoder_decode(dmr, frame, audioBuf);
            else
#endif
            if (codec2 != NULL)
                codec2_decode(codec2, audioBuf, frame);
            else
                memset(audioBuf, 0x00,
                       CODEC2_FRAME_SAMPLES * sizeof(int16_t));

#ifdef PLATFORM_MD3x0
            // Bump up volume a little bit, as on MD3x0 is quite low
            for (size_t i = 0; i < CODEC2_FRAME_SAMPLES; i++)
                audioBuf[i] *= 2;
#endif

        } else {
            memset(audioBuf, 0x00, CODEC2_FRAME_SAMPLES * sizeof(int16_t));
        }

        outputStream_sync(oStream, true);
    }

    // Stop stream and wait until its effective termination
    audioStream_stop(oStream);
    if (codec2 != NULL)
        codec2_destroy(codec2);
#ifdef CONFIG_DSTAR
    dstarDecoder_destroy(dstar);
#endif
#if defined(CONFIG_DMR) || defined(CONFIG_YSF)
    dmrDecoder_destroy(dmr);
#endif

    // In case thread terminates due to invalid path or stream error, detach it
    // to ensure that its memory gets freed by the OS.
    if (reqStop == false)
        pthread_detach(pthread_self());

    running = false;
    return NULL;
}

static bool startThread(const pathId path, const enum codecType type,
                        void *(*func)(void *))
{
    // Bad incoming path
    if (audioPath_getStatus(path) != PATH_OPEN)
        return false;

    // Handle access contention when starting the codec thread to ensure that
    // only one call at a time can effectively start the thread.
    pthread_mutex_lock(&init_mutex);
    if (running) {
        // Same path and codec as before, path open, codec already running: all
        // good.
        if ((path == audioPath) && (type == curCodec)) {
            pthread_mutex_unlock(&init_mutex);
            return true;
        }

        // Same path, different codec: restart with the new one
        pathInfo_t newPath = audioPath_getInfo(path);
        pathInfo_t curPath = audioPath_getInfo(audioPath);
        if (path == audioPath)
            curPath.status = PATH_CLOSED;

        // New path takes over the current one only if it has an higher priority
        // or the current one is closed/suspended.
        if ((curPath.status == PATH_OPEN) && (curPath.prio >= newPath.prio)) {
            pthread_mutex_unlock(&init_mutex);
            return false;
        } else {
            stopThread();
        }
    }

    running = true;
    audioPath = path;
    curCodec = type;
    queueSize = QUEUE_SIZE;
#ifdef CONFIG_DSTAR
    if (type == CODEC_DSTAR_AMBE)
        frameSize = DSTAR_VOCODER_FRAME_BYTES;
    else
#endif
#ifdef CONFIG_DMR
    if (type == CODEC_DMR_AMBE) {
        frameSize = DMR_VOCODER_FRAME_BYTES;
        queueSize = DMR_QUEUE_SIZE;
    } else
#endif
#ifdef CONFIG_YSF
    if (type == CODEC_YSF_AMBE) {
        frameSize = DMR_VOCODER_PARAMS_BYTES;
        queueSize = YSF_QUEUE_SIZE;
    } else
#endif
        frameSize = 8;
    pthread_mutex_unlock(&init_mutex);

    readPos = 0;
    writePos = 0;
    numElements = 0;
    reqStop = false;

    pthread_attr_init(&codecAttr);

    size_t stackSize = CODEC2_THREAD_STKSIZE;
#if defined(CONFIG_DSTAR) || defined(CONFIG_DMR) || defined(CONFIG_YSF)
    if (type != CODEC_CODEC2_3200)
        stackSize = AMBE_THREAD_STKSIZE;
#endif
    (void)stackSize;

#if defined(_MIOSIX)
    // Set stack size of CODEC2 thread to 16kB, more for the AMBE codecs.
    pthread_attr_setstacksize(&codecAttr, stackSize);

    // Set priority of CODEC2 thread to the maximum one, the same of RTX thread.
    struct sched_param param;
    param.sched_priority = THREAD_PRIO_HIGH;
    pthread_attr_setschedparam(&codecAttr, &param);
#elif defined(__ZEPHYR__)
    // Allocate and set the stack for CODEC2 thread. The large stack of the
    // AMBE codecs goes to PSRAM on the C62, where SRAM is too small for it.
    void *codec_thread_stack;
#if defined(PLATFORM_C62) \
    && (defined(CONFIG_DSTAR) || defined(CONFIG_DMR) || defined(CONFIG_YSF))
    stackInPsram = (stackSize != CODEC2_THREAD_STKSIZE);
    if (stackInPsram)
        codec_thread_stack = mbe_heap_aligned_alloc(64, stackSize);
    else
#endif
        codec_thread_stack = malloc(stackSize * sizeof(uint8_t));

    if (codec_thread_stack == NULL) {
        running = false;
        return false;
    }

    pthread_attr_setstack(&codecAttr, codec_thread_stack, stackSize);
#endif

    // Start thread
    int ret = pthread_create(&codecThread, &codecAttr, func, &audioPath);
    if (ret < 0)
        running = false;

    return running;
}

static void stopThread()
{
    reqStop = true;
    pthread_join(codecThread, NULL);
    running = false;

#ifdef __ZEPHYR__
    void *addr;
    size_t size;

    pthread_attr_getstack(&codecAttr, &addr, &size);
#if defined(PLATFORM_C62) \
    && (defined(CONFIG_DSTAR) || defined(CONFIG_DMR) || defined(CONFIG_YSF))
    if (stackInPsram)
        mbe_heap_free(addr);
    else
#endif
        free(addr);
#endif
}
