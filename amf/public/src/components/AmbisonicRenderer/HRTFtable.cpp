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

#include <stdio.h>
#include "public/include/core/Platform.h"
#include "wav.h"
#include "HRTFtable.h"

typedef struct _IRdescr {
    float elevation;
    float azimuth;
    bool swap;
    const amf_uint8 *pIR;
} IRdescr;

#include "./measuredHRTF/headers44.1/H0e000a.wav44.h"
#include "./measuredHRTF/headers44.1/H0e020a.wav44.h"
#include "./measuredHRTF/headers44.1/H0e160a.wav44.h"
#include "./measuredHRTF/headers44.1/H-10e090a.wav44.h"
#include "./measuredHRTF/headers44.1/H20e090a.wav44.h"
#include "./measuredHRTF/headers44.1/H-30e048a.wav44.h"
#include "./measuredHRTF/headers44.1/H-30e138a.wav44.h"
#include "./measuredHRTF/headers44.1/H40e045a.wav44.h"
#include "./measuredHRTF/headers44.1/H40e135a.wav44.h"
#include "./measuredHRTF/headers44.1/H70e000a.wav44.h"
#include "./measuredHRTF/headers44.1/H70e180a.wav44.h"

#include "./measuredHRTF/headers48/H0e000a.wav48.h"
#include "./measuredHRTF/headers48/H0e020a.wav48.h"
#include "./measuredHRTF/headers48/H0e160a.wav48.h"
#include "./measuredHRTF/headers48/H-10e090a.wav48.h"
#include "./measuredHRTF/headers48/H20e090a.wav48.h"
#include "./measuredHRTF/headers48/H-30e048a.wav48.h"
#include "./measuredHRTF/headers48/H-30e138a.wav48.h"
#include "./measuredHRTF/headers48/H40e045a.wav48.h"
#include "./measuredHRTF/headers48/H40e135a.wav48.h"
#include "./measuredHRTF/headers48/H70e000a.wav48.h"
#include "./measuredHRTF/headers48/H70e180a.wav48.h"


static const IRdescr IRtable48[IRTABLEN] = {
    //{ 0.0, 0.0, false, HRTF0e000aS48 },
    { 0.0, 20.0, false, HRTF0e020aS48 },
    { 0.0, 160.0, false, HRTF0e160aS48 },
    { -10.0, 90.0, false, HRTFm10e090aS48 },
    { 20.0, 90.0, false, HRTF20e090aS48 },
    { -30.0, 48.0, false, HRTFm30e048aS48 },
    { -30.0, 138.0, false, HRTFm30e138aS48 },
    { 40.0, 45.0, false, HRTF40e045aS48 },
    { 40.0, 135.0, false, HRTF40e135aS48 },
    { 70.0, 0.0, false, HRTF70e000aS48 },
    { 70.0, 180.0, false, HRTF70e180aS48 },
    { -70.0, 0.0, false, HRTF70e000aS48 },
    { -70.0, 180.0, false, HRTF70e180aS48 },
    { 0.0, -20.0, true, HRTF0e020aS48 },
    { 0.0, -160.0, true, HRTF0e160aS48 },
    { -10.0, -90.0, true, HRTFm10e090aS48 },
    { 20.0, -90.0, true, HRTF20e090aS48 },
    { -30.0, -48.0, true, HRTFm30e048aS48 },
    { -30.0, -138.0, true, HRTFm30e138aS48 },
    { 40.0, -45.0, true, HRTF40e045aS48 },
    { 40.0, -135.0, true, HRTF40e135aS48 },

};

static const IRdescr IRtable441[IRTABLEN] = {
    //{ 0.0, 0.0, false, HRTF0e000aS44 },
    { 0.0, 20.0, false, HRTF0e020aS44 },
    { 0.0, 160.0, false, HRTF0e160aS44 },
    { -10.0, 90.0, false, HRTFm10e090aS44 },
    { 20.0, 90.0, false, HRTF20e090aS44 },
    { -30.0, 48.0, false, HRTFm30e048aS44 },
    { -30.0, 138.0, false, HRTFm30e138aS44 },
    { 40.0, 45.0, false, HRTF40e045aS44 },
    { 40.0, 135.0, false, HRTF40e135aS44 },
    { 70.0, 0.0, false, HRTF70e000aS44 },
    { 70.0, 180.0, false, HRTF70e180aS44 },
    { -70.0, 0.0, false, HRTF70e000aS44 },
    { -70.0, 180.0, false, HRTF70e180aS44 },
    { 0.0, -20.0, true, HRTF0e020aS44 },
    { 0.0, -160.0, true, HRTF0e160aS44 },
    { -10.0, -90.0, true, HRTFm10e090aS44 },
    { 20.0, -90.0, true, HRTF20e090aS44 },
    { -30.0, -48.0, true, HRTFm30e048aS44 },
    { -30.0, -138.0, true, HRTFm30e138aS44 },
    { 40.0, -45.0, true, HRTF40e045aS44 },
    { 40.0, -135.0, true, HRTF40e135aS44 },
};




