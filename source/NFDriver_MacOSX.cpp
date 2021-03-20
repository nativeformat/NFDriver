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
#ifdef __APPLE__
#include "TargetConditionals.h"
#if !TARGET_OS_IOS
#include "NFDriverAdapter.h"

#import <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>

namespace nativeformat {
namespace driver {

static const char *mainQueueKey = "main";

typedef struct NFSoundCardDriverInternals {
  NFDriverAdapter *adapter;
  void *clientdata;
  NF_ERROR_CALLBACK errorCallback;
  AudioComponentInstance outputAudioUnit;
  bool isPlaying;
} NFSoundCardDriverInternals;

static void recreateAudioUnit(NFSoundCardDriverInternals *internals);
static void startAudioUnitIfNeeded(NFSoundCardDriverInternals *internals);
static void destroyAudioUnit(AudioComponentInstance *unit);

// Called by Core Audio if something significant changes, such as the sample
// rate.
static void streamFormatChangedCallback(void *inRefCon,
                                        AudioUnit inUnit,
                                        AudioUnitPropertyID inID,
                                        AudioUnitScope inScope,
                                        AudioUnitElement inElement) {
  if ((inScope == kAudioUnitScope_Output) && (inElement == 0)) {  // Do we care?
    AudioOutputUnitStop(inUnit);

    NFSoundCardDriverInternals *internals = (NFSoundCardDriverInternals *)inRefCon;
    dispatch_async(dispatch_get_main_queue(), ^{
      recreateAudioUnit(internals);
      startAudioUnitIfNeeded(internals);
    });
  };
}

// Called by Core Audio if the default audio output device changes.
static OSStatus defaultDeviceChangedCallback(AudioObjectID inObjectID,
                                             UInt32 inNumberAddresses,
                                             const AudioObjectPropertyAddress inAddresses[],
                                             void *inClientData) {
  NFSoundCardDriverInternals *internals = (NFSoundCardDriverInternals *)inClientData;
  dispatch_async(dispatch_get_main_queue(), ^{
    recreateAudioUnit(internals);
    startAudioUnitIfNeeded(internals);
  });
  return noErr;
}

// Called by Core Audio to pump out the next buffer of audio.
static OSStatus audioOutputCallback(void *inRefCon,
                                    AudioUnitRenderActionFlags *ioActionFlags,
                                    const AudioTimeStamp *inTimeStamp,
                                    UInt32 inBusNumber,
                                    UInt32 inNumberFrames,
                                    AudioBufferList *ioData) {
  if ((ioData->mNumberBuffers < 1) || (inNumberFrames < 1)) return kAudioUnitErr_InvalidParameter;

  NFSoundCardDriverInternals *internals = (NFSoundCardDriverInternals *)inRefCon;
  bool silence = !internals->adapter->getFrames(
      reinterpret_cast<float *>(ioData->mBuffers[0].mData),
      reinterpret_cast<float *>(ioData->mBuffers[ioData->mNumberBuffers < 2 ? 0 : 1].mData),
      static_cast<int>(inNumberFrames),
      static_cast<int>(ioData->mNumberBuffers));

  if (silence) {
    *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
    // Despite of the kAudioUnitRenderAction_OutputIsSilence flag, the output
    // may be garbage sometimes (Core Audio is not bug free either). Have to
    // zero the buffers:
    for (UInt32 n = 0; n < ioData->mNumberBuffers; n++)
      memset(ioData->mBuffers[n].mData, 0, ioData->mBuffers[n].mDataByteSize);
  };
  return noErr;
}

NFSoundCardDriver::NFSoundCardDriver(void *clientdata,
                                     NF_STUTTER_CALLBACK stutter_callback,
                                     NF_RENDER_CALLBACK render_callback,
                                     NF_ERROR_CALLBACK error_callback,
                                     NF_WILL_RENDER_CALLBACK will_render_callback,
                                     NF_DID_RENDER_CALLBACK did_render_callback) {
  // Setting a custom key to the main thread/main queue to properly identify it.
  dispatch_queue_set_specific(dispatch_get_main_queue(), mainQueueKey, (void *)mainQueueKey, NULL);

  internals = new NFSoundCardDriverInternals;
  memset(internals, 0, sizeof(NFSoundCardDriverInternals));
  internals->clientdata = clientdata;
  internals->isPlaying = false;
  internals->errorCallback = error_callback;

  internals->adapter = new NFDriverAdapter(clientdata,
                                           stutter_callback,
                                           render_callback,
                                           error_callback,
                                           will_render_callback,
                                           did_render_callback);
  recreateAudioUnit(internals);

  // Telling Mac OSX that we are okay receiving notifications on any thread.
  CFRunLoopRef runLoop = NULL;
  AudioObjectPropertyAddress address = {kAudioHardwarePropertyRunLoop,
                                        kAudioObjectPropertyScopeGlobal,
                                        kAudioObjectPropertyElementMaster};
  AudioObjectSetPropertyData(
      kAudioObjectSystemObject, &address, 0, NULL, sizeof(CFRunLoopRef), &runLoop);

  // Telling Mac OSX that we'd like to be notified if the default audio output
  // device changes.
  address = {kAudioHardwarePropertyDefaultOutputDevice,
             kAudioObjectPropertyScopeGlobal,
             kAudioObjectPropertyElementMaster};
  AudioObjectAddPropertyListener(
      kAudioObjectSystemObject, &address, defaultDeviceChangedCallback, internals);
}

NFSoundCardDriver::~NFSoundCardDriver() {
  AudioObjectPropertyAddress address = {kAudioHardwarePropertyDefaultOutputDevice,
                                        kAudioObjectPropertyScopeGlobal,
                                        kAudioObjectPropertyElementMaster};
  AudioObjectRemovePropertyListener(
      kAudioObjectSystemObject, &address, defaultDeviceChangedCallback, internals);

  destroyAudioUnit(&internals->outputAudioUnit);
  delete internals->adapter;
  delete internals;
}

bool NFSoundCardDriver::isPlaying() const {
  return internals->isPlaying;
}

void NFSoundCardDriver::setPlaying(bool playing) {
  if (dispatch_get_specific(mainQueueKey) == mainQueueKey) {  // Happens on the main thread.
    internals->isPlaying = playing;

    if (playing)
      startAudioUnitIfNeeded(internals);
    else if (internals->outputAudioUnit) {
      if (AudioOutputUnitStop(internals->outputAudioUnit) != noErr)
        internals->errorCallback(internals->clientdata, "Can't stop the output audio unit.", 0);
    }
  } else
    dispatch_async(dispatch_get_main_queue(),
                   ^{  // Or call it on the main thread otherwise.
                     setPlaying(playing);
                   });
}

static void destroyAudioUnit(AudioComponentInstance *unit) {
  if (*unit == NULL) return;
  AudioOutputUnitStop(*unit);
  AudioUnitUninitialize(*unit);
  AudioComponentInstanceDispose(*unit);
  *unit = NULL;
}

static void startAudioUnitIfNeeded(NFSoundCardDriverInternals *internals) {
  if (dispatch_get_specific(mainQueueKey) == mainQueueKey) {  // Happens on the main thread.
    if (internals->isPlaying && internals->outputAudioUnit) {
      if (AudioOutputUnitStart(internals->outputAudioUnit) != noErr)
        internals->errorCallback(internals->clientdata, "Can't start the output audio unit.", 0);
    }
  } else
    dispatch_async(dispatch_get_main_queue(),
                   ^{  // Or call it on the main thread otherwise.
                     startAudioUnitIfNeeded(internals);
                   });
}

static void recreateAudioUnit(NFSoundCardDriverInternals *internals) {
  if (dispatch_get_specific(mainQueueKey) == mainQueueKey) {  // Happens on the main thread.
    destroyAudioUnit(&internals->outputAudioUnit);

    // We use the HALOutput for low-latency operation.
    // Why not kAudioUnitSubType_DefaultOutput?
    // Because it's slow when the user changes the default output device.
    // Listening to kAudioHardwarePropertyDefaultOutputDevice and recreating the
    // audio unit is almost 1 second faster and instant, therefore provides
    // better UX.
    AudioComponentDescription description;
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_HALOutput;
    description.componentFlags = 0;
    description.componentFlagsMask = 0;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;

    // Creating the output audio unit.
    AudioComponent component = AudioComponentFindNext(NULL, &description);
    if (component == NULL) {
      internals->errorCallback(internals->clientdata, "Can't find the HAL output audio unit.", 0);
      return;
    }
    AudioUnit unit = NULL;
    if (AudioComponentInstanceNew(component, &unit) != noErr) {
      internals->errorCallback(internals->clientdata, "Can't create the HAL output audio unit.", 0);
      return;
    }
    UInt32 value = 1;
    if (AudioUnitSetProperty(unit,
                             kAudioOutputUnitProperty_EnableIO,
                             kAudioUnitScope_Output,
                             0,
                             &value,
                             sizeof(value)) != noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(
          internals->clientdata, "Can't enable output IO for the audio unit.", 0);
      return;
    }

    // Selecting the default output device.
    AudioDeviceID device = 0;
    UInt32 size = sizeof(AudioDeviceID);
    AudioObjectPropertyAddress address = {kAudioHardwarePropertyDefaultOutputDevice,
                                          kAudioObjectPropertyScopeGlobal,
                                          kAudioObjectPropertyElementMaster};
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, &device) !=
        noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(
          internals->clientdata, "Can't get the default audio output device.", 0);
      return;
    }
    if (AudioUnitSetProperty(unit,
                             kAudioOutputUnitProperty_CurrentDevice,
                             kAudioUnitScope_Global,
                             0,
                             &device,
                             sizeof(device)) != noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(
          internals->clientdata, "Can't set the current output device for the audio unit.", 0);
      return;
    }

