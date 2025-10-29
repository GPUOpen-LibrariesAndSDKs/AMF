# Advanced Media Framework (AMF) SDK

AMF is a light-weight, portable multimedia framework that abstracts away most of the platform and API-specific details and allows for easy implementation of multimedia applications using a variety of technologies, such as DirectX 11, OpenGL, and OpenCL and facilitates an efficient interop between them.

<div>
  <a href="https://github.com/GPUOpen-LibrariesAndSDKs/AMF/releases/latest/"><img src="http://gpuopen-librariesandsdks.github.io/media/latest-release-button.svg" alt="Latest release" title="Latest release"></a>
</div>

### Prerequisites
* Windows
    * Windows&reg; 7 (SP1 with the Platform Update) (AMF v1.4.18.0 and older)
    * Windows&reg; 8.1 (AMF v1.4.0.0 and older)
    * Windows&reg; 10, or Windows&reg; 11
    * Windows Subsystem for Linux (DX12 Decoder and Converter Only)
    * Visual Studio&reg; 2019
* Linux
    * RHEL 9.6 / 10
    * Ubuntu 24.04.2 / 22.04.5
    * SLED/SLES 15.7 / 16
* Driver and AMF component installation instructions are available on the [Wiki page](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/wiki).
    
  Note: the 25.10 Linux driver has a known installation bug. See the [Linux Driver Install](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/wiki/Driver%20Linux) Wiki page for details.

  Note: starting with the 25.20 Linux driver, the AMF runtime is released separately. See the [Linux Driver Install](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/wiki/Driver%20Linux) Wiki page for updated instructions.

    * The following table contains the driver versions in which the Linux pro driver started including the AMF runtime, otherwise, the AMF runtime is optional and has to be installed separately.
    * All supported distros include the AMF runtime starting driver version 20.40.

      | OS                        | AMF included starting version:    |
      | -------------             |:-------------:                    |
      | SLE 15                    | 18.40                             |
      | Ubuntu 20.04.0            | 20.20                             |
      | \**All supported distros* | 20.40                             |

