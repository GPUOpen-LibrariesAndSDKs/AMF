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
#include "Pipeline.h"
#include "public/common/Thread.h"
#include "CmdLogger.h"
#include <sstream>

#pragma warning(disable:4355)

typedef amf::AMFQueue<amf::AMFDataPtr>   DataQueue;

class PipelineConnector;
class InputSlot;
class OutputSlot;
//-------------------------------------------------------------------------------------------------
class Slot : public amf::AMFThread
{
public:
    ConnectionThreading     m_eThreading;
    PipelineConnector      *m_pConnector;
    amf_int32               m_iThisSlot;
    amf::AMFPreciseWaiter   m_waiter;
    bool                    m_bEof;
    bool                    m_bFrozen;
    

    Slot(ConnectionThreading eThreading, PipelineConnector *connector, amf_int32 thisSlot);
    virtual ~Slot(){}

    virtual void Stop();
    virtual bool StopRequested();
    virtual bool IsEof(){return m_bEof;}
    virtual void OnEof();
    virtual void Restart(){m_bEof = false;}

    virtual AMF_RESULT Freeze() { m_bFrozen = true; return AMF_OK;}
    virtual AMF_RESULT UnFreeze(){ m_bFrozen = false; return AMF_OK;}
    virtual AMF_RESULT Flush() = 0;

};
//-------------------------------------------------------------------------------------------------
class InputSlot : public Slot
{
public:
    OutputSlot        *m_pUpstreamOutputSlot;

    InputSlot(ConnectionThreading eThreading, PipelineConnector *connector, amf_int32 thisSlot);
    virtual ~InputSlot(){}

    // pull 
    virtual void Run();
    AMF_RESULT Drain();
    AMF_RESULT SubmitInput(amf::AMFData* pData, amf_ulong ulTimeout, bool poll);
    virtual AMF_RESULT Flush() {return AMF_OK;}
};
typedef std::shared_ptr<InputSlot> InputSlotPtr;
//-------------------------------------------------------------------------------------------------
class OutputSlot : public Slot
{
public:
    DataQueue               m_dataQueue;
    InputSlot               *m_pDownstreamInputSlot;

    OutputSlot(ConnectionThreading eThreading, PipelineConnector *connector, amf_int32 thisSlot, amf_int32 queueSize);
    virtual ~OutputSlot(){}

    virtual void Run();
    AMF_RESULT QueryOutput(amf::AMFData** ppData, amf_ulong ulTimeout);
    AMF_RESULT Poll();
    virtual void Restart();
    virtual AMF_RESULT Flush();
};
typedef std::shared_ptr<OutputSlot> OutputSlotPtr;
//-------------------------------------------------------------------------------------------------
class PipelineConnector
{
    friend class Pipeline;
    friend class InputSlot;
    friend class OutputSlot;
protected:

public:
    PipelineConnector(Pipeline *host, PipelineElementPtr element);
    virtual ~PipelineConnector();

    void Start();
    void Stop();
    bool StopRequested() {return m_bStop;}

   
    bool IsEof();
    void OnEof();
    void Restart();

    // a-sync operations from threads
    AMF_RESULT Poll(amf_int32 slot);
    AMF_RESULT PollAll();

    void AddInputSlot(InputSlotPtr pSlot);
    void AddOutputSlot(OutputSlotPtr pSlot);

    amf_int64 GetSubmitFramesProcessed(){return m_iSubmitFramesProcessed;}
    amf_int64 GetPollFramesProcessed(){return m_iPollFramesProcessed;}

    AMF_RESULT Freeze();
    AMF_RESULT UnFreeze();
    AMF_RESULT Flush();

protected:
    Pipeline*               m_pPipeline;
    PipelineElementPtr      m_pElement;
    bool                    m_bStop;
    amf_int64               m_iSubmitFramesProcessed;
    amf_int64               m_iPollFramesProcessed;

