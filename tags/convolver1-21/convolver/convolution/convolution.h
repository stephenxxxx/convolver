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

// To seed random number generator
#include <time.h>

#if defined(DEBUG) | defined(_DEBUG)
#include "debugging\debugging.h"
#include "debugging\debugStream.h"
#endif

#include ".\samplebuffer.h"
#include ".\filter.h"

// FFT routines
#include "fft\fftsg_h.h"


const DWORD MAX_ATTENUATION = 1000; // dB

// CConvolution does the work
// The data path is:
// pbInputData (raw bytes) ->  AttenuatedSample(const double fAttenuation_db, GetSample(BYTE* & container)) ->
// m_InputBuffer (FFT_type) -> [convolve] ->
// m_OutputBuffer (FFT_type) -> NormalizeSample(BYTE* & dstContainer, double& srcSample) ->
// pbOuputData (raw bytes)


class CConvolution
{
public:
	CConvolution(TCHAR szFilterFileName[MAX_PATH], const DWORD nPartitions, const BYTE nContainerSize);
	virtual ~CConvolution(void);

	// This version of the convolution routine does partitioned convolution
	// Returns number of bytes processed  (== number of output bytes, too)

	// This version of the convolution routine does partitioned convolution
	DWORD doPartitionedConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
		DWORD dwBlocksToProcess, // A block contains a sample for each channel
		const double fAttenuation_db,
		const double fWetMix,
		const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
		, CWaveFileHandle& CWaveFileTrace
#endif	
		);  // Returns bytes processed

	// This version does straight overlap-save convolution
	DWORD doConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
		DWORD dwBlocksToProcess,
		const double fAttenuation_db,
		const double fWetMix,
		const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
		, CWaveFileHandle& CWaveFileTrace
#endif	
		);

	void Flush();								// zero buffers, reset pointers

	// TODO: Need to keep the number of source channels, and either mix them, or use a sub-set, if the filter has more channels
	Filter				FIR;					// Order dependent

protected:

	const BYTE			nContainerSize_;			// 8, 16, 24, or 32 bits (64 not implemented)

private:

	SampleBuffer		InputBuffer_;
	SampleBuffer		InputBufferCopy_;			// As FFT routines destroy their input
	SampleBuffer		OutputBuffer_;				// Used by overlap-save
	PartitionedBuffer	ComputationCircularBuffer_;	// Used as the output buffer for partitioned convolution

	int					nInputBufferIndex_;		// placeholder
	int					nPartitionIndex_;		// for partitioned convolution
	int					nOutputPartitionIndex_;	// lags nPartitionIndex_ by 1
	bool				bStartWritingOutput_;	// don't start outputting until we have some convolved output

	// Complex array multiplication -- ordering specific to the Ooura routines. C = A * B
	//void cmult(const std::vector<float>& A, const std::vector<float>& B, std::vector<float>& C, const DWORD N);
	inline void cmult(const ChannelBuffer& A, const ChannelBuffer& B, ChannelBuffer& C, const int N);
	inline void cmultadd(const ChannelBuffer& A, const ChannelBuffer& B, ChannelBuffer& C, const int N);

	// Used to check filter / partion lengths
	bool isPowerOf2(int i) const
	{
		return i > 0 && (i & (i - 1)) == 0;
	};

	// Round the argument to the nearest integer value, using the current rounding direction. 
	// On Intel Pentium processors (especially PIII and probably P4), converting
	// from float to int is very slow. To meet the C specs, the code produced by
	// most C compilers targeting Pentium needs to change the FPU rounding mode
	// before the float to int conversion is performed. Changing the FPU rounding
	// mode causes the FPU pipeline to be flushed. It is this flushing of the
	// pipeline which is so slow.
	//inline long int lrint (double flt)
	//{	
	//	int intgr;

	//	_asm
	//	{
	//		fld flt
	//		fistp intgr
	//	};

	//	return intgr;
	//};

