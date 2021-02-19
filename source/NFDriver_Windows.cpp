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
#if _WIN32

#include <Audioclient.h>
#include <collection.h>
#include <mfapi.h>
#include <mmdeviceapi.h>
#include <ppltasks.h>
#include <wrl\implements.h>
#include "NFDriverAdapter.h"

namespace nativeformat {
namespace driver {

typedef struct streamHandlerInternals {
  NFDriverAdapter *adapter;
  void *clientdata;
  NF_ERROR_CALLBACK errorCallback;
  IAudioClient3 *client;
  IAudioRenderClient *renderClient;
  IMFAsyncResult *sampleReadyAsyncResult;
  HANDLE sampleReadyEvent;
  MFWORKITEM_KEY cancelKey;
  DWORD workQueueId;
  int bufferSize, numberOfSamples;
  bool raw, stop;
} streamHandlerInternals;

// This class handles the actual audio output stream.
// The reason why this class is separate to NFSoundCardDriver is the
// RuntimeClass stuff.
class streamHandler : public Microsoft::WRL::RuntimeClass<
                          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                          Microsoft::WRL::FtmBase,
                          IMFAsyncCallback,
                          IActivateAudioInterfaceCompletionHandler> {
 public:
  bool running;

  streamHandler(void *clientdata,
                NF_STUTTER_CALLBACK stutter_callback,
                NF_RENDER_CALLBACK render_callback,
                NF_ERROR_CALLBACK error_callback,
                NF_WILL_RENDER_CALLBACK will_render_callback,
                NF_DID_RENDER_CALLBACK did_render_callback,
                DWORD workQueueIdentifier,
                bool rawProcessingSupported)
      : running(false) {
    internals = new streamHandlerInternals;
    memset(internals, 0, sizeof(streamHandlerInternals));
    internals->clientdata = clientdata;
    internals->errorCallback = error_callback;

    internals->stop = false;
    internals->workQueueId = workQueueIdentifier;
    internals->raw = rawProcessingSupported;
    internals->sampleReadyEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    internals->adapter = new NFDriverAdapter(clientdata,
                                             stutter_callback,
                                             render_callback,
                                             error_callback,
                                             will_render_callback,
                                             did_render_callback);
  }

  STDMETHODIMP GetParameters(DWORD *flags, DWORD *queue) {
    *queue = internals->workQueueId;
    *flags = 0;
    return S_OK;
  }

  // This is the audio rendering callback, called by a Media Foundation audio
  // thread.
  STDMETHODIMP Invoke(IMFAsyncResult *result) {
    if (internals->stop) {  // Handle stopping.
      if (internals->client) internals->client->Stop();
      fail(NULL);
      // At this point the stream will never call any callbacks anymore.
      return E_FAIL;
    }

    // The buffer may have some padding, we'll not always write from the
    // beginning.
    UINT32 padding;
    HRESULT hr = internals->client->GetCurrentPadding(&padding);
    if (FAILED(hr)) return hr;

    int framesLeft = internals->bufferSize - padding;
    if (framesLeft > 0) {
      // Get the buffer to write.
      BYTE *buffer;
      hr = internals->renderClient->GetBuffer(framesLeft, &buffer);
      if (FAILED(hr)) return hr;

      bool silence = true;
      int framesWritten = 0;

      while (framesLeft >= internals->numberOfSamples) {
        if (!internals->adapter->getFrames((float *)buffer, NULL, internals->numberOfSamples, 2))
          memset(buffer, 0, internals->numberOfSamples * sizeof(float) * 2);
        else
          silence = false;

        framesLeft -= internals->numberOfSamples;
        framesWritten += internals->numberOfSamples;
        buffer += internals->numberOfSamples * sizeof(float) * 2;
      }

      // Release the buffer and enqueue.
      internals->renderClient->ReleaseBuffer(framesWritten,
                                             silence ? AUDCLNT_BUFFERFLAGS_SILENT : 0);
      hr = MFPutWaitingWorkItem(
          internals->sampleReadyEvent, 0, internals->sampleReadyAsyncResult, &internals->cancelKey);
      if (FAILED(hr)) return hr;
    }

    return S_OK;
  }

  // Called by Windows when the audio interface is activated.
  STDMETHOD(ActivateCompleted)
  (IActivateAudioInterfaceAsyncOperation *operation) {
    if (!internals->sampleReadyEvent) return fail("CreateEventEx failed");

    // Get the COM interfaces.
    IUnknown *audioInterface = nullptr;
    HRESULT hrActivateResult = S_OK,
            hr = operation->GetActivateResult(&hrActivateResult, &audioInterface);
    if (FAILED(hr)) return fail("GetActivateResult failed");
    if (FAILED(hrActivateResult)) return fail("ActiveResult is invalid.");
    audioInterface->QueryInterface(IID_PPV_ARGS(&internals->client));
    if (!internals->client) return fail("QueryInterface failed.");

    // Set raw capability (for low-latency).
    AudioClientProperties properties = {0};
    properties.cbSize = sizeof(AudioClientProperties);
    properties.eCategory = AudioCategory_Media;
    if (internals->raw) properties.Options |= AUDCLNT_STREAMOPTIONS_RAW;
    if (FAILED(internals->client->SetClientProperties(&properties)))
      return fail("SetClientProperties failed.");

    // Get the stream properties (sample rate, buffer size).
    WAVEFORMATEX *format;
    if (FAILED(internals->client->GetMixFormat(&format))) return fail("GetMixFormat failed.");
    UINT32 defaultPeriodFrames, fundamentalPeriodFrames, minPeriodFrames, maxPeriodFrames;
    hr = internals->client->GetSharedModeEnginePeriod(
        format, &defaultPeriodFrames, &fundamentalPeriodFrames, &minPeriodFrames, &maxPeriodFrames);
    if (FAILED(hr)) {
      CoTaskMemFree(format);
      return fail("GetSharedModeEnginePeriod failed.");
    }
    internals->adapter->setSamplerate(format->nSamplesPerSec);
    internals->numberOfSamples = minPeriodFrames;
    format->nChannels = 2;

    // Initialize the stream.
    hr = internals->client->InitializeSharedAudioStream(
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minPeriodFrames, format, nullptr);
    CoTaskMemFree(format);
    if (FAILED(hr)) return fail("InitializeSharedAudioStream failed.");

    // Get the buffer size.
    UINT32 uBufferSize = 0;
    if (FAILED(internals->client->GetBufferSize(&uBufferSize)))
      return fail("GetBufferSize failed.");
    internals->bufferSize = static_cast<int>(uBufferSize);

    // Start the audio rendering.
    if (FAILED(internals->client->GetService(__uuidof(IAudioRenderClient),
                                             reinterpret_cast<void **>(&internals->renderClient))))
      return fail("GetService failed.");
    if (FAILED(MFCreateAsyncResult(nullptr, this, nullptr, &internals->sampleReadyAsyncResult)))
      return fail("MFCreateAsyncResult failed.");
    if (FAILED(internals->client->SetEventHandle(internals->sampleReadyEvent)))
      return fail("SetEventHandle failed.");
    if (FAILED(internals->client->Start())) return fail("Start failed.");
    if (FAILED(MFPutWaitingWorkItem(internals->sampleReadyEvent,
                                    0,
                                    internals->sampleReadyAsyncResult,
                                    &internals->cancelKey)))
      return fail("MFPutWaitingWorkItem failed.");

    running = true;
    return S_OK;
  }

  // Delete the handler. The actual operation will happen at Invoke()
  // asynchronously.
  static void release(Microsoft::WRL::ComPtr<streamHandler> stream) {
    if (stream == nullptr) return;
    streamHandler *handler = *stream.GetAddressOf();
    if (handler->running) handler->internals->stop = true;
  }

 private:
  streamHandlerInternals *internals;

  HRESULT fail(const char *message) {
    if (internals->cancelKey != 0) MFCancelWorkItem(internals->cancelKey);
    if (internals->client) internals->client->Release();
    if (internals->renderClient) internals->renderClient->Release();
    if (internals->sampleReadyAsyncResult) internals->sampleReadyAsyncResult->Release();
    if (internals->sampleReadyEvent != INVALID_HANDLE_VALUE)
      CloseHandle(internals->sampleReadyEvent);
    delete internals->adapter;
    if (message) internals->errorCallback(internals->clientdata, message, 0);
    delete internals;
    return E_FAIL;
  }
};

typedef struct nativeformat::driver::NFSoundCardDriverInternals {
  void *clientdata;
  NF_WILL_RENDER_CALLBACK willRenderCallback;
  NF_RENDER_CALLBACK renderCallback;
  NF_DID_RENDER_CALLBACK didRenderCallback;
  NF_STUTTER_CALLBACK stutterCallback;
  NF_ERROR_CALLBACK errorCallback;
  Microsoft::WRL::ComPtr<streamHandler> outputHandler;
  long isPlaying;
} NFSoundCardDriverInternals;

NFSoundCardDriver::NFSoundCardDriver(void *clientdata,
                                     NF_STUTTER_CALLBACK stutter_callback,
                                     NF_RENDER_CALLBACK render_callback,
                                     NF_ERROR_CALLBACK error_callback,
                                     NF_WILL_RENDER_CALLBACK will_render_callback,
                                     NF_DID_RENDER_CALLBACK did_render_callback) {
  internals = new NFSoundCardDriverInternals;
  memset(internals, 0, sizeof(NFSoundCardDriverInternals));
  internals->clientdata = clientdata;
  internals->stutterCallback = stutter_callback;
  internals->renderCallback = render_callback;
  internals->willRenderCallback = will_render_callback;
  internals->didRenderCallback = did_render_callback;
  internals->errorCallback = error_callback;
}

NFSoundCardDriver::~NFSoundCardDriver() {
  streamHandler::release(internals->outputHandler);
  delete internals;
}

static void start(NFSoundCardDriverInternals *internals) {
  // Getting the default output device.
  Platform::String ^ outputDeviceId = Windows::Media::Devices::MediaDevice::GetDefaultAudioRenderId(
      Windows::Media::Devices::AudioDeviceRole::Default);
  if (!outputDeviceId) {
    internals->errorCallback(internals->clientdata, "GetDefaultAudioRenderId failed.", 0);
    return;
  }

  // Start Media Foundation.
  if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
    internals->errorCallback(internals->clientdata, "MFStartup failed.", 0);
    return;
  }

