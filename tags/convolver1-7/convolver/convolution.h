#pragma once
// Convolver: DSP plug-in for Windows Media Player that convolves an impulse respose
// filter it with the input stream.
//
// Copyright (C) 2005  John Pavel
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

// Pull in Common DX classes
#include "Common\dxstdafx.h"

#include ".\containerBuffer.h"

// FFT routines
#include <fftsg_h.h>

template <typename FFT_type>
class CConvolution
{
public:
	CConvolution(unsigned int n2xSampleSize);
	~CConvolution(void);  // TODO: should this be here?

	DWORD // Returns number of bytes processed
	doConvolution(const BYTE* pbInputData, BYTE* pbOutputData,
				const unsigned int nChannels,
				const CContainerBuffer<FFT_type>* filter,
				CContainerBuffer<FFT_type>* inputBuffer,
				CContainerBuffer<FFT_type>* outputBuffer,
				DWORD dwBlocksToProcess,
				const double fAttenuation_db,
				const double fWetMix,
				const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
				, CWaveFile* CWaveFileTrace
#endif	
				);

private:

	CChannelBuffer<FFT_type>* m_SampleBufferChannelCopy;

	DWORD m_n2xContainerSize;
	DWORD m_nContainerSize;
	DWORD m_nInputBufferIndex;

	// Complex array multiplication -- ordering specific to the Ooura routines. C = A * B
	void cmult(const FFT_type * A, const FFT_type * B, FFT_type * C,const int N);

	FFT_type attenuated_sample(const double fAttenuation_db, const FFT_type sample)
	{
		return fAttenuation_db == 0 ? sample : 
			static_cast<FFT_type>(static_cast<double>(sample) * pow(static_cast<double>(10), static_cast<double>(-fAttenuation_db / 20.0L)));
	}

protected:
	// Pure virtual functions that convert from a the container type, to the FFT type
	virtual FFT_type get_sample(const BYTE* container) const = 0;						// converts container into a value of the right type
	virtual DWORD normalize_sample(BYTE* dstContainer, double srcSample) const = 0;	// returns number of bytes processed
	virtual BYTE* next_container(BYTE* container) const = 0;								// used to move the buffer pointer on
};



// Specializations with the appropriate functions for accessing the container buffer
template <typename FFT_type>
class Cconvolution_ieeefloat : public CConvolution<FFT_type>
{
public:
	Cconvolution_ieeefloat(unsigned int n2xSampleSize) : CConvolution<FFT_type>(n2xSampleSize){};

private:
	FFT_type get_sample(const BYTE* container) const
	{
		//return *(FFT_type *)container;
		return *reinterpret_cast<const FFT_type *>(container);
	};	// TODO: find a cleaner way to do this

	DWORD normalize_sample(BYTE* dstContainer, double srcSample) const 
	{ 
		float fsrcSample = static_cast<float>(srcSample);
		//dstContainer = (BYTE*)(&fsrcSample);
		dstContainer = reinterpret_cast<BYTE*>(&fsrcSample);
		return sizeof(float);
	};

	BYTE* next_container(BYTE* container) const 
	{ 
		return container + sizeof(float);
	};
};

// 8-bit sound is 0..255 with 128 == silence
template <typename FFT_type>
class Cconvolution_pcm8 : public CConvolution<FFT_type>
{
public:
	Cconvolution_pcm8(unsigned int n2xSampleSize) : CConvolution<FFT_type>(n2xSampleSize){};

private:

	FFT_type get_sample(const BYTE* container) const
	{
		return static_cast<FFT_type>(*container - 128);
	};

	DWORD normalize_sample(BYTE* dstContainer, double srcSample) const
	{   // Truncate if exceeded full scale.
		if (srcSample > 127)
			srcSample = 127;
		if (srcSample < -128)
			srcSample = -128;
		*dstContainer = static_cast<BYTE>(srcSample + 128);
		return 1; // ie, 1 byte
	};

	BYTE* next_container(BYTE* container) const 
	{ 
		return container + sizeof(BYTE);
	};
};

// 16-bit sound is -32768..32767 with 0 == silence
template <typename FFT_type>
class Cconvolution_pcm16 : public CConvolution<float>
{
public:
	Cconvolution_pcm16(unsigned int n2xSampleSize) : CConvolution<FFT_type>(n2xSampleSize){};

private:

