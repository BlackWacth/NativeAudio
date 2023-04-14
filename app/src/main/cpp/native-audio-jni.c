#include <assert.h>
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

// for native asset manager
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <sys/types.h>

#define UNUSED(x) (void)(x);

static const char hello[] =
#include "hello_clip.h"
        ;
//这种写法不对，#include "android_clip.h" 不是一个常量表达式，因为它的值只有在编译时才能确定。
//static const char android[] = #include "android_clip.h";

//在C++中，#include是一个预处理指令，它告诉编译器在编译时将指定的头文件包含到程序中。
// 当你在头文件中定义一个静态常量字符数组时，你需要使用一个常量表达式来初始化它。
// 常量表达式是指值不会改变并且在编译时就可以计算出来的表达式。
// 因此，如果你想在头文件中定义静态常量字符数组，你需要使用一个常量表达式来初始化它。
//
//换行符只是用来分隔代码的一种方式，它不会影响程序的语义。
// 因此，当你在头文件中定义静态常量字符数组时，你可以使用换行符来分隔代码行，以使代码更易于阅读和维护。
static const char android[] =
#include "android_clip.h"
        ;
// engine interfaces
static SLObjectItf engineObject = NULL;
static SLEngineItf enginEngine;

// output mix interfaces
static SLObjectItf outputMixObject = NULL;
static SLEnvironmentalReverbItf outputMixEnvironmentalReverb = NULL;

const SLEnvironmentalReverbSettings reverbSettings = SL_I3DL2_ENVIRONMENT_PRESET_STONECORRIDOR;

static SLmilliHertz bqPlayerSampleRate = 0;
static jint bqPlayerBufSize = 0;
static SLObjectItf bqPlayerObject = NULL;
static SLPlayItf bqPlayerPlay;
static SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;
static SLEffectSendItf bqPlayerEffectSend;
static SLVolumeItf bqPlayerVolume;
static short *resampleBuf = NULL;

//要排队的下一个播放器缓冲区的指针和大小，以及剩余缓冲区的数量
static short *nextBuffer;
static unsigned nextSize;
static int nextCount;

static pthread_mutex_t audioEngineLock = PTHREAD_MUTEX_INITIALIZER;

static SLObjectItf fdPlayerObject = NULL;
static SLPlayItf fdPlayerPlay;
static SLSeekItf fdPlayerSeek;
static SLMuteSoloItf fdPlayerMuteSolo;
static SLVolumeItf fdPlayerVolume;

// URI player interfaces
static SLObjectItf uriPlayerObject = NULL;
static SLPlayItf uriPlayerPlay;
static SLSeekItf uriPlayerSeek;
static SLMuteSoloItf uriPlayerMuteSolo;
static SLVolumeItf uriPlayerVolume;

static SLMuteSoloItf bqPlayerMuteSolo;

// recorder interfaces
static SLObjectItf recorderObject = NULL;
static SLRecordItf recorderRecord;
static SLAndroidSimpleBufferQueueItf recorderBufferQueue;

// synthesized sawtooth clip
#define SAWTOOTH_FRAMES 8000
static short sawtoothBuffer[SAWTOOTH_FRAMES];

// 以 16 kHz 单声道、16 位带符号小端序录制的 5 秒音频
#define RECORDER_FRAMES (16000 * 5)
static short recorderBuffer[RECORDER_FRAMES];
static unsigned recorderSize = 0;

//这段代码是在函数onDlOpen上面添加了一个特殊的属性__attribute__((constructor))，这个属性表示在函数初始化时会自动执行这个函数。
// 因此，这个函数会在程序启动时自动执行。
__attribute__((constructor)) static void onDlOpen(void) {
    unsigned i;
    for (i = 0; i < SAWTOOTH_FRAMES; ++i) {
        sawtoothBuffer[i] = 32768 - ((i % 100) * 660);
    }
}

short *createResampleBuf(uint32_t idx, uint32_t srcRate, unsigned *size);

void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    assert(bq == recorderBufferQueue);
    assert(NULL == context);
    SLresult result;
    result = (*recorderRecord)->SetRecordState(recorderRecord, SL_RECORDSTATE_STOPPED);
    if (result == SL_RESULT_SUCCESS) {
        recorderSize = RECORDER_FRAMES * sizeof(short);
    }
    pthread_mutex_unlock(&audioEngineLock);
}

