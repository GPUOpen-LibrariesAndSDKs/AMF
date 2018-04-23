#include "StitchLoomSL.h"
using namespace amf;

#include "runtime/include/core/ProgramsCompute.h"
#include "public/common/TraceAdapter.h"
#include "LoomConverter.cl.h"

#define AMF_FACILITY L"LoomConverter"

StitchLoomSL::StitchLoomSL(amf::AMFContext* pContext, AMFSize sizeCamera, AMFSize sizeSrc, AMFSize sizeDst, std::wstring PTGuiProject, std::wstring workFolder) :
m_pContext(pContext),
m_sizeCamera(sizeCamera),
m_sizeSrc(sizeSrc),
m_sizeDst(sizeDst),
m_PTGuiProject(PTGuiProject),
m_workFolder(workFolder),
m_contextLoom(0),
m_readyLoomSL(false),
m_countFrame(0),
m_eof(false),
m_enableDump(false)
{
    m_countStream = m_sizeCamera.width * m_sizeCamera.height;
}

StitchLoomSL::~StitchLoomSL()
{
}

AMF_RESULT StitchLoomSL::Drain()
{
    m_eof = true;
    return AMF_OK;
}
AMF_RESULT StitchLoomSL::Flush()
{
    m_eof = false;
    return AMF_OK;
}

AMF_RESULT StitchLoomSL::SubmitInputInt(amf::AMFData* pData, amf_int32 slot)
{
    AMF_RESULT res = AMF_OK;
    AMFSurfacePtr       pSurfaceIn(pData);
    Init();

    if (pSurfaceIn)
    {
        if (m_inputs[slot])
        {
            return AMF_INPUT_FULL; // this channel already processed for this output - resubmit later
        }

        wchar_t fileName[_MAX_PATH];
        _swprintf(fileName, L"src_%04lld_%02d.bmp", m_countFrame, slot);
        SaveToBmp(pSurfaceIn, fileName);

        m_inputs[slot] = pData;
    }

    return res;
}

AMF_RESULT StitchLoomSL::QueryOutput(amf::AMFData** ppData)
{
    AMF_RESULT res = AMF_OK;
    if (m_eof)
    {
        return AMF_EOF;
    }

    if (m_inputs.size() == 0)   return AMF_REPEAT;

    for (amf_size ch = 0; ch < m_inputs.size(); ch++)
    {
        if (m_inputs[ch]== NULL)
        {
            return AMF_REPEAT; // need more input
        }
    }

    Compose(&m_dstLoomSL);

    if (m_readyLoomSL && m_dstLoomSL)
    {
        if (m_dstLoomSL != NULL)
        {
            (*ppData) = m_dstLoomSL.Detach();
        }
    }


    return res;
}

AMF_RESULT StitchLoomSL::Compose(AMFSurface **ppSurfaceOut)
{
    AMF_RESULT res = AMF_OK;
    AllocateResource();
    InitLoomSL();

    //combine images
    amf_size offset = 0;
    for (amf_int32 ch = 0; ch < (amf_int32)m_inputs.size(); ch++)
    {
        AMFSurfacePtr  pSurf(m_inputs[ch]);
        if (pSurf)
        {
            res = pSurf->Convert(amf::AMF_MEMORY_OPENCL);    //workaround the pitch issue
            res = LoomConvert(m_srcLoomSLBuff, pSurf, amf::AMF_SURFACE_BGRA, offset);
            offset += m_sizeSrc.width * m_sizeSrc.height * 3;
            m_inputs[ch] = NULL;
        }
    }
    wchar_t fileName[_MAX_PATH];
    _swprintf(fileName, L"src_loom_%04lld.bmp", m_countFrame);
    SaveToBmp(m_srcLoomSLBuff, fileName, m_sizeSrcLoom);

    m_countFrame++;

    cl_mem pSrc = (cl_mem)m_srcLoomSLBuff->GetNative();
    vx_status status = ::lsSetCameraBuffer(m_contextLoom, &pSrc);

    cl_mem pDst = (cl_mem)m_dstLoomSLBuff->GetNative();
    status = ::lsSetOutputBuffer(m_contextLoom, &pDst);

    ::lsScheduleFrame(m_contextLoom);
    ::lsWaitForCompletion(m_contextLoom);

    _swprintf(fileName, L"dst_loom_%04lld.bmp", m_countFrame);
    SaveToBmp(m_dstLoomSLBuff, fileName, m_sizeDstLoom);

    res = LoomConvert(m_dstLoomSLBuff, m_dstLoomSL, amf::AMF_SURFACE_RGBA);
    return res;
}

