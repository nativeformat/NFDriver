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
#include <alsa/asoundlib.h>
#include <pthread.h>
#include "NFDriverAdapter.h"

using namespace nativeformat::driver;

typedef struct nativeformat::driver::NFSoundCardDriverInternals {
  void *clientdata;
  NF_WILL_RENDER_CALLBACK willRenderCallback;
  NF_RENDER_CALLBACK renderCallback;
  NF_DID_RENDER_CALLBACK didRenderCallback;
  NF_STUTTER_CALLBACK stutterCallback;
  NF_ERROR_CALLBACK errorCallback;
  int isPlaying, threadsRunning;  // Integers because of atomics.
} NFSoundCardDriverInternals;

typedef struct alsaPCMContext {
  float *buffer;
  snd_pcm_t *handle;
  struct pollfd *pollDescriptors;
  unsigned int outputSamplerate, periodSizeFrames, numChannels;
  int pollDescriptorsCount;
} alsaPCMContext;

// Called when the hardware audio driver has problems with I/O.
static bool underrunRecovery(snd_pcm_t *handle,
                             int error,
                             void *clientdata,
                             NF_ERROR_CALLBACK errorCallback) {
  if (error == -EPIPE) {
    error = snd_pcm_prepare(handle);
    if (error < 0) errorCallback(clientdata, "underrun recovery snd_pcm_prepare error 1", 0);
    return true;
  } else if (error == -ESTRPIPE) {
    while ((error = snd_pcm_resume(handle)) == -EAGAIN) sleep(1);

    if (error < 0) {
      error = snd_pcm_prepare(handle);
      if (error < 0) errorCallback(clientdata, "underrun recovery snd_pcm_prepare error 2", 0);
    }
    return true;
  }
  return (error >= 0);
}

// Waiting for a significant event, such as enough audio consumed by the
// hardware audio driver.
static bool waitForPoll(alsaPCMContext *context,
                        bool *init,
                        void *clientdata,
                        NF_ERROR_CALLBACK errorCallback) {
  unsigned short revents;
  while (1) {
    poll(context->pollDescriptors, context->pollDescriptorsCount, -1);
    snd_pcm_poll_descriptors_revents(
        context->handle, context->pollDescriptors, context->pollDescriptorsCount, &revents);

    if (revents & POLLOUT) return true;

    if (revents & POLLERR) {
      snd_pcm_state_t state = snd_pcm_state(context->handle);

      if ((state == SND_PCM_STATE_XRUN) || (state == SND_PCM_STATE_SUSPENDED)) {
        int error = (state == SND_PCM_STATE_XRUN) ? -EPIPE : -ESTRPIPE;
        if (!underrunRecovery(context->handle, error, clientdata, errorCallback)) {
          errorCallback(clientdata, "wait for poll write error", 0);
          return false;
        }
        *init = true;
      } else {
        errorCallback(clientdata, "wait for poll failed", 0);
        return false;
      }
    }
  }
  return true;
}

