/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "aja_source.hpp"

#include <cuda.h>
#include <cuda_runtime.h>

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gxf/multimedia/video.hpp"
#include "holoscan/core/condition.hpp"
#include "holoscan/core/execution_context.hpp"
#include "holoscan/core/gxf/entity.hpp"
#include "holoscan/core/io_spec.hpp"
#include "holoscan/core/operator_spec.hpp"

#include <fmt/format.h>

namespace holoscan::ops {

// NTV2Channel string to enum mapping
constexpr std::array<std::pair<std::string_view, NTV2Channel>, 8> NTV2ChannelMapping = {{
    {"NTV2_CHANNEL1", NTV2Channel::NTV2_CHANNEL1},
    {"NTV2_CHANNEL2", NTV2Channel::NTV2_CHANNEL2},
    {"NTV2_CHANNEL3", NTV2Channel::NTV2_CHANNEL3},
    {"NTV2_CHANNEL4", NTV2Channel::NTV2_CHANNEL4},
    {"NTV2_CHANNEL5", NTV2Channel::NTV2_CHANNEL5},
    {"NTV2_CHANNEL6", NTV2Channel::NTV2_CHANNEL6},
    {"NTV2_CHANNEL7", NTV2Channel::NTV2_CHANNEL7},
    {"NTV2_CHANNEL8", NTV2Channel::NTV2_CHANNEL8}
}};

// Convert string to NTV2Channel enum
constexpr NTV2Channel ToNTV2Channel(std::string_view value) {
  for (const auto& [name, channel] : NTV2ChannelMapping) {
    if (name == value) {
      return channel;
    }
  }
  return NTV2Channel::NTV2_CHANNEL_INVALID;
}

// used in more than one function
constexpr uint32_t kNumBuffers = 2;

AJASourceOp::AJASourceOp() {
  HOLOSCAN_LOG_INFO("AJASourceOp::AJASourceOp - Constructor called");
}

void AJASourceOp::setup(OperatorSpec& spec) {
  auto& video_buffer_output = spec.output<gxf::Entity>("video_buffer_output");
  auto& video_buffer_output_2 = spec.output<gxf::Entity>("video_buffer_output_2");
  auto& video_buffer_output_3 = spec.output<gxf::Entity>("video_buffer_output_3");
  auto& video_buffer_output_4 = spec.output<gxf::Entity>("video_buffer_output_4");
  spec.input<gxf::Entity>("overlay_buffer_input").condition(ConditionType::kNone);
  auto& overlay_buffer_output = spec.output<gxf::Entity>("overlay_buffer_output");
  auto& overlay_buffer_output_2 = spec.output<gxf::Entity>("overlay_buffer_output_2");
  auto& overlay_buffer_output_3 = spec.output<gxf::Entity>("overlay_buffer_output_3");
  auto& overlay_buffer_output_4 = spec.output<gxf::Entity>("overlay_buffer_output_4");

  constexpr char kDefaultDevice[] = "0";
  const std::vector<std::string> kDefaultChannel = {"NTV2_CHANNEL1"};
  constexpr uint32_t kDefaultWidth = 1920;
  constexpr uint32_t kDefaultHeight = 1080;
  constexpr uint32_t kDefaultFramerate = 60;
  constexpr bool kDefaultInterlaced = false;
  constexpr bool kDefaultRDMA = false;
  constexpr bool kDefaultEnableOverlay = false;
  constexpr bool kDefaultOverlayRDMA = false;
  const std::vector<std::string> kDefaultOverlayChannel = {"NTV2_CHANNEL3"};

  spec.param(video_buffer_output_,
             "video_buffer_output",
             "VideoBufferOutput",
             "Output for the first video buffer.",
             &video_buffer_output);
  spec.param(video_buffer_output_2_,
             "video_buffer_output_2",
             "VideoBufferOutput2",
             "Output for the second video buffer.",
             &video_buffer_output_2);
  spec.param(video_buffer_output_3_,
             "video_buffer_output_3",
             "VideoBufferOutput3",
             "Output for the third video buffer.",
             &video_buffer_output_3);
  spec.param(video_buffer_output_4_,
             "video_buffer_output_4",
             "VideoBufferOutput4",
             "Output for the fourth video buffer.",
             &video_buffer_output_4);
  spec.param(
      device_specifier_, "device", "Device", "Device specifier.", std::string(kDefaultDevice));
  spec.param(channels_param_, "channels", "Channels", "NTV2Channels to use.", kDefaultChannel);
  spec.param(
      overlay_channels_param_,"overlay_channels", "OverlayChannels",
      "NTV2Channels to use for overlay output.", kDefaultOverlayChannel);
  spec.param(width_, "width", "Width", "Width of the stream.", kDefaultWidth);
  spec.param(height_, "height", "Height", "Height of the stream.", kDefaultHeight);
  spec.param(framerate_, "framerate", "Framerate", "Framerate of the stream.", kDefaultFramerate);
  spec.param(interlaced_, "interlaced", "Interlaced", "Interlaced or not.", kDefaultInterlaced);
  spec.param(use_rdma_, "rdma", "RDMA", "Enable RDMA.", kDefaultRDMA);
  spec.param(
      enable_overlay_, "enable_overlay", "EnableOverlay", "Enable overlay.", kDefaultEnableOverlay);
  spec.param(
      overlay_rdma_, "overlay_rdma", "OverlayRDMA", "Enable overlay RDMA.", kDefaultOverlayRDMA);
  spec.param(overlay_buffer_output_,
             "overlay_buffer_output",
             "OverlayBufferOutput",
             "Output for the first overlay buffer.",
             &overlay_buffer_output);
  spec.param(overlay_buffer_output_2_,
             "overlay_buffer_output_2",
             "OverlayBufferOutput2",
             "Output for the second overlay buffer.",
             &overlay_buffer_output_2);
  spec.param(overlay_buffer_output_3_,
             "overlay_buffer_output_3",
             "OverlayBufferOutput3",
             "Output for the third overlay buffer.",
             &overlay_buffer_output_3);
  spec.param(overlay_buffer_output_4_,
             "overlay_buffer_output_4",
             "OverlayBufferOutput4",
             "Output for the fourth overlay buffer.",
             &overlay_buffer_output_4);
  spec.param(overlay_buffer_input_,
             "overlay_buffer_input",
             "OverlayBufferInput",
             "Input for a filled overlay buffer.");
}

AJAStatus AJASourceOp::DetermineVideoFormat() {
  video_format_ = NTV2_FORMAT_UNKNOWN;

  if (interlaced_) {
    if (width_ == 1920 && height_ == 1080) {
      if (framerate_ == 50) {
        video_format_ = NTV2_FORMAT_1080i_5000;
      } else if (framerate_ == 59) {
        video_format_ = NTV2_FORMAT_1080i_5994;
      } else if (framerate_ == 60) {
        video_format_ = NTV2_FORMAT_1080i_6000;
      }
    }
  } else {
    if (width_ == 1280 && height_ == 720) {
      if (framerate_ == 50) {
        video_format_ = NTV2_FORMAT_720p_5000;
      } else if (framerate_ == 59) {
        video_format_ = NTV2_FORMAT_720p_5994;
      } else if (framerate_ == 60) {
        video_format_ = NTV2_FORMAT_720p_6000;
      }
    } else if (width_ == 1920 && height_ == 1080) {
      if (framerate_ == 23) {
        video_format_ = NTV2_FORMAT_1080p_2398;
      } else if (framerate_ == 24) {
        video_format_ = NTV2_FORMAT_1080p_2400;
      } else if (framerate_ == 25) {
        video_format_ = NTV2_FORMAT_1080p_2500;
      } else if (framerate_ == 29) {
        video_format_ = NTV2_FORMAT_1080p_2997;
      } else if (framerate_ == 30) {
        video_format_ = NTV2_FORMAT_1080p_3000;
      } else if (framerate_ == 50) {
        video_format_ = NTV2_FORMAT_1080p_5000_A;
      } else if (framerate_ == 59) {
        video_format_ = NTV2_FORMAT_1080p_5994_A;
      } else if (framerate_ == 60) {
        video_format_ = NTV2_FORMAT_1080p_6000_A;
      }
    } else if (width_ == 3840 && height_ == 2160) {
      if (framerate_ == 23) {
        video_format_ = NTV2_FORMAT_3840x2160p_2398;
      } else if (framerate_ == 24) {
        video_format_ = NTV2_FORMAT_3840x2160p_2400;
      } else if (framerate_ == 25) {
        video_format_ = NTV2_FORMAT_3840x2160p_2500;
      } else if (framerate_ == 29) {
        video_format_ = NTV2_FORMAT_3840x2160p_2997;
      } else if (framerate_ == 30) {
        video_format_ = NTV2_FORMAT_3840x2160p_3000;
      } else if (framerate_ == 50) {
        video_format_ = NTV2_FORMAT_3840x2160p_5000;
      } else if (framerate_ == 59) {
        video_format_ = NTV2_FORMAT_3840x2160p_5994;
      } else if (framerate_ == 60) {
        video_format_ = NTV2_FORMAT_3840x2160p_6000;
      }
    } else if (width_ == 4096 && height_ == 2160) {
      if (framerate_ == 23) {
        video_format_ = NTV2_FORMAT_4096x2160p_2398;
      } else if (framerate_ == 24) {
        video_format_ = NTV2_FORMAT_4096x2160p_2400;
      } else if (framerate_ == 25) {
        video_format_ = NTV2_FORMAT_4096x2160p_2500;
      } else if (framerate_ == 29) {
        video_format_ = NTV2_FORMAT_4096x2160p_2997;
      } else if (framerate_ == 30) {
        video_format_ = NTV2_FORMAT_4096x2160p_3000;
      } else if (framerate_ == 50) {
        video_format_ = NTV2_FORMAT_4096x2160p_5000;
      } else if (framerate_ == 59) {
        video_format_ = NTV2_FORMAT_4096x2160p_5994;
      } else if (framerate_ == 60) {
        video_format_ = NTV2_FORMAT_4096x2160p_6000;
      }
    }
  }

  return (video_format_ == NTV2_FORMAT_UNKNOWN) ? AJA_STATUS_UNSUPPORTED : AJA_STATUS_SUCCESS;
}

AJAStatus AJASourceOp::OpenDevice() {
  // Get the requested device.
  if (!CNTV2DeviceScanner::GetFirstDeviceFromArgument(device_specifier_, device_)) {
    HOLOSCAN_LOG_ERROR("Device {} not found.", device_specifier_.get());
    return AJA_STATUS_OPEN;
  }

  // Check if the device is ready.
  if (!device_.IsDeviceReady(false)) {
    HOLOSCAN_LOG_ERROR("Device {} not ready.", device_specifier_.get());
    return AJA_STATUS_INITIALIZE;
  }

  // Get the device ID.
  device_id_ = device_.GetDeviceID();

  // Detect Kona HDMI device.
  is_kona_hdmi_ = NTV2DeviceGetNumHDMIVideoInputs(device_id_) > 1;

  // Check if a TSI 4x format is needed.
  if (is_kona_hdmi_) { use_tsi_ = GetNTV2VideoFormatTSI(&video_format_); }

  // Check device capabilities.
  if (!NTV2DeviceCanDoVideoFormat(device_id_, video_format_)) {
    HOLOSCAN_LOG_ERROR("AJA device does not support requested video format.");
    return AJA_STATUS_UNSUPPORTED;
  }
  if (!NTV2DeviceCanDoFrameBufferFormat(device_id_, pixel_format_)) {
    HOLOSCAN_LOG_ERROR("AJA device does not support requested pixel format.");
    return AJA_STATUS_UNSUPPORTED;
  }
  if (!NTV2DeviceCanDoCapture(device_id_)) {
    HOLOSCAN_LOG_ERROR("AJA device cannot capture video.");
    return AJA_STATUS_UNSUPPORTED;
  }
  // Validate all channels
  for (const auto& channel : channels_) {
    if (!NTV2_IS_VALID_CHANNEL(channel)) {
      HOLOSCAN_LOG_ERROR("Invalid AJA channel: {}", static_cast<int>(channel));
      return AJA_STATUS_UNSUPPORTED;
    }
  }

  // Check overlay capabilities.
  if (enable_overlay_) {
    for (const auto& overlay_channel : overlay_channels_) {
      if (!NTV2_IS_VALID_CHANNEL(overlay_channel)) {
        HOLOSCAN_LOG_ERROR("Invalid overlay channel: {}", static_cast<int>(overlay_channel));
        return AJA_STATUS_UNSUPPORTED;
      }
    }

    if (NTV2DeviceGetNumVideoChannels(device_id_) < 2) {
      HOLOSCAN_LOG_ERROR("Insufficient number of video channels");
      return AJA_STATUS_UNSUPPORTED;
    }

    if (NTV2DeviceGetNumFrameStores(device_id_) < 2) {
      HOLOSCAN_LOG_ERROR("Insufficient number of frame stores");
      return AJA_STATUS_UNSUPPORTED;
    }

    if (NTV2DeviceGetNumMixers(device_id_) < 1) {
      HOLOSCAN_LOG_ERROR("Hardware mixing not supported");
      return AJA_STATUS_UNSUPPORTED;
    }

    if (!NTV2DeviceHasBiDirectionalSDI(device_id_)) {
      HOLOSCAN_LOG_ERROR("BiDirectional SDI not supported");
      return AJA_STATUS_UNSUPPORTED;
    }
  }

  return AJA_STATUS_SUCCESS;
}



AJAStatus AJASourceOp::SetupVideo() {
  constexpr size_t kWarmupFrames = 5;

  // Setup each channel individually
  for (const auto& channel : channels_) {
    NTV2InputSourceKinds input_kind = is_kona_hdmi_ ? NTV2_INPUTSOURCES_HDMI : NTV2_INPUTSOURCES_SDI;
    NTV2InputSource input_src = ::NTV2ChannelToInputSource(channel, input_kind);
    NTV2Channel tsi_channel = static_cast<NTV2Channel>(channel + 1);

    if (!IsRGBFormat(pixel_format_)) {
      HOLOSCAN_LOG_ERROR("YUV formats not yet supported");
      return AJA_STATUS_UNSUPPORTED;
    }

    // Detect if the source is YUV or RGB (i.e. if CSC is required or not).
    bool is_input_rgb(false);
    if (input_kind == NTV2_INPUTSOURCES_HDMI) {
      NTV2LHIHDMIColorSpace input_color;
      device_.GetHDMIInputColor(input_color, channel);
      is_input_rgb = (input_color == NTV2_LHIHDMIColorSpaceRGB);
    }

    // Setup the input routing for this channel.
    device_.EnableChannel(channel);
    if (use_tsi_) {
      device_.SetTsiFrameEnable(true, channel);
      device_.EnableChannel(tsi_channel);
    }
    device_.SetMode(channel, NTV2_MODE_CAPTURE);
    if (NTV2DeviceHasBiDirectionalSDI(device_id_) && NTV2_INPUT_SOURCE_IS_SDI(input_src)) {
      device_.SetSDITransmitEnable(channel, false);
    }
    device_.SetVideoFormat(video_format_, false, false, channel);
    device_.SetFrameBufferFormat(channel, pixel_format_);
    if (use_tsi_) { device_.SetFrameBufferFormat(tsi_channel, pixel_format_); }
    device_.EnableInputInterrupt(channel);
    device_.SubscribeInputVerticalEvent(channel);

    NTV2OutputXptID input_output_xpt =
        GetInputSourceOutputXpt(input_src, /*DS2*/ false, is_input_rgb, /*Quadrant*/ 0);
    NTV2InputXptID fb_input_xpt(GetFrameBufferInputXptFromChannel(channel));
    if (use_tsi_) {
      if (!is_input_rgb) {
        if (NTV2DeviceGetNumCSCs(device_id_) < 4) {
          HOLOSCAN_LOG_ERROR("CSCs not available for TSI input.");
          return AJA_STATUS_UNSUPPORTED;
        }
        device_.Connect(NTV2_XptFrameBuffer1Input, NTV2_Xpt425Mux1ARGB);
        device_.Connect(NTV2_XptFrameBuffer1DS2Input, NTV2_Xpt425Mux1BRGB);
        device_.Connect(NTV2_XptFrameBuffer2Input, NTV2_Xpt425Mux2ARGB);
        device_.Connect(NTV2_XptFrameBuffer2DS2Input, NTV2_Xpt425Mux2BRGB);
        device_.Connect(NTV2_Xpt425Mux1AInput, NTV2_XptCSC1VidRGB);
        device_.Connect(NTV2_Xpt425Mux1BInput, NTV2_XptCSC2VidRGB);
        device_.Connect(NTV2_Xpt425Mux2AInput, NTV2_XptCSC3VidRGB);
        device_.Connect(NTV2_Xpt425Mux2BInput, NTV2_XptCSC4VidRGB);
        device_.Connect(NTV2_XptCSC1VidInput, NTV2_XptHDMIIn1);
        device_.Connect(NTV2_XptCSC2VidInput, NTV2_XptHDMIIn1Q2);
        device_.Connect(NTV2_XptCSC3VidInput, NTV2_XptHDMIIn1Q3);
        device_.Connect(NTV2_XptCSC4VidInput, NTV2_XptHDMIIn1Q4);
      } else {
        device_.Connect(NTV2_XptFrameBuffer1Input, NTV2_Xpt425Mux1ARGB);
        device_.Connect(NTV2_XptFrameBuffer1DS2Input, NTV2_Xpt425Mux1BRGB);
        device_.Connect(NTV2_XptFrameBuffer2Input, NTV2_Xpt425Mux2ARGB);
        device_.Connect(NTV2_XptFrameBuffer2DS2Input, NTV2_Xpt425Mux2BRGB);
        device_.Connect(NTV2_Xpt425Mux1AInput, NTV2_XptHDMIIn1RGB);
        device_.Connect(NTV2_Xpt425Mux1BInput, NTV2_XptHDMIIn1Q2RGB);
        device_.Connect(NTV2_Xpt425Mux2AInput, NTV2_XptHDMIIn1Q3RGB);
        device_.Connect(NTV2_Xpt425Mux2BInput, NTV2_XptHDMIIn1Q4RGB);
      }
    } else if (!is_input_rgb) {
      if (NTV2DeviceGetNumCSCs(device_id_) <= static_cast<int>(channel)) {
        HOLOSCAN_LOG_ERROR("No CSC available for NTV2_CHANNEL{}", static_cast<int>(channel) + 1);
        return AJA_STATUS_UNSUPPORTED;
      }
      NTV2InputXptID csc_input = GetCSCInputXptFromChannel(channel);
      NTV2OutputXptID csc_output =
          GetCSCOutputXptFromChannel(channel, /*inIsKey*/ false, /*inIsRGB*/ true);
      device_.Connect(fb_input_xpt, csc_output);
      device_.Connect(csc_input, input_output_xpt);
    } else {
      device_.Connect(fb_input_xpt, input_output_xpt);
    }
  }

  // Setup overlay channels with one-to-one mapping to input channels
  if (enable_overlay_) {
    // Ensure we have matching numbers of input and overlay channels
    if (overlay_channels_.size() > channels_.size()) {
      HOLOSCAN_LOG_WARN("More overlay channels than input channels. Some overlay channels will be unused.");
    }
    
    // Map each input channel to its corresponding overlay channel
    size_t num_overlays = std::min(channels_.size(), overlay_channels_.size());
    
    for (size_t i = 0; i < num_overlays; ++i) {
      NTV2Channel input_channel = channels_[i];
      NTV2Channel overlay_channel = overlay_channels_[i];
      
      HOLOSCAN_LOG_INFO("Setting up overlay: input channel {} -> overlay channel {}", 
                        static_cast<int>(input_channel), static_cast<int>(overlay_channel));
      
      // Setup output channel.
      device_.SetReference(NTV2_REFERENCE_INPUT1);
      device_.SetMode(overlay_channel, NTV2_MODE_DISPLAY);
      device_.SetSDITransmitEnable(overlay_channel, true);
      device_.SetVideoFormat(video_format_, false, false, overlay_channel);
      device_.SetFrameBufferFormat(overlay_channel, NTV2_FBF_ABGR);

      // Setup mixer controls.
      device_.SetMixerFGInputControl(0, NTV2MIXERINPUTCONTROL_SHAPED);
      device_.SetMixerBGInputControl(0, NTV2MIXERINPUTCONTROL_FULLRASTER);
      device_.SetMixerCoefficient(0, 0x10000);
      device_.SetMixerFGMatteEnabled(0, false);
      device_.SetMixerBGMatteEnabled(0, false);

      // Setup routing (overlay frame to CSC, CSC and SDI input to mixer, mixer to SDI output).
      NTV2OutputDestination output_dst = ::NTV2ChannelToOutputDestination(overlay_channel);
      device_.Connect(GetCSCInputXptFromChannel(overlay_channel),
                      GetFrameBufferOutputXptFromChannel(overlay_channel, true /*RGB*/));
      device_.Connect(NTV2_XptMixer1FGVidInput,
                      GetCSCOutputXptFromChannel(overlay_channel, false /*Key*/));
      device_.Connect(NTV2_XptMixer1FGKeyInput,
                      GetCSCOutputXptFromChannel(overlay_channel, true /*Key*/));
      
      // Connect the mixer background to the corresponding input channel
      NTV2InputSourceKinds input_kind = is_kona_hdmi_ ? NTV2_INPUTSOURCES_HDMI : NTV2_INPUTSOURCES_SDI;
      NTV2InputSource input_src = ::NTV2ChannelToInputSource(input_channel, input_kind);
      bool is_input_rgb = false;
      if (input_kind == NTV2_INPUTSOURCES_HDMI) {
        NTV2LHIHDMIColorSpace input_color;
        device_.GetHDMIInputColor(input_color, input_channel);
        is_input_rgb = (input_color == NTV2_LHIHDMIColorSpaceRGB);
      }
      NTV2OutputXptID input_output_xpt = GetInputSourceOutputXpt(input_src, false, is_input_rgb, 0);
      device_.Connect(NTV2_XptMixer1BGVidInput, input_output_xpt);
      
      device_.Connect(GetOutputDestInputXpt(output_dst), NTV2_XptMixer1VidYUV);

      // Set initial output frame (overlay uses HW frames 2 and 3).
      current_overlay_hw_frame_ = 2;
      device_.SetOutputFrame(overlay_channel, current_overlay_hw_frame_);
    }
  }

  // Set each channel to its own dedicated hardware frame for independent capture
  for (size_t i = 0; i < channels_.size(); ++i) {
    const auto& channel = channels_[i];
    // Each channel gets its own dedicated frame
    uint32_t channel_frame = i;
    device_.SetInputFrame(channel, channel_frame);
    HOLOSCAN_LOG_INFO("Set channel {} to dedicated hardware frame {}", static_cast<int>(channel), channel_frame);
  }

  // Wait for vertical interrupt on first channel
  // AJA devices keep channels synchronized when set to same frame
  device_.WaitForInputVerticalInterrupt(channels_.front(), kWarmupFrames);

  return AJA_STATUS_SUCCESS;
}

bool AJASourceOp::AllocateBuffers(std::vector<void*>& buffers, size_t num_buffers,
                                  size_t buffer_size, bool rdma) {
  buffers.resize(num_buffers);
  for (auto& buf : buffers) {
    if (rdma) {
      if (is_igpu_) {
        cudaHostAlloc(&buf, buffer_size, cudaHostAllocDefault);
      } else {
        cudaMalloc(&buf, buffer_size);
      }
      unsigned int syncFlag = 1;
      if (cuPointerSetAttribute(
              &syncFlag, CU_POINTER_ATTRIBUTE_SYNC_MEMOPS, reinterpret_cast<CUdeviceptr>(buf))) {
        HOLOSCAN_LOG_ERROR("Failed to set SYNC_MEMOPS CUDA attribute for RDMA");
        return false;
      }
    } else {
      buf = malloc(buffer_size);
    }

    if (!buf) {
      HOLOSCAN_LOG_ERROR("Failed to allocate buffer memory");
      return false;
    }

    if (!device_.DMABufferLock(static_cast<const ULWord*>(buf), buffer_size, true, rdma)) {
      HOLOSCAN_LOG_ERROR("Failed to map buffer for DMA");
      return false;
    }
  }

  return true;
}

void AJASourceOp::FreeBuffers(std::vector<void*>& buffers, bool rdma) {
  for (auto& buf : buffers) {
    if (rdma) {
      if (is_igpu_) {
        cudaFreeHost(buf);
      } else {
        cudaFree(buf);
      }
    } else {
      free(buf);
    }
  }
  buffers.clear();
}

AJAStatus AJASourceOp::SetupBuffers() {
  auto size = GetVideoWriteSize(video_format_, pixel_format_);

  // Initialize buffer arrays for each channel
  channel_buffers_.resize(channels_.size());
  if (enable_overlay_) {
    overlay_channel_buffers_.resize(overlay_channels_.size());
  }

  // Clear the channel to buffer mappings
  channel_to_buffer_map_.clear();
  if (enable_overlay_) {
    overlay_channel_to_buffer_map_.clear();
  }

  // Allocate buffers for each input channel sequentially
  for (size_t i = 0; i < channels_.size(); ++i) {
    if (!AllocateBuffers(channel_buffers_[i], kNumBuffers, size, use_rdma_)) {
      HOLOSCAN_LOG_ERROR("Failed to allocate buffers for channel {} (buffer index: {})", 
                        static_cast<int>(channels_[i]), i);
      return AJA_STATUS_INITIALIZE;
    }
    // Map this channel to buffer index i
    channel_to_buffer_map_[channels_[i]] = i;
    HOLOSCAN_LOG_INFO("Allocated {} buffers for channel {} (buffer index: {}) (size: {} bytes)", 
                      kNumBuffers, static_cast<int>(channels_[i]), i, size);
  }

  // Allocate overlay buffers for each overlay channel sequentially
  if (enable_overlay_) {
    for (size_t i = 0; i < overlay_channels_.size(); ++i) {
      if (!AllocateBuffers(overlay_channel_buffers_[i], kNumBuffers, size, overlay_rdma_)) {
        HOLOSCAN_LOG_ERROR("Failed to allocate overlay buffers for channel {} (buffer index: {})", 
                          static_cast<int>(overlay_channels_[i]), i);
        return AJA_STATUS_INITIALIZE;
      }
      // Map this overlay channel to buffer index i
      overlay_channel_to_buffer_map_[overlay_channels_[i]] = i;
      HOLOSCAN_LOG_INFO("Allocated {} overlay buffers for channel {} (buffer index: {}) (size: {} bytes)", 
                        kNumBuffers, static_cast<int>(overlay_channels_[i]), i, size);
    }
  }

  return AJA_STATUS_SUCCESS;
}

void AJASourceOp::initialize() {
  register_converter<NTV2Channel>();

  // Pre-initialize the 'enable_overlay' parameter.
  auto enable_overlay_arg = std::find_if(args().rbegin(), args().rend(), [](const auto& arg) {
    return (arg.name() == "enable_overlay");
  });
  if (enable_overlay_arg != args().rend()) {
    auto& param_wrap = spec()->params()["enable_overlay"];
    ArgumentSetter::set_param(param_wrap, (*enable_overlay_arg));
  }
  if (!enable_overlay_.has_value()) { enable_overlay_.set_default_value(); }
  spec()->outputs()["video_buffer_output_3"]->condition(ConditionType::kNone);
  spec()->outputs()["video_buffer_output_4"]->condition(ConditionType::kNone);
  video_outputs_disabled_[2] = true;
  video_outputs_disabled_[3] = true;
  spec()->outputs()["overlay_buffer_output"]->condition(ConditionType::kNone);
  spec()->outputs()["overlay_buffer_output_2"]->condition(ConditionType::kNone);
  spec()->outputs()["overlay_buffer_output_3"]->condition(ConditionType::kNone);
  spec()->outputs()["overlay_buffer_output_4"]->condition(ConditionType::kNone);
  overlay_outputs_disabled_[0] = true;
  overlay_outputs_disabled_[1] = true;
  overlay_outputs_disabled_[2] = true;
  overlay_outputs_disabled_[3] = true;
  Operator::initialize();

  channels_.clear();
  overlay_channels_.clear();
  for (const auto& channel_str : channels_param_.get()) {
    channels_.push_back(ToNTV2Channel(channel_str));
  }
  for (const auto& overlay_channel_str : overlay_channels_param_.get()) {
    overlay_channels_.push_back(ToNTV2Channel(overlay_channel_str));
  }
  
  // Set the active channels (first in each list)
  channel_ = channels_.front();
  overlay_channel_ = overlay_channels_.front();
}

void AJASourceOp::start() {
  // Determine whether or not we're using the iGPU.
  // TODO(unknown): This assumes we're using the first GPU device (as does the rest of the
  // operator).
  cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, 0);
  is_igpu_ = prop.integrated;