* AMF SDK is backward compatible with all previous driver versions.
* Version 1.5.0: AMD Radeon Software Adrenalin Edition 25.10.2 (25.20.21.01) or newer. Added 4:4:4/4:2:2 chroma subsampling support for VideoConverter color space conversion and scaling. Samples updated to VS 2022.
* Version 1.4.36: AMD Radeon Software Adrenalin Edition 25.1.1 (24.30.31.03) or newer. Added B-frame support and picture management options for AV1 encoder. New high quality presets for HEVC and AVC encoders. New SimpleFRC sample app.
* Version 1.4.35: AMD Radeon Software Adrenalin Edition 24.9.1 (24.20.11.01) or newer. Added multi-HW instance encoder mode. Updated FFmpeg to 7.0. Added split frame encoding.
* Version 1.4.34: AMD Radeon Software Adrenalin Edition 24.6.1 (24.10.21.01) or newer. Added Linux support for DVR sample. New HEVC header insertion mode. Stable support for RADV drivers for AMF on Linux in VideoConverter/HQScaler/VideoEncoder and experimental for decoder. Added DX11 support for FRC component.
* Version 1.4.33: AMD Radeon Software Adrenalin Edition 24.1.1 (23.40.02) or newer. Added native DX12 support for encoding and PreAnalysis. Switched to public Vulkan Khronos extensions for decoder. AMF on Linux can now be used with AMD Pro Vulkan, and experimentally with RADV drivers.
* Version 1.4.32: AMD Radeon Software Adrenalin Edition 23.12.1 (23.30.13.01) or newer. Added new FRC component and API doc. Added new VQEnhancer doc. Improved presenters. Updated FFmpeg to 6.0.
* Version 1.4.30: AMD Radeon Software Adrenalin Edition 23.5.2 (23.10.01.45) or newer. Added AMF wrappers for AVC / HEVC / AV1 FFmpeg software encoders, frame in -> slice / tile output support for AVC / HEVC / AV1 encoders, and multi-monitor support for DVR. Updated FFmpeg to 5.1.2.
* Version 1.4.29: AMD Radeon Software Adrenalin Edition 23.1.2 (22.40.01.34) or newer. Added SmartAccess Video for AVC / HEVC / AV1. New options for VQEnhancer and AV1 encoder components. Switched to Markdown based API docs which enable easier open source developer contributions.
* Version 1.4.28: AMD Radeon Software Adrenalin Edition 22.12.1 (22.40.00.24) or newer. Added AV1 encoding support and 12-bit AV1 decoding. New VQEnhancer component. New AVC / HEVC encoder rate control methods.
* Version 1.4.26: AMD Radeon Software Adrenalin Edition 22.7.1 (22.20.15.01) or newer. Added new PAQ, TAQ, and high motion quality boost modes for PreAnalysis. New HQScaler sharpness, low latency decoder and temporal SVC encoder options. 
* Version 1.4.24: AMD Radeon Software Adrenalin Edition 22.3.1 (21.50.02.01) or newer. Added new AMD Direct Capture mode, new HQScaler feature(Bilinear/Bicubic/FSR), new Vulkan HEVC encoder on Navi family, improvements on H264 Vulkan encoding.
* Version 1.4.23: AMD Radeon Software Adrenalin Edition 21.12.1 (21.40.11.03) or newer. Added new Auto LTR encoder mode, additional encoder usage presets and encoder statistics/feedback.
* Version 1.4.21: AMD Radeon Software Adrenalin Edition 21.10.1 (21.30.25.01) or newer. Added PSNR/SSIM score feedback, new QVBR rate control mode and LTR mode for encoders, added HDR support for HEVC encoder and color converter, new EncoderLatency sample app.
* Version 1.4.18: AMD Radeon Software Adrenalin Edition 20.11.2 or newer. Added Pre-Encode filter within Pre-Processing component in 1.4.18.
* Version 1.4.9 or later requires Vulkan SDK for some samples: https://vulkan.lunarg.com/  and AMD Radeon Software Adrenalin Edition 18.8.1 (18.30.01.01) or newer. This version supports Linux (see amd.com for driver support)
* Version 1.4.4 or later requires OCL_SDK_Light: https://github.com/GPUOpen-LibrariesAndSDKs/OCL-SDK/releases. Previous versions of AMF require the AMD APP SDK (Version 3.0 or later), Windows 10 SDK (Version 10586). This version requires AMD Radeon Software Crimson Edition 17.7.2 (17.30.1041) or newer
* Version 1.4: AMD Radeon Software Crimson Edition 17.1.1 (16.50.2611) or newer
* Version 1.3: AMD Radeon Software Crimson Edition 16.7.3 (16.30.2311) or newer


The AMF framework is compatible with most recent Radeon GPUs starting with the Southern Islands family and APUs of the Kabini, Kaveri, Carrizo families and newer.

### Getting Started
* Visual Studio solutions can be found in the `amf\public\samples` directory.
* Additional documentation can be found in the `amf\doc` directory.
* To build samples on Linux use 'makefile' in `amf\public\samples`.
* Follow this guide to build your own first encoder application
  * [Practical Guide for Video CODEC Encoder App Developers](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/wiki/Guide%20for%20Video%20CODEC%20Encoder%20App%20Developers)
* Find hardware features and support
  * [GPU and APU HW Features and Support](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/wiki/GPU%20and%20APU%20HW%20Features%20and%20Support)

### Third-Party Software
* FFmpeg is distributed under the terms of the LGPLv2.1.

### Attribution
* AMD, the AMD Arrow logo, Radeon, and combinations thereof are either registered trademarks or trademarks of Advanced Micro Devices, Inc. in the United States and/or other countries.
* Microsoft, DirectX, Visual Studio, and Windows are either registered trademarks or trademarks of Microsoft Corporation in the United States and/or other countries.
* OpenGL and the oval logo are trademarks or registered trademarks of Silicon Graphics, Inc. in the United States and/or other countries worldwide.
* OpenCL and the OpenCL logo are trademarks of Apple Inc. used by permission by Khronos.
* Vulkan and the Vulkan logo are registered trademarks of the Khronos Group Inc.
