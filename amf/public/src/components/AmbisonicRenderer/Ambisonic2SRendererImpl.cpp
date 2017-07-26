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

#include "HRTFtable.h"

#include "Ambisonic2SRendererImpl.h"

#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"


extern "C"
{
    AMF_RESULT AMF_CDECL_CALL AMFCreateComponentAmbisonic(amf::AMFContext* pContext, void* reserved, amf::AMFComponent** ppComponent)
    {
        *ppComponent = new amf::AMFInterfaceMultiImpl< amf::AMFAmbisonic2SRendererImpl, amf::AMFComponent, amf::AMFContext* >(pContext);
        (*ppComponent)->Acquire();
        return AMF_OK;
    }
}

#define AMF_FACILITY L"AMFAmbisonic2SRendererImpl"

using namespace amf;

void Ambi2Stereo::loadTabulatedHRTFs(){
 
    float *hrtf = new float[responseLength];

    LeftResponseW = new float[responseLength];
    LeftResponseX = new float[responseLength];
    LeftResponseY = new float[responseLength];
    LeftResponseZ = new float[responseLength];
    RightResponseW = new float[responseLength];
    RightResponseX = new float[responseLength];
    RightResponseY = new float[responseLength];
    RightResponseZ = new float[responseLength];

    for (int k = 0; k < 8; k++) {
        OutData[k] = new float[bufSize];
    }

    theta = new float[IRTABLEN];
    phi = new float[IRTABLEN];

    for (int n = 0; n < IRTABLEN; n++){
        float elevation, azimuth;
        vSpkrNresponse_L[n] = new float[responseLength];
        vSpkrNresponse_R[n] = new float[responseLength];
        memset(&vSpkrNresponse_L[n][0], 0, responseLength*sizeof(float));
        memset(&vSpkrNresponse_R[n][0], 0, responseLength*sizeof(float));
        getIR((float)inSampleRate, n, &elevation, &azimuth, responseLength, vSpkrNresponse_L[n], vSpkrNresponse_R[n]);
        theta[n] = azimuth;
        phi[n] = elevation;
    }
    
    m_convolution = new convolution(IRTABLEN,responseLength);
    m_convolution->init();

}

Ambi2Stereo::Ambi2Stereo(AMF_AMBISONIC2SRENDERER_MODE_ENUM decodemethod, amf_int64 inSampleRate_) :
inSampleRate(inSampleRate_)
{
    m_convolution = NULL;
    method = decodemethod;

    bufSize = 64;
    prevHeadTheta = prevHeadPhi = 0.0;


    for (int n = 0; n < ICO_NVERTICES; n++){
        vSpkrNresponse_L[n] = NULL;
        vSpkrNresponse_R[n] = NULL;
    }

    switch (method){
    case AMF_AMBISONIC2SRENDERER_MODE_SIMPLE:
        responseLength = 1;
        LeftResponseW = new float[responseLength];
        LeftResponseX = new float[responseLength];
        LeftResponseY = new float[responseLength];
        LeftResponseZ = new float[responseLength];
        RightResponseW = new float[responseLength];
        RightResponseX = new float[responseLength];
        RightResponseY = new float[responseLength];
        RightResponseZ = new float[responseLength];

        break;
    case AMF_AMBISONIC2SRENDERER_MODE_HRTF_AMD0:
        break;
    case AMF_AMBISONIC2SRENDERER_MODE_HRTF_MIT1:
        responseLength = ICO_HRTF_LEN;
        bufSize = responseLength / 4;

        loadTabulatedHRTFs();
        break;


    default:
        break;
    };

}

Ambi2Stereo::~Ambi2Stereo()
{
    for (int n = 0; n < ICO_NVERTICES; n++){
        if (vSpkrNresponse_L[n] != NULL){
            delete vSpkrNresponse_L[n];
        }
        vSpkrNresponse_L[n] = NULL;
        if (vSpkrNresponse_R[n] != NULL){
            delete vSpkrNresponse_R[n];
        }
        vSpkrNresponse_R[n] = NULL;
    }
    delete LeftResponseW;
    delete LeftResponseX;
    delete LeftResponseY;
    delete LeftResponseZ;
    delete RightResponseW;
    delete RightResponseX;
    delete RightResponseY;
    delete RightResponseZ;

    if (m_convolution)
    {
        delete m_convolution;
    }
}

//virtual mic 
//2D:
// M(a,p) = p sqr(2)W +(1-p)(cos(a)X + sin(a)Y)
//3D: ???
// M3d(theta,phi,p) = p*sqr(2)*W +( 1 - p)*(cos(theta)cos(phi)*X + sin(theta)cos(phi)*Y + sin(phi)*Z)
// where:
// p = 0 => figure 8
// p = .5 => Cardiod
// p = 1.0 => Omnidirectional

