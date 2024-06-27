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

target_name = PlaybackHW
target_type = exe

pp_include_dirs = \
    $(amf_root) \
    $(VULKAN_SDK)/include \
    $(build_dir) \

 linker_libs += asound

src_files = \
    public/samples/CPPSamples/PlaybackHW/PlaybackHW.cpp \
    $(public_common_dir)/AMFFactory.cpp \
    $(public_common_dir)/AMFSTL.cpp \
    $(public_common_dir)/DataStreamFactory.cpp \
    $(public_common_dir)/DataStreamFile.cpp \
    $(public_common_dir)/DataStreamMemory.cpp \
    $(public_common_dir)/Thread.cpp \
    $(public_common_dir)/TraceAdapter.cpp \
    $(public_common_dir)/IOCapsImpl.cpp \
    $(public_common_dir)/PropertyStorageExImpl.cpp \
    $(public_common_dir)/VulkanImportTable.cpp \
    $(public_common_dir)/OpenGLImportTable.cpp \
    $(public_common_dir)/Linux/ThreadLinux.cpp \
    $(samples_common_dir)/AudioPresenter.cpp \
    $(samples_common_dir)/AudioPresenterWin.cpp \
    $(samples_common_dir)/AudioPresenterLinux.cpp \
    $(samples_common_dir)/BitStreamParser.cpp \
    $(samples_common_dir)/BitStreamParserH264.cpp \
    $(samples_common_dir)/BitStreamParserH265.cpp \
    $(samples_common_dir)/BitStreamParserIVF.cpp \
    $(samples_common_dir)/CmdLogger.cpp \
    $(samples_common_dir)/CmdLineParser.cpp \
    $(samples_common_dir)/ParametersStorage.cpp \
    $(samples_common_dir)/Pipeline.cpp \
    $(samples_common_dir)/PlaybackPipeline.cpp \
    $(samples_common_dir)/PlaybackPipelineBase.cpp \
    $(samples_common_dir)/SwapChain.cpp \
    $(samples_common_dir)/SwapChainVulkan.cpp \
    $(samples_common_dir)/SwapChainOpenGL.cpp \
    $(samples_common_dir)/VideoPresenter.cpp \
    $(samples_common_dir)/VideoPresenterOpenGL.cpp \
    $(samples_common_dir)/VideoPresenterVulkan.cpp \

vulkan_shader_sources = \
    $(amf_root)/$(samples_common_dir)/QuadVulkan_fs.frag \
    $(amf_root)/$(samples_common_dir)/QuadVulkan_vs.vert

include $(amf_root)/public/make/common_rules.mak

$(build_dir)/public/samples/CPPSamples/common/VideoPresenterVulkan.o : $(vulkan_shader_headers)