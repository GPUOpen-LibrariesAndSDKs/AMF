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
// Copyright (C) 2023 Advanced Micro Devices, Inc. All rights reserved.
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

#include "public/include/core/Platform.h"

class AMFHalfFloat
{
public:
    AMFHalfFloat()
    {
        GenerateHalfFloatConversionTables();
    }

    AMF_FORCEINLINE static amf_uint16 ToHalfFloat(amf_float value)
    {
        union FloatBits
        {
            amf_float f;
            amf_uint32 u;
        };

        FloatBits val;
        val.f = value;

        return amf_uint16(m_basetable[(val.u >> 23) & 0x1ff] + ((val.u & 0x007fffff) >> m_shifttable[(val.u >> 23) & 0x1ff]));
    }

    AMF_FORCEINLINE static float FromHalfFloat(amf_uint16 value)
    {
        uint32_t mantissa = (uint32_t)(value & 0x03FF);

        uint32_t exponent = (value & 0x7C00);
        if (exponent == 0x7C00) // INF/NAN
        {
            exponent = (uint32_t)0x8f;
        }
        else if (exponent != 0)  // The value is normalized
        {
            exponent = (uint32_t)((value >> 10) & 0x1F);
        }
        else if (mantissa != 0)     // The value is denormalized
        {
            // Normalize the value in the resulting float
            exponent = 1;

            do
            {
                exponent--;
                mantissa <<= 1;
            } while ((mantissa & 0x0400) == 0);

            mantissa &= 0x03FF;
        }
        else                        // The value is zero
        {
            exponent = (uint32_t)-112;
        }

        uint32_t result = ((value & 0x8000) << 16) | // Sign
            ((exponent + 112) << 23) | // exponent
            (mantissa << 13);          // mantissa

        return reinterpret_cast<float*>(&result)[0];
    }
private:

    static amf_uint16 m_basetable[512];
    static amf_uint8 m_shifttable[512];

    static void GenerateHalfFloatConversionTables()
    {
        for (unsigned int i = 0; i < 256; i++)
        {
            int e = i - 127;

            // map very small numbers to 0
            if (e < -24)
            {
                m_basetable[i | 0x000] = 0x0000;
                m_basetable[i | 0x100] = 0x8000;
                m_shifttable[i | 0x000] = 24;
                m_shifttable[i | 0x100] = 24;
            }
            // map small numbers to denorms
            else if (e < -14)
            {
                m_basetable[i | 0x000] = (0x0400 >> (-e - 14));
                m_basetable[i | 0x100] = (0x0400 >> (-e - 14)) | 0x8000;
                m_shifttable[i | 0x000] = amf_uint8(-e - 1);
                m_shifttable[i | 0x100] = amf_uint8(-e - 1);
            }
            // normal numbers lose precision
            else if (e <= 15)
            {
                m_basetable[i | 0x000] = amf_uint16((e + 15) << 10);
                m_basetable[i | 0x100] = amf_uint16(((e + 15) << 10) | 0x8000);
                m_shifttable[i | 0x000] = 13;
                m_shifttable[i | 0x100] = 13;
            }
            // large numbers map to infinity
            else if (e < 128)
            {
                m_basetable[i | 0x000] = 0x7C00;
                m_basetable[i | 0x100] = 0xFC00;
                m_shifttable[i | 0x000] = 24;
                m_shifttable[i | 0x100] = 24;
            }
            // infinity an NaN stay so
            else
            {
                m_basetable[i | 0x000] = 0x7C00;
                m_basetable[i | 0x100] = 0xFC00;
                m_shifttable[i | 0x000] = 13;
                m_shifttable[i | 0x100] = 13;
            }
        }
    }

};

static AMFHalfFloat s_InitHalfFLoat;