// The coordinate system used in Ambisonics follows the right hand rule convention with positive X pointing forwards,
// positive Y pointing to the left and positive Z pointing upwards. Horizontal angles run anticlockwise 
// from due front and vertical angles are positive above the horizontal, negative below.


void Ambi2Stereo::getResponses(float thetaHead, float phiHead,
    int channel,
    float *Wresponse, float *Xresponse, float *Yresponse, float *Zresponse)
{
    float earAngle = channel == 0 ? (float)(EAR_FWD_AMBI_ANGLE) : (float)(-EAR_FWD_AMBI_ANGLE);

    float Xcoeff = (float)( cos((thetaHead + earAngle)*PI / 180.0) * cos(phiHead*PI / 180.0) );
    float Ycoeff = (float)( sin((thetaHead + earAngle)*PI / 180.0) * cos(phiHead*PI / 180.0) );
    float Zcoeff = (float)( sin(phiHead*PI / 180.0) );


    float p = 0.5; // Cardiod
    //area radius 1 cardiod = (2.0 / 3.0)*PI
    //area radius 1 circle = PI
    //ratio circle / cardiod = 3 / 2
    const float scale = (float)( (3.0 / 2.0) / 20.0 );

    float W0 = (float)( p*sqrt(2.0) );
    float X0 = (1 - p)*Xcoeff;
    float Y0 = (1 - p)*Ycoeff;
    float Z0 = (1 - p)*Zcoeff;

    switch (method){
    case AMF_AMBISONIC2SRENDERER_MODE_SIMPLE:
        Wresponse[0] = W0;
        Xresponse[0] = X0;
        Yresponse[0] = Y0;
        Zresponse[0] = Z0;
        break;
    
    case AMF_AMBISONIC2SRENDERER_MODE_HRTF_AMD0:
    case AMF_AMBISONIC2SRENDERER_MODE_HRTF_MIT1:

        W0 = (float)( p*sqrt(2.0) );

        memset(Wresponse, 0, sizeof(float)*responseLength);
        memset(Xresponse, 0, sizeof(float)*responseLength);
        memset(Yresponse, 0, sizeof(float)*responseLength);
        memset(Zresponse, 0, sizeof(float)*responseLength);

        for (int n = 0; n < 20; n++){

            X0 = (float)((1 - p)*cos((thetaHead - theta[n])*PI / 180.0) * cos((phiHead - phi[n])*PI / 180.0));
            Y0 = (float)((1 - p)*sin((thetaHead - theta[n])*PI / 180.0) * cos((phiHead - phi[n])*PI / 180.0));
            Z0 = (float)((1 - p)*sin((phiHead - phi[n])*PI / 180.0));

            switch (channel){
            case 0:
                for (unsigned int i = 0; i < responseLength; i++){
                    Wresponse[i] += W0*vSpkrNresponse_L[n][i] * scale;
                    Xresponse[i] += X0*vSpkrNresponse_L[n][i] * scale;
                    Yresponse[i] += Y0*vSpkrNresponse_L[n][i] * scale;
                    Zresponse[i] += Z0*vSpkrNresponse_L[n][i] * scale;
                }
                break;
            case 1:
                for (unsigned int i = 0; i < responseLength; i++){
                    Wresponse[i] += W0*vSpkrNresponse_R[n][i] * scale;
                    Xresponse[i] += X0*vSpkrNresponse_R[n][i] * scale;
                    Yresponse[i] += Y0*vSpkrNresponse_R[n][i] * scale;
                    Zresponse[i] += Z0*vSpkrNresponse_R[n][i] * scale;
                }
                break;
            default:
                break;
            }
        }

        break;
    //case AMF_AMBISONIC2SRENDERER_MODE_HRTF_MIT1:

    //    break;
        
    default:
        break;
    }
}