    std::vector<InputSlotPtr>               m_InputSlots;
    std::vector<OutputSlotPtr>              m_OutputSlots;
};
//-------------------------------------------------------------------------------------------------
// class Pipeline
//-------------------------------------------------------------------------------------------------
Pipeline::Pipeline() : 
    m_state(PipelineStateNotReady),
    m_startTime(0),
    m_stopTime(0)
{
}
//-------------------------------------------------------------------------------------------------
Pipeline::~Pipeline()
{
    Stop();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Pipeline::Connect(PipelineElementPtr pElement, amf_int32 queueSize, ConnectionThreading eThreading)
{
    amf::AMFLock lock(&m_cs);
    PipelineConnectorPtr upstreamConnector;
    if(!m_connectors.empty())
    {
        upstreamConnector = *m_connectors.rbegin();
    }
    return Connect(pElement, 0, upstreamConnector == NULL ? NULL : upstreamConnector->m_pElement, 0, queueSize, eThreading);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Pipeline::Connect(PipelineElementPtr pElement, amf_int32 slot, PipelineElementPtr upstreamElement, amf_int32 upstreamSlot, amf_int32 queueSize, ConnectionThreading eThreading)
{
    amf::AMFLock lock(&m_cs);

    PipelineConnectorPtr upstreamConnector;
    PipelineConnectorPtr connector;
    for(ConnectorList::iterator it = m_connectors.begin(); it != m_connectors.end(); it++)
    {
        if(it->get()->m_pElement.get() == upstreamElement.get())
        {
            upstreamConnector = *it;
        }
        if(it->get()->m_pElement.get() == pElement.get())
        {
            connector = *it;
        }
    }

    if(connector == NULL)
    { 
        connector = PipelineConnectorPtr(new PipelineConnector(this, pElement));
    }
    if(upstreamConnector != NULL)
    {
        OutputSlotPtr pOutoutSlot = OutputSlotPtr(new OutputSlot(eThreading, upstreamConnector.get() , upstreamSlot, queueSize));
        InputSlotPtr pInputSlot = InputSlotPtr(new InputSlot(eThreading, connector.get(), slot));
        pOutoutSlot->m_pDownstreamInputSlot = pInputSlot.get();
        pInputSlot->m_pUpstreamOutputSlot = pOutoutSlot.get();
        upstreamConnector->AddOutputSlot(pOutoutSlot);
        connector->AddInputSlot(pInputSlot);
    }

    m_connectors.push_back(connector);

    m_state = PipelineStateReady;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Pipeline::Start()
{
    amf::AMFLock lock(&m_cs);
    if(m_state != PipelineStateReady)
    {
        return AMF_WRONG_STATE;
    }
    m_startTime = amf_high_precision_clock();

    for(ConnectorList::iterator it = m_connectors.begin(); it != m_connectors.end() ; it++)
    {
        (*it)->Start();
    }
    m_state = PipelineStateRunning;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Pipeline::Stop()
{
    for(ConnectorList::iterator it = m_connectors.begin(); it != m_connectors.end(); it++)
    {
        (*it)->Stop();
    }
    amf::AMFLock lock(&m_cs);
    m_connectors.clear();
    m_state = PipelineStateNotReady;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void Pipeline::OnEof()
{
    amf::AMFLock lock(&m_cs);
    for(ConnectorList::iterator it = m_connectors.begin(); it != m_connectors.end(); it++)
    {
        if(!(*it)->IsEof())
        {
            return;
        }
    }

    m_state = PipelineStateEof;
    m_stopTime = amf_high_precision_clock();
}
//-------------------------------------------------------------------------------------------------
PipelineState Pipeline::GetState() const
{
    amf::AMFLock lock(&m_cs);
    // test code 
    for(ConnectorList::const_iterator it = m_connectors.begin(); it != m_connectors.end(); it++)
    {
        if(!(*it)->IsEof())
        {
            int a = 1;
        }
    }

    return m_state;
}
//-------------------------------------------------------------------------------------------------
void Pipeline::DisplayResult()
{
    amf::AMFLock lock(&m_cs);
    if(m_connectors.size())
    {


        PipelineConnectorPtr last = *m_connectors.rbegin();

        amf_int64 frameCount = last->GetSubmitFramesProcessed();
        amf_int64 startTime = m_startTime;
        amf_int64 stopTime = m_stopTime;

        std::wstringstream messageStream;
        messageStream.precision(1);
        messageStream.setf(std::ios::fixed, std::ios::floatfield);


        // trace individual connectors
        for(ConnectorList::iterator it = m_connectors.begin(); it != m_connectors.end() ; it++)
        {
            std::wstring text = (*it)->m_pElement->GetDisplayResult();
            LOG_SUCCESS(text);
        }


        messageStream << L" Frames processed: " << frameCount;

        double encodeTime = double(stopTime - startTime) / 10000. / frameCount;
        messageStream << L" Frame process time: " << encodeTime << L"ms" ;
    

        messageStream <<L" FPS: " << double(frameCount) / ( double(stopTime - startTime) / double(AMF_SECOND) );

        LOG_SUCCESS(messageStream.str());
    }
}
//-------------------------------------------------------------------------------------------------
double Pipeline::GetFPS()
{
    amf::AMFLock lock(&m_cs);
    if(m_connectors.size())
    {
        PipelineConnectorPtr first = *m_connectors.begin();
        amf_int64 frameCount = first->GetPollFramesProcessed();
        amf_int64 startTime = m_startTime;
        amf_int64 stopTime = m_stopTime;
        return double(frameCount) / ( double(stopTime - startTime) / double(AMF_SECOND) );
    }
    return 0;
}
//-------------------------------------------------------------------------------------------------
double Pipeline::GetProcessingTime()
{
    return double(m_stopTime - m_startTime) / 10000.;
}
//-------------------------------------------------------------------------------------------------
amf_int64 Pipeline::GetNumberOfProcessedFrames()
{
    if(m_connectors.size() == 0)
    {
        return 0;
    }
    PipelineConnectorPtr last = *m_connectors.rbegin();
    return last->GetSubmitFramesProcessed();
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT Pipeline::Restart()
{
    amf::AMFLock lock(&m_cs);
    for(ConnectorList::iterator it = m_connectors.begin(); it != m_connectors.end(); it++)
    {
        (*it)->Restart();
    }
    m_startTime = amf_high_precision_clock();
    m_state = PipelineStateRunning;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Pipeline::Freeze()
{
    amf::AMFLock lock(&m_cs);
    for(ConnectorList::iterator it = m_connectors.begin(); it != m_connectors.end(); it++)
    {
        (*it)->Freeze();
    }
    m_state = PipelineStateFrozen;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Pipeline::UnFreeze()
{
    amf::AMFLock lock(&m_cs);
    for(ConnectorList::iterator it = m_connectors.begin(); it != m_connectors.end(); it++)
    {
        (*it)->UnFreeze();
    }
    m_state = PipelineStateRunning;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Pipeline::Flush()
{
    amf::AMFLock lock(&m_cs);
    for(ConnectorList::iterator it = m_connectors.begin(); it != m_connectors.end(); it++)
    {
        (*it)->Flush();
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
// class Slot
//-------------------------------------------------------------------------------------------------
Slot::Slot(ConnectionThreading eThreading, PipelineConnector *connector, amf_int32 thisSlot) :
    m_eThreading(eThreading),
    m_pConnector(connector),
    m_iThisSlot(thisSlot),
    m_bEof(false),
    m_bFrozen(false)
{
}
//-------------------------------------------------------------------------------------------------
void Slot::Stop()
{
    RequestStop();
    WaitForStop();
}
//-------------------------------------------------------------------------------------------------
void Slot::OnEof()
{
    m_bEof = true;
    m_pConnector->OnEof();
}
//-------------------------------------------------------------------------------------------------
bool Slot::StopRequested()
{
    return amf::AMFThread::IsRunning() ? amf::AMFThread::StopRequested() : m_pConnector->StopRequested();
}
//-------------------------------------------------------------------------------------------------
// class InputSlot
//-------------------------------------------------------------------------------------------------
InputSlot::InputSlot(ConnectionThreading eThreading, PipelineConnector *connector, amf_int32 thisSlot) :
        Slot(eThreading, connector, thisSlot),
        m_pUpstreamOutputSlot(NULL)
{
}
//-------------------------------------------------------------------------------------------------
// pull 
void InputSlot::Run()
{
    AMF_RESULT res = AMF_OK;
    while(!StopRequested())
    {
        if(m_bFrozen)
        {
            m_waiter.Wait(1);
            continue;
        }
        if(!IsEof()) // after EOF thread waits for stop
        {
            amf::AMFDataPtr data;

            res = m_pUpstreamOutputSlot->QueryOutput(&data, 50);
            if(((res == AMF_OK && data != NULL)  || res == AMF_EOF) && !m_bFrozen)
            {
                res = SubmitInput(data, 50, false);
            }
            else
            {
                m_waiter.Wait(1);
            }
        }
        else
        {
            m_waiter.Wait(1);
        }

    }
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT InputSlot::Drain()
{
    AMF_RESULT res = AMF_OK;
    OnEof();
    while(!StopRequested())
    {
        res = m_pConnector->m_pElement->Drain(m_iThisSlot);
        if(res != AMF_INPUT_FULL)
        {

            break;
        }
        // LOG_INFO(L"m_pElement->Drain() returned AMF_INPUT_FULL");
        if(this->m_eThreading != CT_Direct)
        {
            m_waiter.Wait(1);
        }
        else
        {
            res = m_pConnector->PollAll();
        }
    }
    return AMF_EOF;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT InputSlot::SubmitInput(amf::AMFData* pData, amf_ulong ulTimeout, bool poll)
{
    if(m_bFrozen)
    {
        return AMF_OK;
    }
    AMF_RESULT  res = AMF_OK;
    if(pData == NULL)
    {
        res = Drain();
    }
    else
    {
        //push input
        while(!StopRequested())
        {
            res = m_pConnector->m_pElement->SubmitInput(pData, m_iThisSlot);
            if(m_bFrozen)
            {
                break;
            }
            if(res == AMF_EOF)
            {
//                OnEof();
            }
            if(res == AMF_NEED_MORE_INPUT)
            {
                // do nothing
				break;
            }
            else if(res == AMF_INPUT_FULL  || res == AMF_DECODER_NO_FREE_SURFACES)
            {
                if(poll)
                {
                    res = m_pConnector->PollAll(); // no poll thread: poll right here
                    if(m_bFrozen)
                    {
                        break;
                    }
                    if(res != AMF_OK && res != AMF_REPEAT && res != AMF_RESOLUTION_UPDATED)
                    {
                        break;
                    }
                }
                else
                {
                    m_waiter.Wait(1); // wait till Poll thread clears input
                }
            }
            else if(res == AMF_REPEAT)
            {
                pData = NULL; // need to submit one more time
            }
            else
            {
                if(res == AMF_OK || res == AMF_RESOLUTION_UPDATED)
                {
                    if(pData != NULL)
                    {
                        m_pConnector->m_iSubmitFramesProcessed++;
                    }
                }
                else if(res != AMF_EOF)
                {
                    LOG_ERROR(L"SubmitInput() returned error: " << g_AMFFactory.GetTrace()->GetResultText(res));
                }

                break;
            }
        }
    }
    if(res != AMF_OK  && res != AMF_EOF)
    {
        return res;
    }
    if(m_bFrozen)
    {
        return AMF_OK;
    }

    // poll output
    AMF_RESULT resPoll = m_pConnector->PollAll();

    return res;
}
//-------------------------------------------------------------------------------------------------
// class OutputSlot
//-------------------------------------------------------------------------------------------------
OutputSlot::OutputSlot(ConnectionThreading eThreading, PipelineConnector *connector, amf_int32 thisSlot, amf_int32 queueSize) :
    Slot(eThreading, connector, thisSlot),
    m_pDownstreamInputSlot(NULL)
{
    m_dataQueue.SetQueueSize(queueSize);
}
//-------------------------------------------------------------------------------------------------
void OutputSlot::Run()
{
    AMF_RESULT res  = AMF_OK;
    while(!StopRequested())
    {
        if(m_bFrozen)
        {
            m_waiter.Wait(1);
            continue;
        }

        if(!IsEof()) // after EOF thread waits for stop
        {
            res = Poll();
            if(res != AMF_OK) // 
            {
                m_waiter.Wait(1);
            }
        }
        else
        {
            m_waiter.Wait(1);
        }
    }
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT OutputSlot::QueryOutput(amf::AMFData** ppData, amf_ulong ulTimeout) 
{
    AMF_RESULT res = AMF_OK;
    if(m_bFrozen)
    {
        return AMF_OK;
    }

    if(m_eThreading == CT_ThreadPoll || m_eThreading == CT_Direct)
    {
        res = m_pConnector->m_pElement->QueryOutput(ppData, m_iThisSlot);

        if(res == AMF_EOF) // EOF is sent as NULL data to the next element
        {
            OnEof();
        }
        return res;
    }
    // m_eThreading == CT_ThreadQueue
    amf::AMFDataPtr data;
    amf_ulong id=0;
    if(m_dataQueue.Get(id, data, ulTimeout))
    {
        if(m_bFrozen)
        {
            return AMF_OK;
        }
        *ppData=data.Detach();
        if(*ppData == NULL)
        {
            OnEof();
            return AMF_EOF;
        }
        return AMF_OK;
    }
    return AMF_REPEAT; 
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT OutputSlot::Poll()
{
    AMF_RESULT res = AMF_OK;

    while(!StopRequested())
    {
        if(m_bFrozen)
        {
            break;
        }

        amf::AMFDataPtr data;

        res = m_pConnector->m_pElement->QueryOutput(&data, m_iThisSlot);
        if(m_bFrozen)
        {
            break;
        }

        if(res == AMF_EOF) // EOF is sent as NULL data to the next element
        {
            OnEof();
        }
        if(data != NULL)
        {
            m_pConnector->m_iPollFramesProcessed++; // EOF is not included
        }
        if(data != NULL || res == AMF_EOF) // EOF is sent as NULL data to the next element
        {
            // have data - send it
            if(m_eThreading == CT_ThreadQueue)
            {
                while(!StopRequested())
                {
                    if(m_bFrozen)
                    {
                        break;
                    }
                    amf_ulong id=0;
                    if(m_dataQueue.Add(id, data, 0, 50))
                    {
                        break;
                    }
                }
            }
            else
            {
                res = m_pDownstreamInputSlot->SubmitInput(data, 50, true);
            }
        }
        else if(res == AMF_OK)
        {
            res = AMF_REPEAT;
        }
        if(data == NULL)
        {
            if(IsEof())
            {
                res = AMF_EOF;
            }
            break; // nothing in component - exit
            /*
            }
            else
            {
                m_waiter.Wait(1);
            }
            */
        }
    }
    return res;
}
//-------------------------------------------------------------------------------------------------
void OutputSlot::Restart()
{
    m_dataQueue.Clear();
    Slot::Restart();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT OutputSlot::Flush()
{
    m_dataQueue.Clear();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
// class PipelineConnector
//-------------------------------------------------------------------------------------------------
PipelineConnector::PipelineConnector(Pipeline *host, PipelineElementPtr element)  : 
  m_pPipeline(host),
  m_pElement(element),
  m_bStop(false),
  m_iSubmitFramesProcessed(0),
  m_iPollFramesProcessed(0)
{
}
//-------------------------------------------------------------------------------------------------
PipelineConnector::~PipelineConnector()
{
    Stop();
}
//-------------------------------------------------------------------------------------------------
void PipelineConnector::Start()
{
    m_bStop = false;

    for(amf_size i  =0; i < m_InputSlots.size(); i++)
    {
        InputSlotPtr pSlot = m_InputSlots[i];

        if(pSlot->m_eThreading == CT_ThreadQueue || pSlot->m_eThreading == CT_ThreadPoll)
        {
            pSlot->Start();
        }
    }
    for(amf_size i  =0; i < m_OutputSlots.size(); i++)
    {
        OutputSlotPtr pSlot = m_OutputSlots[i];

        if(pSlot->m_eThreading == CT_ThreadQueue || m_pElement->GetInputSlotCount() == 0 )
        {
            pSlot->Start();
        }
    }

}
//-------------------------------------------------------------------------------------------------
void PipelineConnector::Stop()
{
    m_bStop = true; // must be atomic but will work

    for(amf_size i  =0; i < m_InputSlots.size(); i++)
    {
        m_InputSlots[i]->Stop();
        m_pElement->Drain((amf_int32)i);
    }
    for(amf_size i  =0; i < m_OutputSlots.size(); i++)
    {
        m_OutputSlots[i]->Stop();
    }

}
//-------------------------------------------------------------------------------------------------
void PipelineConnector::OnEof()
{
    m_pPipeline->OnEof();

}
//-------------------------------------------------------------------------------------------------
bool PipelineConnector::IsEof()
{
    for(amf_size i  = 0; i < m_InputSlots.size(); i++)
    {
        if(!m_InputSlots[i]->IsEof())
        {
            return false;
        }
    }
    for(amf_size i  = 0; i < m_OutputSlots.size(); i++)
    {
        if(!m_OutputSlots[i]->IsEof())
        {
            return false;
        }
    }
    return true;
}
//-------------------------------------------------------------------------------------------------
void PipelineConnector::Restart()
{
    for(amf_size i  = 0; i < m_InputSlots.size(); i++)
    {
        m_InputSlots[i]->Restart();
    }
    for(amf_size i  = 0; i < m_OutputSlots.size(); i++)
    {
        m_OutputSlots[i]->Restart();
    }
}
//-------------------------------------------------------------------------------------------------
// a-sync operations from threads
//-------------------------------------------------------------------------------------------------
AMF_RESULT PipelineConnector::Poll(amf_int32 slot)
{
    return m_OutputSlots[slot]->Poll();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT PipelineConnector::PollAll()
{
    bool bEof = false;
    AMF_RESULT res = AMF_OK;
    for(amf_size i = 0; i < m_OutputSlots.size(); i++)
    {
        if(m_OutputSlots[i]->m_eThreading == CT_Direct)
        {
            res = m_OutputSlots[i]->Poll();
        }
        if(res == AMF_EOF )
        {
            bEof =  true;
        }
        else if(res != AMF_OK )
        {
            break;
        }
    }
    return bEof ? AMF_EOF : res;
}
//-------------------------------------------------------------------------------------------------
void PipelineConnector::AddInputSlot(InputSlotPtr pSlot)
{
    m_InputSlots.push_back(pSlot);
}
//-------------------------------------------------------------------------------------------------
void PipelineConnector::AddOutputSlot(OutputSlotPtr pSlot)
{
    m_OutputSlots.push_back(pSlot);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT PipelineConnector::Freeze()
{
    for(amf_size i = 0; i < m_OutputSlots.size(); i++)
    {
        m_OutputSlots[i]->Freeze();
    }
    for(amf_size i = 0; i < m_InputSlots.size(); i++)
    {
        m_InputSlots[i]->Freeze();
    }
    return m_pElement->Freeze();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT PipelineConnector::UnFreeze()
{
    for(amf_size i = 0; i < m_OutputSlots.size(); i++)
    {
        m_OutputSlots[i]->UnFreeze();
    }
    for(amf_size i = 0; i < m_InputSlots.size(); i++)
    {
        m_InputSlots[i]->UnFreeze();
    }
    return m_pElement->UnFreeze();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT PipelineConnector::Flush()
{
    for(amf_size i = 0; i < m_OutputSlots.size(); i++)
    {
        m_OutputSlots[i]->Flush();
    }
    for(amf_size i = 0; i < m_InputSlots.size(); i++)
    {
        m_InputSlots[i]->Flush();
    }
    return m_pElement->Flush();
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
