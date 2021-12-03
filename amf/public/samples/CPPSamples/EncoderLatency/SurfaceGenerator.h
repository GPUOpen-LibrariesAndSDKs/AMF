#pragma once
#ifdef _WIN32
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#endif

#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"
#include "../common/RawStreamReader.h"


#define AMF_FACILITY L"EncoderLatency"

void       PrepareFillFromHost(amf::AMFContext* context);
AMF_RESULT FillSurface(amf::AMFContextPtr context, amf::AMFSurface** surfaceIn, bool bWait);
AMF_RESULT ReadSurface(PipelineElementPtr pipelineElPtr, amf::AMFSurface** surface, amf::AMF_MEMORY_TYPE memoryType);
void FillSurfaceDX9(amf::AMFContext* context, amf::AMFSurface* surface);
void FillSurfaceDX11(amf::AMFContext* context, amf::AMFSurface* surface);
void FillSurfaceVulkan(amf::AMFContext* context, amf::AMFSurface* surface);

