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
#include "NFDriverFileAACImplementation.h"

#include <cstdlib>
#include <cstring>

#include <AudioToolbox/AudioToolbox.h>

namespace nativeformat {
namespace driver {

NFDriverFileAACImplementation::NFDriverFileAACImplementation(
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

NFDriverFileAACImplementation::~NFDriverFileAACImplementation() {
  if (isPlaying()) {
    setPlaying(false);
  }
}

bool NFDriverFileAACImplementation::isPlaying() const {
  return !!_thread;
}

void NFDriverFileAACImplementation::setPlaying(bool playing) {
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
    _thread = std::make_shared<std::thread>(&NFDriverFileAACImplementation::run, this);
  }
}

void NFDriverFileAACImplementation::run(NFDriverFileAACImplementation *driver) {
    CFStringRef output_file_str = CFStringCreateWithCString(NULL,
                                                            driver->_output_destination.c_str(),
                                                            kCFStringEncodingUTF8);
    CFURLRef output_file_url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault,
                                                             output_file_str,
                                                             kCFURLPOSIXPathStyle,
                                                             false);
    AudioStreamBasicDescription description;
    description.mFormatID = kAudioFormatMPEG4AAC;
    description.mSampleRate = NF_DRIVER_SAMPLERATE;
    description.mFormatFlags = kMPEG4Object_AAC_Main;
    description.mChannelsPerFrame = NF_DRIVER_CHANNELS;
    description.mBitsPerChannel = 0;
    description.mBytesPerFrame = 0;
    description.mBytesPerPacket = 0;
    description.mFramesPerPacket = 1024;
    
    // Create audio file
    ExtAudioFileRef audio_file;
    OSStatus result = noErr;
    if ((result = ExtAudioFileCreateWithURL(output_file_url,
                                            kAudioFileM4AType,
                                            &description,
                                            NULL,
                                            kAudioFileFlags_EraseFile,
                                            &audio_file)) != noErr) {
        driver->_error_callback(driver->_clientdata, "Failed to create file.", result);
        CFRelease(output_file_url);
        CFRelease(output_file_str);
        return;
    }
    
    // Set the input format
    AudioStreamBasicDescription input_format;
    input_format.mSampleRate = description.mSampleRate;
    input_format.mFormatID = kAudioFormatLinearPCM;
    input_format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
    input_format.mChannelsPerFrame = NF_DRIVER_CHANNELS;
    input_format.mBitsPerChannel = sizeof(float) * 8;
    input_format.mBytesPerFrame = sizeof(float) * NF_DRIVER_CHANNELS;
    input_format.mFramesPerPacket = 1;
    input_format.mBytesPerPacket = input_format.mBytesPerFrame * input_format.mFramesPerPacket;
    const auto input_format_size = sizeof(input_format);
    if ((result = ExtAudioFileSetProperty(audio_file,
                                          kExtAudioFileProperty_ClientDataFormat,
                                          input_format_size,
                                          &input_format)) != noErr) {
        driver->_error_callback(driver->_clientdata, "Failed to set input format on file.", result);
        ExtAudioFileDispose(audio_file);
        CFRelease(output_file_url);
        CFRelease(output_file_str);
        return;
    }
    
    // Find the converter
    AudioConverterRef converter;
    UInt32 converter_format_size = sizeof(converter);
    if ((result = ExtAudioFileGetProperty(audio_file,
                                          kExtAudioFileProperty_AudioConverter,
                                          &converter_format_size,
                                          &converter)) != noErr) {
        driver->_error_callback(driver->_clientdata, "Failed to fetch converter.", result);
        ExtAudioFileDispose(audio_file);
        CFRelease(output_file_url);
        CFRelease(output_file_str);
        return;
    }
    
    // Set the bitrate
    UInt32 bit_rate = driver->_bitrate * 1000;
    if ((result = AudioConverterSetProperty(converter, kAudioConverterEncodeBitRate, sizeof(bit_rate), &bit_rate)) != noErr) {
        driver->_error_callback(driver->_clientdata, "Failed to set bitrate.", result);
        ExtAudioFileDispose(audio_file);
        CFRelease(output_file_url);
        CFRelease(output_file_str);
        return;
    }
    
    // Tell the file the converter config changed
    CFArrayRef config = nullptr;
    if ((result = ExtAudioFileSetProperty(audio_file, kExtAudioFileProperty_ConverterConfig, sizeof(config), &config)) != noErr) {
        driver->_error_callback(driver->_clientdata, "Failed to set converter config.", result);
        ExtAudioFileDispose(audio_file);
        CFRelease(output_file_url);
        CFRelease(output_file_str);
        return;
    }
    
    // Create the buffer
    AudioBufferList buffer_list;
    buffer_list.mNumberBuffers = 1;
    buffer_list.mBuffers[0].mNumberChannels = NF_DRIVER_CHANNELS;
    buffer_list.mBuffers[0].mDataByteSize = input_format.mBytesPerFrame * NF_DRIVER_SAMPLE_BLOCK_SIZE;
    buffer_list.mBuffers[0].mData = malloc(buffer_list.mBuffers[0].mDataByteSize);
    
    // Run the driver
    do {
      float *buffer = static_cast<float *>(buffer_list.mBuffers[0].mData);
      const auto buffer_samples = NF_DRIVER_SAMPLE_BLOCK_SIZE * NF_DRIVER_CHANNELS;
      for (int i = 0; i < buffer_samples; ++i) {
        buffer[i] = 0.0f;
      }
      driver->_will_render_callback(driver->_clientdata);
      const size_t num_frames =
          (size_t)driver->_render_callback(driver->_clientdata, buffer, NF_DRIVER_SAMPLE_BLOCK_SIZE);
      if (num_frames < 1) {
        driver->_stutter_callback(driver->_clientdata);
      } else {
        if ((result = ExtAudioFileWrite(audio_file, num_frames, &buffer_list)) != noErr) {
          driver->_error_callback(driver->_clientdata, "Failed to write frames to disk.", result);
          break;
        }
      }
      driver->_did_render_callback(driver->_clientdata);
    } while (driver->_run);
    
    // Cleanup
    free(buffer_list.mBuffers[0].mData);
    ExtAudioFileDispose(audio_file);
    CFRelease(output_file_url);
    CFRelease(output_file_str);
}

}  // namespace driver
}  // namespace nativeformat
