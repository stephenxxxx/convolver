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

#include "convolution\config.h"
#include "convolution\holder.h"
#include "convolution\sample.h"
#include "convolution\samplebuffer.h"
#include "convolution\channelpaths.h"
#include "convolution\waveformat.h"
#include "convolution\lrint.h"

// For random number seed
#include <time.h>

// The following routines are external to pick up multiplication routines that have been vectorized using Intel C99
#ifndef FFTW
#if (defined(__ICC) || defined(__ICL) || defined(__ECC) || defined(__ECL))
// Intel compiler, for vectorization
extern "C" void cmul(float *, float *, float *, int);
extern "C" void cmuladd(float *, float *, float *, int);
#endif
#endif

// CConvolution does the work
// The data path is:
// pbInputData (raw bytes) ->  AttenuatedSample(const float fAttenuation_db, GetSample(const BYTE* & container)) ->
// m_InputBuffer (FFT_type) -> [convolve] ->
// m_OutputBuffer (FFT_type) -> NormalizeSample(BYTE* dstContainer, float& srcSample) ->
// pbOuputData (raw bytes)

template <typename T>
class CConvolution
{
public:
	CConvolution(TCHAR szConfigFileName[MAX_PATH], const WORD& nPartitions, const unsigned int& nPlanningRigour);
//	virtual ~CConvolution(void) {};

	// This version of the convolution routine does partitioned convolution
	// Returns number of bytes processed  (== number of output bytes, too)

	// This version of the convolution routine does partitioned convolution
	DWORD doPartitionedConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
		Sample<T>* input_sample_convertor,	// The functionoid for converting between BYTE* and T
		Sample<T>* output_sample_convertor,	// The functionoid for converting between T and BYTE*
		DWORD dwBlocksToProcess,		// A block contains a sample for each channel
		const T fAttenuation_db);  // Returns bytes generated

	// This version does straight overlap-save convolution
	DWORD doConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
		Sample<T>* input_sample_convertor,	// The functionoid for converting between BYTE* and T
		Sample<T>* output_sample_convertor,	// The functionoid for converting between T and BYTE*
		DWORD dwBlocksToProcess,
		const T fAttenuation_db); // Returns bytes generated

	void Flush();								// zero buffers, reset pointers

	ChannelPaths		Mixer;				// Order dependent

	int cbLookAhead(Sample<T>* & sample_convertor)
	{
		if(sample_convertor != NULL)
		{
			return Mixer.nPartitionLength / 2 * sample_convertor->nContainerSize(); // The lag
		}
		else
		{
			throw convolutionException("Internal Error: LookAhead incalculable");
		}
	}

	// Calculate the size in bytes of the output buffer corresponding to a given input buffer size
	unsigned int cbOutputBuffer(const int cbInputBuffer, Sample<T>* & InputSampleConvertor, Sample<T>* & OutputSampleConvertor)
	{
		// Size of each input frame / block
		const unsigned int cbInputFrame = InputSampleConvertor->nContainerSize() * Mixer.nInputChannels;
		// Check that the input buffer size is a whole number of blocks / frames
		assert(cbInputBuffer % cbInputFrame == 0);

		const unsigned int nInputFrames = cbInputBuffer / cbInputFrame; // No of frames/blocks in each input buffer

		// Size of each output frame
		const unsigned int cbOutputFrame = OutputSampleConvertor->nContainerSize() * Mixer.nOutputChannels;;

		return cbOutputFrame * nInputFrames;
	}

private:
	SampleBuffer		InputBuffer_;
	ChannelBuffer		InputBufferAccumulator_;
	ChannelBuffer		OutputBuffer_;				// The output for a particular path, before mixing
	SampleBuffer		OutputBufferAccumulator_;	// For collecting path outputs
	PartitionedBuffer	ComputationCircularBuffer_;	// Used as the output buffer for partitioned convolution

	DWORD				nInputBufferIndex_;			// placeholder
	WORD				nPartitionIndex_;			// for partitioned convolution
	WORD				nPreviousPartitionIndex_;	// lags nPartitionIndex_ by 1
	bool				bStartWriting_;

	void mix_input(const ChannelPaths::ChannelPath& restrict thisPath);
	void mix_output(const ChannelPaths::ChannelPath& restrict thisPath, SampleBuffer& restrict Accumulator, 
		const ChannelBuffer& restrict Output, const DWORD& from, const DWORD& to);

	// The following need to be distinguished because different FFT routines use different orderings
#ifdef FFTW
	void inline complex_mul(const fftwf_complex* restrict in1, const fftwf_complex* restrict in2,
										 fftwf_complex* restrict result, const DWORD count);
	void inline complex_mul_add(const fftwf_complex* restrict in1, const fftwf_complex* restrict in2,
											 fftwf_complex* restrict result, const DWORD count);
#if defined(DEBUG) | defined(_DEBUG)
	T verify_convolution(const ChannelBuffer& X, const ChannelBuffer& H, const ChannelBuffer& Y, const DWORD from, const DWORD to) const;
#endif
#elif !(defined(__ICC) || defined(__INTEL_COMPILER))
	// non-vectorized complex array multiplication -- ordering specific to the Ooura routines. C = A * B
	void inline cmult(const ChannelBuffer& restrict A, const ChannelBuffer& restrict B, ChannelBuffer& restrict C, const DWORD N);
	void inline cmultadd(const ChannelBuffer& restrict A, const ChannelBuffer& restrict B, ChannelBuffer& restrict C, const DWORD N);
#endif

	// Used to check filter / partion lengths
	bool isPowerOf2(int i) const
	{
		return i > 0 && (i & (i - 1)) == 0;
	};

	CConvolution(const CConvolution& other); // no impl.
	void operator=(const CConvolution& other); // no impl.
	CConvolution(CConvolution& other);                // discourage use of lvalue Filters
};

template <typename T>
HRESULT calculateOptimumAttenuation(T& fAttenuation, TCHAR szConfigFileName[MAX_PATH], 
									const WORD& nPartitions, const unsigned int& nPlanningRigour);

