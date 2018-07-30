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
#pragma once

#include <NFDriver/NFDriver.h>

#include <atomic>
#include <string>
#include <thread>

namespace nativeformat {
namespace driver {

class NFDriverFileImplementation : public NFDriver {
 public:
  bool isPlaying() const;
  void setPlaying(bool playing);

  NFDriverFileImplementation(void *clientdata,
                             NF_STUTTER_CALLBACK stutter_callback,
                             NF_RENDER_CALLBACK render_callback,
                             NF_ERROR_CALLBACK error_callback,
                             NF_WILL_RENDER_CALLBACK will_render_callback,
                             NF_DID_RENDER_CALLBACK did_render_callback,
                             const char *output_destination);
  ~NFDriverFileImplementation();

 private:
  void *_clientdata;
  const NF_STUTTER_CALLBACK _stutter_callback;
  const NF_RENDER_CALLBACK _render_callback;
  const NF_ERROR_CALLBACK _error_callback;
  const NF_WILL_RENDER_CALLBACK _will_render_callback;
  const NF_DID_RENDER_CALLBACK _did_render_callback;
  const std::string _output_destination;

  std::shared_ptr<std::thread> _thread;
  std::atomic<bool> _run;

  static void run(NFDriverFileImplementation *driver);
};

}  // namespace driver
}  // namespace nativeformat
