/*
 * Copyright (c) 2016 Spotify AB.
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
#pragma once

#if _WIN32
#define WIN32_LEAN_AND_MEAN
#define _USE_MATH_DEFINES
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <windows.h>
#endif

#if __ANDROID__
#include <jni.h>
#endif

namespace nativeformat {
namespace driver {

typedef enum { OutputTypeSoundCard, OutputTypeFile } OutputType;

#define NF_DRIVER_SAMPLE_BLOCK_SIZE 1024
#define NF_DRIVER_SAMPLERATE 44100
#define NF_DRIVER_CHANNELS 2

typedef void (*NF_STUTTER_CALLBACK)(void *clientdata);
typedef void (*NF_WILL_RENDER_CALLBACK)(void *clientdata);
typedef void (*NF_DID_RENDER_CALLBACK)(void *clientdata);
typedef int (*NF_RENDER_CALLBACK)(void *clientdata, float *frames,
                                  int numberOfFrames);
typedef void (*NF_ERROR_CALLBACK)(void *clientdata, const char *errorMessage,
                                  int errorCode);

extern const char *version();

class NFDriver {
public:
  virtual bool isPlaying() const = 0;        // Thread-safe.
  virtual void setPlaying(bool playing) = 0; // Thread-safe.
  virtual ~NFDriver(){};

#if __ANDROID__
  // Should be called only once per app life-cycle.
  static void onAppLaunch(JNIEnv *env, jobject self, void *clientdata,
                          NF_ERROR_CALLBACK errorCallback);
#endif

  static NFDriver *createNFDriver(void *clientdata,
                                  NF_STUTTER_CALLBACK stutter_callback,
                                  NF_RENDER_CALLBACK render_callback,
                                  NF_ERROR_CALLBACK error_callback,
                                  NF_WILL_RENDER_CALLBACK will_render_callback,
                                  NF_DID_RENDER_CALLBACK did_render_callback,
                                  OutputType outputType,
                                  const char *output_destination = 0);
};

} // namespace driver
} // namespace nativeformat
