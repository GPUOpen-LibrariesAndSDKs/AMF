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

target_name = amf-component-ffmpeg
target_type = so

uname_p := $(shell uname -p)
  ifeq ($(uname_p),aarch64)
  platform_name=arm
  else
  platform_name=lnx
endif

ffmpeg_dir = $(amf_root)/../Thirdparty/ffmpeg/ffmpeg/ffmpeg-6.0/$(platform_name)$(host_bits)/release

pp_defines += \
  AMF_COMPONENT_FFMPEG_EXPORTS

cxx_flags += \
 -Wno-deprecated-declarations \
 -Wno-error=attributes


pp_include_dirs = $(amf_root) \
  $(ffmpeg_dir)/include

ffmpeg_libs = \
  libavcodec.so.61 \
  libavdevice.so.61 \
  libavfilter.so.10 \
  libavformat.so.61 \
  libavutil.so.59 \
  libswresample.so.5 \
  libswscale.so.8

linker_libs += $(patsubst %,:"%",$(ffmpeg_libs))

linker_dirs += \
 $(ffmpeg_dir)/bin

src_files = \
    $(public_common_dir)/AMFFactory.cpp \
    $(public_common_dir)/AMFSTL.cpp \
    $(public_common_dir)/DataStreamFactory.cpp \
    $(public_common_dir)/DataStreamFile.cpp \
    $(public_common_dir)/DataStreamMemory.cpp \
    $(public_common_dir)/Thread.cpp \
    $(public_common_dir)/TraceAdapter.cpp \
    $(public_common_dir)/IOCapsImpl.cpp \
    $(public_common_dir)/PropertyStorageExImpl.cpp \
    $(public_common_dir)/Linux/ThreadLinux.cpp \
    public/src/components/ComponentsFFMPEG/AudioConverterFFMPEGImpl.cpp \
    public/src/components/ComponentsFFMPEG/AudioDecoderFFMPEGImpl.cpp \
    public/src/components/ComponentsFFMPEG/AudioEncoderFFMPEGImpl.cpp \
    public/src/components/ComponentsFFMPEG/VideoDecoderFFMPEGImpl.cpp \
	public/src/components/ComponentsFFMPEG/BaseEncoderFFMPEGImpl.cpp \
    public/src/components/ComponentsFFMPEG/H264EncoderFFMPEGImpl.cpp \
    public/src/components/ComponentsFFMPEG/HEVCEncoderFFMPEGImpl.cpp \
    public/src/components/ComponentsFFMPEG/AV1EncoderFFMPEGImpl.cpp \
    public/src/components/ComponentsFFMPEG/ComponentFactory.cpp \
    public/src/components/ComponentsFFMPEG/FileDemuxerFFMPEGImpl.cpp \
    public/src/components/ComponentsFFMPEG/FileMuxerFFMPEGImpl.cpp \
    public/src/components/ComponentsFFMPEG/H264Mp4ToAnnexB.cpp \
    public/src/components/ComponentsFFMPEG/UtilsFFMPEG.cpp

#execute rules

include $(amf_root)/public/make/common_rules.mak

# copy FFmpeg SO libs

define copy_rule_fn
all: $$(bin_dir)/$1

$$(bin_dir)/$1: $$(ffmpeg_dir)/bin/$1
	$$(COPY) -f $$< $$@
endef

$(foreach copy_file,$(ffmpeg_libs),\
    $(eval $(call copy_rule_fn,$(copy_file)))\
)

clean: 
	$(foreach clean_file,$(ffmpeg_libs),\
		$(RM) $(bin_dir)/$(clean_file)\
	)
