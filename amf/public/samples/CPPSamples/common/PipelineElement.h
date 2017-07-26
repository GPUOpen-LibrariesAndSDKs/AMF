// 
// Notice Regarding Standards.  AMD does not provide a license or sublicense to
// any Intellectual Property Rights relating to any standards, including but not
// limited to any audio and/or video codec technologies such as MPEG-2, MPEG-4;
// AVC/H.264; HEVC/H.265; AAC decode/FFMPEG; AAC encode/FFMPEG; VC-1; and MP3
// (collectively, the "Media Technologies"). For clarity, you will pay any
// royalties due for such third party technologies, which may include the Media
// Technologies that are owed as a result of AMD providing the Software to you.
// 
// MIT license 
// 
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
#pragma once

#include "public/include/core/Surface.h"
#include "public/include/core/Context.h"
#include "public/include/components/Component.h"
#include "public/common/DataStream.h"
#include "public/common/Thread.h"
#include "CmdLogger.h"
#include <vector>

class Pipeline;
//-------------------------------------------------------------------------------------------------
class PipelineElement
{
public:
    virtual amf_int32 GetInputSlotCount() const = 0;
    virtual amf_int32 GetOutputSlotCount() const = 0;
    virtual AMF_RESULT SubmitInput(amf::AMFData* pData, amf_int32 slot) { return SubmitInput(pData); }
    virtual AMF_RESULT QueryOutput(amf::AMFData** ppData, amf_int32 slot) { return QueryOutput(ppData); }
    virtual AMF_RESULT SubmitInput(amf::AMFData* pData) { return AMF_NOT_SUPPORTED; }
    virtual AMF_RESULT QueryOutput(amf::AMFData** ppData) { return AMF_NOT_SUPPORTED; }

    virtual AMF_RESULT Freeze() {amf::AMFLock lock(&m_cs); m_bFrozen = true; return AMF_OK;}
    virtual AMF_RESULT UnFreeze() {amf::AMFLock lock(&m_cs); m_bFrozen = false; return AMF_OK;}
    virtual AMF_RESULT Flush() { return AMF_OK; }


    virtual AMF_RESULT Drain(amf_int32 inputSlot) { return AMF_NOT_SUPPORTED; }
    virtual AMF_RESULT OnEof() { return AMF_EOF; }
    virtual std::wstring       GetDisplayResult() { return std::wstring(); }

    virtual ~PipelineElement(){}
protected:
    PipelineElement() : m_host(0), m_bFrozen(false){}

    Pipeline* m_host;
    bool    m_bFrozen;
    mutable amf::AMFCriticalSection m_cs;
};
//-------------------------------------------------------------------------------------------------
typedef std::shared_ptr<PipelineElement> PipelineElementPtr;
//-------------------------------------------------------------------------------------------------
class StreamWriter : public PipelineElement
{
public:
    StreamWriter(amf::AMFDataStream *pDataStream)
        :m_pDataStream(pDataStream), m_framesWritten(0),
        m_maxSize(0), m_totalSize(0)
    {
    }

    virtual ~StreamWriter()
    {
//        LOG_DEBUG(L"Stream Writer: written frames:" << m_framesWritten << L"\n");
    }

    virtual amf_int32 GetInputSlotCount() const { return 1; }
    virtual amf_int32 GetOutputSlotCount() const { return 0; }

