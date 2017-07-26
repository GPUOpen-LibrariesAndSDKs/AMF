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
#include <memory.h>
#include "wav.h"

void SetupWaveHeader(RiffWave *fhd,
	long sampleRate,
	int bitsPerSample,
	int channels,
	long nSamples)
{
	long dataLength;
	dataLength = nSamples*(bitsPerSample / 8)*channels;
	memset(fhd, 0, sizeof(RiffWave));

	fhd->riff.name[0] = 'R';
	fhd->riff.name[1] = 'I';
	fhd->riff.name[2] = 'F';
	fhd->riff.name[3] = 'F';

	fhd->wave.name[0] = 'W';
	fhd->wave.name[1] = 'A';
	fhd->wave.name[2] = 'V';
	fhd->wave.name[3] = 'E';

	fhd->wave.data.name[0] = 'd';
	fhd->wave.data.name[1] = 'a';
	fhd->wave.data.name[2] = 't';
	fhd->wave.data.name[3] = 'a';

	fhd->wave.fmt.name[0] = 'f';
	fhd->wave.fmt.name[1] = 'm';
	fhd->wave.fmt.name[2] = 't';
	fhd->wave.fmt.name[3] = ' ';

	/* set the data size				*/
	fhd->wave.data.length = dataLength;

	/* set the RIFF length				*/
	fhd->riff.length = dataLength + sizeof(WaveHeader);

	/* set the length of the FORMAT block	*/
	fhd->wave.fmt.length = sizeof(WaveInfo);
	fhd->wave.fmt.info.formatTag = 1; 

	/* set up the sample rate, etc...	*/
	fhd->wave.fmt.info.nChannels = (short)channels;
	fhd->wave.fmt.info.nSamplesPerSec = sampleRate;

	//fhd->wave.fmt.info.nAvgBytesPerSec = (sampleRate << (channels - 1)) <<
	//	((bitsPerSample == 8) ? 0 : 1);
	//fhd->wave.fmt.info.nBlockAlign = (1 + ((bitsPerSample == 8) ? 0 : 1))
	//	<< (channels - 1);

    fhd->wave.fmt.info.nAvgBytesPerSec = (sampleRate*channels*bitsPerSample) / 8;
    fhd->wave.fmt.info.nBlockAlign = (channels*bitsPerSample) / 8;

    if (bitsPerSample == 32) {
        fhd->wave.fmt.info.formatTag = 3;
    }

	fhd->wave.fmt.info.nBitsPerSample = (short)bitsPerSample;
}

bool ReadWaveFile(char *fileName, int *pSamplesPerSec, int *pBitsPerSample, int *pNChannels, long *pNSamples, unsigned char **pSamples, float ***pfSamples)
{
	FILE *fpIn = NULL;
	RiffWave fhd;
	unsigned long length;

	memset(&fhd, 0, sizeof(fhd));

	if ((fopen_s(&fpIn,fileName, "rb")) != 0){
		printf("ReadWaveFile: Can't open %s\n", fileName);
		return(false);
	}

	fread((char *)&(fhd.riff), 8, 1, fpIn);
	if (memcmp(fhd.riff.name, "RIFF", 4) != 0){
			printf("ReadWaveFile: File %s is not a valid .WAV file!\n", fileName);
		return(false);
	}
	
	size_t count = 0;
	do {
		count = fread(fhd.wave.name, 4, 1, fpIn);
		if (memcmp(fhd.wave.name, "WAVE", 4) == 0){
			break;
		} 
		fread((char*)&length, 4, 1, fpIn);
		fseek(fpIn, length, SEEK_CUR);

	} while( count > 0);

	do {
		count = fread(fhd.wave.fmt.name, 4, 1, fpIn);
		if (memcmp(fhd.wave.fmt.name, "fmt ", 4) == 0){
			break;
		}
		fread((char*)&length, 4, 1, fpIn);
		fseek(fpIn, length, SEEK_CUR);

	} while (count > 0);

	fread((char*)&fhd.wave.fmt.length, 4, 1, fpIn);
	fread((char*)&fhd.wave.fmt.info, sizeof(fhd.wave.fmt.info), 1, fpIn);

	fseek(fpIn, fhd.wave.fmt.length - 16, SEEK_CUR);
	fread((char *)&fhd.wave.data, 8, 1, fpIn);
	while (memcmp(fhd.wave.data.name, "data", 4) != 0){
		fseek(fpIn, fhd.wave.data.length, SEEK_CUR);
		fread((char *)&fhd.wave.data, 8, 1, fpIn);
	}

	/* get the data size */
	int nChannels = fhd.wave.fmt.info.nChannels;
	int samplesPerSec = fhd.wave.fmt.info.nSamplesPerSec;
	int bitsPerSam = fhd.wave.fmt.info.nBitsPerSample;
	int nSamples = (fhd.wave.data.length * 8) / (bitsPerSam*nChannels);

	*pSamplesPerSec = samplesPerSec;
	*pBitsPerSample = bitsPerSam;
	*pNChannels = nChannels;
	*pNSamples = nSamples;

	/* sampling interval in seconds: */
	//int delta = 1.0 / (float)samplesPerSec;
	//printf("interval = %fs\n", delta);

	printf("ReadWaveFile: File %s has %ld %dbit samples\n", fileName, nSamples, bitsPerSam);
	printf("ReadWaveFile: recorded at %ld samples per second, ", samplesPerSec);
	printf((nChannels == 2) ? "in Stereo.\n" : "%d channels.\n",nChannels);
	printf("ReadWaveFile: Play duration: %6.2f seconds.\n",
		(float)nSamples / (float)samplesPerSec);

	*pfSamples = new float *[nChannels];
	for (int i = 0; i < nChannels; i++)
	{
		float *data;
		(*pfSamples)[i] = data = new float[(nSamples + 1)];
		if (data == NULL) {
			printf("ReadWaveFile: Failed to allocate %d floats\n", nSamples + 1);
			return(false);
		}
		for (int j = 0; j < (nSamples + 1); j++) data[j] = 0.0;
	}

	int bytesPerSam = bitsPerSam / 8;
    if (bytesPerSam == 0)
    {
        printf("ReadWaveFile: broken file\n");
        return false;
    }

	/* read wave samples, convert to floating point: */
	unsigned char *sampleBuf = new unsigned char[nSamples*nChannels * bytesPerSam];
	if (sampleBuf == NULL) {
		printf("ReadWaveFile: Failed to allocate %d bytes\n", nSamples*nChannels * bytesPerSam);
		return(false);
	}
	fread(sampleBuf, nSamples*bytesPerSam*nChannels, 1, fpIn);
	short *sSampleBuf = (short *)sampleBuf;
	float *fSampleBuf = (float *)sampleBuf;
	*pSamples = sampleBuf;

	switch (bitsPerSam){
	case 8:
		for (int i = 0; i < nSamples; i++){
			int k;
			k = i*nChannels;
			for (int n = 0; n < nChannels;n++)
			{
				(*pfSamples)[n][i] = (float)(sampleBuf[k + n] - 127) / 256.0f;
			}
		}
		break;
	case 16:
		for (int i = 0; i < nSamples; i++){
			int k;
			k = i*nChannels;
			for (int n = 0; n < nChannels; n++)
			{
				(*pfSamples)[n][i] = (float)(sSampleBuf[k + n]) / 32768.0f;
			}
		}
		break;
	case 32:
		for (int i = 0; i < nSamples; i++){
			int k;
			k = i*nChannels;
			for (int n = 0; n < nChannels; n++)
			{
				(*pfSamples)[n][i] = fSampleBuf[k + n];
			}
		}
		break;
	}
	
	fclose(fpIn);
	return(true);

}