bool getIR(float srate, int idx, float *elevation, float *azimuth, int IRlength, float *leftIR, float *rightIR){
    const IRdescr *selectedDesc;
    int tabLen = 0;
    if (srate/1000.0 == 48){
        selectedDesc = IRtable48;
        tabLen = sizeof(IRtable48) / sizeof(IRdescr);
    }
    else    
    if (srate/1000.0 == 44.1){
        selectedDesc = IRtable441;
        tabLen = sizeof(IRtable441) / sizeof(IRdescr);
    }
    else {
        return false;
    }

    if (idx >= tabLen){
        return false;
    }
    
    const amf_uint8 *selectedIR = selectedDesc[idx].pIR;
    *elevation = selectedDesc[idx].elevation;
    *azimuth = selectedDesc[idx].azimuth;

    bool swapChans = selectedDesc[idx].swap;



// check data format:
    RiffWave fhd;
    unsigned long length;

    memset(&fhd, 0, sizeof(fhd));

    const amf_uint8 *pdata = selectedIR;
    memcpy(&(fhd.riff), pdata, 8);
    pdata += 8;

    if (memcmp(fhd.riff.name, "RIFF", 4) != 0){
        return(false);
    }

    size_t count = 0;
    do {
        memcpy(&(fhd.wave.name), pdata, 4);
        pdata += 4;
        if (memcmp(fhd.wave.name, "WAVE", 4) == 0){
            break;
        }

        memcpy(&(length), pdata, 4);
        pdata += length;

    } while (pdata - selectedIR < 64);

    do {
        memcpy(&(fhd.wave.fmt.name), pdata, 4);
        pdata += 4;

        if (memcmp(fhd.wave.fmt.name, "fmt ", 4) == 0){
            break;
        }
        memcpy(&(length), pdata, 4);
        pdata += length;

    } while (pdata - selectedIR < 64);

    memcpy(&(fhd.wave.fmt.length), pdata, 4);
    pdata += 4;

    memcpy(&(fhd.wave.fmt.info), pdata, sizeof(fhd.wave.fmt.info));
    pdata += sizeof(fhd.wave.fmt.info);
    pdata += fhd.wave.fmt.length - 16;

    memcpy(&(fhd.wave.data), pdata, 8);
    pdata += 8;

    while (memcmp(fhd.wave.data.name, "data", 4) != 0){
        pdata += fhd.wave.data.length;
        memcpy(&(fhd.wave.data), pdata, 8);
        pdata += 8;
    }

    /* get the data size */
    int nChannels = fhd.wave.fmt.info.nChannels;
    int samplesPerSec = fhd.wave.fmt.info.nSamplesPerSec;
    int bitsPerSam = fhd.wave.fmt.info.nBitsPerSample;
    int nSamples = (fhd.wave.data.length * 8) / (bitsPerSam*nChannels);

    if (!(nChannels == 2)) {
        return false;
    }


    if (nSamples > IRlength){
        nSamples = IRlength;
    }

    short *sSampleBuf = (short *)pdata;
    float *fSampleBuf = (float *)pdata;
    unsigned char *sampleBuf = (unsigned char *)pdata;

    float *pfSamples[2] = { leftIR, rightIR};

    if (swapChans){
        pfSamples[0] = rightIR;
        pfSamples[1] = leftIR;
    }

    switch (bitsPerSam){
    case 8:
        for (int i = 0; i < nSamples; i++){
            int k;
            k = i*nChannels;
            for (int n = 0; n < nChannels; n++)
            {
                (pfSamples)[n][i] = (float)(sampleBuf[k + n] - 127) / 256.0f;
            }
        }
        break;
    case 16:
        for (int i = 0; i < nSamples; i++){
            int k;
            k = i*nChannels;
            for (int n = 0; n < nChannels; n++)
            {
                (pfSamples)[n][i] = (float)(sSampleBuf[k + n]) / 32768.0f;
            }
        }
        break;
    case 32:
        for (int i = 0; i < nSamples; i++){
            int k;
            k = i*nChannels;
            for (int n = 0; n < nChannels; n++)
            {
                (pfSamples)[n][i] = fSampleBuf[k + n];
            }
        }
        break;
    }

    return true;
}
