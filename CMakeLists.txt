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
cmake_minimum_required(VERSION 3.6)

project(NFDriver)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(LLVM_STDLIB)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
endif()

if(IOS)
  set(CMAKE_OSX_SYSROOT iphoneos)
  set(CMAKE_OSX_ARCHITECTURES $(ARCHS_STANDARD))
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -x objective-c++")
  link_directories(\${HOME}/\${SDKROOT}/lib)
  set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.spotify.\${PRODUCT_NAME:identifier}")
  set(APP_TYPE MACOSX_BUNDLE)
  set(CMAKE_EXE_LINKER_FLAGS "-framework UIKit")
endif()

if(WIN32)
  set(WINDOWS_FLAGS "/W3")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /WX")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /bigobj")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /D_CRT_SECURE_NO_WARNINGS=1")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4003")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4018")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4091")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4200")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4250")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4275")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4355")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4520")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4530")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4996")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4146")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /we4053")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /we4063")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /we4064")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /EHsc")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4503")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /DWIN32")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /DUNICODE")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /D_UNICODE")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /D_WINDOWS")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /DNOMINMAX")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /D_WIN32_IE=0x0700")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /D_WIN32_WINNT=0xA00")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /ZW")
  set(WINDOWS_FLAGS "${WINDOWS_FLAGS} /wd4447")
  set(WINDOWS_LINKER_FLAGS "${WINDOWS_LINKER_FLAGS} /INCREMENTAL:NO")
  set(WINDOWS_LINKER_FLAGS "${WINDOWS_LINKER_FLAGS} /OPT:ICF")
  set(WINDOWS_LINKER_FLAGS "${WINDOWS_LINKER_FLAGS} /OPT:REF")
  set(CMAKE_C_FLAGS
    "${CMAKE_C_FLAGS} ${WINDOWS_FLAGS}")
  set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} ${WINDOWS_FLAGS}")
  set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} ${WINDOWS_LINKER_FLAGS}")
endif()

add_subdirectory(source)

execute_process(COMMAND python
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate-version.py"
  ${CMAKE_BINARY_DIR}/output
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
