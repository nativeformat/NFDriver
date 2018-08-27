<img alt="NFDriver" src="NFDriver.png" width="100%" max-width="888">

[![CircleCI](https://circleci.com/gh/spotify/NFDriver/tree/master.svg?style=svg)](https://circleci.com/gh/spotify/NFDriver/tree/master)
[![Build status](https://ci.appveyor.com/api/projects/status/m2bn47mr3u26k6jq/branch/master?svg=true)](https://ci.appveyor.com/project/8W9aG/nfdriver/branch/master)
[![License](https://img.shields.io/github/license/spotify/NFDriver.svg)](LICENSE)
[![Spotify FOSS Slack](https://slackin.spotify.com/badge.svg)](https://slackin.spotify.com)
[![Readme Score](http://readme-score-api.herokuapp.com/score.svg?url=https://github.com/spotify/nfdriver)](http://clayallsopp.github.io/readme-score?url=https://github.com/spotify/nfdriver)

A cross platform C++ audio driver with low latency.

- [x] 📱 iOS 9.0+
- [x] 💻 OS X 10.11+
- [x] 🐧 Ubuntu Trusty 14.04+ (clang 3.9 or gcc 4.9)
- [x] 🤖 Android NDK r17b+
- [x] 🖥️ Microsoft Windows Store 10

## Raison D'être :thought_balloon:
During the development of innovative new audio experiences, we required a driver that would reliably work on a number of different platforms for our experimentation purposes. We noticed that at the time of development no such open source software existed (that managed to support all the platforms we were looking for), so we decided to create a new one. Given that the common language we could use across our experimentation platforms was C++ we decided on that as the language of choice for our interface. It is also worth noting that this wasn't just designed for front end use cases, and as such has the ability to write out WAV files to disk at above real time speeds to support backend rendering use cases.

## Architecture :triangular_ruler:
`NFDriver` is designed as a common C++ interface to write information to different systems sound drivers in a low latency. The API simply allows you to create a driver that will then call the callbacks fed into it every time a new block of audio data is requested. It uses very basic C functions in order to reduce the amount of latency when interfacing to it, and to prevent unwanted locks in some implementations of the C++ 11 STL. It always has a fixed block size of 1024 samples it will ask for at any one time. It also has the ability to report errors, stutters, and give callbacks before and after the rendering of a block.

You may notice it has a fixed samplerate and number of channels. This was done due to this being the standard configuration in music output, so in order to lower the complexity of the API and the way each wrapper acts with the system we decided to hardcode these values.

It is designed with 2 major layers:
- **The Normalisation layer**, which takes input and resamples it to whatever channel format and samplerate the driver expects.
- **The System Layer**, which interfaces to the operating systems audio drivers.

Our support table looks like so:

| OS            | Underlying Framework                                                                                         | Status  |
| ------------- |:------------------------------------------------------------------------------------------------------------:| -------:|
| iOS           | [Core Audio](https://developer.apple.com/documentation/coreaudio?changes=_8)                                 | Stable  |
| OSX           | [Core Audio](https://developer.apple.com/documentation/coreaudio?changes=_8)                                 | Stable  |
| Linux         | [ALSA](https://en.wikipedia.org/wiki/Advanced_Linux_Sound_Architecture)                                      | Stable  |
| Android       | [OpenSL ES](https://developer.android.com/ndk/guides/audio/opensl/)                                          | Beta    |
| Windows       | [Media Foundation](https://docs.microsoft.com/en-us/windows/desktop/medfound/about-the-media-foundation-sdk) | Alpha   |

## Installation :inbox_tray:
`NFDriver` is a cmake project, while you can feel free to download the prebuilt static libraries it is recommended to use cmake to install this project into your wider project. In order to add this into a wider Cmake project, simply add the following line to your `CMakeLists.txt` file:
```
add_subdirectory(NFDriver)
```

### For iOS/OSX
Generate an Xcode project from the Cmake project like so:

```shell
$ mkdir build
$ cd build
$ cmake .. -GXcode
```

### For Linux
Generate a Ninja project from the Cmake project like so:

```shell
$ mkdir build
$ cd build
$ cmake .. -GNinja
```

### For Android
Use gradle to include the NFDriver project like so:

```
android {
    compileSdkVersion 26
    defaultConfig {
        applicationId "com.spotify.nfdrivertest_android"
        minSdkVersion 19
        targetSdkVersion 26
        versionCode 1
        versionName "1.0"
        externalNativeBuild {
            cmake {
                cppFlags ""
                arguments "-DANDROID_APP=1 -DANDROID=1"
            }
        }
    }

    sourceSets {
        main {
            jniLibs.srcDirs = ['src/main/cpp']
        }
    }

    externalNativeBuild {
        cmake {
            path "../CMakeLists.txt"
        }
    }
}
```

### For Windows
Generate a Visual Studio project from the Cmake project like so:

```shell
$ mkdir build
$ cd build
$ cmake .. -G "Visual Studio 12 2013 Win64"
```

## Usage example :eyes:
For examples of this in use, see the demo program `src/cli/NFDriverCLI.cpp`. The API is rather small, it basically has a create function, and a stop/start interface on the created class. You feed the create function with the necessary callbacks used for inputting audio data and then press play.

```C++
NF_STUTTER_CALLBACK stutter_callback = [](void *clientdata) {
  printf("Driver Stuttered...\n");
};

NF_RENDER_CALLBACK render_callback = [](void *clientdata, float *frames, int numberOfFrames) {
  const float samplerate = 2000.0f;
  const float multiplier = (2.0f * float(M_PI) * samplerate) / float(NF_DRIVER_SAMPLERATE);
  static unsigned int sinewave = 0;
  for (int n = 0; n < numberOfFrames; n++) {
    float audio = sinf(multiplier * sinewave++);
    for (int i = 0; i < NF_DRIVER_CHANNELS; ++i) {
      *frames++ = audio;
    }
  }
  return numberOfFrames;

};
NF_ERROR_CALLBACK error_callback = [](void *clientdata, const char *errorMessage, int errorCode) {
  printf("Driver Error (%d): %s\n", errorCode, errorMessage);
};

NF_WILL_RENDER_CALLBACK will_render_callback = [](void *clientdata) {};

NF_DID_RENDER_CALLBACK did_render_callback = [](void *clientdata) {};

NFDriver *driver = nativeformat::driver::createDriver(nullptr /* Anything client specific to use in the callbacks */,
                                                      stutter_callback /* called on stutter */,
                                                      render_callback /* called when we have samples to output */,
                                                      error_callback /* called upon driver error */,
                                                      will_render_callback /* Called before render_callback */,
                                                      did_render_callback /* Called after render callback */,
                                                      nativeformat::driver::OutputTypeSoundCard);
driver->setPlaying(true);
```

The above will output a sine wave at 2kHz on the audio card.

## Contributing :mailbox_with_mail:
Contributions are welcomed, have a look at the [CONTRIBUTING.md](CONTRIBUTING.md) document for more information.

## License :memo:
The project is available under the [Apache 2.0](http://www.apache.org/licenses/LICENSE-2.0) license.

### Acknowledgements
- Icon in readme banner is “[Audio](https://thenounproject.com/search/?q=audio&i=1514798)” by Becris from the Noun Project.

#### Contributors
* [Will Sackfield](https://github.com/8W9aG)
* Gabor Szanto
* [Julia Cox](https://github.com/astrocox)
* [Justin Sarma](https://github.com/jsarma)
* Noah Hilt
* [David Rubinstein](https://github.com/drubinstein)