void releaseResampleBuf() {
    if (0 == bqPlayerSampleRate) {
        return;
    }
    free(resampleBuf);
    resampleBuf = NULL;
}

void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    assert(bq == bqPlayerBufferQueue);
    assert(NULL == context);
    //对于流式播放，请将此测试替换为逻辑以查找并填充下一个缓冲区
    if (--nextCount > 0 && nextBuffer != NULL && nextSize) {
        SLresult result;
        result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, nextBuffer, nextSize);
        if (result != SL_RESULT_SUCCESS) {
            pthread_mutex_unlock(&audioEngineLock);
        }
        UNUSED(result)
    } else {
        releaseResampleBuf();
        pthread_mutex_unlock(&audioEngineLock);
    }
}

JNIEXPORT void JNICALL
Java_com_hzw_nativeaudio_MainActivity_createEngine(JNIEnv *env, jobject thiz) {
    SLresult result;

    // slCreateEngine 是 OpenSL ES 中的一个函数，用于创建一个引擎对象.
    // 第一个参数是指向引擎对象的指针，第二个参数是选项数目，第三个参数是选项数组，第四个参数是接口数目，第五个参数是接口数组，第六个参数是接口是否必须的标志数组。
    // 详细介绍：https://juejin.cn/post/7031848037311840293
    result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    assert(SL_RESULT_SUCCESS == result);
    // 在这段代码中，(void) result; 的作用是告诉编译器，我们不需要使用result变量的值，这样可以避免编译器产生未使用变量的警告。
    // 这种方法比使用ignore_unused更加清晰明了，因为它直接告诉编译器我们不需要使用这个变量的值
    // 也可以定义宏 #define UNUSED(x) (void)(x);
    (void) result;

    //实例化一个对象
    //第一个参数是指向对象的指针，第二个参数是异步标志。
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    //获取引擎对象接口
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &enginEngine);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    //用于创建一个输出混音器对象, 该对象可以将多个音频流混合到一起并输出到设备的音频输出端口。
    const SLInterfaceID ids[1] = {SL_IID_ENVIRONMENTALREVERB};
    const SLboolean req[1] = {SL_BOOLEAN_FALSE};
    result = (*enginEngine)->CreateOutputMix(enginEngine, &outputMixObject, 1, ids, req);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    result = (*outputMixObject)->GetInterface(outputMixObject, SL_IID_ENVIRONMENTALREVERB,
                                              &outputMixEnvironmentalReverb);
    if (SL_RESULT_SUCCESS == result) {
        //用于设置环境混响效果的参数。
        //它可以设置混响的房间大小、混响的时间、混响的强度等参数，从而实现不同的混响效果
        result = (*outputMixEnvironmentalReverb)->SetEnvironmentalReverbProperties(
                outputMixEnvironmentalReverb, &reverbSettings);
        UNUSED(result)
    }
}

JNIEXPORT void JNICALL
Java_com_hzw_nativeaudio_MainActivity_createBufferQueueAudioPlayer(JNIEnv *env, jobject thiz,
                                                                   jint sampleRate, jint bufSize) {
    SLresult result;
    if (sampleRate >= 0 && bufSize >= 0) {
        bqPlayerSampleRate = sampleRate * 1000;
        //设备本机缓冲区大小是最小化音频延迟的另一个因素，此示例中未使用：我们在这里只播放一个巨大的缓冲区
        bqPlayerBufSize = bufSize;
    }

    //配置音频源
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2
    };

    SLDataFormat_PCM format_pcm = {
            SL_DATAFORMAT_PCM, 1,
            SL_SAMPLINGRATE_8, SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_CENTER,
            SL_BYTEORDER_LITTLEENDIAN
    };

    //尽可能启用快速音频：一旦我们将相同的速率设置为本机，将触发快速音频路径
    if (bqPlayerSampleRate) {
        format_pcm.samplesPerSec = bqPlayerSampleRate;
    }
    SLDataSource audioSrc = {
            &loc_bufq, &format_pcm
    };
    //配置音频接收器
    SLDataLocator_OutputMix loc_outmix = {
            SL_DATALOCATOR_OUTPUTMIX, outputMixObject
    };
    SLDataSink audioSnk = {
            &loc_outmix, NULL
    };

    //创建音频播放器：快速音频在需要SL_IID_EFFECTSEND时不支持，跳过它以获得快速音频案例
    const SLInterfaceID ids[3] = {
            SL_IID_BUFFERQUEUE, SL_IID_VOLUME, SL_IID_EFFECTSEND,
    };
    const SLboolean req[3] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE,};

    result = (*enginEngine)->CreateAudioPlayer(enginEngine, &bqPlayerObject, &audioSrc, &audioSnk,
                                               bqPlayerSampleRate ? 2 : 3, ids, req);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE,
                                             &bqPlayerBufferQueue);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    result = (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, bqPlayerCallback, NULL);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    bqPlayerEffectSend = NULL;
    if (bqPlayerSampleRate == 0) {
        result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_EFFECTSEND, &bqPlayerEffectSend);
        assert(SL_RESULT_SUCCESS == result);
        UNUSED(result)
    }

