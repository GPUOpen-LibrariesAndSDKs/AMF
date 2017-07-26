//
// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
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
// CPU implementation
#include "public/include/core/Interface.h"
#include "public/include/core/Data.h"
#include <stdio.h>
#include "convolution.h"

convolution::convolution(int nChannels, int responseLength){
    m_nChannels = nChannels;
    m_ResponseLength = responseLength;
    m_sampHistPos = NULL;
    m_SampleHistory = NULL;
}

bool convolution::init(){
    m_sampHistPos = new int[m_nChannels];

    m_SampleHistory = new float*[m_nChannels];
    
    for (int i = 0; i < m_nChannels; i++){
        m_SampleHistory[i] = new float[m_ResponseLength];
        memset(m_SampleHistory[i], 0, m_ResponseLength*sizeof(float));
    }

    return(m_sampHistPos != NULL && m_SampleHistory != NULL);

}

convolution::~convolution(){
    if (m_sampHistPos != NULL){
        delete m_sampHistPos;
    }
    if (m_SampleHistory != NULL){
        for (int i = 0; i < m_nChannels; i++){
            if (m_SampleHistory[i] != NULL)
                delete m_SampleHistory[i];
        }
        delete m_SampleHistory;
    }
}


void convolution::timeDomainCPU(
    float *resp,
    amf_uint32 firstNonZero,
    amf_uint32 lastNonZero,
    float *in,
    float *out,
    int chanIdx,    
    amf_size datalength,
    amf_size convlength)
{
    int bufPos = m_sampHistPos[chanIdx];
    bufPos = bufPos % convlength;
    float *histBuf = m_SampleHistory[chanIdx];

    // circular buffer....
    for (amf_size i = 0; i < datalength; i++){
        histBuf[(bufPos + i) % convlength] = in[i];
    }

    for (amf_size j = 0; j < datalength; j++){
        out[j] = 0.0;
        for (int k = (int)firstNonZero; k < (int)lastNonZero; k++){
            out[j] += histBuf[(bufPos + j - k + convlength) % convlength] * resp[k];
        }
    }
    m_sampHistPos[chanIdx] += (int)datalength;
}