bool WriteWaveFileF(const char *fileName, int samplesPerSec, int nChannels, int bitsPerSample, long nSamples, float **pSamples)
{
	/* write wave samples: */
	RiffWave fhd;
	FILE *fpOut;

	SetupWaveHeader(&fhd, samplesPerSec, bitsPerSample, nChannels, nSamples);
	int bytesPerSample = bitsPerSample / 8;
    if (fopen_s(&fpOut, fileName, "wb") != 0 || !fpOut) return false;
	fwrite(&fhd, sizeof(fhd), 1, fpOut);

	char *buffer = new char[bytesPerSample*nChannels*nSamples];
	short *sSamBuf = (short *)buffer;
	float *fSamBuf = (float *)buffer;

	switch (bytesPerSample){
	case 1:
		for (int i = 0; i<nSamples; i++){
			int k;
			k = i*nChannels;
			for (int n = 0; n<nChannels; n++) {
				float value = pSamples[n][i];
				if (value > 1.0) value = 1.0;
				if (value < -1.0) value = -1.0;
				buffer[k + n] = (char)(value * 127.0);
			}
		}
		break;
	case 2:
		for (int i = 0; i<nSamples; i++){
			int k;
			k = i*nChannels;
			for (int n = 0; n<nChannels; n++) {
				float value = pSamples[n][i];
				if (value > 1.0) value = 1.0;
				if (value < -1.0) value = -1.0;
				sSamBuf[k+n] = (short)(value * 32767.0);
			}
		}
		break;
	case 4:
		for (int i = 0; i<nSamples; i++){
			int k;
			k = i*nChannels;
			for (int n = 0; n<nChannels; n++) {
				fSamBuf[k + n] = pSamples[n][i];
			}
		}
		break;
	default:
		return false;
		break;
	}


	fwrite(buffer, nSamples*nChannels * bytesPerSample, 1, fpOut);
	fclose(fpOut);

	return(0);

}

bool WriteWaveFileS(const char *fileName, int samplesPerSec, int nChannels, int bitsPerSample, long nSamples, short *pSamples)
{
	/* write wave samples: */
	RiffWave fhd;
	FILE *fpOut;

	SetupWaveHeader(&fhd, samplesPerSec, bitsPerSample, nChannels, nSamples);
	int bytesPerSample = bitsPerSample / 8;
	fopen_s(&fpOut,fileName, "wb");
	if (fpOut == NULL)
		return false;

	fwrite(&fhd, sizeof(fhd), 1, fpOut);

	char *buffer = new char[bytesPerSample*nChannels*nSamples];
	short *sSamBuf = (short *)buffer;
	float *fSamBuf = (float *)buffer;

	switch (bytesPerSample){
	case 1:
		for (int i = 0; i<nSamples; i++){
			int k;
			k = i*nChannels;
			for (int n = 0; n<nChannels; n++) {
				short value = pSamples[k + n];
				buffer[k + n] = (short)(value >> 8);
			}
		}
		break;
	case 2:
		for (int i = 0; i<nSamples; i++){
			int k;
			k = i*nChannels;
			for (int n = 0; n<nChannels; n++) {
				short value = pSamples[k + n];
				sSamBuf[k + n] = value;
			}
		}
		break;
	case 4:
		for (int i = 0; i<nSamples; i++){
			int k;
			k = i*nChannels;
			for (int n = 0; n<nChannels; n++) {
				fSamBuf[k + n] = (float)pSamples[k + n] / 32767;
			}
		}
		break;
	default:
		return false;
		break;
	}


	fwrite(buffer, nSamples*nChannels * bytesPerSample, 1, fpOut);
	fclose(fpOut);

	return(true);

}

