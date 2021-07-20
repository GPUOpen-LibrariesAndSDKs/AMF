#pragma once
#ifdef _WIN32
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#endif
#include <atlbase.h>

#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"

using namespace ATL;

#define AMF_FACILITY L"EncoderLatency"

void       PrepareFillFromHost(amf::AMFContext* context);
AMF_RESULT FillSurface(amf::AMFContextPtr context, amf::AMFSurface** surfaceIn, bool bWait);
