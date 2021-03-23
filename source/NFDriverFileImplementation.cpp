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
#include "NFDriverFileImplementation.h"

#include <cstring>
#include <vector>

namespace nativeformat {
namespace driver {

int bytesPerFormat(NFDriverFileWAVHeaderAudioFormat wav_format) {
  switch (wav_format) {
    case NFDriverFileWAVHeaderAudioFormatPCM:
      return sizeof(short);
    case NFDriverFileWAVHeaderAudioFormatIEEEFloat:
      return sizeof(float);
  }
  return sizeof(float);
}

NFDriverFileImplementation::NFDriverFileImplementation(void *clientdata,
                                                       NF_STUTTER_CALLBACK stutter_callback,
                                                       NF_RENDER_CALLBACK render_callback,
                                                       NF_ERROR_CALLBACK error_callback,
                                                       NF_WILL_RENDER_CALLBACK will_render_callback,
                                                       NF_DID_RENDER_CALLBACK did_render_callback,
                                                       const char *output_destination,
                                                       NFDriverFileWAVHeaderAudioFormat wav_format)
    : _clientdata(clientdata),
      _stutter_callback(stutter_callback),
      _render_callback(render_callback),
      _error_callback(error_callback),
      _will_render_callback(will_render_callback),
      _did_render_callback(did_render_callback),
      _output_destination(output_destination),
      _wav_format(wav_format),
      _thread(nullptr) {}

NFDriverFileImplementation::~NFDriverFileImplementation() {
  if (isPlaying()) {
    setPlaying(false);
  }
}

bool NFDriverFileImplementation::isPlaying() const {
  return !!_thread;
}

void NFDriverFileImplementation::setPlaying(bool playing) {
  if (isPlaying() == playing) {
    return;
  }

  if (!playing) {
    _run = false;
    if (std::this_thread::get_id() != _thread->get_id()) {
      _thread->join();
    }
    _thread = nullptr;
  } else {
    _run = true;
    _thread = std::make_shared<std::thread>(&NFDriverFileImplementation::run, this);
  }
}

void NFDriverFileImplementation::run(NFDriverFileImplementation *driver) {
  FILE *fhandle = fopen(driver->_output_destination.c_str(), "wb");
  if (fhandle == nullptr) {
    driver->_error_callback(driver->_clientdata, "Failed to create file.", 0);
  }

  // Write the header.
  struct {
    unsigned char RIFF[4];
    unsigned int chunkSize;
    unsigned char WAVE[4];
    unsigned char FMT[4];
    unsigned int sixteen;
    unsigned short int audioFormat;
    unsigned short int numChannels;
    unsigned int samplerate;
    unsigned int byteRate;
    unsigned short int blockAlign;
    unsigned short int bitsPerSample;
    unsigned char DATA[4];
    unsigned int dataSize;
  } header;
  std::memcpy(header.RIFF, "RIFF", 4);
  std::memcpy(header.WAVE, "WAVE", 4);
  std::memcpy(header.FMT, "fmt ", 4);
  header.sixteen = 16;
  header.audioFormat = driver->_wav_format;
  header.numChannels = NF_DRIVER_CHANNELS;
  header.bitsPerSample = bytesPerFormat(driver->_wav_format) * 8;
  header.samplerate = NF_DRIVER_SAMPLERATE;
  header.byteRate = header.samplerate * header.numChannels * (header.bitsPerSample / 8);
  header.blockAlign = header.numChannels * (header.bitsPerSample / 8);
  std::memcpy(header.DATA, "data", 4);
  fwrite(&header, 1, sizeof(header), fhandle);

  // Rendering.
  const auto buffer_samples = NF_DRIVER_SAMPLE_BLOCK_SIZE * NF_DRIVER_CHANNELS;
  float buffer[buffer_samples];
  while (driver->_run) {
    for (int i = 0; i < buffer_samples; ++i) {
      buffer[i] = 0.0f;
    }
    driver->_will_render_callback(driver->_clientdata);
    const size_t num_frames = static_cast<size_t>(
        driver->_render_callback(driver->_clientdata, buffer, NF_DRIVER_SAMPLE_BLOCK_SIZE));
    if (num_frames < 1) {
      driver->_stutter_callback(driver->_clientdata);
    } else {
      switch (driver->_wav_format) {
        case NFDriverFileWAVHeaderAudioFormatPCM: {
          std::vector<short> converted_samples(num_frames * NF_DRIVER_CHANNELS);
          for (int i = 0; i < converted_samples.size(); ++i) {
            converted_samples[i] =
                static_cast<short>(buffer[i] * std::numeric_limits<short>::max());
          }
          fwrite(converted_samples.data(), sizeof(short), num_frames * NF_DRIVER_CHANNELS, fhandle);
          break;
        }
        case NFDriverFileWAVHeaderAudioFormatIEEEFloat:
          fwrite(buffer, sizeof(float), num_frames * NF_DRIVER_CHANNELS, fhandle);
          break;
      }
    }

    driver->_did_render_callback(driver->_clientdata);
  }

  // Write the size into the header and close the file.
  unsigned int position =
      static_cast<unsigned int>((static_cast<size_t>(ftell(fhandle)) - sizeof(header)));
  fseek(fhandle, 40, SEEK_SET);
  fwrite(&position, 1, 4, fhandle);
  position += 36;
  fseek(fhandle, 4, SEEK_SET);
  fwrite(&position, 1, 4, fhandle);
  fclose(fhandle);
}

}  // namespace driver
}  // namespace nativeformat
