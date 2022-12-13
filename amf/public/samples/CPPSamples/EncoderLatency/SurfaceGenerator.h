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
void FillRGBASurfaceWithColor(amf::AMFSurface* surface, amf_uint8 R, amf_uint8 G, amf_uint8 B);
void FillNV12SurfaceWithColor(amf::AMFSurface* surface, amf_uint8 Y, amf_uint8 U, amf_uint8 V);
void FillBGRASurfaceWithColor(amf::AMFSurface* surface, amf_uint8 R, amf_uint8 G, amf_uint8 B);
void FillR10G10B10A2SurfaceWithColor(amf::AMFSurface* surface, amf_uint8 R, amf_uint8 G, amf_uint8 B);
void FillRGBA_F16SurfaceWithColor(amf::AMFSurface* surface, amf_uint8 R, amf_uint8 G, amf_uint8 B);
void FillP010SurfaceWithColor(amf::AMFSurface* surface, amf_uint8 Y, amf_uint8 U, amf_uint8 V);