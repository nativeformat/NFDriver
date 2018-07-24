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
#include "NFDriverAdapter.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace nativeformat {
namespace driver {

// Cross-platform atomic operations.
#if _WIN32
#include <windows.h>
#define ATOMIC_SIGNED_INT long
#define MEMORYBARRIER MemoryBarrier()
#define ATOMICZERO(var) InterlockedAnd(&var, 0)
#else
#define ATOMIC_SIGNED_INT int
#define MEMORYBARRIER __sync_synchronize()
#define ATOMICZERO(var) __sync_fetch_and_and(&var, 0)
#endif

// Linear resampler stuff.
// Why linear? Because more sophisticated resamplers are killing treble without
// oversampling. The noise of this resampler will typically happen around the
// Nyquist frequency and in the -90 db or lower region. Audiophile bats may
// complain. Humans are not able to notice.
typedef struct resamplerData {
  uint64_t *
      input; // A buffer on the heap to store NF_DRIVER_SAMPLE_BLOCK_SIZE audio.
  union {
    float f[2];
    uint64_t i; // Makes loads faster a bit. Don't believe the hype, compilers
                // are still quite dumb.
  } prev;
  float rate, slopeCount;
} resamplerData;

// This linear resampler is not "Superpowered", but still faster than most naive
// implementations.
static int resample(float *output, resamplerData *resampler, int numFrames) {
  resamplerData stack = *resampler; // Local copy on the stack, preventing the
                                    // compiler writing back intermediate
                                    // results to memory.
  float left, right, invSlopeCount;
  int outFrames = 0;

  while (true) {
    while (stack.slopeCount > 1.0f) {
      numFrames--;
      stack.slopeCount -= 1.0f;

      if (!numFrames) { // Quit resampling, writing back the intermediate
                        // results to memory.
        resampler->slopeCount = stack.slopeCount;
        resampler->prev.i = stack.prev.i;
        return outFrames;
      }

      stack.prev.i = *stack.input++;
    }

    // Linear resampling, the compiler may recognize that these are primitive
    // Assembly instructions.
    invSlopeCount = 1.0f - stack.slopeCount;
    left = invSlopeCount * stack.prev.f[0];
    right = invSlopeCount * stack.prev.f[1];

    stack.prev.i = *stack.input;

    *output++ = left + stack.slopeCount * stack.prev.f[0];
    *output++ = right + stack.slopeCount * stack.prev.f[1];

    stack.slopeCount += stack.rate;
    outFrames++;
  }
}

static void makeOutput(float *input, float **outputLeft, float **outputRight,
                       int numFrames, int numChannels) {
  if (numChannels == 1) { // Mono output.
    float *mono = *outputLeft;
    *outputLeft += numFrames;

    while (numFrames--) {
      *mono++ = (input[0] + input[1]) * 0.5f;
      input += 2;
    }
  } else if (*outputRight) { // Stereo non-interleaved output, deinterleave to
                             // left and right.
    float *left = *outputLeft, *right = *outputRight;
    *outputLeft += numFrames;
    *outputRight += numFrames;

    while (numFrames--) {
      *left++ = *input++;
      *right++ = *input++;
    }
  } else if (numChannels > 2) { // Interleaved output with more than 2 channels.
                                // Can happen with Linux only.
    float *output = *outputLeft;
    *outputLeft += numFrames * numChannels;
    memset(output, 0, (size_t)(numFrames * numChannels) * sizeof(float));

    while (numFrames--) {
      *((uint64_t *)output) = *((uint64_t *)input);
      output += numChannels;
      input += 2;
    }
  } else { // Stereo interleaved output.
    memcpy(*outputLeft, input, (size_t)numFrames * sizeof(float) * 2);
    *outputLeft += numFrames * 2;
  }
}

// Finally, the adapter implementation starts here.
typedef struct NFDriverAdapterInternals {
  resamplerData resampler;
  void *clientdata;
  NF_WILL_RENDER_CALLBACK willRenderCallback;
  NF_RENDER_CALLBACK renderCallback;
  NF_DID_RENDER_CALLBACK didRenderCallback;
  NF_STUTTER_CALLBACK stutterCallback;
  float *interleavedBuffer;
  int bufferCapacityFrames, framesInBuffer, readPositionFrames,
      writePositionFrames, bufferCapacityToEndNeeded;
  ATOMIC_SIGNED_INT nextSamplerate;
  bool needsResampling;
} NFDriverAdapterInternals;

NFDriverAdapter::NFDriverAdapter(void *clientdata,
                                 NF_STUTTER_CALLBACK stutter_callback,
                                 NF_RENDER_CALLBACK render_callback,
                                 NF_ERROR_CALLBACK error_callback,
                                 NF_WILL_RENDER_CALLBACK will_render_callback,
                                 NF_DID_RENDER_CALLBACK did_render_callback) {
  internals = new NFDriverAdapterInternals;
  memset(internals, 0, sizeof(NFDriverAdapterInternals));

  internals->clientdata = clientdata;
  internals->stutterCallback = stutter_callback;
  internals->renderCallback = render_callback;
  internals->willRenderCallback = will_render_callback;
  internals->didRenderCallback = did_render_callback;

  int volatile numBlocks = NF_DRIVER_SAMPLERATE / NF_DRIVER_SAMPLE_BLOCK_SIZE;
  internals->bufferCapacityFrames =
      numBlocks * NF_DRIVER_SAMPLE_BLOCK_SIZE; // Will be around 1 second big.
  internals->interleavedBuffer =
      (float *)malloc((size_t)internals->bufferCapacityFrames * sizeof(float) *
                      2); // 344 kb at 44100 Hz and 1024 frames.
  if (!internals->interleavedBuffer)
    error_callback(clientdata, "Out of memory in NFDriverAdapter.", 0);

  internals->resampler.input =
      (uint64_t *)malloc(NF_DRIVER_SAMPLE_BLOCK_SIZE * sizeof(uint64_t));
  if (!internals->resampler.input)
    error_callback(clientdata, "Out of memory in NFDriverAdapter.", 0);
}

NFDriverAdapter::~NFDriverAdapter() {
  if (internals->interleavedBuffer)
    free(internals->interleavedBuffer);
  if (internals->resampler.input)
    free(internals->resampler.input);
  delete internals;
}

// Should be called in the audio processing/rendering callback of the audio I/O.
bool NFDriverAdapter::getFrames(float *outputLeft, float *outputRight,
                                int numFrames, int numChannels) {
  if (!internals->interleavedBuffer || !internals->resampler.input)
    return false;
  internals->willRenderCallback(internals->clientdata);

  ATOMIC_SIGNED_INT nextSamplerate = ATOMICZERO(
      internals
          ->nextSamplerate); // Make it zero, return with the previous value.
  if (nextSamplerate != 0) {
    internals->needsResampling = (nextSamplerate != NF_DRIVER_SAMPLERATE);
    internals->resampler.rate =
        float(NF_DRIVER_SAMPLERATE) / float(nextSamplerate);
    internals->bufferCapacityToEndNeeded =
        internals->needsResampling
            ? int((float(nextSamplerate) / float(NF_DRIVER_SAMPLERATE)) *
                  (NF_DRIVER_SAMPLE_BLOCK_SIZE + 2))
            : NF_DRIVER_SAMPLE_BLOCK_SIZE;
  }

  // Render audio if needed.
  while (internals->framesInBuffer < numFrames) {
    // Do we have enough space in the buffer?
    if (internals->bufferCapacityToEndNeeded >
        (internals->bufferCapacityFrames - internals->writePositionFrames)) {
      // Memmove looks inefficient? This will happen only once in every second,
      // and all "virtual memory tricks" will do this anyway behind the curtain.
      if (internals->framesInBuffer > 0)
        memmove(internals->interleavedBuffer,
                internals->interleavedBuffer +
                    internals->readPositionFrames * 2,
                (size_t)internals->framesInBuffer * sizeof(float) * 2);
      internals->readPositionFrames = 0;
      internals->writePositionFrames = internals->framesInBuffer;
    }

    int framesRendered;
    if (!internals->needsResampling) { // No resampling needed, render directly
                                       // into our buffer.
      framesRendered = internals->renderCallback(
          internals->clientdata,
          internals->interleavedBuffer + internals->writePositionFrames * 2,
          NF_DRIVER_SAMPLE_BLOCK_SIZE);
      if (framesRendered <= 0)
        break;
    } else { // Resampling needed, render into the resampler's input buffer, the
             // resample into our buffer.
      framesRendered = internals->renderCallback(
          internals->clientdata, (float *)internals->resampler.input,
          NF_DRIVER_SAMPLE_BLOCK_SIZE);
      if (framesRendered <= 0)
        break;
      framesRendered = resample(internals->interleavedBuffer +
                                    internals->writePositionFrames * 2,
                                &internals->resampler, framesRendered);
    }

    internals->writePositionFrames += framesRendered;
    internals->framesInBuffer += framesRendered;
  }

  // Output audio if possible.
  bool success = internals->framesInBuffer >= numFrames;
  if (success) {
    // Output numFrames of audio, or until the end of our buffer.
    int framesAvailableToEnd =
        internals->bufferCapacityFrames - internals->readPositionFrames;
    if (framesAvailableToEnd > numFrames)
      framesAvailableToEnd = numFrames;

    makeOutput(internals->interleavedBuffer + internals->readPositionFrames * 2,
               &outputLeft, &outputRight, framesAvailableToEnd, numChannels);
    internals->readPositionFrames += framesAvailableToEnd;
    if (internals->readPositionFrames >= internals->bufferCapacityFrames)
      internals->readPositionFrames = 0;

    // Start from the beginning of our buffer if needed. (Wrap around.)
    int moreFrames = numFrames - framesAvailableToEnd;
    if (moreFrames > 0) {
      makeOutput(internals->interleavedBuffer, &outputLeft, &outputRight,
                 moreFrames, numChannels);
      internals->readPositionFrames += moreFrames;
    }

    internals->framesInBuffer -= numFrames;
  } else
    internals->stutterCallback(internals->clientdata);

  internals->didRenderCallback(internals->clientdata);
  return success;
}

void NFDriverAdapter::setSamplerate(int samplerate) {
  internals->nextSamplerate = samplerate;
  MEMORYBARRIER;
}

int NFDriverAdapter::getOptimalNumberOfFrames(int samplerate) {
  if (samplerate == NF_DRIVER_SAMPLERATE)
    return NF_DRIVER_SAMPLE_BLOCK_SIZE;

  float rate = float(samplerate) / float(NF_DRIVER_SAMPLERATE);
  return int(NF_DRIVER_SAMPLE_BLOCK_SIZE * rate);
}

} // namespace driver
} // namespace nativeformat