  // Set up Media Foundation for Pro Audio (proper scheduling).
  DWORD taskId = 0, workQueueId = 0;
  if (FAILED(MFLockSharedWorkQueue(L"Pro Audio", 0, &taskId, &workQueueId))) {
    MFShutdown();
    internals->errorCallback(internals->clientdata, "MFLockSharedWorkQueue failed.", 0);
    return;
  }
  auto properties = ref new Platform::Collections::Vector<Platform::String ^>();
  properties->Append("System.Devices.AudioDevice.RawProcessingSupported");

  // Create the stream handler asynchronously. COM is fun! (not)
  Concurrency::create_task(Windows::Devices::Enumeration::DeviceInformation::CreateFromIdAsync(
                               outputDeviceId, properties))
      .then([outputDeviceId, internals, workQueueId](
                Windows::Devices::Enumeration::DeviceInformation ^ deviceInformation) {
        auto obj = deviceInformation->Properties->Lookup(
            "System.Devices.AudioDevice.RawProcessingSupported");
        bool rawProcessingSupported = false;
        if (obj) rawProcessingSupported = obj->Equals(true);

        internals->outputHandler =
            Microsoft::WRL::Make<streamHandler>(internals->clientdata,
                                                internals->stutterCallback,
                                                internals->renderCallback,
                                                internals->errorCallback,
                                                internals->willRenderCallback,
                                                internals->didRenderCallback,
                                                workQueueId,
                                                rawProcessingSupported);
        IActivateAudioInterfaceAsyncOperation *asyncOperation;
        ActivateAudioInterfaceAsync(outputDeviceId->Data(),
                                    __uuidof(IAudioClient3),
                                    nullptr,
                                    *internals->outputHandler.GetAddressOf(),
                                    &asyncOperation);
      });
}

static void stop(NFSoundCardDriverInternals *internals) {
  streamHandler::release(internals->outputHandler);
  internals->outputHandler = nullptr;
  MFShutdown();
}

bool NFSoundCardDriver::isPlaying() const {
  return InterlockedExchangeAdd(&internals->isPlaying, 0) > 0;
}

void NFSoundCardDriver::setPlaying(bool playing) {
  bool changedNow;
  if (playing)
    changedNow = (InterlockedCompareExchange(&internals->isPlaying, 1, 0) == 0);
  else
    changedNow = (InterlockedCompareExchange(&internals->isPlaying, 0, 1) == 1);

  if (changedNow) {
    if (playing)
      start(internals);
    else
      stop(internals);
  }
}

}  // namespace driver
}  // namespace nativeformat

#endif  // _win32
