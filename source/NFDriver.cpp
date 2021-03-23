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
#include <NFDriver/NFDriver.h>

#include <cassert>

#include "NFDriverAdapter.h"
#include "NFDriverFileImplementation.h"
#include "NFDriverFileMP3Implementation.h"
#include "NFDriverFileAACImplementation.h"
#include "nfdriver_generated_header.h"

namespace nativeformat {
namespace driver {

const char *version() {
  return NFDRIVER_VERSION;
}

extern const std::string NF_DRIVER_BITRATE_KEY = "bitrate";

int bitrateOption(const std::map<std::string, std::string> &options) {
  if (options.count(NF_DRIVER_BITRATE_KEY)) {
    return std::stoi(options.at(NF_DRIVER_BITRATE_KEY));
  }
  return 128;
}

NFDriver *NFDriver::createNFDriver(void *clientdata,
                                   NF_STUTTER_CALLBACK stutter_callback,
                                   NF_RENDER_CALLBACK render_callback,
                                   NF_ERROR_CALLBACK error_callback,
                                   NF_WILL_RENDER_CALLBACK will_render_callback,
                                   NF_DID_RENDER_CALLBACK did_render_callback,
                                   OutputType output_type,
                                   const char *output_destination,
                                   std::map<std::string, std::string> options) {
  switch (output_type) {
    case OutputTypeSoundCard:
      return new NFSoundCardDriver(clientdata,
                                   stutter_callback,
                                   render_callback,
                                   error_callback,
                                   will_render_callback,
                                   did_render_callback);
    case OutputTypeFile:
      return new NFDriverFileImplementation(clientdata,
                                            stutter_callback,
                                            render_callback,
                                            error_callback,
                                            will_render_callback,
                                            did_render_callback,
                                            output_destination);
    case OutputTypeMP3File:
#if _WIN32
      assert(false && "No support for MP3 file driver on windows.");
#else
      return new NFDriverFileMP3Implementation(clientdata,
                                               stutter_callback,
                                               render_callback,
                                               error_callback,
                                               will_render_callback,
                                               did_render_callback,
                                               output_destination,
                                               bitrateOption(options));
#endif
    case OutputTypeAACFile:
#if __APPLE__
      return new NFDriverFileAACImplementation(clientdata,
                                               stutter_callback,
                                               render_callback,
                                               error_callback,
                                               will_render_callback,
                                               did_render_callback,
                                               output_destination,
                                               bitrateOption(options));
#else
      assert(false && "No support for AAC file driver on this platform.");
#endif
  }
  return 0;
}

}  // namespace driver
}  // namespace nativeformat