  float framerate;
  if (framerate_ == 23) {
    framerate = 23.98F;
  } else if (framerate_ == 29) {
    framerate = 29.97F;
  } else if (framerate_ == 59) {
    framerate = 59.94F;
  } else {
    framerate = framerate_;
  }
  HOLOSCAN_LOG_INFO("AJA Source: Capturing {}x{}@{}Hz {}from NTV2_CHANNEL{}",
                    width_,
                    height_,
                    framerate,
                    (interlaced_ ? "(interlaced) " : ""),
                    (static_cast<int>(channel_) + 1));
  HOLOSCAN_LOG_INFO("AJA Source: RDMA is {}", use_rdma_ ? "enabled" : "disabled");
  if (enable_overlay_) {
    HOLOSCAN_LOG_INFO("AJA Source: Outputting overlay to NTV2_CHANNEL{}",
                      (static_cast<int>(overlay_channel_) + 1));
    HOLOSCAN_LOG_INFO("AJA Source: Overlay RDMA is {}", overlay_rdma_ ? "enabled" : "disabled");
  } else {
    HOLOSCAN_LOG_INFO("AJA Source: Overlay output is disabled");
  }

  AJAStatus status = DetermineVideoFormat();
  if (AJA_FAILURE(status)) {
    throw std::runtime_error("Video format could not be determined or is not supported.");
  }

