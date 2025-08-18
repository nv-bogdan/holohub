# Converting the `aja_source` Operator from Single Stream to Multi-Stream

## Introduction
The initial goal was to extend the `holoscan aja_source` operator from supporting a single input stream to handling multiple streams. While conceptually straightforward, practical implementation raises several challenges due to the constraints of the capture cards and Holoscan's operator model.

---

## Initial Approach
- **General Operator Attempt**  
  The first step was designing a general, configurable operator capable of handling multiple input and output streams.  
  However, the high degree of customizability of AJA capture cards adds complexity:
  - Each card exposes a limited number of **hardware buffers**, **color converters**, and **mixers**.
  - Applications have different ways of wiring these resources depending on performance goals, formats, and topology.  

Because of this hardware heterogeneity, a single "general-purpose" operator implementation becomes complex and potentially fragile.

---

## Specific Operator Implementation
Due to the challenges above, the approach shifted to building a **specific operator**:
- Two input streams
- Two output streams
- Explicit handling of buffering, synchronization, and channel mapping  

This made the problem more manageable and aligned better with the underlying hardware capabilities.

---

## Key Findings and Observations
1. **Changing Inputs After Initialization**  
   - Holoscan currently does not make it straightforward to reconfigure operator inputs after initialization. Once the graph is defined, input/output contracts are fixed.  
   - This limits dynamic reconfiguration and is an important design consideration when creating multi-stream operators.  

2. **Free-Run Synchronization**  
   - Free-run sync performs adequately on the first channel but degrades significantly when more channels are introduced.  
   - For multiple streams, manual or explicit synchronization strategies may be required.  

3. **Buffering Constraints**  
   - Each channel supports only one hardware buffer.  
   - To ensure reliable streaming, **software double buffering** was introduced to compensate for this limitation.  

4. **Toward a General Solution?**  
   - A truly general operator may be possible using **compilation flags** or configuration-driven specialization.  
   - That said, supporting every possible hardware topology and stream configuration in a single implementation is challenging and may not be practical.  

---

## Recommendations
- Provide **separate operator implementations** (or examples) for single-stream and multi-stream cases.  
- When working with multiple streams:
  - Plan explicit synchronization, as hardware free-run alone may not suffice.  
  - Account for limited hardware buffers by layering in software strategies like double buffering
