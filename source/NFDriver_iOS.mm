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
#if TARGET_OS_IOS
#include "NFDriverAdapter.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AudioUnit.h>
#import <UIKit/UIApplication.h>

using namespace nativeformat::driver;

static const char *mainQueueKey = "main";

typedef struct nativeformat::driver::NFSoundCardDriverInternals {
  NFDriverAdapter *adapter;
  void *clientdata;
  NF_ERROR_CALLBACK errorCallback;
  AudioComponentInstance outputAudioUnit;
  int outputSamplerate;
  bool isPlaying, appInBackground, audioUnitRunning;
} NFSoundCardDriverInternals;

static void setSamplerateAndBuffersize(NFSoundCardDriverInternals *internals);
static void recreateAudioUnit(NFSoundCardDriverInternals *internals);
static void _setPlaying(bool playing, NFSoundCardDriverInternals *internals);

// Called by Core Audio if something significant changes, such as the sample
// rate.
static void streamFormatChangedCallback(void *inRefCon, AudioUnit inUnit,
                                        AudioUnitPropertyID inID,
                                        AudioUnitScope inScope,
                                        AudioUnitElement inElement) {
  if ((inScope == kAudioUnitScope_Output) && (inElement == 0)) { // Do we care?
    UInt32 size = 0;
    if (AudioUnitGetPropertyInfo(inUnit, kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Output, 0, &size,
                                 NULL) == noErr) {
      AudioStreamBasicDescription format;
      if (AudioUnitGetProperty(inUnit, kAudioUnitProperty_StreamFormat,
                               kAudioUnitScope_Output, 0, &format,
                               &size) == noErr) {
        NFSoundCardDriverInternals *internals =
            (NFSoundCardDriverInternals *)inRefCon;
        internals->outputSamplerate = (int)format.mSampleRate;
        setSamplerateAndBuffersize(internals);
      }
    }
  }
}

