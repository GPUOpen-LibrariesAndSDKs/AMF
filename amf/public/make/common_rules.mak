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

### See common_defs.mak for explanations of all this ###


######################## Final setup ###########################################

# Set the target file depending on project type
ifeq (exe,$(target_type))
    target_file = $(exe_target_file)
else
  ifeq (so,$(target_type))
    target_file = $(so_target_file)
  else
    $(error Currently only exe and so targets are supported)
  endif
endif

target_dir = $(dir $(target_file))
# sort removes duplicates
obj_dirs = $(sort $(addprefix $(build_dir)/,$(dir $(src_files))))

CPPFLAGS += $(patsubst %,-I"%",$(pp_include_dirs))
CPPFLAGS += $(patsubst %,-D"%",$(pp_defines))
CXXFLAGS += $(cxx_flags)

LDFLAGS += $(patsubst %,-L"%",$(linker_dirs))
LDFLAGS += $(linker_flags)
LDFLAGS += $(CXXFLAGS)

ifeq (so,$(target_type))
  LDFLAGS += -shared
endif

LDLIBS += $(patsubst %,-l%,$(linker_libs))

DEPFLAGS = -MMD -MQ $@ -MP -MF $(@:=.d)

######################## Configuration summary #################################

$(info ========================================)
$(info Building configuration:)
$(info ========================================)
$(foreach VARNAME,$(build_info_vars),\
    $(info $(VARNAME): $($(VARNAME)))\
)
$(info ========================================)


#################### Rules for all the targets #################################

.PHONY: all clean
all: $(target_file)

clean:
	$(RM) $(target_file)
	$(RMDIR) $(build_dir)
	$(foreach clean_file,$(vulkan_shader_sources),\
		$(RM) $(clean_file).spv\
		$(RM) $(clean_file).spv.h\
	)

#$(target_file): | $(target_dir)
$(target_file): $(custom_target) | $(target_dir) $(copy_files)
	$(LNK) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(target_dir) $(obj_dirs):
	$(MKDIR) $@

vulkan_shader_headers =
# compile Vulkan shaders
uname_p := $(shell uname -p)
  ifeq ($(uname_p),aarch64)
    VULKAN_COMP = $(amf_root)/../Thirdparty/VulkanSDK/1.2.131.2/arm64/bin/glslangValidator
    FILE_TO_HEADER = $(amf_root)/../Thirdparty/file_to_header/arm64/file_to_header
  else
    VULKAN_COMP = $(amf_root)/../Thirdparty/VulkanSDK/1.2.189.2/x86_64/bin/glslangValidator
    FILE_TO_HEADER = $(amf_root)/../Thirdparty/file_to_header/Linux64/file_to_header
endif


define shader_compile_rule_fn
  $1.spv: $1
		$(VULKAN_COMP) -V "$$<" -o "$$@"

  $1.spv.h: $1.spv
		$(FILE_TO_HEADER) "$$<" "$$(basename $$(basename $$(^F)))"

  vulkan_shader_headers += $1.spv.h
endef

$(foreach shader_file,$(vulkan_shader_sources),\
	$(eval $(call shader_compile_rule_fn,$(shader_file)))\
)
custom_target += $(vulkan_shader_headers)

# Define a macro that creates rules for each source file, then call
# it for each one
define compile_source_rule_fn

$$(target_file): $$(build_dir)/$(1:.cpp=.o)

$$(build_dir)/$(1:.cpp=.o): $$(amf_root)/$1 | $$(obj_dirs)
	$$(CXX) $$(CPPFLAGS) $$(DEPFLAGS) $$(CXXFLAGS) -c $$< -o $$@

$$(build_dir)/$(1:.cpp=.o): $$(build_dir)/$(1:.cpp=.o.d)

$$(build_dir)/$(1:.cpp=.o.d):

-include $$(build_dir)/$(1:.cpp=.o.d)
endef

$(foreach src_file,$(src_files),\
    $(eval $(call compile_source_rule_fn,$(src_file)))\
)

define compile_c_rule_fn
source_basename := $$(basename $1)
$$(target_file): $$(build_dir)/$$(source_basename).o

$$(build_dir)/$$(source_basename).o: $$(amf_root)/$1 | $$(obj_dirs)
	$$(MKDIR) $$(dir $$@)
	$$(CC) -c $$< -o $$@
endef

$(foreach src_file,$(src_c_files),\
    $(eval $(call compile_c_rule_fn,$(src_file)))\
)

$(info target_file = $(target_file))
