/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights
 * reserved. SPDX-License-Identifier: Apache-2.0
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

#include <getopt.h>

#include <holoscan/holoscan.hpp>
#include <holoscan/operators/format_converter/format_converter.hpp>
#include <holoscan/operators/holoviz/holoviz.hpp>
#include <holoscan/operators/inference/inference.hpp>
#include <holoscan/operators/inference_processor/inference_processor.hpp>
#include <holoscan/core/gxf/entity.hpp>
#include <lstm_tensor_rt_inference.hpp>
#include <slang_shader_op.hpp>
#include <string>
#include <tool_tracking_postprocessor.hpp>
#include <aja_source.hpp>

#include <holoscan/version_config.hpp>

#define HOLOSCAN_VERSION \
  (HOLOSCAN_VERSION_MAJOR * 10000 + HOLOSCAN_VERSION_MINOR * 100 + HOLOSCAN_VERSION_PATCH)

class App : public holoscan::Application {
 public:
  void set_path(const std::string& path) { app_path_ = path; }
  void set_postprocessor(const std::string& postprocessor) { this->postprocessor_ = postprocessor; }

  void set_datapath(const std::string& path) { datapath = path; }

  void compose() override {
    using namespace holoscan;

    std::shared_ptr<Operator> source;
    std::shared_ptr<Operator> visualizer_operator;
    std::shared_ptr<Operator> visualizer_operator_2;

    const bool use_rdma = from_config("aja.rdma").as<bool>();
    const bool overlay_enabled = from_config("aja.enable_overlay").as<bool>();
    HOLOSCAN_LOG_INFO("Overlay enabled: {}", overlay_enabled);

    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t source_block_size = 0;
    uint64_t source_num_blocks = 0;

    width = from_config("aja.width").as<uint32_t>();
    height = from_config("aja.height").as<uint32_t>();
    source = make_operator<ops::AJASourceMultiChannelOp>(
        "aja", from_config("aja"));

    source_block_size = width * height * 4 * 4;
    source_num_blocks = use_rdma ? 3 : 4;

      const std::shared_ptr<CudaStreamPool> cuda_stream_pool =
      make_resource<CudaStreamPool>("cuda_stream", 0, 0, 0, 1, 12);  // Increased from 5 to 12 streams

    auto format_converter =
        make_operator<ops::FormatConverterOp>("format_converter",
                                              from_config("format_converter_aja"),
                                              Arg("pool") = make_resource<BlockMemoryPool>(
                                                  "pool_1", 1, source_block_size, source_num_blocks),
                                              Arg("cuda_stream_pool") = cuda_stream_pool);
    format_converter->spec()->input<gxf::Entity>("source_video").condition(
        holoscan::ConditionType::kMessageAvailable,
        holoscan::Arg("min_size", static_cast<uint64_t>(1)),
        holoscan::Arg("front_stage_max_size", static_cast<size_t>(1))
    );
    
    auto format_converter_2 =
        make_operator<ops::FormatConverterOp>("format_converter_2",
                                              from_config("format_converter_aja"),
                                              Arg("pool") = make_resource<BlockMemoryPool>(
                                                  "pool_2", 1, source_block_size, source_num_blocks),
                                              Arg("cuda_stream_pool") = cuda_stream_pool);
    format_converter_2->spec()->input<gxf::Entity>("source_video").condition(
        holoscan::ConditionType::kMessageAvailable,
        holoscan::Arg("min_size", static_cast<uint64_t>(1)),
        holoscan::Arg("front_stage_max_size", static_cast<size_t>(1))
    );
    const std::string model_file_path = datapath + "/tool_loc_convlstm.onnx";
    const std::string engine_cache_dir = datapath + "/engines";

    const uint64_t lstm_inferer_block_size = 107 * 60 * 7 * 4;
    const uint64_t lstm_inferer_num_blocks = 2 + 5 * 2;
    auto lstm_inferer = make_operator<ops::LSTMTensorRTInferenceOp>(
        "lstm_inferer",
        from_config("lstm_inference"),
        Arg("model_file_path", model_file_path),
        Arg("engine_cache_dir", engine_cache_dir),
        Arg("pool") = make_resource<BlockMemoryPool>(
            "pool_lstm_1", 1, lstm_inferer_block_size, lstm_inferer_num_blocks),
        Arg("cuda_stream_pool") = cuda_stream_pool);

    auto lstm_inferer_2 = make_operator<ops::LSTMTensorRTInferenceOp>(
        "lstm_inferer_2",
        from_config("lstm_inference"),
        Arg("model_file_path", model_file_path),
        Arg("engine_cache_dir", engine_cache_dir),
        Arg("pool") = make_resource<BlockMemoryPool>(
            "pool_lstm_2", 1, lstm_inferer_block_size, lstm_inferer_num_blocks),
        Arg("cuda_stream_pool") = cuda_stream_pool);

    // the tool tracking post process outputs
    // - a RGBA float32 color mask
    // - coordinates with x,y and size in float32
    const uint64_t tool_tracking_postprocessor_block_size =
        std::max(107 * 60 * 7 * 4 * sizeof(float), 7 * 3 * sizeof(float));
    const uint64_t tool_tracking_postprocessor_num_blocks = 2 * 2;
    std::shared_ptr<Operator> tool_tracking_postprocessor;
    std::shared_ptr<Operator> tool_tracking_postprocessor_2;
    std::shared_ptr<BlockMemoryPool> postprocessor_allocator =
        make_resource<BlockMemoryPool>("device_allocator",
                                       1,
                                       tool_tracking_postprocessor_block_size,
                                       tool_tracking_postprocessor_num_blocks);
    std::shared_ptr<BlockMemoryPool> postprocessor_allocator_2 =
        make_resource<BlockMemoryPool>("device_allocator_2",
                                       1,
                                       tool_tracking_postprocessor_block_size,
                                       tool_tracking_postprocessor_num_blocks);
    if (postprocessor_ == "slang_shader") {
      tool_tracking_postprocessor = make_operator<ops::SlangShaderOp>(
          "slang_postprocessor",
          Arg("shader_source_file", app_path_ + "/postprocessor.slang"),
          Arg("allocator") = postprocessor_allocator);
      tool_tracking_postprocessor_2 = make_operator<ops::SlangShaderOp>(
          "slang_postprocessor_2",
          Arg("shader_source_file", app_path_ + "/postprocessor.slang"),
          Arg("allocator") = postprocessor_allocator_2);
    } else if (postprocessor_ == "tool_tracking_postprocessor") {
      tool_tracking_postprocessor = make_operator<ops::ToolTrackingPostprocessorOp>(
          "tool_tracking_postprocessor",
          Arg("device_allocator") = postprocessor_allocator);
      tool_tracking_postprocessor_2 = make_operator<ops::ToolTrackingPostprocessorOp>(
          "tool_tracking_postprocessor_2",
          Arg("device_allocator") = postprocessor_allocator_2);
    } else {
      throw std::runtime_error("Invalid postprocessor: " + postprocessor_);
    }
    std::shared_ptr<BlockMemoryPool> visualizer_allocator;

    visualizer_operator = make_operator<ops::HolovizOp>(
        "holoviz",
        from_config(overlay_enabled ? "holoviz_overlay" : "holoviz"),
        Arg("width") = width,
        Arg("height") = height,
        Arg("enable_render_buffer_input") = overlay_enabled,
        Arg("enable_render_buffer_output") = overlay_enabled,
        Arg("allocator") = visualizer_allocator,
        Arg("cuda_stream_pool") = cuda_stream_pool);

    visualizer_operator_2 = make_operator<ops::HolovizOp>(
        "holoviz_2",
        from_config(overlay_enabled ? "holoviz_overlay" : "holoviz"),
        Arg("width") = width,
        Arg("height") = height,
        Arg("enable_render_buffer_input") = overlay_enabled,
        Arg("enable_render_buffer_output") = overlay_enabled,
        Arg("allocator") = visualizer_allocator,
        Arg("cuda_stream_pool") = cuda_stream_pool);

    // SR operators
    auto visualizer_sr = make_operator<ops::HolovizOp>("holoviz_sr", from_config("holoviz_sr"),
        Arg("cuda_stream_pool") = cuda_stream_pool);

    auto inference = make_operator<ops::InferenceOp>(
        "inference",
        from_config("sr_inference"),
        Arg("allocator") = make_resource<UnboundedAllocator>("pool_inference"));

    auto drop_alpha =
        make_operator<ops::FormatConverterOp>("drop_alpha", from_config("drop_alpha"),
        Arg("pool") = make_resource<UnboundedAllocator>("pool_drop_alpha"));

    auto preprocessor =
        make_operator<ops::FormatConverterOp>("preprocessor", from_config("inference_preprocessor"),
        Arg("pool") = make_resource<UnboundedAllocator>("pool_preprocessor"));

    auto postprocessor =
        make_operator<ops::FormatConverterOp>("postprocessor", from_config("inference_postprocessor"),
        Arg("pool") = make_resource<UnboundedAllocator>("pool_postprocessor"));


    // Flow definition for Channel 1
    add_flow(lstm_inferer, tool_tracking_postprocessor, {{"tensor", "in"}});
    add_flow(tool_tracking_postprocessor, visualizer_operator, {{"out", "receivers"}});
    add_flow(source, format_converter, {{"video_buffer_output", "source_video"}});
    add_flow(format_converter, lstm_inferer);

    // Flow definition for Channel 2
    add_flow(lstm_inferer_2, tool_tracking_postprocessor_2, {{"tensor", "in"}});
    add_flow(tool_tracking_postprocessor_2, visualizer_operator_2, {{"out", "receivers"}});
    add_flow(source, format_converter_2, {{"video_buffer_output_2", "source_video"}});
    add_flow(format_converter_2, lstm_inferer_2);

    if (overlay_enabled) {
      // Overlay buffer flow for Channel 1
      add_flow(source, visualizer_operator, {{"overlay_buffer_output", "render_buffer_input"}});
      add_flow(visualizer_operator, source, {{"render_buffer_output", "overlay_buffer_input"}});
      
      // Overlay buffer flow for Channel 2
      add_flow(source, visualizer_operator_2, {{"overlay_buffer_output_2", "render_buffer_input"}});
      add_flow(visualizer_operator_2, source, {{"render_buffer_output", "overlay_buffer_input_2"}});
    } else {
      add_flow(source, visualizer_operator, {{"video_buffer_output", "receivers"}});
      add_flow(source, visualizer_operator_2, {{"video_buffer_output_2", "receivers"}});
    }

    // SR flow
    add_flow(source, drop_alpha, {{"video_buffer_output_2", ""}});
    add_flow(drop_alpha, preprocessor);
    add_flow(preprocessor, inference, {{"", "receivers"}});
    add_flow(inference, postprocessor);
    add_flow(postprocessor, visualizer_sr, {{"tensor", "receivers"}});
  }

