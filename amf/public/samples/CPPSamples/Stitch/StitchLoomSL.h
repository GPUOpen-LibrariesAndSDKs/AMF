#pragma once
#include <vector>

#include "public/include/core/Context.h"
#include "public/include/components/Component.h"
#include "public/include/components/VideoConverter.h"
#include "live_stitch_api.h"

class StitchLoomSL
{
public:
    StitchLoomSL(amf::AMFContext* pContext, AMFSize sizeCamera, AMFSize sizeSrc, AMFSize sizeDst, 
        std::wstring PTGuiProject, std::wstring workFolder);
    virtual ~StitchLoomSL();
    AMF_RESULT SubmitInputInt(amf::AMFData* pData, amf_int32 slot);
    AMF_RESULT QueryOutput(amf::AMFData** ppData);
    AMF_RESULT Init();
    AMF_RESULT Drain();
    AMF_RESULT Flush();

protected:
    AMF_RESULT AllocateResource();
    AMF_RESULT InitLoomSL();
    AMF_RESULT Compose(amf::AMFSurface **ppSurfaceOut);
    AMF_RESULT SaveToBmp(amf::AMFSurface* pSurf, std::wstring fileName);
    AMF_RESULT SaveToBmp(amf::AMFBuffer* pBuffIn, std::wstring, AMFSize sizeImage);
    AMF_RESULT LoomConvert(amf::AMFBuffer* pBuffIn, amf::AMFSurface* pSurfOut, amf::AMF_SURFACE_FORMAT fmtSrc, amf_size offset=0);
    AMF_RESULT SaveToBmp(amf_uint8* pData, std::wstring fileName, amf_int32 width, amf_int32 height, amf_int32 channels, amf_int32 pitch);

protected:
    amf::AMFContext*     m_pContext;
    amf::AMFComputePtr   m_Compute;

    std::vector<amf::AMFDataPtr> m_inputs;

    ls_context         m_contextLoom;
    amf::AMFSurfacePtr m_dstLoomSL;
    amf::AMFBufferPtr m_srcLoomSLBuff;
    amf::AMFBufferPtr m_dstLoomSLBuff;

    AMFSize m_sizeSrc;
    AMFSize m_sizeDst;
    AMFSize m_sizeCamera;
    AMFSize m_sizeSrcLoom;
    AMFSize m_sizeDstLoom;
    vx_df_image m_fmtSrcLoom;
    vx_df_image m_fmtDstLoom;
    amf_uint32 m_countStream;
    std::wstring m_PTGuiProject;
    amf_bool  m_readyLoomSL;
    amf_int64 m_countFrame;

    bool                m_eof;

    amf::AMF_KERNEL_ID      m_kernelIDLoomBGRAtoRGB;
    amf::AMF_KERNEL_ID      m_kernelIDLoomRGBtoBGRA;
    amf::AMFComputeKernelPtr m_pKernelLoomBGRAtoRGB;
    amf::AMFComputeKernelPtr m_pKernelLoomRGBtoBGRA;
    bool                m_enableDump;
    std::wstring       m_workFolder;
};
