/*
 * Copyright (c) 2018 Spotify AB.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include <string>

#if __ANDROID__
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "NFDriverAdapter.h"

namespace nativeformat {
namespace driver {

typedef struct NFSoundCardDriverInternals {
  void *clientdata;
  NF_WILL_RENDER_CALLBACK willRenderCallback;
  NF_RENDER_CALLBACK renderCallback;
  NF_DID_RENDER_CALLBACK didRenderCallback;
  NF_STUTTER_CALLBACK stutterCallback;
  NF_ERROR_CALLBACK errorCallback;
  NFDriverAdapter *adapter;
  float *buffer;
  SLObjectItf openSLEngine, outputMix, outputBufferQueue;
  SLAndroidSimpleBufferQueueItf outputBufferQueueInterface;
  int isPlaying;
} NFSoundCardDriverInternals;

static int openslesSamplerate = 48000;
static int openslesBuffersize = 960;

/*
 Getting the sample rate and buffer size of the Android system audio stack.
 This function performs the following Java code in the main activity:

    AudioManager audioManager = (AudioManager)
 this.getSystemService(Context.AUDIO_SERVICE); openslesSamplerate =
 Integer.parseInt(audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE));
    openslesBuffersize =
 Integer.parseInt(audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER));
*/
void NFDriver::onAppLaunch(JNIEnv *env,
                           jobject self,
                           void *clientdata,
                           NF_ERROR_CALLBACK errorCallback) {
  jclass contextClass = env->FindClass("android/content/Context");
  if (!contextClass) {
    errorCallback(clientdata, "Can't find the Context class.", 0);
    return;
  }
  jmethodID getSystemService =
      env->GetMethodID(contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
  if (!getSystemService) {
    errorCallback(clientdata, "Can't find Context.GetSystemService.", 0);
    return;
  }
  jfieldID audioServiceID =
      env->GetStaticFieldID(contextClass, "AUDIO_SERVICE", "Ljava/lang/String;");
  if (!audioServiceID) {
    errorCallback(clientdata, "Can't find Context.AUDIO_SERVICE id.", 0);
    return;
  }
  jobject audioService = env->GetStaticObjectField(contextClass, audioServiceID);
  if (!audioService) {
    errorCallback(clientdata, "Can't find Context.AUDIO_SERVICE.", 0);
    return;
  }
  jobject audioManager = env->CallObjectMethod(self, getSystemService, audioService);
  if (!audioManager) {
    errorCallback(clientdata, "Can't get AudioManager.", 0);
    return;
  }

  jclass audioManagerClass = env->FindClass("android/media/AudioManager");
  if (!audioManagerClass) {
    errorCallback(clientdata, "Can't find the AudioManager class.", 0);
    return;
  }
  jmethodID getProperty =
      env->GetMethodID(audioManagerClass, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;");
  if (!getProperty) {
    errorCallback(clientdata, "Can't find AudioManager.getProperty.", 0);
    return;
  }

  jfieldID samplerateID =
      env->GetStaticFieldID(audioManagerClass, "PROPERTY_OUTPUT_SAMPLE_RATE", "Ljava/lang/String;");
  if (!samplerateID) {
    errorCallback(clientdata, "Can't find AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE id.", 0);
    return;
  }
  jobject samplerateStr = env->GetStaticObjectField(audioManagerClass, samplerateID);
  if (!samplerateStr) {
    errorCallback(clientdata, "Can't find AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE.", 0);
    return;
  }
  jobject samplerateObj = env->CallObjectMethod(audioManager, getProperty, samplerateStr);
  if (!samplerateObj) {
    errorCallback(clientdata, "Can't get the samplerate.", 0);
    return;
  } else {
    const char *str = env->GetStringUTFChars((jstring)samplerateObj, 0);
    if (!str) {
      errorCallback(clientdata, "Can't read the samplerate.", 0);
      return;
    }
    openslesSamplerate = std::stol(str);
    env->ReleaseStringUTFChars((jstring)samplerateObj, str);
    if (openslesSamplerate < 0) {
      errorCallback(clientdata, "Can't parse the sample rate.", 0);
      return;
    }
  }

  jfieldID buffersizeID = env->GetStaticFieldID(
      audioManagerClass, "PROPERTY_OUTPUT_FRAMES_PER_BUFFER", "Ljava/lang/String;");
  if (!buffersizeID) {
    errorCallback(clientdata, "Can't find AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER id.", 0);
    return;
  }
  jobject buffersizeStr = env->GetStaticObjectField(audioManagerClass, buffersizeID);
  if (!buffersizeStr) {
    errorCallback(clientdata, "Can't find AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER.", 0);
    return;
  }
  jobject buffersizeObj = env->CallObjectMethod(audioManager, getProperty, buffersizeStr);
  if (!buffersizeObj) {
    errorCallback(clientdata, "Can't get the buffer size.", 0);
    return;
  } else {
    const char *str = env->GetStringUTFChars((jstring)buffersizeObj, 0);
    if (!str) {
      errorCallback(clientdata, "Can't read the buffer size.", 0);
      return;
    }
    openslesBuffersize = std::stol(str);
    env->ReleaseStringUTFChars((jstring)buffersizeObj, str);
    if (openslesBuffersize < 0) {
      errorCallback(clientdata, "Can't parse the buffer size.", 0);
      return;
    }
  }
}

// Called by the Android system audio stack to enqueue the next buffer of audio.
static void audioRenderCallback(SLAndroidSimpleBufferQueueItf caller, void *pContext) {
  NFSoundCardDriverInternals *internals = (NFSoundCardDriverInternals *)pContext;

  if (internals->adapter->getFrames(internals->buffer, NULL, openslesBuffersize, 2)) {
    // Convert floats to 16-bit short int output.
    float *input = internals->buffer;
    short int *output = (short int *)internals->buffer;
    int n = openslesBuffersize;

    while (n--) {
      *output++ = (short int)(*input++ * 32767.0f);
      *output++ = (short int)(*input++ * 32767.0f);
    }
  } else
    memset(internals->buffer, 0,
           openslesBuffersize * sizeof(short int) * 2);  // Silence.

  // No error handling here, otherwise the log gets full very quickly.
  (*caller)->Enqueue(
      caller, internals->buffer, (SLuint32)openslesBuffersize * sizeof(short int) * 2);
}

// Called only once per instance, in the constructor.
static const char *setupOpenSLES(NFSoundCardDriverInternals *internals) {
  // Create the OpenSL ES engine.
  if (slCreateEngine(&internals->openSLEngine, 0, NULL, 0, NULL, NULL) != SL_RESULT_SUCCESS) {
    internals->openSLEngine = NULL;
    return "slCreateEngine failed.";
  }
  if ((*internals->openSLEngine)->Realize(internals->openSLEngine, SL_BOOLEAN_FALSE) !=
      SL_RESULT_SUCCESS)
    return "Engine Realize failed.";
  SLEngineItf openSLEngineInterface = NULL;
  if ((*internals->openSLEngine)
          ->GetInterface(internals->openSLEngine, SL_IID_ENGINE, &openSLEngineInterface) !=
      SL_RESULT_SUCCESS)
    return "Engine GetInterface failed.";

  // Create the output mix.
  if ((*openSLEngineInterface)
          ->CreateOutputMix(openSLEngineInterface, &internals->outputMix, 0, NULL, NULL) !=
      SL_RESULT_SUCCESS) {
    internals->outputMix = NULL;
    return "CreateOutputMix failed.";
  }
  if ((*internals->outputMix)->Realize(internals->outputMix, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS)
    return "OutputMix Realize failed.";

  // Create the output buffer queue.
  SLDataLocator_OutputMix outputMixLocator = {SL_DATALOCATOR_OUTPUTMIX, internals->outputMix};
  SLDataLocator_AndroidSimpleBufferQueue outputLocator = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                                                          1};
  SLDataFormat_PCM outputFormat = {SL_DATAFORMAT_PCM,
                                   2,
                                   (SLuint32)openslesSamplerate * 1000,
                                   SL_PCMSAMPLEFORMAT_FIXED_16,
                                   SL_PCMSAMPLEFORMAT_FIXED_16,
                                   SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
                                   SL_BYTEORDER_LITTLEENDIAN};
  SLDataSource outputSource = {&outputLocator, &outputFormat};
  const SLInterfaceID outputInterfaces[2] = {SL_IID_BUFFERQUEUE, SL_IID_ANDROIDCONFIGURATION};
  SLDataSink outputSink = {&outputMixLocator, NULL};
  SLboolean requireds[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_FALSE};
  if ((*openSLEngineInterface)
          ->CreateAudioPlayer(openSLEngineInterface,
                              &internals->outputBufferQueue,
                              &outputSource,
                              &outputSink,
                              2,
                              outputInterfaces,
                              requireds) != SL_RESULT_SUCCESS) {
    internals->outputBufferQueue = NULL;
    return "CreateAudioPlayer failed.";
  }
  // Set the stream type (if available).
  SLAndroidConfigurationItf outputConfiguration;
  if ((*internals->outputBufferQueue)
          ->GetInterface(internals->outputBufferQueue,
                         SL_IID_ANDROIDCONFIGURATION,
                         &outputConfiguration) == SL_RESULT_SUCCESS) {
    SLint32 streamType = (SLint32)SL_ANDROID_STREAM_MEDIA;
    (*outputConfiguration)
        ->SetConfiguration(
            outputConfiguration, SL_ANDROID_KEY_STREAM_TYPE, &streamType, sizeof(SLint32));
  };
  if ((*internals->outputBufferQueue)->Realize(internals->outputBufferQueue, SL_BOOLEAN_FALSE) !=
      SL_RESULT_SUCCESS)
    return "Output buffer queue Realize failed.";

  // Initialize the output buffer queue.
  if ((*internals->outputBufferQueue)
          ->GetInterface(internals->outputBufferQueue,
                         SL_IID_BUFFERQUEUE,
                         &internals->outputBufferQueueInterface) != SL_RESULT_SUCCESS)
    return "Output buffer queue GetInterface failed.";
  if ((*internals->outputBufferQueueInterface)
          ->RegisterCallback(internals->outputBufferQueueInterface,
                             audioRenderCallback,
                             internals) != SL_RESULT_SUCCESS)
    return "Output buffer queue RegisterCallback failed.";

  // Enqueue silence.
  internals->buffer = (float *)malloc(openslesBuffersize * sizeof(float) * 2);
  if (!internals->buffer) return "Out of memory in setupOpenSLES.";
  memset(internals->buffer, 0, openslesBuffersize * sizeof(short int) * 2);
  if ((*internals->outputBufferQueueInterface)
          ->Enqueue(internals->outputBufferQueueInterface,
                    internals->buffer,
                    (SLuint32)openslesBuffersize * sizeof(short int) * 2) != SL_RESULT_SUCCESS)
    return "Output enqueue failed.";

  return NULL;
}

NFSoundCardDriver::NFSoundCardDriver(void *clientdata,
                                     NF_STUTTER_CALLBACK stutter_callback,
                                     NF_RENDER_CALLBACK render_callback,
                                     NF_ERROR_CALLBACK error_callback,
                                     NF_WILL_RENDER_CALLBACK will_render_callback,
                                     NF_DID_RENDER_CALLBACK did_render_callback) {
  internals = new NFSoundCardDriverInternals;
  memset(internals, 0, sizeof(NFSoundCardDriverInternals));
  internals->clientdata = clientdata;
  internals->stutterCallback = stutter_callback;
  internals->renderCallback = render_callback;
  internals->willRenderCallback = will_render_callback;
  internals->didRenderCallback = did_render_callback;
  internals->errorCallback = error_callback;

  const char *error = setupOpenSLES(internals);
  if (error)
    error_callback(clientdata, error, 0);
  else {
    internals->adapter = new NFDriverAdapter(clientdata,
                                             internals->stutterCallback,
                                             internals->renderCallback,
                                             internals->errorCallback,
                                             internals->willRenderCallback,
                                             internals->didRenderCallback);
    internals->adapter->setSamplerate(openslesSamplerate);
  }
}

NFSoundCardDriver::~NFSoundCardDriver() {
  setPlaying(false);
  usleep(200000);  // Ugly, but there is no reliable way to get notified when the
                   // audio stack stops.
  if (internals->outputBufferQueue)
    (*internals->outputBufferQueue)->Destroy(internals->outputBufferQueue);
  if (internals->outputMix) (*internals->outputMix)->Destroy(internals->outputMix);
  if (internals->openSLEngine) (*internals->openSLEngine)->Destroy(internals->openSLEngine);
  if (internals->adapter) delete internals->adapter;
  if (internals->buffer) free(internals->buffer);
  delete internals;
}

bool NFSoundCardDriver::isPlaying() const {
  return __sync_fetch_and_add(&internals->isPlaying, 0) > 0;
}

void NFSoundCardDriver::setPlaying(bool playing) {
  if (!internals->adapter) return;

  bool changedNow;
  if (playing)
    changedNow = __sync_bool_compare_and_swap(&internals->isPlaying, 0, 1);
  else
    changedNow = __sync_bool_compare_and_swap(&internals->isPlaying, 1, 0);

  if (changedNow) {
    SLPlayItf outputPlayInterface;
    if ((*internals->outputBufferQueue)
            ->GetInterface(internals->outputBufferQueue, SL_IID_PLAY, &outputPlayInterface) !=
        SL_RESULT_SUCCESS)
      internals->errorCallback(internals->clientdata, "Getting SL_IID_PLAY failed.", 0);
    else if ((*outputPlayInterface)
                 ->SetPlayState(outputPlayInterface,
                                playing ? SL_PLAYSTATE_PLAYING : SL_PLAYSTATE_STOPPED) !=
             SL_RESULT_SUCCESS)
      internals->errorCallback(internals->clientdata, "Setting SL_IID_PLAY failed.", 0);
  }
}

}  // namespace driver
}  // namespace nativeformat

#endif  // __android__
