/*
 * Copyright (c) 2021 Spotify AB.
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
#include "NFDriverFileMP3Implementation.h"

#include <cstdlib>
#include <cstring>
#if _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <lame.h>

namespace nativeformat {
namespace driver {

NFDriverFileMP3Implementation::NFDriverFileMP3Implementation(
    void *clientdata,
    NF_STUTTER_CALLBACK stutter_callback,
    NF_RENDER_CALLBACK render_callback,
    NF_ERROR_CALLBACK error_callback,
    NF_WILL_RENDER_CALLBACK will_render_callback,
    NF_DID_RENDER_CALLBACK did_render_callback,
    const char *output_destination,
    int bitrate)
    : _clientdata(clientdata),
      _stutter_callback(stutter_callback),
      _render_callback(render_callback),
      _error_callback(error_callback),
      _will_render_callback(will_render_callback),
      _did_render_callback(did_render_callback),
      _output_destination(output_destination),
      _bitrate(bitrate),
      _thread(nullptr) {}

NFDriverFileMP3Implementation::~NFDriverFileMP3Implementation() {
  if (isPlaying()) {
    setPlaying(false);
  }
}

bool NFDriverFileMP3Implementation::isPlaying() const {
  return !!_thread;
}

void NFDriverFileMP3Implementation::setPlaying(bool playing) {
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
    _thread = std::make_shared<std::thread>(&NFDriverFileMP3Implementation::run, this);
  }
}

void NFDriverFileMP3Implementation::run(NFDriverFileMP3Implementation *driver) {
  // Open LAME lib
  std::string lame_lib_path(getenv("LAME_DYLIB"));
#if _WIN32
  HINSTANCE lame_handle = LoadLibrary(lame_lib_path.c_str());
  decltype(&lame_init) lame_init_dynamic =
      (decltype(&lame_init))GetProcAddress(lame_handle, "lame_init");
  decltype(&lame_set_in_samplerate) lame_set_in_samplerate_dynamic =
      (decltype(&lame_set_in_samplerate))GetProcAddress(lame_handle, "lame_set_in_samplerate");
  decltype(&lame_set_VBR) lame_set_VBR_dynamic =
      (decltype(&lame_set_VBR))GetProcAddress(lame_handle, "lame_set_VBR");
  decltype(&lame_init_params) lame_init_params_dynamic =
      (decltype(&lame_init_params))GetProcAddress(lame_handle, "lame_init_params");
  decltype(&lame_encode_buffer_interleaved_ieee_float)
      lame_encode_buffer_interleaved_ieee_float_dynamic =
          (decltype(&lame_encode_buffer_interleaved_ieee_float))GetProcAddress(
              lame_handle, "lame_encode_buffer_interleaved_ieee_float");
  decltype(&lame_encode_flush) lame_encode_flush_dynamic =
      (decltype(&lame_encode_flush))GetProcAddress(lame_handle, "lame_encode_flush");
  decltype(&lame_close) lame_close_dynamic =
      (decltype(&lame_close))GetProcAddress(lame_handle, "lame_close");
  decltype(&lame_set_mode) lame_set_mode_dynamic =
      (decltype(&lame_set_mode))GetProcAddress(lame_handle, "lame_set_mode");
  decltype(&lame_set_VBR_mean_bitrate_kbps) lame_set_VBR_mean_bitrate_kbps_dynamic =
      (decltype(&lame_set_VBR_mean_bitrate_kbps))GetProcAddress(lame_handle,
                                                                "lame_set_VBR_mean_bitrate_kbps");
#else
  void *lame_handle = dlopen(lame_lib_path.c_str(), RTLD_LAZY);
  decltype(&lame_init) lame_init_dynamic = (decltype(&lame_init))dlsym(lame_handle, "lame_init");
  decltype(&lame_set_in_samplerate) lame_set_in_samplerate_dynamic =
      (decltype(&lame_set_in_samplerate))dlsym(lame_handle, "lame_set_in_samplerate");
  decltype(&lame_set_VBR) lame_set_VBR_dynamic =
      (decltype(&lame_set_VBR))dlsym(lame_handle, "lame_set_VBR");
  decltype(&lame_init_params) lame_init_params_dynamic =
      (decltype(&lame_init_params))dlsym(lame_handle, "lame_init_params");
  decltype(&lame_encode_buffer_interleaved_ieee_float)
      lame_encode_buffer_interleaved_ieee_float_dynamic =
          (decltype(&lame_encode_buffer_interleaved_ieee_float))dlsym(
              lame_handle, "lame_encode_buffer_interleaved_ieee_float");
  decltype(&lame_encode_flush) lame_encode_flush_dynamic =
      (decltype(&lame_encode_flush))dlsym(lame_handle, "lame_encode_flush");
  decltype(&lame_close) lame_close_dynamic =
      (decltype(&lame_close))dlsym(lame_handle, "lame_close");
  decltype(&lame_set_mode) lame_set_mode_dynamic =
      (decltype(&lame_set_mode))dlsym(lame_handle, "lame_set_mode");
  decltype(&lame_set_VBR_mean_bitrate_kbps) lame_set_VBR_mean_bitrate_kbps_dynamic =
      (decltype(&lame_set_VBR_mean_bitrate_kbps))dlsym(lame_handle,
                                                       "lame_set_VBR_mean_bitrate_kbps");
#endif

  // Open file
  FILE *fhandle = fopen(driver->_output_destination.c_str(), "wb");
  if (fhandle == nullptr) {
    driver->_error_callback(driver->_clientdata, "Failed to create file.", 0);
  }

  // Open LAME
  lame_t lame = lame_init_dynamic();
  lame_set_in_samplerate_dynamic(lame, NF_DRIVER_SAMPLERATE);
  lame_set_VBR_dynamic(lame, vbr_default);
  lame_set_mode_dynamic(lame, STEREO);
  lame_set_VBR_mean_bitrate_kbps_dynamic(lame, driver->_bitrate);
  lame_init_params_dynamic(lame);

  // Perform Encoding
  unsigned char mp3_buffer[8192];
  do {
    const auto buffer_samples = NF_DRIVER_SAMPLE_BLOCK_SIZE * NF_DRIVER_CHANNELS;
    float buffer[buffer_samples];
    for (int i = 0; i < buffer_samples; ++i) {
      buffer[i] = 0.0f;
    }
    driver->_will_render_callback(driver->_clientdata);
    const size_t num_frames =
        (size_t)driver->_render_callback(driver->_clientdata, buffer, NF_DRIVER_SAMPLE_BLOCK_SIZE);
    if (num_frames < 1) {
      driver->_stutter_callback(driver->_clientdata);
    } else {
      const auto write = lame_encode_buffer_interleaved_ieee_float_dynamic(
          lame, buffer, num_frames, mp3_buffer, sizeof(mp3_buffer));
      fwrite(mp3_buffer, write, 1, fhandle);
    }
    driver->_did_render_callback(driver->_clientdata);
  } while (driver->_run);
  const auto write = lame_encode_flush_dynamic(lame, mp3_buffer, sizeof(mp3_buffer));
  fwrite(mp3_buffer, write, 1, fhandle);

  // Cleanup
  lame_close_dynamic(lame);
  fclose(fhandle);
#ifndef _WIN32
  dlclose(lame_handle);
#endif
}

}  // namespace driver
}  // namespace nativeformat