void Ambi2Stereo::process(float newtheta, float newphi, int nSamples, float *W, float *X, float *Y, float *Z, float *left, float *right)
{
    float theta = prevHeadTheta;
    float phi = prevHeadPhi;



    if (fabs(newtheta - theta) > 180.0){
        theta += 360;
    }
    float deltaTheta = (newtheta - theta)*bufSize / nSamples;
    float deltaPhi = (newphi - phi)*bufSize / nSamples;

    //AMFTraceWarning(AMF_FACILITY, L"theta: %f, phi: %f, delta theta %f, phi %f\n", newtheta, newphi, deltaTheta, deltaPhi);

    prevHeadTheta = newtheta;
    prevHeadPhi = newphi;

    memset(left, 0, nSamples*sizeof(float));
    memset(right, 0, nSamples*sizeof(float));

    float *Responses[8];
    Responses[0] = LeftResponseW;
    Responses[1] = LeftResponseX;
    Responses[2] = LeftResponseY;
    Responses[3] = LeftResponseZ;
    Responses[4] = RightResponseW;
    Responses[5] = RightResponseX;
    Responses[6] = RightResponseY;
    Responses[7] = RightResponseZ;

    float *Data[8];// = { W, X, Y, Z, W, X, Y, Z };
    Data[0] = W;
    Data[1] = X;
    Data[2] = Y;
    Data[3] = Z;
    Data[4] = W;
    Data[5] = X;
    Data[6] = Y;
    Data[7] = Z;

    float updTime = 0.0;
    if (responseLength == 1){
        getResponses(newtheta, newphi, 0, LeftResponseW, LeftResponseX, LeftResponseY, LeftResponseZ);
        getResponses(newtheta, newphi, 1, RightResponseW, RightResponseX, RightResponseY, RightResponseZ);
        for (int i = 0; i < nSamples; i++){
            // convolve streams with left and right responses:
            left[i] = W[i] * LeftResponseW[0] + X[i] * LeftResponseX[0] + Y[i] * LeftResponseY[0] + Z[i] * LeftResponseZ[0];
            right[i] = W[i] * RightResponseW[0] + X[i] * RightResponseX[0] + Y[i] * RightResponseY[0] + Z[i] * RightResponseZ[0];
        }
    }
    else {

        for (long i = 0; i < nSamples; i += bufSize){
            amf_size nProcessed;
            getResponses(theta, phi, 0, LeftResponseW, LeftResponseX, LeftResponseY, LeftResponseZ);
            getResponses(theta, phi, 1, RightResponseW, RightResponseX, RightResponseY, RightResponseZ);
            theta += deltaTheta;
            phi += deltaPhi;

            nProcessed = bufSize;
            memset(OutData[0], 0, sizeof(float)*bufSize);
            memset(OutData[1], 0, sizeof(float)*bufSize);
            memset(OutData[2], 0, sizeof(float)*bufSize);
            memset(OutData[3], 0, sizeof(float)*bufSize);
            memset(OutData[4], 0, sizeof(float)*bufSize);
            memset(OutData[5], 0, sizeof(float)*bufSize);
            memset(OutData[6], 0, sizeof(float)*bufSize);
            memset(OutData[7], 0, sizeof(float)*bufSize);

            // convolve streams with left and right responses:
            int nzFL[16];
            for (int k = 0; k < 8; k++){
                for (unsigned int n = 0; n < responseLength; n++){
                    if (Responses[k][n] != 0.0) {
                        nzFL[k * 2] = n;
                        break;
                    }
                }
                for (unsigned int n = responseLength-1; n >= 0; n--){
                    if (Responses[k][n] != 0.0) {
                        nzFL[k * 2+1] = n;
                        break;
                    }
                }
            }

            //pConvolution->ProcessDirect(Responses, Data, OutData, bufSize, &nProcessed, nzFL); 
            for (int k = 0; k < 8; k++){
               m_convolution->timeDomainCPU(Responses[k], nzFL[k * 2], nzFL[k * 2 + 1], Data[k], OutData[k], k, bufSize, responseLength);
            }

            for (int ii = 0; ii < 8; ii++){
                Data[ii] += bufSize;
            }


            //mix down to stereo:
            for (unsigned int j = 0; j < bufSize; j++){
                left[i + j]  = OutData[0][j] + OutData[1][j] + OutData[2][j] + OutData[3][j];
                right[i + j] = OutData[4][j] + OutData[5][j] + OutData[6][j] + OutData[7][j];
             }      
         }
    }

}


const AMFEnumDescriptionEntry AMF_SAMPLE_INPUT_FORMAT_ENUM_DESCRIPTION[] =
{
    { AMFAF_S16, L"S16" },
    { AMFAF_S32, L"S32" },
    { AMFAF_FLT, L"FLT" },
    { AMFAF_S16P, L"S16P" },
    { AMFAF_S32P, L"S32P" },
    { AMFAF_FLTP, L"FLTP" },

    { AMFAF_UNKNOWN, 0 }  // This is end of description mark
};

const AMFEnumDescriptionEntry AMF_SAMPLE_OUTPUT_FORMAT_ENUM_DESCRIPTION[] =
{
    { AMFAF_FLTP, L"FLTP" },
    { AMFAF_UNKNOWN, 0 }  // This is end of description mark
};

