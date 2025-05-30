/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OPERATORS_VOLUME_LOADER_VOLUME_LOADER
#define OPERATORS_VOLUME_LOADER_VOLUME_LOADER

#include <holoscan/holoscan.hpp>

namespace holoscan::ops {

class VolumeLoaderOp : public Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(VolumeLoaderOp);

  void initialize() override;
  void setup(OperatorSpec& spec) override;
  void compute(InputContext&, OutputContext& op_output, ExecutionContext&) override;

 private:
  Parameter<std::string> file_name_;
  Parameter<std::shared_ptr<Allocator>> allocator_;
};

}  // namespace holoscan::ops

#endif /* OPERATORS_VOLUME_LOADER_VOLUME_LOADER */
