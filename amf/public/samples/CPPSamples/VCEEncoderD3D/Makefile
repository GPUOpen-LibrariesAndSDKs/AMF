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

target_name = VCEEncoderD3D
target_type = exe

pp_include_dirs = \
 $(amf_root) \
 $(VULKAN_SDK)/include

 sample_path = public/samples/CPPSamples/VCEEncoderD3D

src_files = \
    $(public_common_dir)/AMFFactory.cpp \
    $(public_common_dir)/AMFSTL.cpp \
    $(public_common_dir)/DataStreamFactory.cpp \
    $(public_common_dir)/DataStreamFile.cpp \
    $(public_common_dir)/DataStreamMemory.cpp \
    $(public_common_dir)/Thread.cpp \
    $(public_common_dir)/Linux/ThreadLinux.cpp \
    $(public_common_dir)/TraceAdapter.cpp \
    $(public_common_dir)/VulkanImportTable.cpp \
    $(samples_common_dir)/CmdLogger.cpp \
    $(samples_common_dir)/CmdLineParser.cpp \
    $(samples_common_dir)/DeviceVulkan.cpp \
    $(samples_common_dir)/EncoderParamsAVC.cpp \
    $(samples_common_dir)/EncoderParamsHEVC.cpp \
    $(samples_common_dir)/EncoderParamsAV1.cpp \
    $(samples_common_dir)/ParametersStorage.cpp \
    $(samples_common_dir)/Pipeline.cpp \
    $(samples_common_dir)/SwapChain.cpp \
    $(samples_common_dir)/SwapChainVulkan.cpp \
    $(sample_path)/VCEEncoderD3D.cpp \
    $(sample_path)/RenderEncodePipeline.cpp \
    $(sample_path)/RenderWindow.cpp \
    $(sample_path)/VideoRender.cpp \
    $(sample_path)/VideoRenderHost.cpp \
    $(sample_path)/VideoRenderVulkan.cpp \


shader_sources = \
    cube.frag \
    cube.vert

include $(amf_root)/public/make/common_rules.mak

# compile Vulkan shaders

define shader_compile_rule_fn
all: $$(bin_dir)/$1.spv

$$(bin_dir)/$1.spv: $$(amf_root)/$$(sample_path)/$1
	$$(VK_SDK_PATH)/x86_64/bin/glslangValidator -V "$$<" -o "$$@"
endef

$(foreach shader_file,$(shader_sources),\
    $(eval $(call shader_compile_rule_fn,$(shader_file)))\
)

clean:
	$(foreach clean_file,$(shader_sources),\
		$(RM) $(bin_dir)/$(clean_file).spv\
	)