    // Setting the most compatible format: native sample rate, any number of
    // channels, non-interleaved. Floating point. Gone are the days of the
    // "canonical audio format", it's deprecated.
    if (AudioUnitAddPropertyListener(
            unit, kAudioUnitProperty_StreamFormat, streamFormatChangedCallback, internals) !=
        noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(internals->clientdata, "Can't set the stream format listener.", 0);
      return;
    }
    size = 0;
    if (AudioUnitGetPropertyInfo(
            unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &size, NULL) !=
        noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(
          internals->clientdata, "Can't get the output stream format info size.", 0);
      return;
    }
    AudioStreamBasicDescription format;
    if (AudioUnitGetProperty(
            unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &format, &size) !=
        noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(internals->clientdata, "Can't get the output stream format.", 0);
      return;
    }
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked |
                          kAudioFormatFlagIsNonInterleaved | kAudioFormatFlagsNativeEndian;
    format.mBitsPerChannel = 32;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 4;
    format.mBytesPerPacket = 4;
    format.mSampleRate = NF_DRIVER_SAMPLERATE;
    if (format.mChannelsPerFrame > 2)
      format.mChannelsPerFrame = 2;  // We are open to provide 1 or 2 channels.
    if (AudioUnitSetProperty(unit,
                             kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input,
                             0,
                             &format,
                             sizeof(format)) != noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(internals->clientdata, "Can't set the output stream format.", 0);
      return;
    }

