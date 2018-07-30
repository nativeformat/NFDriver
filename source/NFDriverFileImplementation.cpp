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

namespace nativeformat {
namespace driver {

NFDriverFileImplementation::NFDriverFileImplementation(void *clientdata,
                                                       NF_STUTTER_CALLBACK stutter_callback,
                                                       NF_RENDER_CALLBACK render_callback,
                                                       NF_ERROR_CALLBACK error_callback,
                                                       NF_WILL_RENDER_CALLBACK will_render_callback,
                                                       NF_DID_RENDER_CALLBACK did_render_callback,
                                                       const char *output_destination)
    : _clientdata(clientdata),
      _stutter_callback(stutter_callback),
      _render_callback(render_callback),
      _error_callback(error_callback),
      _will_render_callback(will_render_callback),
      _did_render_callback(did_render_callback),
      _output_destination(output_destination),
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
    _thread->join();
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
  header.audioFormat = 3;
  header.numChannels = 2;
  header.bitsPerSample = 32;
  header.samplerate = NF_DRIVER_SAMPLERATE;
  header.byteRate = header.samplerate * header.numChannels * (header.bitsPerSample >> 3);
  header.blockAlign = header.numChannels * (header.bitsPerSample >> 3);
  std::memcpy(header.DATA, "data", 4);
  fwrite(&header, 1, sizeof(header), fhandle);

  // Rendering.
  float buffer[NF_DRIVER_SAMPLE_BLOCK_SIZE * NF_DRIVER_CHANNELS];
  size_t numFrames = 0;
  while (driver->_run) {
    driver->_will_render_callback(driver->_clientdata);
    numFrames =
        (size_t)driver->_render_callback(driver->_clientdata, buffer, NF_DRIVER_SAMPLE_BLOCK_SIZE);
    if (numFrames < 1) {
      driver->_stutter_callback(driver->_clientdata);
    } else {
      fwrite(buffer, sizeof(float), numFrames * NF_DRIVER_CHANNELS, fhandle);
    }

    driver->_did_render_callback(driver->_clientdata);
  }

  // Write the size into the header and close the file.
  unsigned int position = (unsigned int)((size_t)ftell(fhandle) - sizeof(header));
  fseek(fhandle, 40, SEEK_SET);
  fwrite(&position, 1, 4, fhandle);
  position += 36;
  fseek(fhandle, 4, SEEK_SET);
  fwrite(&position, 1, 4, fhandle);
  fclose(fhandle);
}

}  // namespace driver
}  // namespace nativeformat