  status = OpenDevice();
  if (AJA_FAILURE(status)) {
    throw std::runtime_error(fmt::format("Failed to open device {}", device_specifier_.get()));
  }

  status = SetupVideo();
  if (AJA_FAILURE(status)) {
    throw std::runtime_error(fmt::format("Failed to setup device {}", device_specifier_.get()));
  }

  status = SetupBuffers();
  if (AJA_FAILURE(status)) { throw std::runtime_error("Failed to setup AJA buffers."); }
}

void AJASourceOp::compute(InputContext& op_input, OutputContext& op_output,
                          ExecutionContext& context) {
  HOLOSCAN_LOG_INFO("=== AJA Source Compute Start ===");
  HOLOSCAN_LOG_INFO("Channels: {}, Overlay channels: {}, Enable overlay: {}", 
                    channels_.size(), overlay_channels_.size(), enable_overlay_);
  
  // holoscan::gxf::Entity
  bool have_overlay_in = false;
  holoscan::gxf::Entity overlay_in_message;
  auto maybe_overlay_message = op_input.receive<gxf::Entity>("overlay_buffer_input");
  if (!maybe_overlay_message || maybe_overlay_message.value().is_null()) {
    HOLOSCAN_LOG_TRACE("Operator '{}' failed to find overlay_buffer_input", name_);
  } else {
    overlay_in_message = maybe_overlay_message.value();
    have_overlay_in = true;
  }

  // Handle overlay input if available
  if (enable_overlay_ && have_overlay_in) {
    nvidia::gxf::Handle<nvidia::gxf::VideoBuffer> overlay_buffer;
    try {
      overlay_buffer = holoscan::gxf::get_videobuffer(overlay_in_message);
      // Overlay uses HW frames 2 and 3.
      current_overlay_hw_frame_ = ((current_overlay_hw_frame_ + 1) % 2) + 2;

      ULWord* ptr = reinterpret_cast<ULWord*>(overlay_buffer->pointer());
      // Write overlay to all overlay channels
      for (const auto& overlay_channel : overlay_channels_) {
        device_.DMAWriteFrame(current_overlay_hw_frame_, ptr, overlay_buffer->size());
        device_.SetOutputFrame(overlay_channel, current_overlay_hw_frame_);
      }
      device_.SetMixerMode(0, NTV2MIXERMODE_MIX);
    } catch (const std::runtime_error& r_) {
      HOLOSCAN_LOG_TRACE("Failed to read VideoBuffer with error: {}", std::string(r_.what()));
    }
  }

  // Each channel maintains its own hardware frame (set in SetupVideo)
  // We just need to wait for the next frame to be ready
  uint32_t next_hw_frame = (current_hw_frame_ + 1) % 2;
  
  // Wait for vertical interrupt on first channel
  device_.WaitForInputFieldID(NTV2_FIELD0, channels_.front());

  // Read frames from ALL channels
  auto size = GetVideoWriteSize(video_format_, pixel_format_);
  std::vector<void*> current_frame_buffers;
  current_frame_buffers.reserve(channels_.size());
  
  for (size_t i = 0; i < channels_.size(); ++i) {
    const auto& channel = channels_[i];
    size_t buffer_index = channel_to_buffer_map_[channel];
    auto ptr = static_cast<ULWord*>(channel_buffers_[buffer_index][current_buffer_]);
    
    // Each channel gets its own dedicated hardware frame (no more alternating!)
    uint32_t channel_frame = i;
    
    HOLOSCAN_LOG_INFO("=== CHANNEL {} DEBUG ===", i);
    HOLOSCAN_LOG_INFO("  Channel enum: NTV2_CHANNEL{}", static_cast<int>(channel));
    HOLOSCAN_LOG_INFO("  Buffer index: {}", buffer_index);
    HOLOSCAN_LOG_INFO("  Dedicated hardware frame: {}", channel_frame);
    HOLOSCAN_LOG_INFO("  Buffer array: channel_buffers_[{}][{}]", buffer_index, current_buffer_);
    HOLOSCAN_LOG_INFO("  Buffer pointer: {}", static_cast<void*>(ptr));
    HOLOSCAN_LOG_INFO("  Buffer address: 0x{:x}", reinterpret_cast<uintptr_t>(ptr));
    
    device_.DMAReadFrame(channel_frame, ptr, size);
    current_frame_buffers.push_back(ptr);
    
    HOLOSCAN_LOG_INFO("  After DMA: current_frame_buffers[{}] = 0x{:x}", i, reinterpret_cast<uintptr_t>(current_frame_buffers[i]));
    HOLOSCAN_LOG_INFO("  Successfully read frame {} from NTV2_CHANNEL{} to buffer {}", 
                      channel_frame, static_cast<int>(channel), buffer_index);
  }

  // Set the frame to read for the next tick.
  current_hw_frame_ = next_hw_frame;

  // Common (output and overlay) buffer info
  nvidia::gxf::VideoTypeTraits<nvidia::gxf::VideoFormat::GXF_VIDEO_FORMAT_RGBA> video_type;
  nvidia::gxf::VideoFormatSize<nvidia::gxf::VideoFormat::GXF_VIDEO_FORMAT_RGBA> color_format;
  auto color_planes = color_format.getDefaultColorPlanes(width_, height_);
  nvidia::gxf::VideoBufferInfo info{width_,
                                    height_,
                                    video_type.value,
                                    std::move(color_planes),
                                    nvidia::gxf::SurfaceLayout::GXF_SURFACE_LAYOUT_PITCH_LINEAR};

  // Create and emit overlay buffers for each overlay channel to their respective outputs
  if (enable_overlay_) {
    for (size_t i = 0; i < overlay_channels_.size(); ++i) {
      const auto& overlay_channel = overlay_channels_[i];
      size_t buffer_index = overlay_channel_to_buffer_map_[overlay_channel];
      
      // Use the original string parameter for logging
      std::string overlay_name = overlay_channels_param_.get()[i];
      
      HOLOSCAN_LOG_INFO("Adding overlay buffer for channel {} (buffer index: {}) with name '{}'", 
                        static_cast<int>(overlay_channel), buffer_index, overlay_name);
      
      auto overlay_output = nvidia::gxf::Entity::New(context.context());
      if (!overlay_output) {
        HOLOSCAN_LOG_ERROR("Failed to allocate overlay output for channel {} (buffer index: {}); terminating.", 
                          static_cast<int>(overlay_channel), buffer_index);
        return;
      }
      
      auto overlay_buffer = overlay_output.value().add<nvidia::gxf::VideoBuffer>();
      if (!overlay_buffer) {
        HOLOSCAN_LOG_ERROR("Failed to allocate overlay buffer for channel {} (buffer index: {}); terminating.", 
                          static_cast<int>(overlay_channel), buffer_index);
        return;
      }
      
      // Use the same buffer info and storage type logic
      auto overlay_storage_type = overlay_rdma_ ? nvidia::gxf::MemoryStorageType::kDevice
                                                : nvidia::gxf::MemoryStorageType::kHost;
      
      // Same buffer info creation as your working code
      nvidia::gxf::VideoTypeTraits<nvidia::gxf::VideoFormat::GXF_VIDEO_FORMAT_RGBA> video_type;
      nvidia::gxf::VideoFormatSize<nvidia::gxf::VideoFormat::GXF_VIDEO_FORMAT_RGBA> color_format;
      auto color_planes = color_format.getDefaultColorPlanes(width_, height_);
      nvidia::gxf::VideoBufferInfo info{width_, height_, video_type.value, std::move(color_planes), 
                                        nvidia::gxf::SurfaceLayout::GXF_SURFACE_LAYOUT_PITCH_LINEAR};
      
      size_t size = width_ * height_ * 4; // RGBA format
      
      // Same wrapMemory call as your working code
      overlay_buffer.value()->wrapMemory(info, size, overlay_storage_type, overlay_channel_buffers_[buffer_index][current_buffer_], nullptr);
      
      // Emit to the appropriate overlay output based on channel index
      auto result = gxf::Entity(std::move(overlay_output.value()));
      
      if (i >= 4) {
        HOLOSCAN_LOG_WARN("Overlay channel index {} exceeds supported outputs, skipping", i);
        continue;
      }
      
      // Check if this output is disabled
      if (overlay_outputs_disabled_[i]) {
        HOLOSCAN_LOG_INFO("Output {} is disabled, skipping emission", overlay_output_names_[i]);
        continue;
      }
      
      HOLOSCAN_LOG_INFO("Emitting {} for overlay channel {} (NTV2_CHANNEL{})", 
                        overlay_output_names_[i], i, static_cast<int>(overlay_channel));
      op_output.emit(result, overlay_output_names_[i]);
      HOLOSCAN_LOG_INFO("Successfully emitted {}", overlay_output_names_[i]);
    }
  }

  // Create and emit video buffers for each channel to their respective outputs
  for (size_t i = 0; i < channels_.size(); ++i) {
    const auto& channel = channels_[i];
    size_t buffer_index = channel_to_buffer_map_[channel];
    
    auto video_output = nvidia::gxf::Entity::New(context.context());
    if (!video_output) {
      throw std::runtime_error(fmt::format("Failed to allocate video output for channel {} (buffer index: {}); terminating.", 
                                          static_cast<int>(channel), buffer_index));
      return;
    }

    auto video_buffer = video_output.value().add<nvidia::gxf::VideoBuffer>();
    if (!video_buffer) {
      throw std::runtime_error(fmt::format("Failed to allocate video buffer for channel {} (buffer index: {}); terminating.", 
                                          static_cast<int>(channel), buffer_index));
      return;
    }

    auto storage_type =
        use_rdma_ ? nvidia::gxf::MemoryStorageType::kDevice : nvidia::gxf::MemoryStorageType::kHost;
    
    // Use the buffer that was just filled for this specific channel
    HOLOSCAN_LOG_INFO("=== VIDEO OUTPUT {} DEBUG ===", i);
    HOLOSCAN_LOG_INFO("  Channel: NTV2_CHANNEL{}", static_cast<int>(channel));
    HOLOSCAN_LOG_INFO("  Using current_frame_buffers[{}] = 0x{:x}", i, reinterpret_cast<uintptr_t>(current_frame_buffers[i]));
    HOLOSCAN_LOG_INFO("  Buffer index: {}", buffer_index);
    
    video_buffer.value()->wrapMemory(info, size, storage_type, current_frame_buffers[i], nullptr);

    // Emit to the appropriate output based on channel index
    auto result = gxf::Entity(std::move(video_output.value()));
    
    if (i >= 4) {
      HOLOSCAN_LOG_WARN("Channel index {} exceeds supported outputs, skipping", i);
      continue;
    }
    
    // Check if this output is disabled
    if (video_outputs_disabled_[i]) {
      HOLOSCAN_LOG_INFO("Output {} is disabled, skipping emission", video_output_names_[i]);
      continue;
    }
    
    HOLOSCAN_LOG_INFO("Emitting {} for channel {} (NTV2_CHANNEL{})", 
                      video_output_names_[i], i, static_cast<int>(channel));
    op_output.emit(result, video_output_names_[i]);
    HOLOSCAN_LOG_INFO("Successfully emitted {}", video_output_names_[i]);
  }

  

  // Update the current buffer (index shared between video and overlay)
  current_buffer_ = (current_buffer_ + 1) % kNumBuffers;
  
  HOLOSCAN_LOG_INFO("=== AJA Source Compute Complete ===");
}

