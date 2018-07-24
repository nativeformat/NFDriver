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
#pragma once

#include <NFDriver/NFDriver.h>

namespace nativeformat {
namespace driver {

struct NFDriverAdapterInternals;

// This class connects audio I/O to the audio provider (the player for example).
// It will always ask the audio provider for 2 channels interleaved audio, with
// fixed buffer size and fixed samplerate (in NFDriver.h). The class performs
// buffering, resampling and deinterleaving automatically as needed.

class NFDriverAdapter {
public:
  NFDriverAdapter(void *clientdata, NF_STUTTER_CALLBACK stutter_callback,
                  NF_RENDER_CALLBACK render_callback,
                  NF_ERROR_CALLBACK error_callback,
                  NF_WILL_RENDER_CALLBACK will_render_callback,
                  NF_DID_RENDER_CALLBACK did_render_callback);
  ~NFDriverAdapter();

  static int getOptimalNumberOfFrames(int samplerate); // Returns with the ideal
                                                       // number of frames for
                                                       // the specific
                                                       // samplerate for minimal
                                                       // buffering and latency.

  void
  setSamplerate(int samplerate); // Thread-safe, can be called in any thread.
  bool getFrames(float *outputLeft, float *outputRight, int numFrames,
                 int numChannels); // Should be called in the audio
                                   // processing/rendering callback of the audio
                                   // I/O.

private:
  NFDriverAdapterInternals *internals;
};

struct NFSoundCardDriverInternals;

class NFSoundCardDriver : public NFDriver {
public:
  bool isPlaying() const;
  void setPlaying(bool playing);

  NFSoundCardDriver(void *clientdata, NF_STUTTER_CALLBACK stutter_callback,
                    NF_RENDER_CALLBACK render_callback,
                    NF_ERROR_CALLBACK error_callback,
                    NF_WILL_RENDER_CALLBACK will_render_callback,
                    NF_DID_RENDER_CALLBACK did_render_callback);
  ~NFSoundCardDriver();

private:
  NFSoundCardDriverInternals *internals;
};

} // namespace driver
} // namespace nativeformat