// Called by Core Audio to pump out the next buffer of audio.
static OSStatus audioOutputCallback(void *inRefCon,
                                    AudioUnitRenderActionFlags *ioActionFlags,
                                    const AudioTimeStamp *inTimeStamp,
                                    UInt32 inBusNumber, UInt32 inNumberFrames,
                                    AudioBufferList *ioData) {
  if ((ioData->mNumberBuffers < 1) || (inNumberFrames < 1))
    return kAudioUnitErr_InvalidParameter;

  NFSoundCardDriverInternals *internals =
      (NFSoundCardDriverInternals *)inRefCon;
  bool silence = !internals->adapter->getFrames(
      (float *)ioData->mBuffers[0].mData,
      (float *)ioData->mBuffers[ioData->mNumberBuffers < 2 ? 0 : 1].mData,
      (int)inNumberFrames, (int)ioData->mNumberBuffers);

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

// Phone call or other major audio interruption happened.
static void beginInterruption(NFSoundCardDriverInternals *internals) {
  if (dispatch_get_specific(mainQueueKey) ==
      mainQueueKey) { // Happens on the main thread.
    internals->audioUnitRunning = false;
    if (internals->outputAudioUnit)
      AudioOutputUnitStop(internals->outputAudioUnit);
  } else
    dispatch_async(dispatch_get_main_queue(),
                   ^{ // Or call it on the main thread otherwise.
                     beginInterruption(internals);
                   });
}

// Major audio interruption ended.
static void endInterruption(NFSoundCardDriverInternals *internals) {
  if (dispatch_get_specific(mainQueueKey) ==
      mainQueueKey) { // Happens on the main thread.
    if (!internals->audioUnitRunning &&
        internals->isPlaying) { // Our audio should run, but it doesn't do it
                                // for some unknown reason.
      [[AVAudioSession sharedInstance] setActive:NO error:nil];
      recreateAudioUnit(internals);
      _setPlaying(true, internals);
    }
  } else
    dispatch_async(dispatch_get_main_queue(),
                   ^{ // Or call it on the main thread otherwise.
                     endInterruption(internals);
                   });
}

// The app comes foreground.
static void onForeground(CFNotificationCenterRef center, void *observer,
                         CFStringRef name, const void *object,
                         CFDictionaryRef userInfo) {
  NFSoundCardDriverInternals *internals =
      (NFSoundCardDriverInternals *)observer;
  if (internals->appInBackground) {
    internals->appInBackground = false;
    endInterruption(internals);
  }
}

// The app goes background.
static void onBackground(CFNotificationCenterRef center, void *observer,
                         CFStringRef name, const void *object,
                         CFDictionaryRef userInfo) {
  NFSoundCardDriverInternals *internals =
      (NFSoundCardDriverInternals *)observer;
  internals->appInBackground = true;
}

// iOS media server daemon restarted for some reason, such as a buggy
// third-party app or iOS bug. Need to recreate everything.
static void onMediaServerReset(CFNotificationCenterRef center, void *observer,
                               CFStringRef name, const void *object,
                               CFDictionaryRef userInfo) {
  if (dispatch_get_specific(mainQueueKey) ==
      mainQueueKey) { // Happens on the main thread.
    NFSoundCardDriverInternals *internals =
        (NFSoundCardDriverInternals *)observer;
    if (internals->outputAudioUnit)
      AudioOutputUnitStop(internals->outputAudioUnit);
    internals->audioUnitRunning = false;
    [[AVAudioSession sharedInstance] setActive:NO error:nil];
    recreateAudioUnit(internals);
    _setPlaying(true, internals);
  } else
    dispatch_async(dispatch_get_main_queue(),
                   ^{ // Or call it on the main thread otherwise.
                     onMediaServerReset(NULL, observer, NULL, NULL, NULL);
                   });
}

// Called by Core Audio when something significant happened.
static void onAudioSessionInterrupted(CFNotificationCenterRef center,
                                      void *observer, CFStringRef name,
                                      const void *object,
                                      CFDictionaryRef userInfo) {
  NFSoundCardDriverInternals *internals =
      (NFSoundCardDriverInternals *)observer;
  NSDictionary *info = (__bridge NSDictionary *)userInfo;
  NSNumber *interruption =
      [info objectForKey:AVAudioSessionInterruptionTypeKey];
  if (interruption == nil)
    return;

  switch ([interruption intValue]) {
  case AVAudioSessionInterruptionTypeBegan: {
    bool wasSuspended = false;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-pointer-compare"
    if (&AVAudioSessionInterruptionWasSuspendedKey != NULL) {
      NSNumber *obj =
          [info objectForKey:AVAudioSessionInterruptionWasSuspendedKey];
      if ((obj != nil) && ([obj boolValue] == TRUE))
        wasSuspended = true;
    }
#pragma clang diagnostic pop
#pragma clang diagnostic pop
    if (!wasSuspended)
      beginInterruption(internals);
  } break;
  case AVAudioSessionInterruptionTypeEnded: {
    NSNumber *shouldResume =
        [info objectForKey:AVAudioSessionInterruptionOptionKey];
    if ((shouldResume == nil) ||
        [shouldResume unsignedIntegerValue] ==
            AVAudioSessionInterruptionOptionShouldResume)
      endInterruption(internals);
  } break;
  };
}

static void destroyAudioUnit(AudioComponentInstance *unit) {
  if (*unit == NULL)
    return;
  AudioOutputUnitStop(*unit);
  AudioUnitUninitialize(*unit);
  AudioComponentInstanceDispose(*unit);
  *unit = NULL;
}

static void setSamplerateAndBuffersize(NFSoundCardDriverInternals *internals) {
  if (dispatch_get_specific(mainQueueKey) ==
      mainQueueKey) { // Happens on the main thread.
    if (internals->outputSamplerate > 0) {
      internals->adapter->setSamplerate(internals->outputSamplerate);

      if ([[AVAudioSession sharedInstance] preferredSampleRate] !=
          internals->outputSamplerate)
        [[AVAudioSession sharedInstance]
            setPreferredSampleRate:internals->outputSamplerate
                             error:NULL];

      float numFrames = (float)NFDriverAdapter::getOptimalNumberOfFrames(
          internals->outputSamplerate);
      [[AVAudioSession sharedInstance]
          setPreferredIOBufferDuration:numFrames /
                                       float(internals->outputSamplerate)
                                 error:NULL];
    }
  } else
    dispatch_async(dispatch_get_main_queue(),
                   ^{ // Or call it on the main thread otherwise.
                     setSamplerateAndBuffersize(internals);
                   });
}

static void recreateAudioUnit(NFSoundCardDriverInternals *internals) {
  if (dispatch_get_specific(mainQueueKey) ==
      mainQueueKey) { // Happens on the main thread.
    destroyAudioUnit(&internals->outputAudioUnit);

    // Setting up the audio session.
    if (@available(iOS 10.0, *)) {
      [[AVAudioSession sharedInstance]
          setCategory:AVAudioSessionCategoryPlayback
          withOptions:AVAudioSessionCategoryOptionAllowBluetooth |
                      AVAudioSessionCategoryOptionMixWithOthers |
                      AVAudioSessionCategoryOptionAllowAirPlay
                error:NULL];
    } else {
      [[AVAudioSession sharedInstance]
          setCategory:AVAudioSessionCategoryPlayback
          withOptions:AVAudioSessionCategoryOptionAllowBluetooth |
                      AVAudioSessionCategoryOptionMixWithOthers
                error:NULL];
    }
    [[AVAudioSession sharedInstance] setMode:AVAudioSessionModeMeasurement
                                       error:NULL];
    setSamplerateAndBuffersize(internals);
    [[AVAudioSession sharedInstance] setActive:YES error:NULL];

    // Creating the output audio unit.
    AudioComponentDescription description;
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_RemoteIO;
    description.componentFlags = 0;
    description.componentFlagsMask = 0;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent component = AudioComponentFindNext(NULL, &description);
    if (component == NULL) {
      internals->errorCallback(internals->clientdata,
                               "Can't find the HAL output audio unit.", 0);
      return;
    }
    AudioUnit unit = NULL;
    if (AudioComponentInstanceNew(component, &unit) != noErr) {
      internals->errorCallback(internals->clientdata,
                               "Can't create the HAL output audio unit.", 0);
      return;
    }
    UInt32 value = 1;
    if (AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO,
                             kAudioUnitScope_Output, 0, &value,
                             sizeof(value)) != noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(internals->clientdata,
                               "Can't enable output IO for the audio unit.", 0);
      return;
    }
    if (AudioUnitAddPropertyListener(unit, kAudioUnitProperty_StreamFormat,
                                     streamFormatChangedCallback,
                                     internals) != noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(internals->clientdata,
                               "Can't set the stream format listener.", 0);
      return;
    }

    // Setting the most compatible format: native sample rate, any number of
    // channels, non-interleaved. Floating point. Gone are the days of the
    // "canonical audio format", it's deprecated.
    UInt32 size = 0;
    if (AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Output, 0, &size,
                                 NULL) != noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(internals->clientdata,
                               "Can't get the output stream format info size.",
                               0);
      return;
    }
    AudioStreamBasicDescription format;
    if (AudioUnitGetProperty(unit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Output, 0, &format,
                             &size) != noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(internals->clientdata,
                               "Can't get the output stream format.", 0);
      return;
    }
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked |
                          kAudioFormatFlagIsNonInterleaved |
                          kAudioFormatFlagsNativeEndian;
    format.mBitsPerChannel = 32;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 4;
    format.mBytesPerPacket = 4;
    format.mChannelsPerFrame =
        2; // On iOS we can fix this. On Mac OSX we can not.
    if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &format,
                             sizeof(format)) != noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(internals->clientdata,
                               "Can't set the output stream format.", 0);
      return;
    }

    // Render callback and initialize.
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = audioOutputCallback;
    callbackStruct.inputProcRefCon = internals;
    if (AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0, &callbackStruct,
                             sizeof(callbackStruct)) != noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(internals->clientdata,
                               "Can't set the render callback.", 0);
      return;
    }
    if (AudioUnitInitialize(unit) != noErr) {
      destroyAudioUnit(&unit);
      internals->errorCallback(internals->clientdata,
                               "Can't initialize the audio unit.", 0);
      return;
    }

    internals->outputAudioUnit = unit;
  } else
    dispatch_async(dispatch_get_main_queue(),
                   ^{ // Or call it on the main thread otherwise.
                     recreateAudioUnit(internals);
                   });
}