#if 0  \
    // 已知为单声道的源不支持mutesolo，因为这是获取mutesolo接口
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_MUTESOLO, &bqPlayerMuteSolo);
    assert(SL_RESULT_SUCCESS == result);
    (void)result;
#endif

    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME, &bqPlayerVolume);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    // 将玩家的状态设置为正在播放
    result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)
}

JNIEXPORT jboolean JNICALL
Java_com_hzw_nativeaudio_MainActivity_createAssetAudioPlayer(JNIEnv *env, jobject thiz,
                                                             jobject assetManager,
                                                             jstring filename) {
    SLresult result;
    const char *utf8 = (*env)->GetStringUTFChars(env, filename, NULL);
    assert(utf8 != NULL);

    AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);
    assert(mgr != NULL);
    AAsset *asset = AAssetManager_open(mgr, utf8, AASSET_MODE_UNKNOWN);
    (*env)->ReleaseStringUTFChars(env, filename, utf8);

    if (asset == NULL) {
        return JNI_FALSE;
    }

    off_t start, length;
    int fd = AAsset_openFileDescriptor(asset, &start, &length);
    assert(0 <= fd);
    AAsset_close(asset);


    // 配置音频源
    SLDataLocator_AndroidFD loc_fd = {SL_DATALOCATOR_ANDROIDFD, fd, start, length};
    SLDataFormat_MIME format_mime = {SL_DATAFORMAT_MIME, NULL, SL_CONTAINERTYPE_UNSPECIFIED};
    SLDataSource audioSrc = {&loc_fd, &format_mime};
    // 配置音频接收器
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataSink audioSnk = {&loc_outmix, NULL};

    // 创建音频播放器
    const SLInterfaceID ids[3] = {SL_IID_SEEK, SL_IID_MUTESOLO, SL_IID_VOLUME};
    const SLboolean req[3] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    result = (*enginEngine)->CreateAudioPlayer(enginEngine, &fdPlayerObject, &audioSrc, &audioSnk,
                                               3, ids, req);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    // 实例化播放器
    result = (*fdPlayerObject)->Realize(fdPlayerObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    // 获取播放器接口
    result = (*fdPlayerObject)->GetInterface(fdPlayerObject, SL_IID_PLAY, &fdPlayerPlay);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    // 获取Seek接口
    result = (*fdPlayerObject)->GetInterface(fdPlayerObject, SL_IID_SEEK, &fdPlayerSeek);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    // 获取静音接口
    result = (*fdPlayerObject)->GetInterface(fdPlayerObject, SL_IID_MUTESOLO, &fdPlayerMuteSolo);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    // 获取音量接口
    result = (*fdPlayerObject)->GetInterface(fdPlayerObject, SL_IID_VOLUME, &fdPlayerVolume);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    // 启用整个文件循环
    result = (*fdPlayerSeek)->SetLoop(fdPlayerSeek, SL_BOOLEAN_TRUE, 0, SL_TIME_UNKNOWN);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_hzw_nativeaudio_MainActivity_setPlayingAssetAudioPlayer(JNIEnv *env, jobject thiz,
                                                                 jboolean isPlaying) {
    SLresult result;

    // 确保Asset音频播放器已创建
    if (NULL != fdPlayerPlay) {
        // 设置播放器播放和暂停状态
        result = (*fdPlayerPlay)->SetPlayState(fdPlayerPlay, isPlaying ? SL_PLAYSTATE_PLAYING
                                                                       : SL_PLAYSTATE_PAUSED);
        assert(SL_RESULT_SUCCESS == result);
        UNUSED(result)
    }
}

JNIEXPORT jboolean JNICALL
Java_com_hzw_nativeaudio_MainActivity_createUriAudioPlayer(JNIEnv *env, jobject thiz, jstring uri) {
    SLresult result;

    // 将 Java 字符串转换为 UTF-8
    const char *utf8 = (*env)->GetStringUTFChars(env, uri, NULL);
    assert(NULL != utf8);

    // 配置音频源 需要网络访问权限
    SLDataLocator_URI loc_uri = {SL_DATALOCATOR_URI, (SLchar *) utf8};
    SLDataFormat_MIME format_mime = {SL_DATAFORMAT_MIME, NULL, SL_CONTAINERTYPE_UNSPECIFIED};
    SLDataSource audioSrc = {&loc_uri, &format_mime};

    // 配置音频接收器
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataSink audioSnk = {&loc_outmix, NULL};

    // 创建音频播放器
    const SLInterfaceID ids[3] = {SL_IID_SEEK, SL_IID_MUTESOLO, SL_IID_VOLUME};
    const SLboolean req[3] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    result = (*enginEngine)->CreateAudioPlayer(enginEngine, &uriPlayerObject, &audioSrc, &audioSnk,
                                               3, ids, req);
    // 请注意，此处未检测到无效的 URI，但在 Android 上的准备预取期间，或者可能在其他平台上的实现期间检测到无效的 URI。
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    // 释放 Java 字符串和 UTF-8
    (*env)->ReleaseStringUTFChars(env, uri, utf8);

    // 实例化播放器对象
    result = (*uriPlayerObject)->Realize(uriPlayerObject, SL_BOOLEAN_FALSE);
    // 这在 Android 上总是会成功，但我们检查结果是否可以移植到其他平台
    if (SL_RESULT_SUCCESS != result) {
        (*uriPlayerObject)->Destroy(uriPlayerObject);
        uriPlayerObject = NULL;
        return JNI_FALSE;
    }

    // 获取播放器接口
    result = (*uriPlayerObject)->GetInterface(uriPlayerObject, SL_IID_PLAY, &uriPlayerPlay);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    // 获取Seek接口
    result = (*uriPlayerObject)->GetInterface(uriPlayerObject, SL_IID_SEEK, &uriPlayerSeek);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    // 获取静音接口
    result = (*uriPlayerObject)->GetInterface(uriPlayerObject, SL_IID_MUTESOLO, &uriPlayerMuteSolo);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    // 获取音量接口
    result = (*uriPlayerObject)->GetInterface(uriPlayerObject, SL_IID_VOLUME, &uriPlayerVolume);
    assert(SL_RESULT_SUCCESS == result);
    UNUSED(result)

    return JNI_TRUE;
}

void checkResult(SLresult *result) {
    assert(*result == SL_RESULT_SUCCESS);
    UNUSED(*result)
}

JNIEXPORT void JNICALL
Java_com_hzw_nativeaudio_MainActivity_setPlayingUriAudioPlayer(JNIEnv *env, jobject thiz,
                                                               jboolean isPlaying) {
    SLresult result;
    if (uriPlayerPlay != NULL) {
        result = (*uriPlayerPlay)->SetPlayState(uriPlayerPlay, isPlaying ? SL_PLAYSTATE_PLAYING
                                                                         : SL_PLAYSTATE_PAUSED);
        checkResult(&result);
    }
}

JNIEXPORT void JNICALL
Java_com_hzw_nativeaudio_MainActivity_setLoopingUriAudioPlayer(JNIEnv *env, jobject thiz,
                                                               jboolean isLooping) {
    SLresult result;
    if (uriPlayerPlay != NULL) {
        result = (*uriPlayerSeek)->SetLoop(uriPlayerSeek, (SLboolean) isLooping, 0,
                                           SL_TIME_UNKNOWN);
        checkResult(&result);
    }
}

static SLMuteSoloItf getMuteSolo() {
    if (uriPlayerMuteSolo != NULL) {
        return uriPlayerMuteSolo;
    } else if (fdPlayerMuteSolo != NULL) {
        return fdPlayerMuteSolo;
    } else {
        return bqPlayerMuteSolo;
    }
}

JNIEXPORT void JNICALL
Java_com_hzw_nativeaudio_MainActivity_setChannelMuteUriAudioPlayer(JNIEnv *env, jobject thiz,
                                                                   jint chan, jboolean mute) {
    SLresult result;
    SLMuteSoloItf muteSolo = getMuteSolo();
    if (muteSolo != NULL) {
        result = (*muteSolo)->SetChannelMute(muteSolo, chan, mute);
        checkResult(&result);
    }
}

JNIEXPORT void JNICALL
Java_com_hzw_nativeaudio_MainActivity_setChannelSoloUriAudioPlayer(JNIEnv *env, jobject thiz,
                                                                   jint chan, jboolean solo) {
    SLresult result;
    SLMuteSoloItf muteSolo = getMuteSolo();
    if (muteSolo != NULL) {
        result = (*muteSolo)->SetChannelSolo(muteSolo, chan, solo);
        checkResult(&result);
    }
}

JNIEXPORT jint JNICALL
Java_com_hzw_nativeaudio_MainActivity_getNumChannelsUriAudioPlayer(JNIEnv *env, jobject thiz) {
    SLuint8 numChannels;
    SLresult result;
    SLMuteSoloItf muteSolo = getMuteSolo();
    if (muteSolo != NULL) {
        result = (*muteSolo)->GetNumChannels(muteSolo, &numChannels);
        if (result == SL_RESULT_PRECONDITIONS_VIOLATED) {
            numChannels = 0;
        } else {
            assert(result == SL_RESULT_SUCCESS);
        }
    } else {
        numChannels = 0;
    }
    return numChannels;
}

static SLVolumeItf getVolume() {
    if (uriPlayerVolume != NULL) {
        return uriPlayerVolume;
    } else if (fdPlayerVolume != NULL) {
        return fdPlayerVolume;
    } else {
        return bqPlayerVolume;
    }
}

JNIEXPORT void JNICALL
Java_com_hzw_nativeaudio_MainActivity_setVolumeUriAudioPlayer(JNIEnv *env, jobject thiz,
                                                              jint millibel) {
    SLresult result;
    SLVolumeItf volume = getVolume();
    if (volume != NULL) {
        result = (*volume)->SetVolumeLevel(volume, millibel);
        checkResult(&result);
    }
}

JNIEXPORT void JNICALL
Java_com_hzw_nativeaudio_MainActivity_setMuteUriAudioPlayer(JNIEnv *env, jobject thiz,
                                                            jboolean mute) {
    SLresult result;
    SLVolumeItf volume = getVolume();
    if (volume != NULL) {
        result = (*volume)->SetMute(volume, mute);
        checkResult(&result);
    }
}

JNIEXPORT void JNICALL
Java_com_hzw_nativeaudio_MainActivity_enableStereoPositionUriAudioPlayer(JNIEnv *env, jobject thiz,
                                                                         jboolean enable) {
    SLresult result;
    SLVolumeItf volume = getVolume();
    if (volume != NULL) {
        result = (*volume)->EnableStereoPosition(volume, enable);
        checkResult(&result);
    }
}

JNIEXPORT void JNICALL
Java_com_hzw_nativeaudio_MainActivity_setStereoPositionUriAudioPlayer(JNIEnv *env, jobject thiz,
                                                                      jint permille) {
    SLresult result;
    SLVolumeItf volume = getVolume();
    if (volume != NULL) {
        result = (*volume)->SetStereoPosition(volume, permille);
        checkResult(&result);
    }
}

JNIEXPORT short *createResampleBuf(uint32_t idx, uint32_t srcRate, unsigned *size) {
    short *src = NULL;
    short *workBuf;
    int upSampleRate;
    int32_t srcSampleCount = 0;

    if (bqPlayerSampleRate == 0) {
        return NULL;
    }

    if (bqPlayerSampleRate % srcRate) {
        return NULL;
    }

    upSampleRate = bqPlayerSampleRate / srcRate;

    switch (idx) {
        case 0:
            return NULL;

        case 1:
            srcSampleCount = sizeof(hello) >> 1;
            src = (short *) hello;
            break;

        case 2:
            srcSampleCount = sizeof(android) >> 1;
            src = (short *) android;
            break;

        case 3:
            srcSampleCount = SAWTOOTH_FRAMES;
            src = sawtoothBuffer;
            break;

        case 4:
            srcSampleCount = recorderSize / sizeof(short);
            src = recorderBuffer;
            break;
        default:
            assert(0);
    }

    resampleBuf = (short *) malloc((srcSampleCount * upSampleRate) << 1);
    if (!resampleBuf) {
        return resampleBuf;
    }
    workBuf = resampleBuf;
    for (int sample = 0; sample < srcSampleCount; ++sample) {
        for (int dup = 0; dup < upSampleRate; ++dup) {
            *workBuf++ = src[sample];
        }
    }
    *size = (srcSampleCount * upSampleRate) << 1;
    return resampleBuf;
}

jboolean JNICALL
Java_com_hzw_nativeaudio_MainActivity_selectClip(JNIEnv *env, jobject thiz, jint which,
                                                 jint count) {
    if (pthread_mutex_trylock(&audioEngineLock)) {
        //如果我们无法获取音频引擎锁，请拒绝此请求，客户端应重试
        return JNI_FALSE;
    }

    switch (which) {
        case 0:
            nextBuffer = (short *) NULL;
            nextSize = 0;
            break;

        case 1: //CLIP_HELLO
            nextBuffer = createResampleBuf(1, SL_SAMPLINGRATE_8, &nextSize);
            if (!nextBuffer) {
                nextBuffer = (short *) hello;
                nextSize = sizeof(hello);
            }
            break;

        case 2: //CLIP_ANDROID
            nextBuffer = createResampleBuf(2, SL_SAMPLINGRATE_8, &nextSize);
            if (!nextBuffer) {
                nextBuffer = (short *) android;
                nextSize = sizeof(android);
            }
            break;

        case 3: //CLIP_SAWTOOTH
            nextBuffer = createResampleBuf(3, SL_SAMPLINGRATE_8, &nextSize);
            if (!nextBuffer) {
                nextBuffer = (short *) sawtoothBuffer;
                nextSize = sizeof(sawtoothBuffer);
            }
            break;

        case 4: //CLIP_PLAYBACK
            nextBuffer = createResampleBuf(4, SL_SAMPLINGRATE_16, &nextSize);
            if (!nextBuffer) {
                unsigned i;
                for (i = 0; i < recorderSize; i += sizeof(short)) {
                    recorderBuffer[i >> 2] = recorderBuffer[i >> 1];
                }
                recorderSize = recorderSize >> 1;
                nextBuffer = recorderBuffer;
                nextSize = recorderSize;
            }
            break;

        default:
            nextBuffer = NULL;
            nextSize = 0;
            break;
    }
    nextCount = count;
    if (nextSize > 0) {
        //在这里，我们只对一个缓冲区进行排队，因为它是一个长剪辑，但对于流式播放，我们通常会将至少 2 个缓冲区排队以启动
        SLresult result;
        result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, nextBuffer, nextSize);
        if (result != SL_RESULT_SUCCESS) {
            pthread_mutex_unlock(&audioEngineLock);
            return JNI_FALSE;
        }
    } else {
        pthread_mutex_unlock(&audioEngineLock);
    }
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_hzw_nativeaudio_MainActivity_enableReverb(JNIEnv *env, jobject thiz, jboolean enable) {
    SLresult result;
    if (outputMixEnvironmentalReverb == NULL) {
        return JNI_FALSE;
    }
    if (bqPlayerSampleRate) {
        return JNI_FALSE;
    }
    result = (*bqPlayerEffectSend)->EnableEffectSend(bqPlayerEffectSend,
                                                     outputMixEnvironmentalReverb,
                                                     (SLboolean) enable, (SLmillibel) 0);
    if (result != SL_RESULT_SUCCESS) {
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_hzw_nativeaudio_MainActivity_createAudioRecorder(JNIEnv *env, jobject thiz) {
    SLresult result;

    // configure audio source
    SLDataLocator_IODevice loc_dev = {
            SL_DATALOCATOR_IODEVICE,
            SL_IODEVICE_AUDIOINPUT,
            SL_DEFAULTDEVICEID_AUDIOINPUT, NULL
    };
    SLDataSource audioSrc = {&loc_dev, NULL};

    // configure audio sink
    SLDataLocator_AndroidSimpleBufferQueue loc_bq = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM format_pcm = {
            SL_DATAFORMAT_PCM, 1,
            SL_SAMPLINGRATE_16, SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_CENTER,
            SL_BYTEORDER_LITTLEENDIAN};
    SLDataSink audioSnk = {&loc_bq, &format_pcm};

    // create audio recorder
    // (requires the RECORD_AUDIO permission)
    const SLInterfaceID id[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    result = (*enginEngine)->CreateAudioRecorder(enginEngine, &recorderObject, &audioSrc, &audioSnk,
                                                 1, id, req);
    if (SL_RESULT_SUCCESS != result) {
        return JNI_FALSE;
    }

    result = (*recorderObject)->Realize(recorderObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        return JNI_FALSE;
    }

    result = (*recorderObject)->GetInterface(recorderObject, SL_IID_RECORD, &recorderRecord);
    if (SL_RESULT_SUCCESS != result) {
        return JNI_FALSE;
    }
    UNUSED(result);

    result = (*recorderObject)->GetInterface(recorderObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                             &recorderBufferQueue);
    if (SL_RESULT_SUCCESS != result) {
        return JNI_FALSE;
    }
    UNUSED(result);

    result = (*recorderBufferQueue)->RegisterCallback(recorderBufferQueue, bqRecorderCallback,
                                                      NULL);
    if (SL_RESULT_SUCCESS != result) {
        return JNI_FALSE;
    }
    UNUSED(result);

    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_hzw_nativeaudio_MainActivity_startRecording(JNIEnv *env, jobject thiz) {
    SLresult result;
    if (pthread_mutex_trylock(&audioEngineLock)) {
        return JNI_FALSE;
    }

    //如果已经录制，请停止录制并清除缓冲区队列
    result = (*recorderRecord)->SetRecordState(recorderRecord, SL_RECORDSTATE_STOPPED);
    if (SL_RESULT_SUCCESS != result) {
        return JNI_FALSE;
    }
    UNUSED(result);
    result = (*recorderBufferQueue)->Clear(recorderBufferQueue);
    if (SL_RESULT_SUCCESS != result) {
        return JNI_FALSE;
    }
    UNUSED(result);

    //缓冲区尚不能播放
    recorderSize = 0;

    //将一个空缓冲区排队由记录器填充（对于流式录制，我们将至少排队 2 个空缓冲区以开始工作）
    result = (*recorderBufferQueue)->Enqueue(recorderBufferQueue, recorderBuffer,RECORDER_FRAMES * sizeof(short));
    if (SL_RESULT_SUCCESS != result) {
        return JNI_FALSE;
    }
    UNUSED(result);

    result = (*recorderRecord)->SetRecordState(recorderRecord, SL_RECORDSTATE_RECORDING);
    if (SL_RESULT_SUCCESS != result) {
        return JNI_FALSE;
    }
    UNUSED(result);

    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_hzw_nativeaudio_MainActivity_shutdown(JNIEnv *env, jobject thiz) {
// destroy buffer queue audio player object, and invalidate all associated
    // interfaces
    if (bqPlayerObject != NULL) {
        (*bqPlayerObject)->Destroy(bqPlayerObject);
        bqPlayerObject = NULL;
        bqPlayerPlay = NULL;
        bqPlayerBufferQueue = NULL;
        bqPlayerEffectSend = NULL;
        bqPlayerMuteSolo = NULL;
        bqPlayerVolume = NULL;
    }

    // destroy file descriptor audio player object, and invalidate all associated
    // interfaces
    if (fdPlayerObject != NULL) {
        (*fdPlayerObject)->Destroy(fdPlayerObject);
        fdPlayerObject = NULL;
        fdPlayerPlay = NULL;
        fdPlayerSeek = NULL;
        fdPlayerMuteSolo = NULL;
        fdPlayerVolume = NULL;
    }

    // destroy URI audio player object, and invalidate all associated interfaces
    if (uriPlayerObject != NULL) {
        (*uriPlayerObject)->Destroy(uriPlayerObject);
        uriPlayerObject = NULL;
        uriPlayerPlay = NULL;
        uriPlayerSeek = NULL;
        uriPlayerMuteSolo = NULL;
        uriPlayerVolume = NULL;
    }

    // destroy audio recorder object, and invalidate all associated interfaces
    if (recorderObject != NULL) {
        (*recorderObject)->Destroy(recorderObject);
        recorderObject = NULL;
        recorderRecord = NULL;
        recorderBufferQueue = NULL;
    }

    // destroy output mix object, and invalidate all associated interfaces
    if (outputMixObject != NULL) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = NULL;
        outputMixEnvironmentalReverb = NULL;
    }

    // destroy engine object, and invalidate all associated interfaces
    if (engineObject != NULL) {
        (*engineObject)->Destroy(engineObject);
        engineObject = NULL;
        enginEngine = NULL;
    }

    pthread_mutex_destroy(&audioEngineLock);
}