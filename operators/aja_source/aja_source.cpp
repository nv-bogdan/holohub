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
// constexpr uint32_t kNumBuffers = 2;  // No longer needed - single buffering only

AJASourceOp::AJASourceOp() {
  HOLOSCAN_LOG_INFO("AJASourceOp::AJASourceOp - Constructor called");
}

void AJASourceOp::setup(OperatorSpec& spec) {
  auto& video_buffer_output = spec.output<gxf::Entity>("video_buffer_output");
  auto& video_buffer_output_2 = spec.output<gxf::Entity>("video_buffer_output_2");
  spec.input<gxf::Entity>("overlay_buffer_input").condition(ConditionType::kNone);
  spec.input<gxf::Entity>("overlay_buffer_input_2").condition(ConditionType::kNone);
  auto& overlay_buffer_output = spec.output<gxf::Entity>("overlay_buffer_output");
  auto& overlay_buffer_output_2 = spec.output<gxf::Entity>("overlay_buffer_output_2");

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
  spec.param(overlay_buffer_input_,
             "overlay_buffer_input",
             "OverlayBufferInput",
             "Input for overlay buffer for channel 1.");
  spec.param(overlay_buffer_input_2_,
             "overlay_buffer_input_2",
             "OverlayBufferInput2",
             "Input for overlay buffer for channel 2.");
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
      
      // Use the default channel-based routing (like the original working code)
      // This lets the hardware handle the routing naturally
      NTV2InputXptID csc_input = GetCSCInputXptFromChannel(channel);
      NTV2OutputXptID csc_output = GetCSCOutputXptFromChannel(channel, false, true);
      device_.Connect(fb_input_xpt, csc_output);
      device_.Connect(csc_input, input_output_xpt);
      
      HOLOSCAN_LOG_INFO("  Input routing: NTV2_CHANNEL{} -> CSC -> Frame Buffer (using default routing)", static_cast<int>(channel));
    } else {
      device_.Connect(fb_input_xpt, input_output_xpt);
    }
  }

  // Set each channel to its own dedicated hardware frame for independent capture
  
  // Set each channel to its own dedicated hardware frame for independent capture
  for (size_t i = 0; i < channels_.size(); ++i) {
    const auto& channel = channels_[i];
    uint32_t hw_frame = i;  // 0 for first, 1 for second
    device_.SetInputFrame(channel, hw_frame);
    
    // Ensure consistent video format across all channels
    device_.SetVideoFormat(video_format_, false, false, channel);
    device_.SetFrameBufferFormat(channel, pixel_format_);
    
    HOLOSCAN_LOG_INFO("Channel {}: NTV2_CHANNEL{} -> Frame Buffer {} (hardware frame {})", 
                      i + 1, static_cast<int>(channel), hw_frame, hw_frame);
    HOLOSCAN_LOG_INFO("  Hardware frame {} will receive data from NTV2_CHANNEL{}", hw_frame, static_cast<int>(channel));
    HOLOSCAN_LOG_INFO("  Note: Frame Buffer {} input is hardwired to NTV2_CHANNEL{}", hw_frame, static_cast<int>(channel));
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
      HOLOSCAN_LOG_INFO("  Channel {}: Input NTV2_CHANNEL{} -> Frame Buffer {} -> video_buffer_output{}", 
                        i + 1, static_cast<int>(input_channel), i, (i == 0) ? "" : "_2");
      HOLOSCAN_LOG_INFO("  Overlay {}: NTV2_CHANNEL{} -> Frame Buffer {} -> CSC -> Mixer{} -> SDI Output", 
                        i + 1, static_cast<int>(overlay_channel), i + 2, i + 1);
      
      // Setup output channel.
      device_.SetReference(NTV2_REFERENCE_INPUT1);
      device_.SetMode(overlay_channel, NTV2_MODE_DISPLAY);
      device_.SetSDITransmitEnable(overlay_channel, true);
      device_.SetVideoFormat(video_format_, false, false, overlay_channel);
      device_.SetFrameBufferFormat(overlay_channel, pixel_format_);  // Use same format as input

      // Setup mixer controls - use different mixer for each channel
      int mixer_index = i;  // Mixer 0 for first channel, Mixer 1 for second channel
      device_.SetMixerFGInputControl(mixer_index, NTV2MIXERINPUTCONTROL_SHAPED);
      device_.SetMixerBGInputControl(mixer_index, NTV2MIXERINPUTCONTROL_FULLRASTER);
      device_.SetMixerCoefficient(mixer_index, 0x10000);
      device_.SetMixerFGMatteEnabled(mixer_index, false);
      device_.SetMixerBGMatteEnabled(mixer_index, false);
      
      // Set mixer reference based on VSync signal availability
      device_.SetReference(NTV2_REFERENCE_INPUT3);
      
      // Setup routing (overlay frame to CSC, CSC and SDI input to mixer, mixer to SDI output).
      NTV2OutputDestination output_dst = ::NTV2ChannelToOutputDestination(overlay_channel);
      
      // Use the default channel-based routing (like the original working code)
      // Connect overlay frame to CSC (let hardware handle the routing)
      device_.Connect(GetCSCInputXptFromChannel(overlay_channel),
                      GetFrameBufferOutputXptFromChannel(overlay_channel, true /*RGB*/));
      
      // Connect CSC to mixer foreground - use different mixer for each channel
      if (i == 0) {
        // First channel: overlay goes to Mixer1 foreground
        device_.Connect(NTV2_XptMixer1FGVidInput,
                        GetCSCOutputXptFromChannel(overlay_channel, false /*inIsKey*/, true /*inIsRGB*/));
        device_.Connect(NTV2_XptMixer1FGKeyInput,
                        GetCSCOutputXptFromChannel(overlay_channel, true /*inIsKey*/, true /*inIsRGB*/));
        
        HOLOSCAN_LOG_INFO("  Overlay routing: Frame Buffer {} → CSC → Mixer1", i + 2);
      } else {
        // Second channel: overlay goes to Mixer2 foreground
        device_.Connect(NTV2_XptMixer2FGVidInput,
                        GetCSCOutputXptFromChannel(overlay_channel, false /*inIsKey*/, true /*inIsRGB*/));
        device_.Connect(NTV2_XptMixer2FGKeyInput,
                        GetCSCOutputXptFromChannel(overlay_channel, true /*inIsKey*/, true /*inIsRGB*/));
        
        HOLOSCAN_LOG_INFO("  Overlay routing: Frame Buffer {} → CSC → Mixer2", i + 2);
      }
      
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
      
      // Connect background to the same mixer as foreground for each channel
      if (i == 0) {
        // First channel: background goes to Mixer1
        device_.Connect(NTV2_XptMixer1BGVidInput, input_output_xpt);
      } else {
        // Second channel: background goes to Mixer2
        device_.Connect(NTV2_XptMixer2BGVidInput, input_output_xpt);
      }
      
      // Connect mixer output to SDI output - each channel uses its own mixer
      if (i == 0) {
        // First channel: output from Mixer1
        device_.Connect(GetOutputDestInputXpt(output_dst), NTV2_XptMixer1VidYUV);
      } else {
        // Second channel: output from Mixer2
        device_.Connect(GetOutputDestInputXpt(output_dst), NTV2_XptMixer2VidYUV);
      }

      // Set initial output frame (overlay uses HW frames 2 and 3).
      uint32_t hw_overlay_frame = i + 2;  // 2 for first overlay, 3 for second overlay
      device_.SetOutputFrame(overlay_channel, hw_overlay_frame);
      HOLOSCAN_LOG_INFO("Set overlay channel {} to hardware frame {}", static_cast<int>(overlay_channel), hw_overlay_frame);
    }
    
    // Log the complete routing summary
    HOLOSCAN_LOG_INFO("=== COMPLETE ROUTING SUMMARY ===");
    HOLOSCAN_LOG_INFO("Channel 1: NTV2_CHANNEL{} (Frame Buffer 0 → CSC) + NTV2_CHANNEL{} (Frame Buffer 2 → CSC → Mixer1) -> SDI Output", 
                      static_cast<int>(channels_[0]), static_cast<int>(overlay_channels_[0]));
    if (channels_.size() > 1) {
      HOLOSCAN_LOG_INFO("Channel 2: NTV2_CHANNEL{} (Frame Buffer 1 → CSC) + NTV2_CHANNEL{} (Frame Buffer 3 → CSC → Mixer2) -> SDI Output", 
                        static_cast<int>(channels_[1]), static_cast<int>(overlay_channels_[1]));
    }
    HOLOSCAN_LOG_INFO("Note: Channel 1 uses Mixer1, Channel 2 uses Mixer2");
    HOLOSCAN_LOG_INFO("=================================");
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

  // Initialize buffer arrays for each channel (2 buffers per channel for double buffering)
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
    if (!AllocateBuffers(channel_buffers_[i], 2, size, use_rdma_)) {  // 2 buffers per channel for double buffering
      HOLOSCAN_LOG_ERROR("Failed to allocate buffers for channel {} (buffer index: {})", 
                        static_cast<int>(channels_[i]), i);
      return AJA_STATUS_INITIALIZE;
    }
    // Map this channel to buffer index i
    channel_to_buffer_map_[channels_[i]] = i;
    HOLOSCAN_LOG_INFO("Allocated 2 buffers for channel {} (buffer index: {}) (size: {} bytes)", 
                      static_cast<int>(channels_[i]), i, size);
  }

  // Allocate overlay buffers for each overlay channel sequentially
  if (enable_overlay_) {
    for (size_t i = 0; i < overlay_channels_.size(); ++i) {
      if (!AllocateBuffers(overlay_channel_buffers_[i], 2, size, overlay_rdma_)) {  // 2 buffers per channel for double buffering
        HOLOSCAN_LOG_ERROR("Failed to allocate overlay buffers for channel {} (buffer index: {})", 
                          static_cast<int>(overlay_channels_[i]), i);
        return AJA_STATUS_INITIALIZE;
      }
      // Map this overlay channel to buffer index i
      overlay_channel_to_buffer_map_[overlay_channels_[i]] = i;
      HOLOSCAN_LOG_INFO("Allocated 2 overlay buffers for channel {} (buffer index: {}) (size: {} bytes)", 
                        static_cast<int>(overlay_channels_[i]), i, size);
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
  
  // DEBUG ONLY: Disable second outputs to focus on first channel
  // spec()->outputs()["video_buffer_output_2"]->condition(ConditionType::kNone);
  // spec()->outputs()["overlay_buffer_output"]->condition(ConditionType::kNone);
  // spec()->outputs()["overlay_buffer_output_2"]->condition(ConditionType::kNone);
  HOLOSCAN_LOG_INFO("DEBUG: Disabled video_buffer_output_2 and overlay_buffer_output_2 for single channel testing");
  
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
  
  // Initialize buffer tracking for each channel (start with buffer 0)
  current_channel_buffers_.resize(channels_.size(), 0);
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
  HOLOSCAN_LOG_INFO("Double buffering state - Channel buffers: [{}]", 
                    fmt::join(current_channel_buffers_, ", "));
  
  // Handle overlay inputs for all channels
  size_t num_channels = channels_.size();
  size_t num_overlay_channels = overlay_channels_.size();
  std::vector<bool> have_overlay_in(num_overlay_channels, false);
  std::vector<holoscan::gxf::Entity> overlay_in_messages(num_overlay_channels);
  std::vector<nvidia::gxf::Handle<nvidia::gxf::VideoBuffer>> overlay_buffers(num_overlay_channels);
  
  if (enable_overlay_) {
    // Receive overlay inputs for all channels using a loop
    for (size_t i = 0; i < num_overlay_channels; ++i) {
      const char* input_name = (i == 0) ? "overlay_buffer_input" : "overlay_buffer_input_2";
      auto maybe_overlay_message = op_input.receive<gxf::Entity>(input_name);
      
      if (!maybe_overlay_message || maybe_overlay_message.value().is_null()) {
        HOLOSCAN_LOG_TRACE("Operator '{}' failed to find {}", name_, input_name);
      } else {
        overlay_in_messages[i] = maybe_overlay_message.value();
        have_overlay_in[i] = true;
        try {
          overlay_buffers[i] = holoscan::gxf::get_videobuffer(overlay_in_messages[i]);
          HOLOSCAN_LOG_INFO("Received overlay input for channel {}", i + 1);
        } catch (const std::runtime_error& r_) {
          HOLOSCAN_LOG_ERROR("Failed to read VideoBuffer from {}: {}", input_name, r_.what());
        }
      }
    }
    
    // Process overlays for each channel
    if (std::any_of(have_overlay_in.begin(), have_overlay_in.end(), [](bool b) { return b; })) {
      // Apply overlays using fixed hardware frames (2 for first channel, 3 for second)
      for (size_t i = 0; i < num_overlay_channels; ++i) {
        if (have_overlay_in[i] && overlay_buffers[i]) {
          uint32_t hw_overlay_frame = i + 2;  // 2 for first, 3 for second
          ULWord* ptr = reinterpret_cast<ULWord*>(overlay_buffers[i]->pointer());
          
          // Write overlay to hardware frame
          device_.DMAWriteFrame(hw_overlay_frame, ptr, overlay_buffers[i]->size());
          device_.SetOutputFrame(overlay_channels_[i], hw_overlay_frame);
          
          HOLOSCAN_LOG_INFO("Applied overlay to channel {} using hardware frame {}", i + 1, hw_overlay_frame);
        }
      }
  
  // Set mixer modes for both channels
  if (enable_overlay_) {
    device_.SetMixerMode(0, NTV2MIXERMODE_MIX);  // First channel uses Mixer1
    if (num_overlay_channels > 1) {
      device_.SetMixerMode(1, NTV2MIXERMODE_MIX);  // Second channel uses Mixer2
    }
  }
    }
  }

  // Wait for vertical interrupt on first channel
  device_.WaitForInputFieldID(NTV2_FIELD0, channels_.front());

  // Read frames from all channels using their dedicated hardware frames with double buffering
  auto size = GetVideoWriteSize(video_format_, pixel_format_);
  std::vector<void*> current_frame_buffers(num_channels);
  std::vector<uint8_t> filled_buffer_indices(num_channels);
  
  for (size_t i = 0; i < num_channels; ++i) {
    const auto& channel = channels_[i];
    size_t buffer_index = channel_to_buffer_map_[channel];
    uint8_t current_buffer = current_channel_buffers_[i];
    auto ptr = static_cast<ULWord*>(channel_buffers_[buffer_index][current_buffer]);
    
    // Each channel has its dedicated hardware frame (0 for first, 1 for second)
    uint32_t hw_frame = i;
    
    HOLOSCAN_LOG_INFO("=== CHANNEL {} BUFFER MAPPING ===", i + 1);
    HOLOSCAN_LOG_INFO("  Channel: NTV2_CHANNEL{}", static_cast<int>(channel));
    HOLOSCAN_LOG_INFO("  Hardware Frame: {}", hw_frame);
    HOLOSCAN_LOG_INFO("  Allocated Buffer Index: {}", buffer_index);
    HOLOSCAN_LOG_INFO("  Current Buffer: {}", static_cast<int>(current_buffer));
    HOLOSCAN_LOG_INFO("  Buffer Address: 0x{:x}", reinterpret_cast<uintptr_t>(ptr));
    
    device_.DMAReadFrame(hw_frame, ptr, size);
    current_frame_buffers[i] = ptr;
    filled_buffer_indices[i] = current_buffer;  // Remember which buffer was just filled
    
    HOLOSCAN_LOG_INFO("  Successfully read from hardware frame {} to buffer {}[{}] (NTV2_CHANNEL{})", 
                      hw_frame, buffer_index, static_cast<int>(current_buffer), static_cast<int>(channel));
    
    // Update buffer index for next tick (alternate between 0 and 1)
    current_channel_buffers_[i] = (current_buffer + 1) % 2;
  }

  // Common buffer info
  nvidia::gxf::VideoTypeTraits<nvidia::gxf::VideoFormat::GXF_VIDEO_FORMAT_RGBA> video_type;
  nvidia::gxf::VideoFormatSize<nvidia::gxf::VideoFormat::GXF_VIDEO_FORMAT_RGBA> color_format;
  auto color_planes = color_format.getDefaultColorPlanes(width_, height_);
  nvidia::gxf::VideoBufferInfo info{width_,
                                    height_,
                                    video_type.value,
                                    std::move(color_planes),
                                    nvidia::gxf::SurfaceLayout::GXF_SURFACE_LAYOUT_PITCH_LINEAR};

  // Create and emit overlay outputs
  if (enable_overlay_) {
    for (size_t i = 0; i < num_overlay_channels; ++i) {
      const auto& overlay_channel = overlay_channels_[i];
      size_t buffer_index = overlay_channel_to_buffer_map_[overlay_channel];
      
      auto overlay_output = nvidia::gxf::Entity::New(context.context());
      if (!overlay_output) {
        HOLOSCAN_LOG_ERROR("Failed to allocate overlay output for channel {}; terminating.", i + 1);
        return;
      }
      
      auto overlay_buffer = overlay_output.value().add<nvidia::gxf::VideoBuffer>();
      if (!overlay_buffer) {
        HOLOSCAN_LOG_ERROR("Failed to allocate overlay buffer for channel {}; terminating.", i + 1);
        return;
      }
      
      auto overlay_storage_type = overlay_rdma_ ? nvidia::gxf::MemoryStorageType::kDevice
                                                : nvidia::gxf::MemoryStorageType::kHost;
      
      // Read overlay data from hardware frame (2 for first, 3 for second)
      uint32_t hw_overlay_frame = i + 2;
      uint8_t current_overlay_buffer = current_channel_buffers_[i];  // Use same buffer pattern as input channels
      auto ptr = static_cast<ULWord*>(overlay_channel_buffers_[buffer_index][current_overlay_buffer]);
      
      // Read the overlay data from the hardware frame into our buffer
      device_.DMAReadFrame(hw_overlay_frame, ptr, size);
      
      // Wrap the buffer that contains the overlay data
      overlay_buffer.value()->wrapMemory(info, size, overlay_storage_type, ptr, nullptr);
      
      HOLOSCAN_LOG_INFO("Read overlay data from hardware frame {} to buffer {}[{}] for channel {}", 
                        hw_overlay_frame, buffer_index, static_cast<int>(current_overlay_buffer), i + 1);
      
      // Update buffer index for next tick (alternate between 0 and 1)
      current_channel_buffers_[i] = (current_overlay_buffer + 1) % 2;
      
      // Emit to the appropriate overlay output
      auto result = gxf::Entity(std::move(overlay_output.value()));
      const char* output_name = (i == 0) ? "overlay_buffer_output" : "overlay_buffer_output_2";
      op_output.emit(result, output_name);
      HOLOSCAN_LOG_INFO("Successfully emitted overlay output for channel {} from buffer {}[{}]", 
                        i + 1, buffer_index, static_cast<int>(current_overlay_buffer));
    }
  }

  // Create and emit video outputs for all channels
  for (size_t i = 0; i < num_channels; ++i) {
    const auto& channel = channels_[i];
    size_t buffer_index = channel_to_buffer_map_[channel];
    
    auto video_output = nvidia::gxf::Entity::New(context.context());
    if (!video_output) {
      throw std::runtime_error(fmt::format("Failed to allocate video output for channel {}; terminating.", i + 1));
      return;
    }

    auto video_buffer = video_output.value().add<nvidia::gxf::VideoBuffer>();
    if (!video_buffer) {
      throw std::runtime_error(fmt::format("Failed to allocate video buffer for channel {}; terminating.", i + 1));
      return;
    }

    auto storage_type = use_rdma_ ? nvidia::gxf::MemoryStorageType::kDevice 
                                   : nvidia::gxf::MemoryStorageType::kHost;
    
    // Use the buffer that was just filled for this specific channel
    video_buffer.value()->wrapMemory(info, size, storage_type, current_frame_buffers[i], nullptr);

    // Emit to the appropriate output
    auto result = gxf::Entity(std::move(video_output.value()));
    const char* output_name = (i == 0) ? "video_buffer_output" : "video_buffer_output_2";
    op_output.emit(result, output_name);
    HOLOSCAN_LOG_INFO("Successfully emitted video output for channel {} (NTV2_CHANNEL{}) from buffer {}[{}]", 
                      i + 1, static_cast<int>(channel), channel_to_buffer_map_[channel], static_cast<int>(filled_buffer_indices[i]));
  }

  HOLOSCAN_LOG_INFO("=== AJA Source Compute Complete ===");
  HOLOSCAN_LOG_INFO("Buffer flow: Each channel alternates between buffer 0 and 1 for smooth capture");
  HOLOSCAN_LOG_INFO("Next tick will use buffers: [{}]", fmt::join(current_channel_buffers_, ", "));
}

void AJASourceOp::stop() {
  // Unsubscribe from all channels
  for (const auto& channel : channels_) {
    device_.UnsubscribeInputVerticalEvent(channel);
  }
  
  device_.DMABufferUnlockAll();

  if (enable_overlay_) { 
    device_.SetMixerMode(0, NTV2MIXERMODE_FOREGROUND_OFF);  // Disable Mixer1
    if (overlay_channels_.size() > 1) {
      device_.SetMixerMode(1, NTV2MIXERMODE_FOREGROUND_OFF);  // Disable Mixer2
    }
  }

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

