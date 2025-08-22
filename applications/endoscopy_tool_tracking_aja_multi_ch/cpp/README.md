# Endoscopy Tool Tracking

Based on a LSTM (long-short term memory) stateful model, these applications demonstrate the use of custom components for tool tracking, including composition and rendering of text, tool position, and mask (as heatmap) combined with the original video stream.


### Known Issues

Python implementaion is currently not working. Use C++ Implementation
This app is modified to use multippel video streams and it only works with AJA cards that support overlays

### Requirements

Follow the [setup instructions from the user guide](https://docs.nvidia.com/holoscan/sdk-user-guide/aja_setup.html) to use the AJA capture card.


### Application workflow diagram

```mermaid
graph TD;
    AJA_Source_multi_ch --Input_1--> Format_Converter;
    Format_Converter --> LSTM_Inference
    LSTM_Inference --> Tool_Tracking_Postprocessor
    Tool_Tracking_Postprocessor --> Visualization
    Visualization --Overlay_1--> AJA_Source_multi_ch
    AJA_Source_multi_ch--Input_2--> Format_Converter_2;
    Format_Converter_2 --> LSTM_Inference_2
    LSTM_Inference_2 --> Tool_Tracking_Postprocessor_2
    Tool_Tracking_Postprocessor_2 --> Visualization_2
    Visualization_2 --Overlay_2--> AJA_Source_multi_ch
    AJA_Source_multi_ch --Input_1--> Drop_Alpha;
    Drop_Alpha --> Pre_Processor
    Pre_Processor --> Inference
    Inference --> Post_Processor
    Post_Processor --> Visualization_4K
```

### Run Instructions

```
./holohub run --language cpp endoscopy_tool_tracking_aja_multi_ch

```