    // Render callback and initialize.
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = audioOutputCallback;
    callbackStruct.inputProcRefCon = internals;
    if (AudioUnitSetProperty(unit,
                             kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input,
                             0,
                             &callbackStruct,
                             sizeof(callbackStruct)) != noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(internals->clientdata, "Can't set the render callback.", 0);
      return;
    }
    if (AudioUnitInitialize(unit) != noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(internals->clientdata, "Can't initialize the audio unit.", 0);
      return;
    }

    internals->outputAudioUnit = unit;

    // Asking for the optimal buffer size. Core Audio can not guarantee it
    // though.
    UInt32 numFrames =
        (UInt32)NFDriverAdapter::getOptimalNumberOfFrames(static_cast<int>(format.mSampleRate));
    address = {kAudioDevicePropertyBufferFrameSize,
               kAudioObjectPropertyScopeGlobal,
               kAudioObjectPropertyElementMaster};
    AudioObjectSetPropertyData(device, &address, 0, NULL, sizeof(numFrames), &numFrames);

    internals->adapter->setSamplerate(static_cast<int>(format.mSampleRate));
  } else
    dispatch_async(dispatch_get_main_queue(),
                   ^{  // Or call it on the main thread otherwise.
                     recreateAudioUnit(internals);
                   });
}

}  // namespace driver
}  // namespace nativeformat

#endif  // target_os_mac
#endif  // apple