//-------------------------------------------------------------------------------------------------
bool IsAudioPlanar(AMF_AUDIO_FORMAT inFormat)
{
    switch (inFormat)
    {
    case AMFAF_U8P:
    case AMFAF_S16P:
    case AMFAF_S32P:
    case AMFAF_FLTP:
    case AMFAF_DBLP:
        return true;
    }

    return false;
}

amf_int32 GetAudioSampleSize(AMF_AUDIO_FORMAT inFormat)
{
    amf_int32 sample_size = 2;
    switch (inFormat)
    {
    case AMFAF_UNKNOWN:
        sample_size = 0;
        break;
    case AMFAF_U8P:
    case AMFAF_U8:
        sample_size = 1;
        break;
    case AMFAF_S16P:
    case AMFAF_S16:
        sample_size = 2;
        break;
    case AMFAF_S32P:
    case AMFAF_FLTP:
    case AMFAF_S32:
    case AMFAF_FLT:
        sample_size = 4;
        break;
    case AMFAF_DBLP:
    case AMFAF_DBL:
        sample_size = 8;
        break;
    }
    return sample_size;
}

const AMFEnumDescriptionEntry AMF_AMBISONIC2SRENDERER_MODE_ENUM_DESCRIPTION[] = {
{AMF_AMBISONIC2SRENDERER_MODE_SIMPLE, L"Simple"},
{ AMF_AMBISONIC2SRENDERER_MODE_HRTF_AMD0, L"HRTF AMD 0" },
{ AMF_AMBISONIC2SRENDERER_MODE_HRTF_MIT1, L"HRTF MIT 1" },

};


//
//
// AMFAmbisonic2SRendererImpl
//
//