static void _setPlaying(bool playing, NFSoundCardDriverInternals *internals) {
  if (dispatch_get_specific(mainQueueKey) ==
      mainQueueKey) { // Happens on the main thread.
    internals->isPlaying = playing;
    if (playing) {
      if (internals->outputAudioUnit) {
        if (AudioOutputUnitStart(internals->outputAudioUnit) != noErr)
          internals->errorCallback(internals->clientdata,
                                   "Can't start the output audio unit.", 0);
        else {
          internals->audioUnitRunning = true;
          [[AVAudioSession sharedInstance] setActive:YES error:nil];
        }
      }
    } else if (internals->outputAudioUnit) {
      if (AudioOutputUnitStop(internals->outputAudioUnit) != noErr)
        internals->errorCallback(internals->clientdata,
                                 "Can't stop the output audio unit.", 0);
    }
  } else
    dispatch_async(dispatch_get_main_queue(),
                   ^{ // Or call it on the main thread otherwise.
                     _setPlaying(playing, internals);
                   });
}

NFSoundCardDriver::NFSoundCardDriver(
    void *clientdata, NF_STUTTER_CALLBACK stutter_callback,
    NF_RENDER_CALLBACK render_callback, NF_ERROR_CALLBACK error_callback,
    NF_WILL_RENDER_CALLBACK will_render_callback,
    NF_DID_RENDER_CALLBACK did_render_callback) {
  // Setting a custom key to the main thread/main queue to properly identify it.
  dispatch_queue_set_specific(dispatch_get_main_queue(), mainQueueKey,
                              (void *)mainQueueKey, NULL);

  internals = new NFSoundCardDriverInternals;
  memset(internals, 0, sizeof(NFSoundCardDriverInternals));
  internals->clientdata = clientdata;
  // Zero is not valid for a bool false, although it works, but breaks the
  // undefined behavior analyzer.
  internals->isPlaying = internals->appInBackground =
      internals->audioUnitRunning = false;
  internals->errorCallback = error_callback;

  internals->adapter = new NFDriverAdapter(
      clientdata, stutter_callback, render_callback, error_callback,
      will_render_callback, did_render_callback);
  recreateAudioUnit(internals);

  // Observing significant app lifecycle events.
  CFNotificationCenterAddObserver(
      CFNotificationCenterGetLocalCenter(), internals, onForeground,
      (CFStringRef)UIApplicationWillEnterForegroundNotification, NULL,
      CFNotificationSuspensionBehaviorDeliverImmediately);
  CFNotificationCenterAddObserver(
      CFNotificationCenterGetLocalCenter(), internals, onBackground,
      (CFStringRef)UIApplicationDidEnterBackgroundNotification, NULL,
      CFNotificationSuspensionBehaviorDeliverImmediately);
  CFNotificationCenterAddObserver(
      CFNotificationCenterGetLocalCenter(), internals, onMediaServerReset,
      (CFStringRef)AVAudioSessionMediaServicesWereResetNotification, NULL,
      CFNotificationSuspensionBehaviorDeliverImmediately);
  CFNotificationCenterAddObserver(
      CFNotificationCenterGetLocalCenter(), internals,
      onAudioSessionInterrupted,
      (CFStringRef)AVAudioSessionInterruptionNotification, NULL,
      CFNotificationSuspensionBehaviorDeliverImmediately);
}

NFSoundCardDriver::~NFSoundCardDriver() {
  CFNotificationCenterRemoveEveryObserver(CFNotificationCenterGetLocalCenter(),
                                          this);
  [[AVAudioSession sharedInstance] setActive:NO error:nil];
  destroyAudioUnit(&internals->outputAudioUnit);
  delete internals->adapter;
  delete internals;
}

bool NFSoundCardDriver::isPlaying() const { return internals->isPlaying; }

void NFSoundCardDriver::setPlaying(bool playing) {
  _setPlaying(playing, internals);
}

#endif // target_os_ios
#endif // apple
