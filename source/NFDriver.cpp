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
#include <NFDriver/NFDriver.h>

#include "NFDriverAdapter.h"
#include "NFDriverFileImplementation.h"
#include "nfdriver_generated_header.h"

namespace nativeformat {
namespace driver {

const char *version() { return NFDRIVER_VERSION; }

NFDriver *NFDriver::createNFDriver(void *clientdata,
                                   NF_STUTTER_CALLBACK stutter_callback,
                                   NF_RENDER_CALLBACK render_callback,
                                   NF_ERROR_CALLBACK error_callback,
                                   NF_WILL_RENDER_CALLBACK will_render_callback,
                                   NF_DID_RENDER_CALLBACK did_render_callback,
                                   OutputType outputType,
                                   const char *output_destination) {
  switch (outputType) {
  case OutputTypeSoundCard:
    return new NFSoundCardDriver(clientdata, stutter_callback, render_callback,
                                 error_callback, will_render_callback,
                                 did_render_callback);
  case OutputTypeFile:
    return new NFDriverFileImplementation(
        clientdata, stutter_callback, render_callback, error_callback,
        will_render_callback, did_render_callback, output_destination);
  }
  return 0;
}

} // namespace driver
} // namespace nativeformat
