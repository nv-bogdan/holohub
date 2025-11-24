# SPDX-FileCopyrightText: Copyright (c) 2025 HoloHub User
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.20)

project(dynamic_flow_demo)

find_package(holoscan 3.6.0 REQUIRED CONFIG
             PATHS "/opt/nvidia/holoscan" "/workspace/holoscan-sdk/install")


add_executable(dynamic_flow_demo
    src/main.cpp
)

target_link_libraries(dynamic_flow_demo
    PRIVATE
    holoscan::core
    holoscan::viz
    holoscan::viz::imgui
)

if(BUILD_TESTING)
  add_test(dynamic_flow_demo-test
    COMMAND echo "Add test coverage for dynamic_flow_demo here"
  )
endif()

