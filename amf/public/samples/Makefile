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

AMF_ROOT = ../..

AMF_SAMPLES = $(AMF_ROOT)/public/samples/CPPSamples
AMF_SAMPLE_COMPONENTS = $(AMF_ROOT)/public/src/components

SUBDIRS = \
	$(AMF_SAMPLE_COMPONENTS)/ComponentsFFMPEG \
	$(AMF_SAMPLES)/CapabilityManager \
	$(AMF_SAMPLES)/PlaybackHW \
	$(AMF_SAMPLES)/EncoderLatency \
	$(AMF_SAMPLES)/SimpleEncoder \
	$(AMF_SAMPLES)/SimpleDecoder \
	$(AMF_SAMPLES)/SimpleConverter \
	$(AMF_SAMPLES)/SimpleFrameInjection \
	$(AMF_SAMPLES)/SimplePA \
	$(AMF_SAMPLES)/SimpleROI \
	$(AMF_SAMPLES)/TranscodeHW \
	$(AMF_SAMPLES)/VCEEncoderD3D


.PHONY: all clean $(SUBDIRS)
all: $(SUBDIRS)

clean:
	$(MAKE) TARGET_NAME=clean

$(SUBDIRS):
	$(MAKE) -C $@ $(TARGET_NAME)
