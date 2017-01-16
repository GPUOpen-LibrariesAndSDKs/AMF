rem
rem Notice Regarding Standards.  AMD does not provide a license or sublicense to
rem any Intellectual Property Rights relating to any standards, including but not
rem limited to any audio and/or video codec technologies such as MPEG-2, MPEG-4;
rem AVC/H.264; HEVC/H.265; AAC decode/FFMPEG; AAC encode/FFMPEG; VC-1; and MP3
rem (collectively, the "Media Technologies"). For clarity, you will pay any
rem royalties due for such third party technologies, which may include the Media
rem Technologies that are owed as a result of AMD providing the Software to you.
rem 
rem MIT license
rem
rem
rem Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
rem
rem Permission is hereby granted, free of charge, to any person obtaining a copy
rem of this software and associated documentation files (the "Software"), to deal
rem in the Software without restriction, including without limitation the rights
rem to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
rem copies of the Software, and to permit persons to whom the Software is
rem furnished to do so, subject to the following conditions:
rem
rem The above copyright notice and this permission notice shall be included in
rem all copies or substantial portions of the Software.
rem
rem THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
rem IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
rem FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
rem AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
rem LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
rem OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
rem THE SOFTWARE.
rem

@echo off
echo running VCEEncoderD3D...
VCEEncoderD3D.exe -output VideoSample_1280x720.h264 -width 1280 -height 720 -usage transcoding -RateControlMethod cbr -targetBitrate 500000 -FRAMES 400 -render dx9