static bool setupALSA(alsaPCMContext *context, void *clientdata, NF_ERROR_CALLBACK errorCallback) {
  memset(context, 0, sizeof(alsaPCMContext));

  snd_pcm_t *handle;
  int error = snd_pcm_open(&handle, "sysdefault", SND_PCM_STREAM_PLAYBACK, 0);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_open error ", 0);
    return false;
  }

  // Set up the hardware parameters.
  snd_pcm_hw_params_t *hwParams;
  snd_pcm_hw_params_alloca(&hwParams);
  error = snd_pcm_hw_params_any(handle, hwParams);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_any error ", 0);
    snd_pcm_close(handle);
    return false;
  }
  // Disable resampling in the hardware audio driver or the hardware itself.
  error = snd_pcm_hw_params_set_rate_resample(handle, hwParams, 0);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_set_rate_resample error ", 0);
    snd_pcm_close(handle);
    return false;
  }
  // Interleaved audio works with all hardware these days. USB class audio is
  // interleaved too.
  error = snd_pcm_hw_params_set_access(handle, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_set_access error ", 0);
    snd_pcm_close(handle);
    return false;
  }
  // Let ALSA perform sample format conversion of the hardware doesn't support
  // floating point audio out-of-the-box. This involves no additional latency.
  error = snd_pcm_hw_params_set_format(handle, hwParams, SND_PCM_FORMAT_FLOAT_LE);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_set_format error ", 0);
    snd_pcm_close(handle);
    return false;
  }
  // Set the number of channels to 2 if possible.
  error = snd_pcm_hw_params_get_channels_max(hwParams, &context->numChannels);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_get_channels_max error", 0);
    snd_pcm_close(handle);
    return false;
  }
  if (context->numChannels > 2) {  // The audio device supports more than 2 channels.
    error = snd_pcm_hw_params_get_channels_min(hwParams, &context->numChannels);
    if (error < 0) {
      errorCallback(clientdata, "snd_pcm_hw_params_get_channels_min error", 0);
      snd_pcm_close(handle);
      return false;
    }
    if (context->numChannels <= 2) context->numChannels = 2;
  }
  error = snd_pcm_hw_params_set_channels(handle, hwParams, context->numChannels);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_set_channels error ", 0);
    snd_pcm_close(handle);
    return false;
  }
  // Set the hardware samplerate.
  context->outputSamplerate = NF_DRIVER_SAMPLERATE;
  error = snd_pcm_hw_params_set_rate_near(handle, hwParams, &context->outputSamplerate, 0);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_set_rate_near error ", 0);
    snd_pcm_close(handle);
    return false;
  }
  error = snd_pcm_hw_params_get_rate(hwParams, &context->outputSamplerate, 0);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_get_rate error", 0);
    snd_pcm_close(handle);
    return false;
  }
  // Try to set an optimal buffer and period size for low latency.
  // Buffer size = 2 * period size is the best (one period for playing, one
  // period for the app filling with data.)
  snd_pcm_uframes_t bufferSizeFrames = 0;
  error = snd_pcm_hw_params_get_buffer_size_min(hwParams, &bufferSizeFrames);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_get_buffer_size_min error", 0);
    snd_pcm_close(handle);
    return false;
  }
  context->periodSizeFrames =
      NFDriverAdapter::getOptimalNumberOfFrames((int)context->outputSamplerate);
  div_t d = div((int)bufferSizeFrames, (int)context->periodSizeFrames);
  if (d.quot < 2) d.quot = 2;
  bufferSizeFrames = context->periodSizeFrames * d.quot;
  error = snd_pcm_hw_params_set_buffer_size_near(handle, hwParams, &bufferSizeFrames);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_set_buffer_size_near error", 0);
    snd_pcm_close(handle);
    return false;
  }
  error = snd_pcm_hw_params_get_buffer_size(hwParams, &bufferSizeFrames);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_get_buffer_size error", 0);
    snd_pcm_close(handle);
    return false;
  }
  snd_pcm_uframes_t frames = context->periodSizeFrames;
  error = snd_pcm_hw_params_set_period_size_near(handle, hwParams, &frames, NULL);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_set_period_size_near error", 0);
    snd_pcm_close(handle);
    return false;
  }
  frames = 0;
  error = snd_pcm_hw_params_get_period_size(hwParams, &frames, NULL);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params_get_period_size error ", 0);
    snd_pcm_close(handle);
    return false;
  }
  context->periodSizeFrames = (unsigned int)frames;
  // Actually trying to set up the hardware (and its driver).
  error = snd_pcm_hw_params(handle, hwParams);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_hw_params error ", 0);
    snd_pcm_close(handle);
    return false;
  }

  // Set up the software parameters.
  snd_pcm_sw_params_t *swParams;
  snd_pcm_sw_params_alloca(&swParams);

  error = snd_pcm_sw_params_current(handle, swParams);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_sw_params_current error ", 0);
    snd_pcm_close(handle);
    return false;
  }
  error = snd_pcm_sw_params_set_start_threshold(
      handle, swParams, (bufferSizeFrames / context->periodSizeFrames) * context->periodSizeFrames);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_sw_params_set_start_threshold error ", 0);
    snd_pcm_close(handle);
    return false;
  }
  error = snd_pcm_sw_params_set_avail_min(handle, swParams, context->periodSizeFrames);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_sw_params_set_avail_min error ", 0);
    snd_pcm_close(handle);
    return false;
  }
  error = snd_pcm_sw_params(handle, swParams);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_sw_params error ", 0);
    snd_pcm_close(handle);
    return false;
  }

  // Set up the poll descriptors to check for significant events, such as enough
  // audio consumed by the hardware audio driver.
  context->pollDescriptorsCount = snd_pcm_poll_descriptors_count(handle);
  if (context->pollDescriptorsCount <= 0) {
    errorCallback(clientdata, "invalid poll descriptors count ", 0);
    snd_pcm_close(handle);
    return false;
  }
  struct pollfd *pollDescriptors =
      (pollfd *)malloc(sizeof(struct pollfd) * context->pollDescriptorsCount);
  if (!pollDescriptors) {
    errorCallback(clientdata, "out of memory", 0);
    snd_pcm_close(handle);
    return false;
  }
  error = snd_pcm_poll_descriptors(handle, pollDescriptors, context->pollDescriptorsCount);
  if (error < 0) {
    errorCallback(clientdata, "snd_pcm_poll_descriptors error ", 0);
    snd_pcm_close(handle);
    free(pollDescriptors);
    return false;
  }

  // Allocate the buffer.
  // Why 8 for each sample? Because of exotic 64-bit audio formats.
  context->buffer = (float *)malloc(context->periodSizeFrames * context->numChannels * 8);
  if (!context->buffer) {
    errorCallback(clientdata, "out of memory", 0);
    snd_pcm_close(handle);
    free(pollDescriptors);
    return false;
  }

  context->handle = handle;
  context->pollDescriptors = pollDescriptors;
  printf(
      "  Buffer size: %i frames\n  Period size: %i frames\n  Sample rate: "
      "%i Hz\n  Number of channels: %i\n",
      (int)bufferSizeFrames,
      context->periodSizeFrames,
      context->outputSamplerate,
      context->numChannels);
  return true;
}

