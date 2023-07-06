#
# MIT license 
#
#
# Copyright (c) 2018 Advanced Micro Devices, Inc. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

amf_root = ../../../..

include $(amf_root)/public/make/common_defs.mak

target_name = SimpleDecoder

pp_include_dirs = $(amf_root)

src_files = \
    public/samples/CPPSamples/SimpleDecoder/SimpleDecoder.cpp \
    $(samples_common_dir)/BitStreamParser.cpp \
    $(samples_common_dir)/BitStreamParserH264.cpp \
    $(samples_common_dir)/BitStreamParserH265.cpp \
    $(samples_common_dir)/BitStreamParserIVF.cpp \
    $(samples_common_dir)/MiscHelpers.cpp \
    $(samples_common_dir)/SurfaceUtils.cpp \
    $(public_common_dir)/AMFFactory.cpp \
    $(public_common_dir)/AMFSTL.cpp \
    $(public_common_dir)/DataStreamFactory.cpp \
    $(public_common_dir)/DataStreamFile.cpp \
    $(public_common_dir)/DataStreamMemory.cpp \
    $(public_common_dir)/Thread.cpp \
    $(public_common_dir)/TraceAdapter.cpp \
    $(public_common_dir)/Linux/ThreadLinux.cpp

include $(amf_root)/public/make/common_rules.mak