void AJASourceOp::stop() {
  // Unsubscribe from all channels
  for (const auto& channel : channels_) {
    device_.UnsubscribeInputVerticalEvent(channel);
  }
  
  device_.DMABufferUnlockAll();

  if (enable_overlay_) { device_.SetMixerMode(0, NTV2MIXERMODE_FOREGROUND_OFF); }

  // Free buffers for all channels
  for (auto& channel_buffers : channel_buffers_) {
    FreeBuffers(channel_buffers, use_rdma_);
  }
  
  if (enable_overlay_) {
    for (auto& overlay_buffers : overlay_channel_buffers_) {
      FreeBuffers(overlay_buffers, overlay_rdma_);
    }
  }
}

bool AJASourceOp::GetNTV2VideoFormatTSI(NTV2VideoFormat* format) {
  switch (*format) {
    case NTV2_FORMAT_3840x2160p_2400:
      *format = NTV2_FORMAT_4x1920x1080p_2400;
      return true;
    case NTV2_FORMAT_3840x2160p_6000:
      *format = NTV2_FORMAT_4x1920x1080p_6000;
      return true;
    case NTV2_FORMAT_4096x2160p_2400:
      *format = NTV2_FORMAT_4x2048x1080p_2400;
      return true;
    case NTV2_FORMAT_4096x2160p_6000:
      *format = NTV2_FORMAT_4x2048x1080p_6000;
      return true;
    default:
      return false;
  }
}

}  // namespace holoscan::ops