	FFT_type get_sample(const BYTE* container) const 
	{ 
		return static_cast<FFT_type>(*reinterpret_cast<const INT16*>(container));
	}; 	// TODO: find a cleaner way to do this

	DWORD normalize_sample(BYTE* dstContainer, double srcSample) const
	{   // Truncate if exceeded full scale.
		if (srcSample > 32767)
			srcSample = 32767;
		else
			if (srcSample < -32768)
				srcSample = -32768;
		// *(INT16 *)dstContainer = static_cast<INT16>(srcSample);
		*reinterpret_cast<INT16 *>(dstContainer) = static_cast<INT16>(srcSample);
		return sizeof(INT16);  // ie, 2 bytes consumed
	};

	BYTE* next_container(BYTE* container) const
	{
		return container + sizeof(INT16);
	};
};

// 24-bit sound
template <typename FFT_type, int validBits>
class Cconvolution_pcm24 : public CConvolution<float>
{
public:
	Cconvolution_pcm24(unsigned int n2xSampleSize) : CConvolution<FFT_type>(n2xSampleSize){};

private:

	FFT_type get_sample(const BYTE* container) const
	{
		int i = (char) container[2];
		i = ( i << 8 ) | container[1];
		switch (validBits)
		{
		case 16:
			break;
		case 20:
			i = (i << 4) | (container[0] >> 4);
			break;
		case 24:
			i = (i << 8) | container[0];
			break;
		};
		return static_cast<FFT_type>(i);
	};

	DWORD normalize_sample(BYTE* dstContainer, double srcSample) const
	{   
		int iClip = 0;
		switch (validBits)
		{
		case 16:
			iClip = (1 << 15);
			break;
		case 20:
			iClip = (1 << 19);
			break;
		case 24:
			iClip = (1 << 23);
			break;
		};

		// Truncate if exceeded full scale.
		int i = 0;
		if (srcSample > (iClip - 1))
			i = iClip - 1;
		else
			if (srcSample < -iClip)
				i = -iClip;
			else
				i = static_cast<int>(srcSample);

		dstContainer[0] = static_cast<BYTE>(i & 0xff);
		dstContainer[1] = static_cast<BYTE>((i >>  8) & 0xff);
		dstContainer[2] = static_cast<BYTE>((i >> 16) & 0xff);

		return 3; // ie, 3 bytes processed
	};

	BYTE* next_container(BYTE* container) const 
	{
		return container + 3; // 3 bytes = 24 bits
	}; 
};


// 32-bit sound
template <typename FFT_type, int validBits>
class Cconvolution_pcm32 : public CConvolution<float>
{
public:
	Cconvolution_pcm32(unsigned int n2xSampleSize) : CConvolution<FFT_type>(n2xSampleSize){};

private:

	FFT_type get_sample(const BYTE* container) const
	{
		INT32 i = *reinterpret_cast<const INT32*>(container);
		switch (validBits)
		{
		case 16:
			i >>= 16;
			break;
		case 20:
			i >>= 12;
			break;
		case 24:
			i >>= 8;
			break;
		case 32:
			break;
		}
		return static_cast<FFT_type>(i);
	}; 

	DWORD normalize_sample(BYTE* dstContainer, double srcSample) const
	{   
		INT32 iClip = 0;
		switch (validBits)
		{
		case 16:
			iClip = (1 << 15);
			break;
		case 20:
			iClip = (1 << 19);
			break;
		case 24:
			iClip = (1 << 23);
			break;
		case 32:
			iClip = (1 << 31);
			break;
		}

		// Truncate if exceeded full scale.
		if (srcSample > (iClip - 1))
			srcSample = static_cast<double>(iClip - 1);
		else
			if (srcSample < -iClip)
				srcSample = static_cast<double>(-iClip);

		*reinterpret_cast<INT32*>(dstContainer) = static_cast<INT32>(srcSample);
		return 4;  // ie, 4 bytes processed
	};

	BYTE* next_container(BYTE* container) const 
	{
		return container + 4;  // 4 bytes = 32 bits
	};  
};