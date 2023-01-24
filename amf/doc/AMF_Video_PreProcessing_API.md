#### Advanced Micro Devices

# Advanced Media Framework – Pre-Processing Component

#### Programming Guide

---

### Disclaimer

The information contained herein is for informational purposes only, and is subject to change without notice. While every precaution has been taken in the preparation of this document, it may contain technical inaccuracies, omissions and typographical errors, and AMD is under no obligation to update or otherwise correct this information.

Advanced Micro Devices, Inc. makes no representations or warranties with respect to the accuracy or completeness of the contents of this document, and assumes no liability of any kind, including the implied warranties of noninfringement, merchantability or fitness for particular purposes, with respect to the operation or use of AMD hardware, software or other products described herein. No license, including implied or arising by estoppel, to any intellectual property rights is granted by this document. Terms and limitations applicable to the purchase or use of AMD’s products are as set forth in a signed agreement between the parties or in AMD's Standard Terms and Conditions of Sale.

AMD, the AMD Arrow logo, ATI Radeon™, CrossFireX™, LiquidVR™, TrueAudio™ and combinations thereof are trademarks of Advanced Micro Devices, Inc. Other product names used in this publication are for identification purposes only and may be trademarks of their respective companies.

Windows™, Visual Studio and DirectX are trademark of Microsoft Corp.

---

### Copyright Notice

© 2013-2022 Advanced Micro Devices, Inc. All rights reserved

Notice Regarding Standards.  AMD does not provide a license or sublicense to any Intellectual Property Rights relating to any standards, including but not limited to any audio and/or video codec technologies such as MPEG-2, MPEG-4; AVC/H.264; HEVC/H.265; AAC decode/FFMPEG; AAC encode/FFMPEG; VC-1; and MP3 (collectively, the “Media Technologies”). For clarity, you will pay any royalties due for such third-party technologies, which may include the Media Technologies that are owed as a result of AMD providing the Software to you.

### MIT license

Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### Contents

1. [Introduction](#1-introduction)
   - [1.1 Scope](#11-scope)
   - [1.2 Overview](#12-overview)
   - [1.3 Supported Hardware](#13-supported-hardware)
   - [1.4 Disclaimer](#14-disclaimer)
2. [AMF Pre-Processing](#2-amf-pre-processing)
   - [2.1 Input / Output Format](#21-input--output-format)
   - [2.2 Properties](#22-properties)
   - [2.3 Sample Application](#23-sample-application)
3. [Annex A: Glossary of Acronyms](#4-annex-a-glossary-of-acronyms)


## 1 Introduction

### 1.1 Scope

This document describes the Pre-Processing component of the AMD Advanced Media Framework (AMF). Full documentation on AMF can be found at <https://gpuopen.com/gaming-product/advanced-media-framework/>.

### 1.2 Overview

The AMF Pre-Processing component is intended to run before the HW encoder aiming at improving its coding efficiency. The AMF Pre-Processing component is instantiated as a separate AMF component. It takes in raw NV12 video surfaces and outputs the processed NV12 video images, which can be passed to a downstream component (Pre-Analysis or encoder).

### 1.3 Supported Hardware

The AMF Pre-Processing component is supported by Radeon RX 5000 Series or newer GPUs as well as Ryzen 2000 U/H series or newer APUs.

### 1.4 Disclaimer

The current AMF Pre-Processing component is in its beta release. Existing features may get optimized over time, and new features may also be added in future releases.


## 2 AMF Pre-Processing

In the current release, the Pre-Processing component includes a JND based edge-adaptive denoising filter that helps the encoder to achieve better coding efficiency. This filter aims at removing bit-expensive high frequency details that are deemed not important for the human visual system. There are two controls for this filter: strength and sensitivity. “Strength” controls the filtering strength (how much picture details will be removed), while “sensitivity” controls the edge detection sensitivity (what regions will be filtered, as regions detected as edges will not be filtered).

The AMF Pre-processing component runs on DX11 and OpenCL kernels. Since the Pre-Processing filter in the AMF Pre-Processing component currently accepts the NV12 format, the AMF Converter can be used to convert images to the compatible NV12 format before being submitted into to the Pre-Processing component.

### 2.1 Input / Output Format

The AMF Pre-Processing component accepts the NV12 format as input and outputs the processed images in NV12 format as well.

### 2.2 Properties

Table 1 provides the detailed description of the available parameters for AMF Pre-Processing.

| Name                               | Type            |
| :--------------------------------- | :-------------- |
| AMF_PP_ENGINE_TYPE                 | AMF_MEMORY_TYPE |
| AMF_PP_ADAPTIVE_FILTER_STRENGTH    | amf_int64       |
| AMF_PP_ADAPTIVE_FILTER_SENSITIVITY | amf_int64       |

<p align="center">
Table 2. AMF PA properties in standalone mode
</p>

---

**Name:**
`AMF_PP_ENGINE_TYPE`

**Values:**
`AMF_MEMORY_HOST`, `AMF_MEMORY_DX11`, `AMF_MEMORY_OPENCL`

**Default Value:**
`AMF_MEMORY_OPENCL`

**Description:**
Determines what type of kernels the pre-processor uses.

---

**Name:**
`AMF_PP_ADAPTIVE_FILTER_STRENGTH`

**Values:**
`0` - `10`

**Default Value:**
`4`

**Description:**
Strength of the pre-processing filter. The higher the strength, the stronger the filtering.

---

**Name:**
`AMF_PP_ADAPTIVE_FILTER_SENSITIVITY`

**Values:**
`0` - `10`

**Default Value:**
`6`

**Description:**
Sensitivity to edges. The higher the value, the more likely edges will be detected, and hence the less likely filtering will occur.

---

### 2.3 Sample Application

The TranscodeHW sample application in the SDK package can be used to illustrate how to enable the AMF Pre-Processing filter. When the Pre-Processing filter is enabled using

`-EnablePreEncoderFilter true`

TranscodeHW will filter the decoded raw images before submitting them to the encoder. Below is an example cmd line enabling the Pre-Processing filter:

`TranscodeHW.exe -input input.mp4  -output output.mp4 -width 1920 -height 1080 -usage transcoding -qualitypreset quality -targetBitrate 5000000 -frames 1000 -engine dx11 -EnablePreEncodeFilter true -PPEngineType opencl -PPAdaptiveFilterStrength 4 -PPAdaptiveFilterSensitivity 6`

## 4 Annex A: Glossary of Acronyms

| Acronym | Definition               |
| ------- | :----------------------- |
| AMF     | Advanced Media Framework |
| PP      | Pre-Processing           |
| PA      | Pre-Analysis             |