protected:
	// Pure virtual functions that convert from a the sample type, to float
	virtual float GetSample(BYTE* & container) const = 0;						// converts sample into a float, [-1..1]
	virtual DWORD NormalizeSample(BYTE* & dstContainer, double& srcSample) const = 0;	// returns number of bytes processed
};


/* The following code class adapted from is taken from the book
* "C++ Templates - The Complete Guide"
* by David Vandevoorde and Nicolai M. Josuttis, Addison-Wesley, 2002
*
* (C) Copyright David Vandevoorde and Nicolai M. Josuttis 2002.
*/
template <typename T>
class Holder {
private:
	T* ptr_;    // refers to the object it holds (if any)

public:
	// default constructor: let the holder refer to nothing
	Holder() : ptr_(0)	{}

	// constructor for a pointer: let the holder refer to where the pointer refers
	explicit Holder (T* p) : ptr_(p) { }

	// destructor: releases the object to which it refers (if any)
	~Holder()
	{ 
		delete ptr_;
	}

	// assignment of new pointer
	Holder<T>& operator= (T* p)
	{
		delete ptr_;
		ptr_ = p;
		return *this;
	}

	// pointer operators
	T& operator* () const
	{ 
		return *ptr_;
	}

	T* operator-> () const
	{ 
		return ptr_; 
	}

    // get referenced object (if any)
    T* get_ptr() const
	{
        return ptr_;
    }

private:
	// no copying and copy assignment allowed
	Holder (Holder<T> const&);
	Holder<T>& operator= (Holder<T> const&);
};

// Specializations with the appropriate functions for accessing the sample buffer
class Cconvolution_ieeefloat : public CConvolution
{
public:
	Cconvolution_ieeefloat(TCHAR szFilterFileName[MAX_PATH], DWORD nPartitions) : CConvolution(szFilterFileName, nPartitions, sizeof(float)){};

private:
	float GetSample(BYTE* & container) const
	{
		//return *(FFT_type *)container;
		return *reinterpret_cast<const float *>(container);
	};	// TODO: find a cleaner way to do this

	DWORD NormalizeSample(BYTE* & dstContainer, double& srcSample) const 
	{ 
		*((float *) dstContainer) = (float) srcSample;
		//* reinterpret_cast<float*>(dstContainer) = static_cast<float>(srcSample); // TODO: find cleaner way to do this
		return sizeof(float);
	};
};

// 8-bit sound is 0..255 with 128 == silence
class Cconvolution_pcm8 : public CConvolution
{
public:
	Cconvolution_pcm8(TCHAR szFilterFileName[MAX_PATH], DWORD nPartitions) : CConvolution(szFilterFileName, nPartitions, sizeof(BYTE)){};

private:

	float GetSample(BYTE* & container) const
	{
		return static_cast<float>((*container - 128.0) / 128.0);  // Normalize to [-1..1]
	};

	DWORD NormalizeSample(BYTE* & dstContainer, double& srcSample) const
	{   
		srcSample *= 128.0;

		// Truncate if exceeded full scale.
		if (srcSample > 127)
			srcSample = 127;
		if (srcSample < -128)
			srcSample = -128;
		*dstContainer = static_cast<BYTE>(srcSample + 128);
		return sizeof(BYTE); // ie, 1 byte
	};
};

// 16-bit sound is -32768..32767 with 0 == silence
class Cconvolution_pcm16 : public CConvolution
{
public:
	Cconvolution_pcm16(TCHAR szFilterFileName[MAX_PATH], DWORD nPartitions) : CConvolution(szFilterFileName, nPartitions, sizeof(INT16)){};

private:

	float GetSample(BYTE* & container) const 
	{ 
		return static_cast<float>(*reinterpret_cast<INT16*>(container) / 32768.0);
	}; 	// TODO: find a cleaner way to do this

	DWORD NormalizeSample(BYTE* & dstContainer, double& srcSample) const
	{   
		srcSample *= 32768.0;

		// Truncate if exceeded full scale.
		if (srcSample > 32767)
			srcSample = 32767;
		else
			if (srcSample < -32768)
				srcSample = -32768;
		*(INT16 *)dstContainer = (INT16)(srcSample);
		//*reinterpret_cast<INT16*>(dstContainer) = static_cast<INT16>(srcSample);
		return sizeof(INT16);  // ie, 2 bytes produced
	}
};