    virtual AMF_RESULT SubmitInput(amf::AMFData* pData)
    {
        amf::AMFLock lock(&m_cs);
        if(m_bFrozen)
        {
            return AMF_INPUT_FULL;
        }

        AMF_RESULT res = AMF_OK;
        if(pData)
        {
            amf::AMFBufferPtr pBuffer(pData);

            amf_size towrite = pBuffer->GetSize();
            amf_size written = 0;
            m_pDataStream->Write(pBuffer->GetNative(), towrite, &written);
            m_framesWritten++;
            if(m_maxSize < towrite)
            {
                m_maxSize = towrite;
            }
            m_totalSize += towrite;
        }
        else
        {
            res = AMF_EOF;
        }
        return res;
    }
    virtual AMF_RESULT QueryOutput(amf::AMFData** ppData)
    {
        return AMF_NOT_SUPPORTED;
    }
    virtual std::wstring       GetDisplayResult()
    {
        amf::AMFLock lock(&m_cs);
        std::wstring ret;

        if(m_framesWritten > 0)
        {
            std::wstringstream messageStream;
            messageStream << L" Average (Max) Frame size: " << m_totalSize / m_framesWritten << L" bytes (" << m_maxSize << " bytes)";
            ret = messageStream.str();
        }
        return ret;
    }
private:
    amf::AMFDataStreamPtr   m_pDataStream;
    amf_int                 m_framesWritten;
    amf_size                m_maxSize;
    amf_int64               m_totalSize;
};
//-------------------------------------------------------------------------------------------------
typedef std::shared_ptr<StreamWriter> StreamWriterPtr;
//-------------------------------------------------------------------------------------------------
class SurfaceWriter : public PipelineElement
{
public:
    SurfaceWriter(amf::AMFDataStream* pDataStream)
        :m_pDataStream(pDataStream), m_framesWritten(0)
    {
    }

    virtual ~SurfaceWriter()
    {
//        LOG_DEBUG(L"Stream Writer: written frames:" << m_framesWritten << L"\n");
    }

    virtual amf_int32 GetOutputSlotCount() const { return 0; }

    virtual AMF_RESULT SubmitInput(amf::AMFData* pData)
    {
        amf::AMFLock lock(&m_cs);
        if(m_bFrozen)
        {
            return AMF_INPUT_FULL;
        }

        AMF_RESULT res = AMF_OK;
        if(pData)
        {
            amf::AMFSurfacePtr pSurface(pData);
            pSurface->Convert(amf::AMF_MEMORY_HOST);

            for(amf_size i = 0; i < pSurface->GetPlanesCount(); i++)
            {
                amf::AMFPlanePtr pPlane = pSurface->GetPlaneAt(i);
                amf_size towrite = (pPlane->GetOffsetY() + pPlane->GetHeight()) * pPlane->GetHPitch();
                amf_size written = 0;
               m_pDataStream->Write(pPlane->GetNative(), towrite, &written);
            }
            m_framesWritten++;
        }
        else
        {
            res = AMF_EOF;
        }
        return res;
    }
    virtual AMF_RESULT QueryOutput(amf::AMFData** ppData)
    {
        return AMF_NOT_SUPPORTED;
    }

private:
    amf::AMFDataStreamPtr    m_pDataStream;
    amf_int                 m_framesWritten;
};
//-------------------------------------------------------------------------------------------------
typedef std::shared_ptr<SurfaceWriter> SurfaceWriterPtr;
//-------------------------------------------------------------------------------------------------
class DummyWriter : public PipelineElement
{
public:
    DummyWriter ()
    {
    }
    virtual ~DummyWriter()
    {
    }

    virtual amf_int32 GetOutputSlotCount() const { return 0; }

