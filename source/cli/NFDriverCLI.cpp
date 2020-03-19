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
#include <NFDriver/NFDriver.h>

#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>
#include <string>

#if __APPLE__
#include <CoreFoundation/CFRunLoop.h>

#include "TargetConditionals.h"
#endif

#ifdef __ANDROID__
#include <android/log.h>
#include <jni.h>
#endif

static void stutterCallback(void *clientdata) {
  printf("stutter\n");
}

static void errorCallback(void *clientdata, const char *errorMessage, int errorCode) {
  printf("error %i: %s\n", errorCode, errorMessage);
}

static int renderCallback(void *clientdata, float *frames, int numberOfFrames) {
  const float *samplerate = (float *)clientdata;
  const float multiplier = (2.0f * float(M_PI) * *samplerate) / float(NF_DRIVER_SAMPLERATE);
  static unsigned int sinewave = 0;
  float audio;

  for (int n = 0; n < numberOfFrames; n++) {
    audio = sinf(multiplier * sinewave++);
    *frames++ = audio;
    *frames++ = audio;
  }

  return numberOfFrames;
}

static void willRenderCallback(void *clientdata) {}

static void didRenderCallback(void *clientdata) {}

#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL
Java_com_spotify_nfdrivertest_1android_MainActivity_nativeMain(JNIEnv *env, jobject self) {
#else
int main(int argc, const char *argv[]) {
#endif

  std::cout << "NativeFormat Driver Command Line Interface " << nativeformat::driver::version()
            << std::endl;

#ifdef __ANDROID__
  NFDriver::onAppLaunch(env, self, NULL, errorCallback);
#endif

#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE || ANDROID
  const std::string samplerate_string = "44100.0";
#else
  if (argc < 2) {
    std::cout << "Invalid number of arguments: ./NFDriver [samplerate]" << std::endl;
    return 1;
  }
  const std::string samplerate_string = argv[1];
#endif

  float samplerate = std::stof(samplerate_string);

  nativeformat::driver::NFDriver *driver =
      nativeformat::driver::NFDriver::createNFDriver(&samplerate,
                                                     stutterCallback,
                                                     renderCallback,
                                                     errorCallback,
                                                     willRenderCallback,
                                                     didRenderCallback,
                                                     nativeformat::driver::OutputTypeSoundCard);
  driver->setPlaying(true);

#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
  while (true) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true);
  }
#elif !ANDROID
  std::cout << std::endl << "Press a key to exit...";
  std::cin.get();
#endif

#ifndef __ANDROID__
  return 0;
#endif
}