// 24-bit sound
template <int validBits>
class Cconvolution_pcm24 : public CConvolution
{
public:
	Cconvolution_pcm24(TCHAR szFilterFileName[MAX_PATH], DWORD nPartitions) : CConvolution(szFilterFileName, nPartitions, 3 /* Bytes */){};

private:

	float GetSample(BYTE* & container) const
	{
		int i = container[2];
		i = ( i << 8 ) | container[1];
		switch (validBits)
		{
		case 16:
			return i / 32768.0; // 2^15
		case 20:
			i = (i << 4) | (container[0] >> 4);
			return i / 524288.0; // 2^19
		case 24:
			i = (i << 8) | container[0];
			return i / 8388608.0; // 2^23
		default:
			assert(false);
			return i / 8388608.0; // 2^23;
		};

	};

	DWORD NormalizeSample(BYTE* & dstContainer, double& srcSample) const
	{   
		int iClip = 0;
		switch (validBits)
		{
		case 16:
			srcSample *= 32768.0;
			iClip = (1 << 15);
			break;
		case 20:
			srcSample *= 524288.0;
			iClip = (1 << 19);
			break;
		case 24:
			srcSample *= 8388608.0;
			iClip = (1 << 23);
			break;
		default:
			assert(false);
			srcSample *= 8388608.0;
			iClip = (1 << 23);
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

		return 3; // ie, 3 bytes generated
	};
};

// 32-bit sound
template <int validBits>
class Cconvolution_pcm32 : public CConvolution
{
public:
	Cconvolution_pcm32(TCHAR szFilterFileName[MAX_PATH], DWORD nPartitions) : CConvolution(szFilterFileName, nPartitions, sizeof(INT32)){};

private:

	float GetSample(BYTE* & container) const
	{
		INT32 i = *reinterpret_cast<INT32*>(container);
		switch (validBits)
		{
		case 16:
			i >>= 16;
			return i / 32768.0; // 2^15;
		case 20:
			i >>= 12;
			return i / 524288.0; // 2^19;
		case 24:
			i >>= 8;
			return i / 8388608.0; // 2^23;
		case 32:
			return i / 2147483648.0; // 2^31
		default:
			assert(false);
			return i /  2147483648.0;  // 2^31
		}
	}; 

	DWORD NormalizeSample(BYTE* & dstContainer, double& srcSample) const
	{   
		INT32 iClip = 0;
		switch (validBits)
		{
		case 16:
			iClip = (1 << 15);
			srcSample *= 32768.0; // 2^15;
			break;
		case 20:
			iClip = (1 << 19);
			srcSample *= 524288.0; // 2^19;
			break;
		case 24:
			srcSample *= 8388608.0; // 2^23;
			iClip = (1 << 23);
			break;
		case 32:
			srcSample *=  2147483648.0;  // 2^31
			iClip = (1 << 31);
			break;
		default:
			assert(false);
			srcSample *=  2147483648.0;  // 2^31
			iClip = (1 << 31);
		}

		// Truncate if exceeded full scale.
		if (srcSample > (iClip - 1))
			srcSample = static_cast<double>(iClip - 1);
		else
			if (srcSample < -iClip)
				srcSample = static_cast<double>(-iClip);
		*(INT32 *)dstContainer = static_cast<INT32>(srcSample);
		//*reinterpret_cast<INT32*>(dstContainer) = static_cast<INT32>(srcSample);
		return sizeof(INT32);  // ie, 4 bytes generated
	};
};

HRESULT SelectConvolution(WAVEFORMATEX* & pWave, Holder<CConvolution>& convolution, TCHAR szFilterFileName[MAX_PATH], const DWORD nPartitions);
HRESULT calculateOptimumAttenuation(double& fAttenuation, TCHAR szFilterFileName[MAX_PATH], const DWORD nPartitions);