    virtual AMF_RESULT SubmitInput(amf::AMFData* pData)
    {
        amf::AMFLock lock(&m_cs);
        if(m_bFrozen)
        {
            return AMF_INPUT_FULL;
        }

        if(!pData)
        {
            return AMF_EOF;
        }
        return AMF_OK;
    }
    virtual AMF_RESULT QueryOutput(amf::AMFData** ppData)
    {
        return AMF_NOT_SUPPORTED;
    }
};
//-------------------------------------------------------------------------------------------------
typedef std::shared_ptr<DummyWriter> DummyWriterPtr;
//-------------------------------------------------------------------------------------------------
class Splitter : public PipelineElement
{
protected:
    struct Data
    {
        amf::AMFDataPtr     data;
        std::vector<bool>   slots;
    };
public:
    Splitter(bool bCopyData = false, amf_int32 outputCount = 2, amf_size queueSize = 1) :
        m_bCopyData(bCopyData),
        m_iOutputCount(outputCount),
        m_QueueSize(queueSize),
        m_bEof(false)
    {
    }
    virtual ~Splitter()
    {
    }
    virtual amf_int32 GetInputSlotCount() const { return 1; }
    virtual amf_int32 GetOutputSlotCount() const { return m_iOutputCount; }
    virtual AMF_RESULT SubmitInput(amf::AMFData* pData) 
    { 
        amf::AMFLock lock(&m_cs);
        if(m_bFrozen)
        {
            return AMF_INPUT_FULL;
        }

        if(m_Queue.size() >= m_QueueSize)
        {
            return AMF_INPUT_FULL;
        }
        Data data;
        data.data = pData;
        data.slots.resize(m_iOutputCount, false); // set all slots to false
        m_Queue.push_back(data);

        return AMF_OK; 
    }
    virtual AMF_RESULT QueryOutput(amf::AMFData** ppData, amf_int32 slot) 
    {
        amf::AMFLock lock(&m_cs);
        if(m_bFrozen)
        {
            return AMF_OK;
        }

        if(slot >= m_iOutputCount)
        {
            LOG_ERROR(L"Bad slot=" << slot );
            return AMF_INVALID_ARG;
        }
         AMF_RESULT res = m_bEof ? AMF_EOF : AMF_REPEAT;
        for(std::list<Data>::iterator it = m_Queue.begin(); it != m_Queue.end(); it++)
        {
            if(!it->slots[slot])
            {
                it->slots[slot] = true;
                if(it->data == NULL)
                {
                    res = AMF_EOF;
                }
                else
                {
                    res = AMF_OK;
                    if(m_bCopyData)
                    {
                        res = it->data->Duplicate(it->data->GetMemoryType(), ppData);
                    }
                    else
                    {
                        *ppData = it->data;
                        (*ppData)->Acquire();
                    }
                }
                //check if we delivered data to all slots and can erase
                bool bUnserved = false;
                for(amf_int32 i = 0; i < m_iOutputCount; i++)
                {
                    if(!it->slots[i])
                    {
                        bUnserved = true;
                        break;
                    }
                }
                if(!bUnserved)
                {
                    m_Queue.erase(it);
                }
                break;
            }
        }
        return res;     
    }
    virtual AMF_RESULT Drain(amf_int32 inputSlot) 
    { 
        inputSlot;

        amf::AMFLock lock(&m_cs);
        m_bEof = true;
        return AMF_OK; 
    }
    virtual AMF_RESULT Flush() 
    { 
        amf::AMFLock lock(&m_cs);
        m_Queue.clear(); 
        m_bEof = false;
        return AMF_OK; 
    }

protected:
    bool                        m_bCopyData;
    amf_int32                   m_iOutputCount;
    amf_size                    m_QueueSize;
    std::list<Data>             m_Queue;
    bool                        m_bEof;
};
//-------------------------------------------------------------------------------------------------
typedef std::shared_ptr<Splitter> SplitterPtr;
//-------------------------------------------------------------------------------------------------
class AMFComponentElement : public PipelineElement
{
public:
    AMFComponentElement(amf::AMFComponent *pComponent) : m_pComponent(pComponent){}
    virtual ~AMFComponentElement(){}
    virtual amf_int32 GetInputSlotCount() const { return 1; }
    virtual amf_int32 GetOutputSlotCount() const { return 1; }

    virtual AMF_RESULT SubmitInput(amf::AMFData* pData) 
    {
        if(m_bFrozen)
        {
            return AMF_INPUT_FULL;
        }

        AMF_RESULT res = AMF_OK;
        if(pData == NULL) // EOF
        {
            res = m_pComponent->Drain();
        }
        else
        {
            res = m_pComponent->SubmitInput(pData);
            if(res == AMF_DECODER_NO_FREE_SURFACES)
            {
                return AMF_INPUT_FULL;
            }
        }
        return res; 
    }