 private:
  std::string app_path_ = "";
  std::string postprocessor_ = "tool_tracking_postprocessor";
  std::string datapath = "";
};

/** Helper function to parse the command line arguments */
bool parse_arguments(int argc, char** argv, std::string& data_path, std::string& config_path) {
  static struct option long_options[] = {
      {"data", required_argument, 0, 'd'}, {"config", required_argument, 0, 'c'}, {0, 0, 0, 0}};

  while (int c = getopt_long(argc, argv, "d:c:", long_options, NULL)) {
    if (c == -1 || c == '?')
      break;

    switch (c) {
      case 'c':
        config_path = optarg;
        break;
      case 'd':
        data_path = optarg;
        break;
      default:
        holoscan::log_error("Unhandled option '{}'", static_cast<char>(c));
        return false;
    }
  }

  return true;
}

/** Main function */
int main(int argc, char** argv) {
  // Parse the arguments
  std::string config_path = "";
  std::string data_directory = "";
  if (!parse_arguments(argc, argv, data_directory, config_path)) {
    return 1;
  }
  if (data_directory.empty()) {
    // Get the input data environment variable
    auto input_path = std::getenv("HOLOSCAN_INPUT_PATH");
    if (input_path != nullptr && input_path[0] != '\0') {
      data_directory = std::string(input_path);
    } else if (std::filesystem::is_directory(std::filesystem::current_path() / "data/endoscopy")) {
      data_directory = std::string((std::filesystem::current_path() / "data/endoscopy").c_str());
    } else {
      HOLOSCAN_LOG_ERROR(
          "Input data not provided. Use --data or set HOLOSCAN_INPUT_PATH environment variable.");
      exit(-1);
    }
  }

  std::string app_path(PATH_MAX, '\0');
  if (readlink("/proc/self/exe", app_path.data(), app_path.size() - 1) == -1) {
    HOLOSCAN_LOG_ERROR("Failed to get the application path");
    exit(-1);
  }
  app_path = std::filesystem::canonical(app_path).parent_path();

  if (config_path.empty()) {
    // Get the input data environment variable
    auto config_file_path = std::getenv("HOLOSCAN_CONFIG_PATH");
    if (config_file_path == nullptr || config_file_path[0] == '\0') {
      config_path = app_path / std::filesystem::path("endoscopy_tool_tracking.yaml");
    } else {
      config_path = config_file_path;
    }
  }

  auto app = holoscan::make_application<App>();

  HOLOSCAN_LOG_INFO("Using configuration file from {}", config_path);
  app->config(config_path);

  app->set_path(app_path);

  auto postprocessor = app->from_config("postprocessor").as<std::string>();
  app->set_postprocessor(postprocessor);

  HOLOSCAN_LOG_INFO("Using input data from {}", data_directory);
  app->set_datapath(data_directory);

  // Configure scheduler from config
  auto scheduler = app->from_config("scheduler").as<std::string>();
  if (scheduler == "multi_thread") {
    // use MultiThreadScheduler instead of the default GreedyScheduler
    app->scheduler(app->make_scheduler<holoscan::MultiThreadScheduler>(
        "multithread-scheduler", app->from_config("multi_thread_scheduler")));
  } else if (scheduler == "event_based") {
    // use EventBasedScheduler instead of the default GreedyScheduler
    app->scheduler(app->make_scheduler<holoscan::EventBasedScheduler>(
        "event-based-scheduler", app->from_config("event_based_scheduler")));
  } else if (scheduler == "greedy") {
    app->scheduler(app->make_scheduler<holoscan::GreedyScheduler>(
        "greedy-scheduler", app->from_config("greedy_scheduler")));
  } else if (scheduler != "default") {
    throw std::runtime_error(fmt::format(
        "unrecognized scheduler option '{}', should be one of {'multi_thread', 'event_based', "
        "'greedy', 'default'}",
        scheduler));
  }

  app->run();

  return 0;
}
