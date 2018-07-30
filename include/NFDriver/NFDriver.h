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

//! Enum that defines the desired output type
/*! This enum will define the output destination. */
typedef enum {
  OutputTypeSoundCard, /*!< Output to hardware (the local sound card). */
  OutputTypeFile       /* Output to a file. */
} OutputType;

/*! Number of samples to process at a time */
#define NF_DRIVER_SAMPLE_BLOCK_SIZE 1024
/*! The sample rate of the blocks to be sampled. In units of samples per second */
#define NF_DRIVER_SAMPLERATE 44100
/*! Number of channels to output a time. 2 means we're outputting stereo */
#define NF_DRIVER_CHANNELS 2

/*!
 * \brief Callback called when driver stutters.
 *
 * \param clientdata Client specific data that gets used by the callback.
 */
typedef void (*NF_STUTTER_CALLBACK)(void *clientdata);
/*!
 * \brief Callback called before rendering.
 *
 * \param clientdata Client specific data that gets used by the callback.
 */
typedef void (*NF_WILL_RENDER_CALLBACK)(void *clientdata);
/*!
 * \brief Callback called after rendering.
 *
 * \param clientdata Client specific data that gets used by the callback.
 */
typedef void (*NF_DID_RENDER_CALLBACK)(void *clientdata);
/*!
 * \brief Callback that gathers frames to be output.
 *
 * \param clientdata Client specific data that gets used by the callback.
 * \param frames Data that will be subsequently be output to the OutputType of choice.
 * \param numberOfFrames Desired number of frames to store in frames.
 * \return Number of frames that were stored in frames.
 */
typedef int (*NF_RENDER_CALLBACK)(void *clientdata, float *frames, int numberOfFrames);
/*!
 * \brief Callback called when NFDriver experiences an error.
 * \param clientdata Client specific data that gets used by the callback.
 * \param errorMessages Error message to print when this callback gets called.
 * \param errorCode Unique number (preferably) that's associated with the error message.
 */
typedef void (*NF_ERROR_CALLBACK)(void *clientdata, const char *errorMessage, int errorCode);

/*!
 * \brief The version of this library.
 *
 * \return The version, for example 1.0 in use
 */
extern const char *version();

/*!
 * Interface used tracking state of the audio output.
 */
class NFDriver {
 public:
  /*!
   * \brief Thread-safe function to check whether the driver is currently outputting samples.
   *
   * \return True if there are samples being output. False otherwise.
   */
  virtual bool isPlaying() const = 0;
  /*!
   * \brief Thread-safe function to set if the driver should output samples.
   *
   * \param playing True to tell the driver to start outputting samples.
   *                False if not.
   */
  virtual void setPlaying(bool playing) = 0;
  /*! \brief Destructor */
  virtual ~NFDriver(){};

#if __ANDROID__
  /*!
   * \brief Called when using Android and an app has just started.
   *
   * This function is meant to be used with the Java Native Interface
   * (JNI). It will be called when the application using NFDriver has just
   * launched. This function should only be called ONCE per app life-cycle.
   * \param env JNI Environment
   * \param self The object calling this function
   * \param clientdata Client specific data that gets used by the callback.
   * \param errorCallback Function to call in case NFDriver errors.
   */
  static void onAppLaunch(JNIEnv *env,
                          jobject self,
                          void *clientdata,
                          NF_ERROR_CALLBACK errorCallback);
#endif

  /*!
   * \brief Factory function to create an NFDriver for the appropriate device.
   *
   * \param clientdata Client specific data that gets used by the callback.
   * \param stutter_callback Function to call if playback stutters.
   * \param render_callback Function called when we have samples to output.
   * \param error_callback Function called when the driver errors.
   * \param will_render_callback Function called before render_callback.
   * \param did_render_callback Function called after render_callback.
   * \param outputType Desired output destination.
   * \param output_destination Name of output destination if it is a
   *                           named device, file etc.
   * \return Instance of NFDriver.
   */
  static NFDriver *createNFDriver(void *clientdata,
                                  NF_STUTTER_CALLBACK stutter_callback,
                                  NF_RENDER_CALLBACK render_callback,
                                  NF_ERROR_CALLBACK error_callback,
                                  NF_WILL_RENDER_CALLBACK will_render_callback,
                                  NF_DID_RENDER_CALLBACK did_render_callback,
                                  OutputType outputType,
                                  const char *output_destination = 0);
};

}  // namespace driver
}  // namespace nativeformat