    virtual AMF_RESULT QueryOutput(amf::AMFData** ppData)
    {
        if(m_bFrozen)
        {
            return AMF_OK;
        }

        AMF_RESULT res = AMF_OK;
        amf::AMFDataPtr data;
        res = m_pComponent->QueryOutput(&data);
        if(res == AMF_REPEAT)
        {
            res = AMF_OK;
        }
        if(res == AMF_EOF && data == NULL)
        {
        }
        if(data != NULL)
        {
            *ppData = data.Detach();
        }
        return res;
    }
    virtual AMF_RESULT Drain(amf_int32 inputSlot) 
    {
        inputSlot;
        return m_pComponent->Drain(); 
    }
    virtual AMF_RESULT Flush()
    {
        return m_pComponent->Flush(); 
    }

protected:
    amf::AMFComponentPtr    m_pComponent;
};
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class AMFComponentExElement : public PipelineElement
{
public:
    AMFComponentExElement(amf::AMFComponentEx *pComponent) : m_pComponent(pComponent)
    {

        amf_int32 slots = 0;
        slots = m_pComponent->GetOutputCount();
        if(slots == 0)
        {
            slots = m_pComponent->GetInputCount();
        }
        m_bEof.resize(slots, false);
    }
    virtual ~AMFComponentExElement(){}
    virtual amf_int32 GetInputSlotCount() const { return m_pComponent->GetInputCount(); }
    virtual amf_int32 GetOutputSlotCount() const { return m_pComponent->GetOutputCount(); }

    virtual AMF_RESULT SubmitInput(amf::AMFData* pData, amf_int32 slot)
    {
        if(m_bFrozen)
        {
            return AMF_INPUT_FULL;
        }

        amf::AMFInputPtr pInput;
        if(m_pComponent->GetInputCount() > 0)
        { 
            m_pComponent->GetInput(slot, &pInput);
            if(pInput == NULL)
            {
                return AMF_INVALID_ARG;
            }
        }
        AMF_RESULT res = AMF_OK;  
        if(pData == NULL) // EOF
        {
            res = m_pComponent->Drain();
            if(m_pComponent->GetOutputCount() == 0)
            {
                m_bEof[slot] = true;
            }
        }
        else
        {
            if(pInput != NULL)
            { 
                res = pInput->SubmitInput(pData);
            }
            else 
            { 
                res = m_pComponent->SubmitInput(pData);
            }
            if(res == AMF_DECODER_NO_FREE_SURFACES)
            {
                return AMF_INPUT_FULL;
            }

        }
        return res; 
    }

    virtual AMF_RESULT QueryOutput(amf::AMFData** ppData, amf_int32 slot) 
    { 
        if(m_bFrozen)
        {
            return AMF_OK;
        }

        amf::AMFOutputPtr pOutput;
        if(m_pComponent->GetOutputCount() > 0)
        { 
            m_pComponent->GetOutput(slot, &pOutput);
            if(pOutput == NULL)
            {
                return AMF_INVALID_ARG;
            }
        }
        AMF_RESULT res = AMF_OK;
        amf::AMFDataPtr data;
        if(pOutput != NULL)
        { 
            res = pOutput->QueryOutput(&data);
        }
        else
        { 
            res = m_pComponent->QueryOutput(&data);
        }

        if(res == AMF_REPEAT)
        {
            res = AMF_OK;
        }
        if(res == AMF_EOF)
        {
            m_bEof[slot] = true;
        }
        if(data != NULL)
        {
            *ppData = data.Detach();
        }
        return res;
    }
    virtual AMF_RESULT Drain(amf_int32 inputSlot) 
    {
        m_bEof[inputSlot] = true;
        if(OnEof() == AMF_EOF)
        { 
            return m_pComponent->Drain(); 
        }
        return AMF_OK;
    }
    virtual AMF_RESULT OnEof() 
    { 
        for(std::vector<bool>::iterator it = m_bEof.begin(); it != m_bEof.end(); it++)
        {
            if(!(*it))
            {
                return AMF_OK;
            }
        }
        return AMF_EOF; 
    }
    virtual AMF_RESULT Flush()
    {
        return m_pComponent->Flush(); 
    }

protected:
    amf::AMFComponentExPtr    m_pComponent;
    std::vector<bool>         m_bEof;
};
//-------------------------------------------------------------------------------------------------
class AVSyncObject
{
    bool m_bVideoStarted = false;
public:
    bool IsVideoStarted() {return m_bVideoStarted;}
    void VideoStarted() {m_bVideoStarted = true;}
    void Reset() {m_bVideoStarted = false;}
};
//-------------------------------------------------------------------------------------------------