//-------------------------------------------------------------------------------------------------
AMFAmbisonic2SRendererImpl::AMFAmbisonic2SRendererImpl(AMFContext* pContext)
  :  m_pContext(pContext)
  ,  m_pInputData(nullptr)
  ,  m_inSampleFormat(AMFAF_UNKNOWN)
  ,  m_outSampleFormat(AMFAF_UNKNOWN)
  ,  m_inChannels(0)
  ,  m_outChannels(0)
  ,  m_wIndex(0)
  ,  m_xIndex(0)
  ,  m_yIndex(0)
  ,  m_zIndex(0)
  ,  m_Theta(0.0f)
  ,  m_Phi(0.0f)
  ,  m_Rho(0.0f)
  ,  m_bEof(false)
  ,  m_bDrained(true)
  ,  m_audioFrameSubmitCount(0)
  ,  m_audioFrameQueryCount(0)
  ,  m_ptsNext(0)
  ,  m_ambi2S(NULL)
  , m_eMode(AMF_AMBISONIC2SRENDERER_MODE_HRTF_MIT1)
  , m_ptsLastTime(-1LL)
{
    g_AMFFactory.Init();
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoInt64(AMF_AMBISONIC2SRENDERER_IN_AUDIO_SAMPLE_RATE,      L"Sample Rate", 41000, 0, 256000, false),
        
        AMFPropertyInfoInt64(AMF_AMBISONIC2SRENDERER_OUT_AUDIO_CHANNELS,        L"# out channels (2)", 2, 2, 2, false),
        AMFPropertyInfoEnum(AMF_AMBISONIC2SRENDERER_OUT_AUDIO_SAMPLE_FORMAT,    L"output Sample Format", AMFAF_FLTP, AMF_SAMPLE_OUTPUT_FORMAT_ENUM_DESCRIPTION, false),
        AMFPropertyInfoInt64(AMF_AMBISONIC2SRENDERER_OUT_AUDIO_CHANNEL_LAYOUT,  L"Channel layout (0 - default)", 3, 0, INT_MAX, false),

        AMFPropertyInfoInt64(AMF_AMBISONIC2SRENDERER_IN_AUDIO_CHANNELS,         L"# in channels (4)", 4, 4, 4, false),
        AMFPropertyInfoEnum(AMF_AMBISONIC2SRENDERER_IN_AUDIO_SAMPLE_FORMAT,     L"input Sample Format", AMFAF_FLTP, AMF_SAMPLE_INPUT_FORMAT_ENUM_DESCRIPTION, false),

        AMFPropertyInfoEnum(AMF_AMBISONIC2SRENDERER_MODE,                       L"Mode", AMF_AMBISONIC2SRENDERER_MODE_HRTF_MIT1, AMF_AMBISONIC2SRENDERER_MODE_ENUM_DESCRIPTION, false),

        AMFPropertyInfoInt64(AMF_AMBISONIC2SRENDERER_W,                         L"w channel", 0, 0, 3, true),
        AMFPropertyInfoInt64(AMF_AMBISONIC2SRENDERER_X,                         L"x channel", 1, 0, 3, true),
        AMFPropertyInfoInt64(AMF_AMBISONIC2SRENDERER_Y,                         L"y channel", 3, 0, 3, true),
        AMFPropertyInfoInt64(AMF_AMBISONIC2SRENDERER_Z,                         L"z channel", 2, 0, 3, true),

        AMFPropertyInfoDouble(AMF_AMBISONIC2SRENDERER_THETA,                    L"Theta/Yaw ", 0.0, -360.0, 360.0, true),
        AMFPropertyInfoDouble(AMF_AMBISONIC2SRENDERER_PHI,                      L"Phi/Pitch", 0.0, -180.0, 180.0, true),
        AMFPropertyInfoDouble(AMF_AMBISONIC2SRENDERER_RHO,                      L"Rho/Roll", 0.0, -180.0, 180.0, true),
    AMFPrimitivePropertyInfoMapEnd
}
//-------------------------------------------------------------------------------------------------
AMFAmbisonic2SRendererImpl::~AMFAmbisonic2SRendererImpl()
{
    Terminate();
    g_AMFFactory.Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAmbisonic2SRendererImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    AMFLock lock(&m_syncProperties);
    AMF_RESULT res = AMF_OK;
    // clean up any information we might previously have
    Terminate();

    m_bEof = false;
    m_bDrained = true;
    m_ptsNext = 0;

    m_wIndex = -1;
    m_xIndex = -1;
    m_yIndex = -1;
    m_zIndex = -1;

    m_Theta = 0.0f;
    m_Phi = 0.0f;
    m_Rho = 0.0f;

    amf_int64  outChannelLayout = 0;
    amf_int64  outSampleFormat = AMFAF_UNKNOWN;

    GetProperty(AMF_AMBISONIC2SRENDERER_IN_AUDIO_CHANNELS, &m_inChannels);
    if (4 != m_inChannels)
    {
        AMFTrace(AMF_TRACE_WARNING, AMF_FACILITY, L"Init: Invalid input channels %d [4]", m_inChannels);
        return AMF_INVALID_ARG;
    }

    amf_int64  inSampleFormat = AMFAF_UNKNOWN;
    GetProperty(AMF_AMBISONIC2SRENDERER_IN_AUDIO_SAMPLE_FORMAT, &inSampleFormat);
    m_inSampleFormat = (AMF_AUDIO_FORMAT)inSampleFormat;

    amf_int64 inSampleRate;
    GetProperty(AMF_AMBISONIC2SRENDERER_IN_AUDIO_SAMPLE_RATE, &inSampleRate);
    


    GetProperty(AMF_AMBISONIC2SRENDERER_W, &m_wIndex);
    amf_int64 mode;
    GetProperty(AMF_AMBISONIC2SRENDERER_MODE, &mode);
    m_eMode = (AMF_AMBISONIC2SRENDERER_MODE_ENUM)mode;
    GetProperty(AMF_AMBISONIC2SRENDERER_X, &m_xIndex);
    GetProperty(AMF_AMBISONIC2SRENDERER_Y, &m_yIndex);
    GetProperty(AMF_AMBISONIC2SRENDERER_Z, &m_zIndex);
    GetProperty(AMF_AMBISONIC2SRENDERER_THETA, &m_Theta);
    GetProperty(AMF_AMBISONIC2SRENDERER_PHI, &m_Phi);
    GetProperty(AMF_AMBISONIC2SRENDERER_RHO, &m_Rho);

    GetProperty(AMF_AMBISONIC2SRENDERER_OUT_AUDIO_CHANNELS, &m_outChannels);
    if (2 != m_outChannels)
    {
        AMFTrace(AMF_TRACE_WARNING, AMF_FACILITY, L"Init: Invalid output channels %d [2]", m_inChannels);
        return AMF_INVALID_ARG;
    }

    amf_int64 format;
    GetProperty(AMF_AMBISONIC2SRENDERER_OUT_AUDIO_SAMPLE_FORMAT, &format);
    m_outSampleFormat = (AMF_AUDIO_FORMAT)format;

    char dllPath[MAX_PATH + 1];
    GetModuleFileNameA(NULL, dllPath, MAX_PATH);
    char *pslash = strrchr(dllPath, '\\');
    if (pslash){
        *pslash = '\0';
    }
    m_ambi2S = new Ambi2Stereo(m_eMode, inSampleRate);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAmbisonic2SRendererImpl::ReInit(amf_int32 width, amf_int32 height)
{
    AMFLock lock(&m_sync);

    Terminate();
    return Init(AMF_SURFACE_UNKNOWN, width, height);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAmbisonic2SRendererImpl::Terminate()
{
    AMFLock lock(&m_sync);

    // clear the internally stored buffer
    m_pInputData = nullptr;
    AMFTraceInfo(AMF_FACILITY, L"Submitted %d, Queried %d", (int)m_audioFrameSubmitCount, (int)m_audioFrameQueryCount);


    m_audioFrameSubmitCount = 0;
    m_audioFrameQueryCount = 0;

    m_bEof = false;
    if(m_ambi2S != NULL)
    {
        delete m_ambi2S;
        m_ambi2S = 0;
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAmbisonic2SRendererImpl::Drain()
{
    AMFLock lock(&m_sync);

    m_bEof = true;
    m_pInputData.Release();

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAmbisonic2SRendererImpl::Flush()
{
    AMFLock lock(&m_sync);

    // clear the internally stored buffer
    m_pInputData = nullptr;
    m_bDrained = true;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAmbisonic2SRendererImpl::SubmitInput(AMFData* pData)
{
    AMFLock lock(&m_sync);

    // if the buffer is already full, we can't set more data
    if (m_pInputData && pData)
    {
        return AMF_INPUT_FULL;
    }
    if(m_bEof)
    {
        return AMF_EOF;
    }

    // if the internal buffer is empty and the in buffer is also empty,
    // we reached the end of the available input...
    if (!m_pInputData && !pData)
    {
        m_bEof = true;
        return AMF_EOF;
    }

    // if the internal buffer is empty and we got new data coming in
    // update internal buffer with new information
    if (!m_pInputData && pData)
    {
        m_pInputData = AMFAudioBufferPtr(pData);
        AMF_RETURN_IF_FALSE(m_pInputData != 0, AMF_INVALID_ARG, L"SubmitInput() - Input should be AudioBuffer");

        AMF_RESULT err = m_pInputData->Convert(AMF_MEMORY_HOST);
        AMF_RETURN_IF_FAILED(err, L"SubmitInput() - Convert(AMF_MEMORY_HOST) failed");

        m_audioFrameSubmitCount++;

        // just pass through
        return AMF_OK;
    }

    return AMF_FAIL;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAmbisonic2SRendererImpl::QueryOutput(AMFData** ppData)
{
    AMFLock lock(&m_sync);

    // check some required parameters
    AMF_RETURN_IF_FALSE(ppData != NULL, AMF_INVALID_ARG, L"GetOutput() - ppData == NULL");
    AMF_RETURN_IF_FALSE(m_inSampleFormat != AMFAF_UNKNOWN && m_outSampleFormat != AMFAF_UNKNOWN, AMF_NOT_INITIALIZED, L"QueryOutput() - Audio Format not Initialized");
    
    // initialize output
    *ppData = NULL;

    // if the input buffer is empty, we need more data or drain
    if (m_pInputData == NULL && m_bEof && m_bDrained)
    {
        return AMF_EOF;
    }
    if(m_pInputData == NULL && !m_bEof)
    {
        return AMF_REPEAT;
    }

    if (m_pInputData == NULL)
    {
        m_bDrained = true;
        return AMF_EOF;
    }
    amf_pts currTime = amf_high_precision_clock();

    //accessing input data
    int       iSampleSizeIn  = GetAudioSampleSize(m_inSampleFormat);
    int       iSampleSizeOut = GetAudioSampleSize(m_outSampleFormat);
    amf_int64 iSamplesIn     = 0;
    uint8_t* pMemIn = NULL;

    if (m_pInputData != NULL)
    {
        AMF_RETURN_IF_FALSE(m_pInputData->GetSize() != 0, AMF_INVALID_ARG, L"QueryOutput() - Invalid Param");
        iSamplesIn     = (amf_int64) m_pInputData->GetSize() / (m_inChannels * iSampleSizeIn);
        pMemIn   = static_cast<uint8_t*>(m_pInputData->GetNative());
    }
    if ((NULL == pMemIn) || (0 == iSamplesIn) )
    { 
        if(m_bEof)
        {
            return AMF_EOF;
        }
        return AMF_REPEAT;
    }

    //assume same size for sample as input
    amf_int64 iSamplesOut    = iSamplesIn;
    //allocated enough space to hold float type of input data
    amf_size new_size       = (amf_size) (iSamplesIn * m_inChannels * sizeof(float));
    new_size = ((((amf_size)new_size) + (64 - 1)) & ~(64 - 1));
    m_InternmediateData.SetSize(new_size);

    float *pInputAsFLTP = (float *) m_InternmediateData.GetData(); 

    AMF_RETURN_IF_FALSE(pInputAsFLTP != NULL, AMF_OUT_OF_MEMORY, L"QueryOutput() - No memory");

    //////TODO: optimize if needed

    ///ibuf: hold each channel as input
    uint8_t *ibuf[s_InputChannelCount] = { pMemIn };
    //int istride[s_InputChannelCount] = { iSampleSizeIn };
    ///inputFloats: hold each channel of input converted to float
    float *inputFloats[s_InputChannelCount] = { pInputAsFLTP };

    if ( IsAudioPlanar(m_inSampleFormat) )
    {
        //copy and convert planner input
        for (amf_int32 ch = 0; ch < m_inChannels; ch++)
        {
            ibuf[ch] = (amf_uint8*) pMemIn + ch * (iSampleSizeIn * iSamplesIn);
            //istride[ch] = iSampleSizeIn;

            inputFloats[ch] = pInputAsFLTP + (ch * iSamplesIn);

            switch (m_inSampleFormat)
            {
                case AMFAF_S16P:
                {
                    float koeff = (float)0x7FFF;
                    amf_int16* theInput = (amf_int16*)ibuf[ch];
                    for (amf_int64 cc = 0; cc < iSamplesIn; cc++)
                    {
                        inputFloats[ch][cc] = (amf_float) (*theInput) / koeff;
                        theInput++;
                    }
                }
                break;
                case AMFAF_S32P:
                {
                    float koeff = (float)0x7FFFFFFF;
                    amf_int32* theInput = (amf_int32*)ibuf[ch];
                    for (amf_int64 cc = 0; cc < iSamplesIn; cc++)
                    {
                        inputFloats[ch][cc] = (amf_float) (*theInput) / koeff;
                        theInput++;
                    }
                }
                break;
                case AMFAF_FLTP:
                {
                    amf_float* theInput = (amf_float*)ibuf[ch];
                    for (amf_int64 cc = 0; cc < iSamplesIn; cc++)
                    {
                        inputFloats[ch][cc] = (*theInput);
                        theInput++;
                    }
                }
                break;
            }

        }
    }
    else
    {
        switch (m_inSampleFormat)
        {
            case AMFAF_S16:
            {
                float koeff = (float)0x7FFF;
                amf_int16* theInput = (amf_int16*)pMemIn;
                for (amf_int64 cc = 0; cc < iSamplesIn; cc++)
                {
                    for (amf_int32 ch = 0; ch < m_inChannels; ch++)
                    {
                        inputFloats[ch][cc] = (amf_float) (*theInput) / koeff;
                        theInput++;
                    }
                }
            }
            break;
            case AMFAF_S32:
            {
                float koeff = (float)0x7FFFFFFF;
                amf_int32* theInput = (amf_int32*)pMemIn;
                for (amf_int64 cc = 0; cc < iSamplesIn; cc++)
                {
                    for (amf_int32 ch = 0; ch < m_inChannels; ch++)
                    {
                        inputFloats[ch][cc] = (amf_float) (*theInput) / koeff;
                        theInput++;
                    }
                }
            }
            break;
            case AMFAF_FLT:
            {
                amf_float* theInput = (amf_float*)pMemIn;
                for (amf_int64 cc = 0; cc < iSamplesIn; cc++)
                {
                    for (amf_int32 ch = 0; ch < m_inChannels; ch++)
                    {
                        inputFloats[ch][cc] = (*theInput);
                        theInput++;
                    }
                }
            }
            break;
        }
    }



        //////TODO
    //from 4 channel,planner, float ---->    2 channel, planner, float

    // allocate output buffer
    amf_int64 outSampleRate = m_pInputData->GetSampleRate(); 

    AMFAudioBufferPtr pOutputAudioBuffer;
    AMF_RESULT  err = m_pContext->AllocAudioBuffer(AMF_MEMORY_HOST, m_outSampleFormat, 
                                     (amf_int32) iSamplesOut, (amf_int32) outSampleRate, (amf_int32) m_outChannels, &pOutputAudioBuffer);
    AMF_RETURN_IF_FAILED(err, L"QueryOutput() - AllocAudioBuffer failed");


    amf_float* stereoOutL = (amf_float*)pOutputAudioBuffer->GetNative();
    amf_float* stereoOutR = stereoOutL + iSamplesOut;

    float Theta;
    float Phi;
    amf_float *W, *X, *Y, *Z;

    {
        AMFLock lock1(&m_syncProperties);

        W = inputFloats[m_wIndex];
        X = inputFloats[m_xIndex];
        Y = inputFloats[m_zIndex]; //MM channels swapped to accomodate implementation
        Z = inputFloats[m_yIndex]; //MM channels swapped to accomodate implementation

        Theta = (float)m_Theta;
        Phi = (float)m_Phi;
    }
    m_ambi2S->process(Theta, Phi, (int)(iSamplesIn), W, X, Y, Z, stereoOutL, stereoOutR);

    //prepaer output data
    //expected output data: 2 channel, interleave, 32 bit

    if(m_pInputData != NULL)
    { 
        pOutputAudioBuffer->SetPts(m_pInputData->GetPts());
    }
    else
    {
        pOutputAudioBuffer->SetPts(m_ptsNext);
    }
    pOutputAudioBuffer->SetDuration(AMF_SECOND * iSamplesOut / outSampleRate);
    m_ptsNext = pOutputAudioBuffer->GetPts() + pOutputAudioBuffer->GetDuration();

    *ppData = pOutputAudioBuffer;
    (*ppData)->Acquire();

    amf_pts endTime = amf_high_precision_clock();
     amf_pts execution = endTime - currTime;
     (*ppData)->SetProperty(L"AmbiStart", currTime);
     (*ppData)->SetProperty(L"AmbiExec", execution);
     amf_pts callDuration = 0;
     if(m_ptsLastTime != -1LL)
     {
         callDuration = endTime - m_ptsLastTime;
     }
     m_ptsLastTime = endTime;
     (*ppData)->SetProperty(L"AmbiPeriod", callDuration);


    m_pInputData = nullptr;
    m_audioFrameQueryCount++;

    return AMF_OK;
}
//-------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFAmbisonic2SRendererImpl::GetCaps(AMFCaps** ppCaps)
{
    AMFLock lock(&m_sync);
    ///////////TODO:///////////////////////////////
    return AMF_NOT_IMPLEMENTED;
}
//-------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFAmbisonic2SRendererImpl::Optimize(AMFComponentOptimizationCallback* pCallback)
{
    ///////////TODO:///////////////////////////////
    return AMF_NOT_IMPLEMENTED;

    amf_set<AMF_MEMORY_TYPE> deviceTypes;
    AMF_RESULT err = AMF_OK;
    bool mclDone = false;//we need mcl once so far

    if(m_pContext->GetDX9Device(amf::AMF_DX9) && !mclDone)
    {
        AMFComputePtr compute;
        err = m_pContext->GetCompute(AMF_MEMORY_COMPUTE_FOR_DX9, &compute);
        if(err == AMF_OK)
        {
            deviceTypes.insert(AMF_MEMORY_COMPUTE_FOR_DX9);
            mclDone = true;
        }
    }
    if(m_pContext->GetDX11Device(AMF_DX11_1) && !mclDone)
    {
        AMFComputePtr compute;
        err = m_pContext->GetCompute(AMF_MEMORY_COMPUTE_FOR_DX11, &compute);
        if(err == AMF_OK)
        {
            deviceTypes.insert(AMF_MEMORY_COMPUTE_FOR_DX11);
            mclDone = true;
        }
    }
    if(m_pContext->GetOpenGLContext())
    {
        m_pContext->InitOpenCL();
    }
    if(m_pContext->GetOpenCLContext())
    {
        deviceTypes.insert(AMF_MEMORY_OPENCL);
    }
    amf_uint count = 0;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFAmbisonic2SRendererImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_syncProperties);
    const amf_wstring  name(pName);

    if (wcscmp(pName, AMF_AMBISONIC2SRENDERER_THETA) == 0)
    {
        GetProperty(AMF_AMBISONIC2SRENDERER_THETA, &m_Theta);
    }
    else if (wcscmp(pName, AMF_AMBISONIC2SRENDERER_PHI) == 0)
    {
        GetProperty(AMF_AMBISONIC2SRENDERER_PHI, &m_Phi);
    }
    else if (wcscmp(pName, AMF_AMBISONIC2SRENDERER_RHO) == 0)
    {
        GetProperty(AMF_AMBISONIC2SRENDERER_RHO, &m_Rho);
    }
    else  if (wcscmp(pName, AMF_AMBISONIC2SRENDERER_W) == 0)
    {
        GetProperty(AMF_AMBISONIC2SRENDERER_RHO, &m_wIndex);
    }
    else  if (wcscmp(pName, AMF_AMBISONIC2SRENDERER_X) == 0)
    {
        GetProperty(AMF_AMBISONIC2SRENDERER_RHO, &m_xIndex);
    }
    else  if (wcscmp(pName, AMF_AMBISONIC2SRENDERER_Y) == 0)
    {
        GetProperty(AMF_AMBISONIC2SRENDERER_RHO, &m_yIndex);
    }
    else  if (wcscmp(pName, AMF_AMBISONIC2SRENDERER_Z) == 0)
    {
        GetProperty(AMF_AMBISONIC2SRENDERER_RHO, &m_zIndex);
    }
}
//-------------------------------------------------------------------------------------------------
