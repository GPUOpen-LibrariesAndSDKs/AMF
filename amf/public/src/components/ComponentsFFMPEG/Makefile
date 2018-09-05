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

ffmpeg_dir = $(amf_root)/../Thirdparty/ffmpeg/ffmpeg-3.3.1/lnx$(host_bits)/release

pp_defines += \
  AMF_COMPONENT_FFMPEG_EXPORTS

cxx_flags += \
 -Wno-deprecated-declarations

pp_include_dirs = $(amf_root) \
  $(ffmpeg_dir)/include

ffmpeg_libs = \
  libavcodec.so.57 \
  libavdevice.so.57 \
  libavfilter.so.6 \
  libavformat.so.57 \
  libavresample.so.3 \
  libavutil.so.55 \
  libswresample.so.2 \
  libswscale.so.4

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