static void setAudioThreadPriority() {
  // Set the thread priority. SCHED_FIFO may need CAP_SYS_NICE permission.
  pthread_t thread = pthread_self();
  struct sched_param schedparam;
  schedparam.sched_priority = sched_get_priority_max(SCHED_FIFO);
  schedparam.sched_priority -= (schedparam.sched_priority / 10);  // 90% of the maximum priority.
  pthread_setschedparam(thread, SCHED_FIFO, &schedparam);

  int actualPolicy = 0;
  pthread_getschedparam(thread, &actualPolicy, &schedparam);
  if (actualPolicy == SCHED_FIFO)
    printf("Running with SCHED_FIFO and priority level %i.\n", schedparam.sched_priority);
  else {
    printf(
        "Running with SCHED_OTHER priority. Audio dropouts may happen.\nRun "
        "with CAP_SYS_NICE permission for proper scheduling.\n");
    schedparam.sched_priority = sched_get_priority_max(SCHED_OTHER);
    pthread_setschedparam(thread, SCHED_OTHER, &schedparam);
  }
}

// The actual audio rendering thread.
static void *playbackThread(void *param) {
  NFSoundCardDriverInternals *internals = (NFSoundCardDriverInternals *)param;
  while (__sync_fetch_and_add(&internals->threadsRunning, 0) > 1)
    usleep(10000);  // Wait until another audio rendering thread is still running.
  alsaPCMContext context;

  if (setupALSA(&context, internals->clientdata, internals->errorCallback)) {
    NFDriverAdapter *adapter = new NFDriverAdapter(internals->clientdata,
                                                   internals->stutterCallback,
                                                   internals->renderCallback,
                                                   internals->errorCallback,
                                                   internals->willRenderCallback,
                                                   internals->didRenderCallback);
    adapter->setSamplerate((int)context.outputSamplerate);
    setAudioThreadPriority();

    bool init = true;
    // "Infinite loop".
    while (internals->isPlaying) {
      // Wait until we can push more data.
      if (!init && !waitForPoll(&context, &init, internals->clientdata, internals->errorCallback))
        break;

      // Get the next buffer from the audio provider (the player).
      if (!adapter->getFrames(context.buffer, NULL, context.periodSizeFrames, context.numChannels))
        memset(context.buffer, 0, context.periodSizeFrames * context.numChannels * sizeof(float));

      // Write the data.
      float *buffer = context.buffer;
      int framesLeft = context.periodSizeFrames;
      snd_pcm_sframes_t framesWritten;

      while (framesLeft > 0) {
        framesWritten = snd_pcm_writei(context.handle, buffer, framesLeft);

        if (framesWritten < 0) {
          if (!underrunRecovery(
                  context.handle, framesWritten, internals->clientdata, internals->errorCallback)) {
            internals->errorCallback(
                internals->clientdata, "underrun recovery write error", framesWritten);
            __sync_fetch_and_and(&internals->isPlaying, 0);
            break;
          }
          init = true;
          internals->errorCallback(internals->clientdata, "skip one period", 0);
          break;
        }

        if (snd_pcm_state(context.handle) == SND_PCM_STATE_RUNNING) init = false;
        buffer += framesWritten * context.numChannels;
        framesLeft -= framesWritten;
        if (framesLeft <= 0) break;

        if (!waitForPoll(&context, &init, internals->clientdata, internals->errorCallback)) {
          __sync_fetch_and_and(&internals->isPlaying, 0);
          break;
        }
      }
    }

    delete adapter;
    snd_pcm_drain(context.handle);
    snd_pcm_close(context.handle);
    free(context.pollDescriptors);
    free(context.buffer);
  }

  __sync_fetch_and_add(&internals->threadsRunning, -1);
  pthread_detach(pthread_self());
  pthread_exit(NULL);
  return NULL;
}

