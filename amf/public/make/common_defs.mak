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

#
# Definitions that will be common to all AMF samples
#
# Here is an example of a minimum makefile that uses this system:
#
#     amf_root = <relative path to root of AMF source>
#     include $(amf_root)/public/make/common_defs.mak
#
#     src_files = <list of sources>
#     target_name = <name of exe target>
#
#     include $(amf_root)/public/make/common_rules.mak
#
#
# Lowercase variables are meant to be local to the makefiles
# UPPERCASE variables are designed so user can override them from command line/environment
#
# Description of variables
#
# MANDATORY variables your makefile must set:
#
#   amf_root - Relative path to the root of the AMF source tree
#   src_files - List of .cpp source files relative to amf_root
#   target_name - Name to give to exe_target
#
# Variables that give you info (treat as read-only):
#   host_bits - 32 or 64 bit compilation
#   build_type - rel or dbg build
#   build_dir - Output directory for the current build configuration
#   bin_dir - Output directory for binaries
#   exe_target_file - Full name of the final executable (for an executable project)
#   public_common_dir - Location of public common dir relative to amf_root
#   samples_common_dir - Location of samples common dir relative to amf_root
#
# Variables projects can add to or overwrite:
#   src_c_files - List of .c source files relative to amf_root
#   target_type - Type of target to produce. Defaults to "exe". Only "exe" and "so" currently supported
#   pp_include_dirs - Preprocessor include search directories (absolute - use $(amf_root) prefix for relative paths)
#   pp_defines - Preprocessor macros
#   cxx_flags - Flags passed to the C++ compiler
#   linker_flags - Flags passed to the linker
#   linker_libs - Extra libraries the linker should use
#   linker_dirs - Extra path the linker should use
#   build_info_vars - Variables that should be inspected and printed before the build
#   custom_target - Custom target
#   vulkan_shader_sources - Vulkan shader sources to compile. vulkan_shader_headers is output
#   vulkan_shader_output_dir - Directory where compiled vulkan shaders to placed in
#
# Variables user can override from the command line:
#   HOST_BITS - 32 or 64
#   BUILD_TYPE - dbg or rel
#   BUILD_ROOT - Relative or absolute path to the build output directory
#		 (Before target-specific suffixes are added to it)
#   CXX - Path to the C++ compiler
#   LNK - Path to the Linker
#   CPPFLAGS, CXXFLAGS, LDFLAGS - Preprocessor flags, C++ flags, linker flags
#   LDLIBS - Linker extra libraries
#   DEPFLAGS - Flags passed to preprocessor to generate dependencies
#
# Dev packages to be installed
#   libvulkan-dev
#   libasound2-dev
#   libx11-dev
#   mesa-common-dev
#   libgl1-mesa-dev
#   libglu1-mesa-dev
#   opencl-dev
#   lib32stdc++-7-dev
#   lib32gcc-7-dev
#   libgcc-7-dev
#   libstdc++-7-dev
#   ocl-icd-ope
#
# Required variable
# VK_SDK_PATH
################### Parameter checks and defaults ##############################

#ifeq (, $(VK_SDK_PATH))
#    $(error +++++ VK_SDK_PATH must be defined ++++)
#    exit 1
#endif


ifeq (32,$(HOST_BITS))
    host_bits = 32
else ifeq (64,$(HOST_BITS))
    host_bits = 64
else ifeq (,$(HOST_BITS))
    host_bits = 64
else
    $(error HOST_BITS must be either 32 or 64)
endif

ifeq (dbg,$(BUILD_TYPE))
    build_type = dbg
else ifeq (rel,$(BUILD_TYPE))
    build_type = rel
else ifeq (,$(BUILD_TYPE))
    build_type = dbg
else
    $(error BUILD_TYPE must be either dbg or rel)
endif

ifneq (,$(BUILD_ROOT))
    build_root = $(BUILD_ROOT)
else
    build_root = $(amf_root)/bin
endif

public_common_dir = public/common
samples_common_dir = public/samples/CPPSamples/common

build_dir = $(build_root)/$(build_type)_$(host_bits)/build/$(target_name)
bin_dir   = $(build_root)/$(build_type)_$(host_bits)
exe_target_file = $(bin_dir)/$(target_name)
so_target_file = $(bin_dir)/$(target_name).so

target_type = exe

######################## Global Overrides ######################################

CXX ?= g++
CC ?= gcc
LNK ?= g++
RM ?= rm -f
MKDIR ?= mkdir -p
RMDIR ?= rm -rf
COPY ?= cp

######################## Common Settings #######################################

pp_defines = \
    _UNICODE \
    UNICODE \
    _GLIBCXX_USE_CXX11_ABI=0

cxx_flags = \
   -pthread \
   -Werror \
   -Wall \
   -Wextra \
   -Wno-unknown-pragmas \
   -Wno-reorder \
   -Wno-unused \
   -Wno-switch \
   -Wno-sign-compare \
   -Wno-nonnull \
   -Wno-missing-field-initializers \
   -Wno-overloaded-virtual \
   -std=c++2a \
   -fexceptions \
   -fno-rtti \
   -fvisibility=hidden \
   -fPIC

ifdef AMF_STDCXX_STATIC
    cxx_flags += -static-libgcc -static-libstdc++
endif

# In the long run, we should fix this warning
cxx_flags += -Wno-unused-result

ifeq (dbg,$(build_type))
    pp_defines += DEBUG _DEBUG
    cxx_flags += -O0 -ggdb
else
    pp_defines += NDEBUG
    cxx_flags += \
	-fno-math-errno -fmerge-all-constants\
	-O3 -fno-strict-aliasing -fno-delete-null-pointer-checks \
	-fno-strict-overflow -flto
    ifndef NDK_PATH
        cxx_flags += -fuse-linker-plugin
    endif
endif

uname_p := $(shell uname -p)
ifneq ($(uname_p),aarch64)
ifeq (32,$(host_bits))
    cxx_flags += -m32
else
    cxx_flags += -m64
endif
endif

linker_flags = \
    -Wl,--no-undefined -Wl,-rpath='$$ORIGIN'

linker_libs = \
  dl \
  m

ifndef NDK_PATH
    linker_libs += \
      X11 \
      GL
else
    CXX = $(NDK_PATH)/bin/$(NDK_PREFIX)clang++
    CC = $(NDK_PATH)/bin/$(NDK_PREFIX)clang
    LNK = $(NDK_PATH)/bin/$(NDK_PREFIX)clang++
    pp_defines += AMF_ANDROID_ENCODER
    linker_libs += log
    cxx_flags += -Wno-inconsistent-missing-override
endif

build_info_vars = \
    amf_root target_name custom_target host_bits build_type exe_target_file target_type CXX LNK

vulkan_shader_output_dir = $(build_dir)

ifeq ($(uname_p),aarch64)
  ffmpeg_platform=arm
else
  ffmpeg_platform=lnx
endif

ffmpeg_dir = $(amf_root)/../Thirdparty/ffmpeg/ffmpeg/ffmpeg-7.0/$(ffmpeg_platform)$(host_bits)/release
