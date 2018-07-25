# Copyright (c) 2018 Spotify AB.
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
set(SOURCE_FILES
  ../include/NFDriver/NFDriver.h
  NFDriverAdapter.h
  NFDriverAdapter.cpp
  NFDriver.cpp
  NFDriverFileImplementation.h
  NFDriverFileImplementation.cpp)
set(LINK_LIBRARIES)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux" AND NOT ANDROID)
  list(APPEND
    SOURCE_FILES
    NFDriver_Linux.cpp)
  find_package(ALSA REQUIRED)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
  list(APPEND
    LINK_LIBRARIES
    ${ALSA_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT})
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin" AND NOT IOS AND NOT ANDROID)
  list(APPEND
    SOURCE_FILES
    NFDriver_MacOSX.cpp)
  find_library(AUDIO_UNIT AudioUnit)
  find_library(CORE_AUDIO CoreAudio)
  list(APPEND
    LINK_LIBRARIES
    ${AUDIO_UNIT}
    ${CORE_AUDIO})
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin" AND IOS AND NOT ANDROID)
  list(APPEND
    SOURCE_FILES
    NFDriver_iOS.mm)
  find_library(AVFOUNDATION AVFoundation)
  find_library(CORE_AUDIO CoreAudio)
  find_library(AUDIO_TOOLBOX AudioToolbox)
  list(APPEND
    LINK_LIBRARIES
    ${AVFOUNDATION}
    ${CORE_AUDIO}
    ${AUDIO_TOOLBOX})
endif()

if(WIN32 AND NOT ANDROID)
  list(APPEND
    SOURCE_FILES
    NFDriver_Windows.cpp)
  list(APPEND
    LINK_LIBRARIES
    dxva2.lib
    evr.lib
    mf.lib
    mfplat.lib
    mfplay.lib
    mfreadwrite.lib
    mfuuid.lib
    mmdevapi.lib)
endif()

if(ANDROID)
  list(APPEND
    SOURCE_FILES
    NFDriver_Android.cpp)
endif()

add_library(NFDriver STATIC ${SOURCE_FILES})
target_include_directories(NFDriver
  PUBLIC
  ../include
  ${CMAKE_BINARY_DIR}/output)
target_link_libraries(NFDriver ${LINK_LIBRARIES})

if(NOT ANDROID)
  add_subdirectory(cli)
endif()
