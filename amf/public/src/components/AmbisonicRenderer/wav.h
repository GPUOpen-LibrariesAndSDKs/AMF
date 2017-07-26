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

#pragma pack(push,1)

    /*\
    |*|----====< ".WAV" file definition >====----
    |*|
    |*|     4 bytes 'RIFF'
    |*|     4 bytes <length>
    |*|     4 bytes 'WAVE'
    |*|     4 bytes 'fmt '
	|*| 	4 bytes  <length>	; 10h - length of 'data' block
    |*|     2 bytes  01 	    ; format tag
    |*|     2 bytes  01 	    ; channels (1=mono, 2=stereo)
    |*|     4 bytes  xxxx	    ; samples per second
    |*|     4 bytes  xxxx	    ; average samples per second
	|*| 	2 bytes  01/02/04	; block alignment
    |*|     2 bytes  08/16	    ; bits per sample
    |*|     4 bytes 'data'
    |*|     4 bytes <length>
    |*|       bytes <sample data>
    |*|
    \*/


    /* Wave format control block					*/

        typedef struct {
	    short  formatTag;		/* format category		*/
	    short  nChannels;		/* stereo/mono			*/
	    long nSamplesPerSec;	/* sample rate			*/
	    long nAvgBytesPerSec;	/* stereo * sample rate 	*/
	    short  nBlockAlign;		/* block alignment (1=byte)	*/
	    short  nBitsPerSample;	/* # byte bits per sample	*/
	} WaveInfo;

	typedef struct {
	    char name[4];
	    long length;
	    WaveInfo info;
	} WaveFormat;

    /* Data header which follows a WaveFormat Block			*/

        typedef struct {
	    char name[4];
	    unsigned long length;
	} DataHeader;

    /* Total Wave Header data in a wave file				*/

        typedef struct {
	    char name[4];
	    WaveFormat fmt;
	    DataHeader data;
	} WaveHeader;

    /* Riff wrapper around the WaveFormat Block (optional)		*/

	typedef struct {
	    char name[4];
	    long length;
	} RiffHeader;

    /* Riff wrapped WaveFormat Block					*/

	typedef struct {
	    RiffHeader riff;
	    WaveHeader wave;
	} RiffWave;

	bool ReadWaveFile(char *fileName, int *pSamplesPerSec, int *pBitsPerSample, int *pNChannels, long *pNSamples, unsigned char **pSamples, float ***pfSamples);
	bool WriteWaveFileF(const char *fileName, int samplesPerSec, int nChannels, int bitsPerSample, long nSamples, float **pSamples);
	bool WriteWaveFileS(const char *fileName, int samplesPerSec, int nChannels, int bitsPerSample, long nSamples, short *pSamples);

#pragma pack(pop)


#ifdef _MSC_VER

#endif

    /*\
	|*| end of WAV.H
	\*/