AMF_RESULT StitchLoomSL::Init()
{
    AMF_RESULT res = AMF_OK;
    m_sizeSrcLoom.width = m_sizeSrc.width * m_sizeCamera.height;
    m_sizeSrcLoom.height = m_sizeSrc.height * m_sizeCamera.width;
    AMF_ASSERT(m_sizeDst.width == (m_sizeDst.height * 2), L"equirectangle output aspect ratio must be 2:1!");

    m_sizeDstLoom.height = m_sizeDst.height;
    m_sizeDstLoom.width = 2 * m_sizeDstLoom.height;

    if (!m_Compute)
    {
        m_pContext->GetCompute(amf::AMF_MEMORY_OPENCL, &m_Compute);
        m_inputs.resize(m_countStream);
    }

   return res;
}

AMF_RESULT StitchLoomSL::AllocateResource()
{
    AMF_RESULT res = AMF_OK;

    if (!m_srcLoomSLBuff)
    {
        res = m_pContext->AllocBuffer(amf::AMF_MEMORY_OPENCL, m_sizeSrcLoom.width * m_sizeSrcLoom.height * 3, &m_srcLoomSLBuff);
    }

    if (!m_dstLoomSL)
    {
        res = m_pContext->AllocSurface(amf::AMF_MEMORY_OPENCL, amf::AMF_SURFACE_BGRA, m_sizeDstLoom.width, m_sizeDstLoom.height, &m_dstLoomSL);
    }

    if (!m_dstLoomSLBuff)
    {
        res = m_pContext->AllocBuffer(amf::AMF_MEMORY_OPENCL, m_sizeDstLoom.width * m_sizeDstLoom.height * 3, &m_dstLoomSLBuff);
    }
return res;
}

AMF_RESULT StitchLoomSL::InitLoomSL()
{
    if (m_readyLoomSL) return AMF_OK;

    AMF_RESULT res = AMF_OK;

    if (!m_contextLoom)
    {
        m_contextLoom = ::lsCreateContext();

    }

    m_fmtSrcLoom = VX_DF_IMAGE_RGB;
    m_fmtDstLoom = VX_DF_IMAGE_RGB;
    vx_status status = ::lsSetCameraConfig(m_contextLoom, m_sizeCamera.width, m_sizeCamera.height, m_fmtSrcLoom, m_sizeSrcLoom.width, m_sizeSrcLoom.height);

    if (!status)
    {
        status = ::lsSetOutputConfig(m_contextLoom, m_fmtDstLoom, m_sizeDstLoom.width, m_sizeDstLoom.height);
    }

    if (!status)
    {
        status = ::lsSetOpenCLContext(m_contextLoom, (cl_context)m_Compute->GetNativeContext());
    }

    if (!status)
    {
        char mbstr[_MAX_PATH];
        std::wcstombs(mbstr, m_PTGuiProject.c_str(), _MAX_PATH);
        status = ::lsImportConfiguration(m_contextLoom, "pts", mbstr);
    }

    if (!status)
    {
        status = ::lsInitialize(m_contextLoom);
    }
    res = status ? AMF_FAIL : AMF_OK;

    if (AMF_OK == res)  m_readyLoomSL = true;
    return res;
}

AMF_RESULT StitchLoomSL::SaveToBmp(amf::AMFSurface* pSurfIn, std::wstring fileName)
{
    if (!m_enableDump)    return AMF_OK;

    AMF_RESULT res = AMF_OK;
    amf::AMFDataPtr pDataHost;
    res = pSurfIn->Duplicate(amf::AMF_MEMORY_HOST, &pDataHost);
    amf::AMFSurfacePtr pSurf(pDataHost);

    AMFPlane* plane = pSurf->GetPlaneAt(0);
    amf_int32 width = plane->GetWidth();
    amf_int32 height = plane->GetHeight();
    amf_int32 pitch = plane->GetHPitch();
    amf_uint8* pData = (amf_uint8*)plane->GetNative();
    res = SaveToBmp(pData, fileName, width, height, 4, pitch);

    return AMF_OK;
}

AMF_RESULT StitchLoomSL::SaveToBmp(amf::AMFBuffer* pBuffIn, std::wstring fileName, AMFSize sizeImage)
{
    if (!m_enableDump)    return AMF_OK;

    AMF_RESULT res = AMF_OK;
    amf::AMF_MEMORY_TYPE memType = pBuffIn->GetMemoryType();
    pBuffIn->Convert(amf::AMF_MEMORY_HOST);
    amf_uint8* pData = (amf_uint8*)pBuffIn->GetNative();
    amf_int32 width = sizeImage.width;
    amf_int32 height = sizeImage.height;
    amf_int32 pitch = width * 3;

    res = SaveToBmp(pData, fileName, width, height, 3, pitch);

    pBuffIn->Convert(memType);

    return AMF_OK;
}

