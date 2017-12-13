# Advanced Media Framework (AMF) SDK

AMF is a light-weight, portable multimedia framework that abstracts away most of the platform and API-specific details and allows for easy implementation of multimedia applications using a variety of technologies, such as DirectX 11, OpenGL, and OpenCL and facilitates an efficient interop between them.

These features are a part of the initial 1.3 version of AMF.  
* Version 1.4 of the AMF SDK includes support for the H.265 encoder(HEVC) and bug fixes.  
* Version 1.4.4 has support for FFmpeg 3.3.1.
* Version 1.4.6 has support for game dvr

<div>
  <a href="https://github.com/GPUOpen-LibrariesAndSDKs/AMF/releases/latest/"><img src="http://gpuopen-librariesandsdks.github.io/media/latest-release-button.svg" alt="Latest release" title="Latest release"></a>
</div>

### Prerequisites
* Windows&reg; 7 (SP1 with the [Platform Update](https://msdn.microsoft.com/en-us/library/windows/desktop/jj863687.aspx)), Windows&reg; 8.1, or Windows&reg; 10
* Visual Studio&reg; 2013, Visual Studio&reg; 2015 or Visual Studio&reg; 2017
* Version 1.4.6: AMD Radeon Software Crimson Edition 17.12.1 (17.50.02) or newer
* Version 1.4.4: AMD Radeon Software Crimson Edition 17.7.2 (17.30.1041) or newer
* Version 1.4: AMD Radeon Software Crimson Edition 17.1.1 (16.50.2611) or newer
* Version 1.3: AMD Radeon Software Crimson Edition 16.7.3 (16.30.2311) or newer
* Version 1.4.4 or later requires OCL_SDK_Light: https://github.com/GPUOpen-LibrariesAndSDKs/OCL-SDK/releases. Previous versions of AMF require the AMD APP SDK (Version 3.0 or later), Windows 10 SDK (Version 10586) and some samples require the Microsoft Foundation Class Library (MFC)

The AMF framework is compatible with most recent Radeon GPUs starting with the Southern Islands family and APUs of the Kabini, Kaveri, Carrizo families and newer.

### Getting Started
* Visual Studio solutions can be found in the `amf\public\samples` directory.
* Additional documentation can be found in the `amf\doc` directory.

### Third-Party Software
* FFmpeg is distributed under the terms of the LGPLv2.1.

### Attribution
* AMD, the AMD Arrow logo, Radeon, and combinations thereof are either registered trademarks or trademarks of Advanced Micro Devices, Inc. in the United States and/or other countries.
* Microsoft, DirectX, Visual Studio, and Windows are either registered trademarks or trademarks of Microsoft Corporation in the United States and/or other countries.
* OpenGL and the oval logo are trademarks or registered trademarks of Silicon Graphics, Inc. in the United States and/or other countries worldwide.
* OpenCL and the OpenCL logo are trademarks of Apple Inc. used by permission by Khronos.