NFSoundCardDriver::NFSoundCardDriver(void *clientdata,
                                     NF_STUTTER_CALLBACK stutter_callback,
                                     NF_RENDER_CALLBACK render_callback,
                                     NF_ERROR_CALLBACK error_callback,
                                     NF_WILL_RENDER_CALLBACK will_render_callback,
                                     NF_DID_RENDER_CALLBACK did_render_callback) {
  internals = new NFSoundCardDriverInternals;
  internals->clientdata = clientdata;
  internals->isPlaying = internals->threadsRunning = 0;
  internals->stutterCallback = stutter_callback;
  internals->renderCallback = render_callback;
  internals->willRenderCallback = will_render_callback;
  internals->didRenderCallback = did_render_callback;
  internals->errorCallback = error_callback;
}

NFSoundCardDriver::~NFSoundCardDriver() {
  __sync_fetch_and_and(&internals->isPlaying,
                       0);  // Notify the audio rendering threads to stop with isPlaying to 0.
  while (__sync_fetch_and_add(&internals->threadsRunning, 0) > 0)
    usleep(10000);  // Wait until any audio rendering thread is running.
  delete internals;
}

bool NFSoundCardDriver::isPlaying() const {
  return __sync_fetch_and_add(&internals->isPlaying, 0) > 0;
}

void NFSoundCardDriver::setPlaying(bool playing) {
  if (playing) {
    if (__sync_bool_compare_and_swap(&internals->isPlaying,
                                     0,
                                     1)) {  // If we actually flip isPlaying from 0 to 1.
      __sync_fetch_and_add(&internals->threadsRunning, 1);
      pthread_t thread;
      pthread_create(&thread, NULL, playbackThread, internals);
    }
  } else
    __sync_fetch_and_and(&internals->isPlaying,
                         0);  // Notify the audio rendering threads to stop with isPlaying to 0.
}