AMF_RESULT StitchLoomSL::SaveToBmp(amf_uint8* pData, std::wstring fileName, amf_int32 width, amf_int32 height, amf_int32 channels, amf_int32 pitch)
{
    AMF_RESULT res = AMF_OK;

    if (!pData)  return AMF_FAIL;

    std::wstring fileNameFull(fileName);

    if (m_workFolder.length() > 0)
    {
        fileNameFull = m_workFolder + fileNameFull;
    }

    FILE*  fp = _wfopen(fileNameFull.c_str(), L"wb");

    if (fp)
    {
        amf_uint32 sizeImage = channels * width * height;

        BITMAPFILEHEADER bmHeader = { 0 };
        BITMAPINFOHEADER bmInfo = { 0 };
        bmHeader.bfType = MAKEWORD('B', 'M');
        bmHeader.bfSize = sizeImage + sizeof(bmHeader) + sizeof(bmInfo);
        bmHeader.bfOffBits = sizeof(bmHeader) + sizeof(bmInfo);

        bmInfo.biSize = sizeof(bmInfo);
        bmInfo.biWidth = width;
        bmInfo.biHeight = -height;
        bmInfo.biBitCount = (channels==4) ? 32 : 24;
        bmInfo.biSizeImage = sizeImage;
        bmInfo.biPlanes = 1;

        fwrite(&bmHeader, 1, sizeof(bmHeader), fp);
        fwrite(&bmInfo, 1, sizeof(bmInfo), fp);

        amf_uint32 lenLine = channels * width;
        for (amf_int32 y = 0; y < height; y++, pData += pitch)
        {
            fwrite(pData, 1, lenLine, fp);
        }

        fclose(fp);
    }

    return AMF_OK;
}

#include "public\common\AMFFactory.h"
extern AMFFactoryHelper g_AMFFactory;

AMF_RESULT StitchLoomSL::LoomConvert(amf::AMFBuffer* pBuff, amf::AMFSurface* pSurf, amf::AMF_SURFACE_FORMAT fmtSrc, amf_size offsetBuf)
{
    AMF_RESULT res = AMF_OK;

    if (!m_pKernelLoomBGRAtoRGB)
    {
        AMFPrograms *pPrograms = NULL;
        g_AMFFactory.GetFactory()->GetPrograms(&pPrograms);
        pPrograms->RegisterKernelSource(&m_kernelIDLoomBGRAtoRGB, L"BGRAtoRGB", "BGRAtoRGB", LoomConverterCount, LoomConverter, 0);
        res = m_Compute->GetKernel(m_kernelIDLoomBGRAtoRGB, &m_pKernelLoomBGRAtoRGB);
    }

    if (!m_pKernelLoomRGBtoBGRA)
    {
        AMFPrograms *pPrograms = NULL;
        g_AMFFactory.GetFactory()->GetPrograms(&pPrograms);
        pPrograms->RegisterKernelSource(&m_kernelIDLoomRGBtoBGRA, L"RGBtoBGRA", "RGBtoBGRA", LoomConverterCount, LoomConverter, 0);
        res = m_Compute->GetKernel(m_kernelIDLoomRGBtoBGRA, &m_pKernelLoomRGBtoBGRA);
    }
    amf::AMF_SURFACE_FORMAT fmt = pSurf->GetFormat();
    
    amf::AMFComputeKernelPtr pKernel = (fmtSrc == amf::AMF_SURFACE_BGRA) ? m_pKernelLoomBGRAtoRGB : m_pKernelLoomRGBtoBGRA;
    AMFPlanePtr plane = pSurf->GetPlaneAt(0);
    amf_size index = 0;
    amf_int32 width = plane->GetWidth();
    amf_int32 height = plane->GetHeight();
    amf_int32 pitch = plane->GetHPitch();

    res = pKernel->SetArgPlane(index++, plane, AMF_ARGUMENT_ACCESS_READ);
    res = pKernel->SetArgBuffer(index++, pBuff, AMF_ARGUMENT_ACCESS_WRITE);
    res = pKernel->SetArgInt32(index++, width);
    res = pKernel->SetArgInt32(index++, height);
    res = pKernel->SetArgInt32(index++, width * 3);
    res = pKernel->SetArgInt32(index++, (amf_int32)offsetBuf);

    amf_size offset[3] = { 0, 0, 0};
    amf_size size[3] = {256, 512, 1};
    amf_size localSize[3] = {8, 8, 1};

    size[0] = localSize[0] * ((width + localSize[0] - 1) / localSize[0]);
   size[1] = localSize[1] * ((height + localSize[1] - 1) / localSize[1]);

   res = pKernel->Enqueue(2, offset, size, localSize);
    m_Compute->FlushQueue();

    return res;
}

