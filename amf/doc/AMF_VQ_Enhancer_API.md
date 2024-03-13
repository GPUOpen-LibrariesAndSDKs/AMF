#### Advanced Micro Devices

# Advanced Media Framework – VQ Enhancer

#### Programming Guide

---

### Disclaimer

The information contained herein is for informational purposes only, and is subject to change without notice. While every precaution has been taken in the preparation of this document, it may contain technical inaccuracies, omissions and typographical errors, and AMD is under no obligation to update or otherwise correct this information.

Advanced Micro Devices, Inc. makes no representations or warranties with respect to the accuracy or completeness of the contents of this document, and assumes no liability of any kind, including the implied warranties of noninfringement, merchantability or fitness for particular purposes, with respect to the operation or use of AMD hardware, software or other products described herein. No license, including implied or arising by estoppel, to any intellectual property rights is granted by this document. Terms and limitations applicable to the purchase or use of AMD's products are as set forth in a signed agreement between the parties or in AMD's Standard Terms and Conditions of Sale.

AMD, the AMD Arrow logo, ATI Radeon™, CrossFireX™, LiquidVR™, TrueAudio™ and combinations thereof are trademarks of Advanced Micro Devices, Inc. Other product names used in this publication are for identification purposes only and may be trademarks of their respective companies.

Windows™, Visual Studio and DirectX are trademark of Microsoft Corp.

### Copyright Notice

© 2022-2024 Advanced Micro Devices, Inc. All rights reserved

Notice Regarding Standards. AMD does not provide a license or sublicense to any Intellectual Property Rights relating to any standards, including but not limited to any audio and/or video codec technologies such as MPEG-2, MPEG-4; AVC/H.264; HEVC/H.265; AAC decode/FFMPEG; AAC encode/FFMPEG; VC-1; and MP3 (collectively, the "Media Technologies"). For clarity, you will pay any royalties due for such third party technologies, which may include the Media Technologies that are owed as a result of AMD providing the Software to you.

### MIT license

Copyright (c) 2024 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### Contents
   
 1. [Introduction](#1-introduction)
 2. [AMF Video VQ Enhancer Component](#2-amf-video-vq-enhancer-component)
    - [2.1 Component Initialization](#21-component-initialization)
    - [2.2 Configuring the VQ Enhancer](#22-configuring-the-vq-enhancer)
    - [2.3 Submitting Input and Retrieving Output](#23-submitting-input-and-retrieving-output)
    - [2.4 Terminating the VQ Enhancer Component](#24-terminating-the-vq-enhancer-component)

 ## 1 Introduction

AMF VQ Enhancer is a technique for reconstructing high-quality videos from low-quality compressed videos. This document provides a complete description of the AMD Advanced Media Framework (AMF) Video Enhancer Component. This component performs the following functions:

- Removing/reducing the blocking artifacts introduced by AVC/HEVC compression with low bit rate.
- Preserving details.

## 2 AMF Video VQ Enhancer Component

Video VQ Enhancer accepts input frames stored in `AMFSurface` objects wrapping DirectX 11/12 textures, Vulkan surfaces, OpenCL surfaces. The output is placed in `AMFSurface` objects wrapping DirectX 11/12 textures, OpenCL surfaces, Vulkan surfaces, depending on the component configuration.

Include `public/include/components/VQEnhancer.h`

### 2.1 Component Initialization

The AMF VQ Enhancer component should be initialized using the following sequence:

1. Create an AMF Context and initialize it for one of the following:
   1. DirectX 11
   2. DirectX 12
   3. Vulkan
   4. OpenCL
2. Configure the VQ Enhancer component by setting the necessary properties using the `AMFPropertyStorage::SetProperty` method on the VQ Enhancer object.
3. Call the `VideoEnhancer::Init` method of the video HQ Scaler object.

The details on compoment setup can be found from the document `AMF_API_Reference.pdf`

### 2.2 Configuring the VQ Enhancer

VQ enhancer supports the following input and output formats:

1. BRGA
1. NV12
1. RGBA
1. R10G10B10A2
1. RGBA_F16
1. P010

The output format must be same as the input and the format conversion is not supported. The parameters of the output stream are set using the following properties:

| Name (prefix "AMF_VIDEO_ENHANCER" / "AMF_VE_FCR_") | Type            |
| :------------------------------- | :-------------- |
| ENGINE_TYPE                      | AMF_MEMORY_TYPE |
| OUTPUT_SIZE                      | AMFSize         |
| ATTENUATION                      | Float           |
| RADIUS [^1]                      | amf_int64       |
| SPLIT_VIEW                       | amf_int64       |

<p align="center">
Table 1. AMF VQ Enhancer properties of the output stream
</p>

[^1]: Deprecated.

---

**Name:**

`AMF_VIDEO_ENHANCER_ENGINE_TYPE`

**Values:**
`AMF_MEMORY_DX11`, `AMF_MEMORY_DX12`, `AMF_MEMORY_VULKAN`,`AMF_MEMORY_OPENCL`

**Default Value:**
`AMF_MEMORY_DX11`

**Description:**
Specifies the memory type of output surfaces (surfaces are allocated internally by the VQ Enhancer component). The ouput surface type can be different from input surface and this enables sharing a resource with another API using interop. For example, DX11 output can be interoped to OCL, processed in OCL and output will be DX11.

---

**Name:**
`AMF_VIDEO_ENHANCER_OUTPUT_SIZE`

**Values:**
A valid size

**Default Value:**
`N\A`

**Description:**
Output image resolution.  VQ enhancer will be performed when this property is set.

---

**Name:**
`AMF_VE_FCR_ATTENUATION`

**Values:**
Float in the range of `[0.02, 0.4]`

**Default Value:**
`0.1`

**Description:**
Control VQEnhancer strength.

---

**Name:**
`AMF_VE_FCR_RADIUS`

**Values:**
Int in the range of `[1, 4]`

**Default Value:**
`4`

**Description:**
Deprecated. Setting this property has no effect.

---

**Name:**
`AMF_VE_FCR_SPLIT_VIEW`

**Values:**
`0` / `1` (OFF / ON)

**Default Value:**
`0`

**Description:**
Experimental. When set, enables a side by side view with processing enabled on one side and disabled on the other side.

---

### 2.3 Submitting Input and Retrieving Output

Once the VQ enhancer component is successfully initialized, you may start submitting input samples to it. Input samples must be submitted as `AMFSurface` objects.

At the same time poll for output by calling `AMFComponent::QueryOutput` on the VQ enhancer object. Polling for output samples can be done either from the same thread or from another thread.

Suspend submission of input samples briefly when `AMFComponent::SubmitInput` returns `AMF_INPUT_FULL`. Continue to poll for output samples and process them as they become available.

### 2.4 Terminating the VQ Enhancer Component

To terminate the VQ Enhancer component, call the `Terminate` method, or simply destroy the object. Ensure that the context used to create the VQ Enhancer component still exists during termination.