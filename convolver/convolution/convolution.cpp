// Convolver: DSP plug-in for Windows Media Player that convolves an impulse respose
// Filter it with the input stream.
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

#include  "convolution\convolution.h"

// For calculate optimum attenuation
#include <boost/random.hpp>
#include <boost/generator_iterator.hpp>
#include <winnt.h>

template class Convolution<float>;
template class ConvolutionList<float>;
//template HRESULT calculateOptimumAttenuation<float>(float& fAttenuation, TCHAR szConfigFileName[MAX_PATH], 
//													const WORD& nPartitions, const unsigned int& nPlanningRigour);

// Convolution Constructor
template <typename T>
Convolution<T>::Convolution(const TCHAR szConfigFileName[MAX_PATH], 
							const DWORD& nPartitions,
							const unsigned int& nPlanningRigour) :
nPartitions_(nPartitions),
Mixer(szConfigFileName, nPartitions, nPlanningRigour),
#ifdef FFTW
InputBufferAccumulator_(Mixer.nFFTWPartitionLength()),
OutputBuffer_(Mixer.nFFTWPartitionLength()),
#ifdef ARRAY
ComputationCircularBuffer_(Mixer.nPaths(), nPartitions, Mixer.nFFTWPartitionLength()),
#else
ComputationCircularBuffer_(Mixer.nPaths(), SampleBuffer(nPartitions, ChannelBuffer(Mixer.nFFTWPartitionLength()))),
#endif
#else
InputBufferAccumulator_(Mixer.nPartitionLength()),
OutputBuffer_(Mixer.nPartitionLength()), // NB. Actually, only need half partition length for DoPartitionedConvolution
#ifdef ARRAY
ComputationCircularBuffer_(Mixer.nPaths(), nPartitions, Mixer.nPartitionLength()),
#else
ComputationCircularBuffer_(Mixer.nPaths(), SampleBuffer(nPartitions, ChannelBuffer(Mixer.nPartitionLength()))),
#endif
#endif
#ifdef ARRAY
InputBuffer_(Mixer.nInputChannels(), Mixer.nPartitionLength()),
OutputBufferAccumulator_(Mixer.nOutputChannels(), Mixer.nPartitionLength()),
#else
InputBuffer_(Mixer.nInputChannels(), ChannelBuffer(Mixer.nPartitionLength())),
OutputBufferAccumulator_(Mixer.nOutputChannels(), ChannelBuffer(Mixer.nPartitionLength())),
#endif
nInputBufferIndex_(Mixer.nHalfPartitionLength()),
nPartitionIndex_(0),
nPreviousPartitionIndex_(nPartitions-1),
bStartWriting_(false)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "Convolution" << std::endl;)
#endif

		// This should not be necessary, as ChannelBuffer should be zero'd on construction.  But it is not for valarray
		Flush();
}

// Reset various buffers and pointers
template <typename T>
void Convolution<T>::Flush()
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "Convolution<T>::Flush" << std::endl;)
#endif
	Zero(InputBuffer_);
	Zero(InputBufferAccumulator_);
	Zero(ComputationCircularBuffer_);
	Zero(OutputBufferAccumulator_);

	nInputBufferIndex_ = 0; //Mixer.nHalfPartitionLength();// placeholder
	nPartitionIndex_ = 0;							// for partitioned convolution
	nPreviousPartitionIndex_ = nPartitions_ - 1;	// lags nPartitionIndex_ by 1
	bStartWriting_ = false;
}


// This version of the convolution routine does partitioned convolution
template <typename T>
DWORD
Convolution<T>::doPartitionedConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
										 const ConvertSample<T>* input_sample_convertor,	// The functionoid for converting between BYTE* and T
										 const ConvertSample<T>* output_sample_convertor,	// The functionoid for converting between T and BYTE*
										 DWORD dwBlocksToProcess,					// A block contains a sample for each channel
										 const T fAttenuation_db)					// Returns bytes processed
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "Convolution<T>::doPartitionedConvolution" << std::endl;);
#endif
#ifndef FFTW
	// FFTW takes arbitrararily-sized args
	assert(isPowerOf2(Mixer.nPartitionLength()));
#endif

	DWORD cbInputBytesProcessed = 0;
	DWORD cbOutputBytesGenerated = 0;

	const BYTE* pbInputDataPointer = pbInputData;
	BYTE* pbOutputDataPointer = pbOutputData;

	const float fAttenuationFactor = powf(10, fAttenuation_db / 20.0f);

	while (dwBlocksToProcess--)	// Frames to process
	{
		// Channels are stored sequentially in a frame (ie, they are interleaved on the channel)

		if(bStartWriting_)
		{
			for (SampleBuffer::size_type nChannel = 0; nChannel<Mixer.nOutputChannels(); ++nChannel)
			{
				int nDelayedIndex = nInputBufferIndex_ - Mixer.nOutputSamplesDelay()[nChannel];
				if(nDelayedIndex < 0)
				{
					nDelayedIndex += Mixer.nPartitionLength();
				}
				output_sample_convertor->PutSample(pbOutputDataPointer,
					OutputBufferAccumulator_[nChannel][nDelayedIndex],
					nChannel, cbOutputBytesGenerated);
			}
		}

		// Get the next frame from pbInputDataPointer to InputBuffer_
		// Output lags input by half a partition length
#pragma loop count (8)
		for (SampleBuffer::size_type nChannel = 0; nChannel<Mixer.nInputChannels(); ++nChannel)
		{
			// Get the next input sample and convert it to a float, and scale it and delay it circularly
			DWORD nDelayedIndex = nInputBufferIndex_ + Mixer.nInputSamplesDelay()[nChannel];
			if(nDelayedIndex >= Mixer.nPartitionLength())
			{
				nDelayedIndex -= Mixer.nPartitionLength();
			}
			input_sample_convertor->GetSample(InputBuffer_[nChannel][nDelayedIndex],
				pbInputDataPointer, fAttenuationFactor, cbInputBytesProcessed);
		} // nChannel

		// Got a frame

		if (nInputBufferIndex_ == Mixer.nHalfPartitionLength() - 1 ||
			nInputBufferIndex_ == Mixer.nPartitionLength() - 1) // Got half a partition-length's worth of frames
		{		
			if(++nInputBufferIndex_ == Mixer.nPartitionLength())
			{
				nInputBufferIndex_ = 0;
			};

			// Apply the filters

			// Initialize the OutputBufferAccumulator_
#pragma loop count(8)
#pragma ivdep
			for(SampleBuffer::size_type nChannel = 0; nChannel < Mixer.nOutputChannels(); ++nChannel)
			{
				// void Zero(const size_type start, const size_type len)
				OutputBufferAccumulator_[nChannel].Zero(nInputBufferIndex_, Mixer.nHalfPartitionLength());
			}

#pragma loop count (8)
			for (SampleBuffer::size_type nPath = 0; nPath < Mixer.nPaths(); ++nPath)
			{
				// Zero the partition from circular coeffs that we have just used, for the next cycle
				ComputationCircularBuffer_[nPath][nPreviousPartitionIndex_] = 0;

				// Mix the input samples for this filter path
				mix_input(Mixer.Paths()[nPath], InputBuffer_, InputBufferAccumulator_);

				// get DFT of InputBufferAccumulator_
#ifdef FFTW
				fftwf_execute_dft_r2c(Mixer.Paths()[nPath].filter.plan(), InputBufferAccumulator_.c_ptr(),
					reinterpret_cast<fftwf_complex*>(InputBufferAccumulator_.c_ptr()));
#elif defined(SIMPLE_OOURA)
				rdft(Mixer.nPartitionLength(), OouraRForward, InputBufferAccumulator_.c_ptr();
#elif defined(OOURA)
				// TODO: rationalize the ip, w references
				rdft(Mixer.nPartitionLength(), OouraRForward, InputBufferAccumulator_.c_ptr(), 
					&Mixer.Paths()[0].filter.ip[0], &Mixer.Paths()[0].filter.w[0]);
#else
#error "No FFT package defined"
#endif

#pragma loop count(4)
				for (PartitionedBuffer::size_type nPartitionIndex = 0; nPartitionIndex < nPartitions_; ++nPartitionIndex)
				{
					// Complex vector multiplication of InputBufferAccumulator_ and Mixer.Paths()[nPath].filter,
					// added to ComputationCircularBuffer_
#ifdef FFTW
					complex_mul_add(reinterpret_cast<fftwf_complex*>(InputBufferAccumulator_.c_ptr()),
						reinterpret_cast<fftwf_complex*>(c_ptr(Mixer.Paths()[nPath].filter.coeffs(), nPartitionIndex)),
						reinterpret_cast<fftwf_complex*>(c_ptr(ComputationCircularBuffer_, nPath, nPartitionIndex_)),
						Mixer.nPartitionLength());
#elif defined(__ICC) || defined(__INTEL_COMPILER)
					// Vectorizable
					cmuladd(InputBufferAccumulator_.c_ptr(), c_ptr(Mixer.Paths()[nPath].filter.coeffs(), nPartitionIndex),
						c_ptr(ComputationCircularBuffer_, nPath, nPartitionIndex_), Mixer.nPartitionLength());
#else
					// Non-vectorizable
					cmultadd(InputBufferAccumulator_.c_ptr(), c_ptr(Mixer.Paths()[nPath].filter.coeffs(), nPartitionIndex),
						c_ptr(ComputationCircularBuffer_, nPath, nPartitionIndex_), Mixer.nPartitionLength());
#endif
					nPartitionIndex_ = (nPartitionIndex_ + 1) % nPartitions_;	// circular
				} // nPartitionIndex

				//get back the yi: take the Inverse DFT. Not necessary to scale here, as did so when reading filter
#ifdef FFTW
				fftwf_execute_dft_c2r(Mixer.Paths()[nPath].filter.reverse_plan(),
					reinterpret_cast<fftwf_complex*>(c_ptr(ComputationCircularBuffer_, nPath, nPartitionIndex_)),
					c_ptr(ComputationCircularBuffer_, nPath, nPartitionIndex_));
#elif defined(SIMPLE_OOURA)
				rdft(Mixer.nPartitionLength(), OouraRBackward, c_ptr(ComputationCircularBuffer_, nPath, nPartitionIndex_));
#elif defined(OOURA)
				rdft(Mixer.nPartitionLength(), OouraRBackward, c_ptr(ComputationCircularBuffer_, nPath, nPartitionIndex_),
					&Mixer.Paths()[0].filter.ip[0], &Mixer.Paths()[0].filter.w[0]);
#else
#error "No FFT package defined"
#endif
				// Mix the outputs
				mix_output(Mixer.Paths()[nPath], OutputBufferAccumulator_, 
					ComputationCircularBuffer_[nPath][nPartitionIndex_],
					nInputBufferIndex_);

			} // nPath

			// Save the partition to be used for output
			nPreviousPartitionIndex_ = nPartitionIndex_;
			if(++nPartitionIndex_ == nPartitions_)
			{
				nPartitionIndex_ = 0;
			}

			bStartWriting_ = true;
		}
		else
			++nInputBufferIndex_ ;		// get next frame

	} // while

#if defined(DEBUG) | defined(_DEBUG)
	cdebug << "cbInputBytesProcessed: " << cbInputBytesProcessed  << ", cbOutputBytesGenerated: " << cbOutputBytesGenerated << std::endl;
#endif

	return cbOutputBytesGenerated;
}


// This version of the convolution routine is just plain overlap-save.
template <typename T>
DWORD
Convolution<T>::doConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
							  const ConvertSample<T>* input_sample_convertor,		// The functionoid for converting between BYTE* and T
							  const ConvertSample<T>* output_sample_convertor,		// The functionoid for converting between T and BYTE*
							  DWORD dwBlocksToProcess,						// A block contains a sample for each channel
							  const T fAttenuation_db)						// Returns bytes processed
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "Convolution<T>::doConvolution" << std::endl;);
#endif

	// This convolution algorithm assumes that the filter is stored in the first, and only, partition
	if(nPartitions_ != 1)
	{
		throw convolutionException("Internal error: attempted to execute plain overlap-save without using 1 partition");
	}
#ifndef FFTW
	// FFTW does not need powers of 2
	assert(isPowerOf2(Mixer.nPartitionLength()));
#endif
	assert(Mixer.nPartitionLength() == Mixer.nFilterLength());

	DWORD cbInputBytesProcessed = 0;
	DWORD cbOutputBytesGenerated = 0;

	const BYTE* pbInputDataPointer = pbInputData;
	BYTE* pbOutputDataPointer = pbOutputData;

	const float fAttenuationFactor = powf(10, fAttenuation_db / 20.0f);

	while (dwBlocksToProcess--)
	{
		// Channels are stored sequentially in a frame (ie, they are interleaved on the channel)

		if(bStartWriting_)
		{
#pragma loop count(8)
			for (SampleBuffer::size_type nChannel = 0; nChannel<Mixer.nOutputChannels(); ++nChannel)
			{
				int nDelayedIndex = nInputBufferIndex_ - Mixer.nOutputSamplesDelay()[nChannel];
				if(nDelayedIndex < 0)
				{
					nDelayedIndex += Mixer.nPartitionLength();
				}
				output_sample_convertor->PutSample(pbOutputDataPointer,
					OutputBufferAccumulator_[nChannel][nDelayedIndex],
					nChannel, cbOutputBytesGenerated);
			}
		}

		// Get the next frame from pbInputDataPointer to InputBuffer_
#pragma loop count(8)
		for (SampleBuffer::size_type nChannel = 0; nChannel<Mixer.nInputChannels(); ++nChannel)
		{
			// Get the next input sample and convert it to a float, and scale it and delay it circularly
			DWORD nDelayedIndex = nInputBufferIndex_ + Mixer.nInputSamplesDelay()[nChannel];
			if(nDelayedIndex >= Mixer.nPartitionLength())
			{
				nDelayedIndex -= Mixer.nPartitionLength();
			}
			input_sample_convertor->GetSample(InputBuffer_[nChannel][nDelayedIndex],
				pbInputDataPointer, fAttenuationFactor, cbInputBytesProcessed);
		} // nChannel

		// Got a frame

		if (nInputBufferIndex_ == Mixer.nHalfPartitionLength() - 1 ||
			nInputBufferIndex_ == Mixer.nPartitionLength() - 1) // Got half a partition-length's worth of frames
		{
			if(++nInputBufferIndex_ == Mixer.nPartitionLength())
			{
				nInputBufferIndex_ = 0;
			};

			// Apply the filters

			// Initialize the OutputBufferAccumulator_
#pragma loop count(8)
#pragma ivdep
			for(SampleBuffer::size_type nChannel = 0; nChannel < Mixer.nOutputChannels(); ++nChannel)
			{
				// void Zero(const size_type start, const size_type len)
				OutputBufferAccumulator_[nChannel].Zero(nInputBufferIndex_, Mixer.nHalfPartitionLength());
			}

#pragma loop count(8)
			for (SampleBuffer::size_type nPath = 0; nPath < Mixer.nPaths(); ++nPath)
			{
				// Mix the input samples for this filter path into InputBufferAccumulator_
				mix_input(Mixer.Paths()[nPath], InputBuffer_, InputBufferAccumulator_);

				// get DFT of InputBufferAccumulator_
#ifdef FFTW
				fftwf_execute_dft_r2c(Mixer.Paths()[nPath].filter.plan(), InputBufferAccumulator_.c_ptr(),
					reinterpret_cast<fftwf_complex*>(InputBufferAccumulator_.c_ptr()));
#elif defined(SIMPLE_OOURA)
				rdft(Mixer.nPartitionLength(), OouraRForward, InputBufferAccumulator_.c_ptr());
#elif defined(OOURA)
				// TODO: rationalize the ip, w references
				rdft(Mixer.nPartitionLength(), OouraRForward, InputBufferAccumulator_.c_ptr(), 
					&Mixer.Paths()[0].filter.ip[0], &Mixer.Paths()[0].filter.w[0]);
#else
#error "No FFT package defined"
#endif

#ifdef FFTW
				complex_mul(reinterpret_cast<fftwf_complex*>(InputBufferAccumulator_.c_ptr()),
					reinterpret_cast<fftwf_complex*>(c_ptr(Mixer.Paths()[nPath].filter.coeffs())),
					reinterpret_cast<fftwf_complex*>(OutputBuffer_.c_ptr()),
					Mixer.nPartitionLength());
#elif defined(__ICC) || defined(__INTEL_COMPILER)
				// vectorized
				cmul(InputBufferAccumulator_.c_ptr(), 
					c_ptr(Mixer.Paths()[nPath].filter.coeffs()),					// use the first partition and channel only
					OutputBuffer_.c_ptr(), Mixer.nPartitionLength());	
#else
				// Non-vectorizable
				cmult(InputBufferAccumulator_,
					Mixer.Paths()[nPath].filter.coeffs.c_ptr(),					// use the first partition and channel only
					OutputBuffer_, Mixer.nPartitionLength());	
#endif
				//get back the yi: take the Inverse DFT. Not necessary to scale here, as did so when reading filter
#ifdef FFTW
				fftwf_execute_dft_c2r(Mixer.Paths()[nPath].filter.reverse_plan(),
					reinterpret_cast<fftwf_complex*>(OutputBuffer_.c_ptr()),
					OutputBuffer_.c_ptr());
#elif defined(SIMPLE_OOURA)
				rdft(Mixer.nPartitionLength(), OouraRBackward, OutputBuffer_.c_ptr());
#elif defined(OOURA)
				rdft(Mixer.nPartitionLength(), OouraRBackward, OutputBuffer_.c_ptr()),
					&Mixer.Paths()[0].filter.ip[0], &Mixer.Paths()[0].filter.w[0]);
#else
#error "No FFT package defined"
#endif
				// Mix the outputs (only use the last half, as the rest is junk)
				mix_output(Mixer.Paths()[nPath], OutputBufferAccumulator_, OutputBuffer_, nInputBufferIndex_);
			} // nPath

			// Save the partition to be used for output
			nPreviousPartitionIndex_ = nPartitionIndex_;
			if(++nPartitionIndex_ == nPartitions_)
			{
				nPartitionIndex_ = 0;
			}

			bStartWriting_ = true;
		}
		else
			++nInputBufferIndex_;

	} // while

#if defined(DEBUG) | defined(_DEBUG)
	cdebug << "cbInputBytesProcessed: " << cbInputBytesProcessed  << ", cbOutputBytesGenerated: " << cbOutputBytesGenerated << std::endl;
#endif

	return cbOutputBytesGenerated;
}

template <typename T>
void Convolution<T>::mix_input(const ChannelPaths::ChannelPath& restrict thisPath, 
							   const SampleBuffer& restrict InputBuffer,
							   ChannelBuffer& restrict InputBufferAccumulator)
{

	const ChannelPaths::ChannelPath::size_type nChannels = thisPath.inChannel.size();
	const DWORD nPartitionLength = Mixer.nPartitionLength();
	const DWORD nHalfPartitionLength = Mixer.nHalfPartitionLength();

	assert(nPartitionLength % 2 ==0);

	assert(nInputBufferIndex_ == nHalfPartitionLength ||
		nInputBufferIndex_ == 0);

	// NB InputBuffer_ is circular

	if (nChannels == 1 && thisPath.inChannel[0].fScale == 1.0f && nInputBufferIndex_ == 0)
	{
		InputBufferAccumulator = InputBuffer_[thisPath.inChannel[0].nChannel];
	}
	else
	{	
		InputBufferAccumulator = 0;
#pragma loop count(8)
#pragma ivdep
		for(SampleBuffer::size_type nChannel = 0; nChannel < nChannels; ++nChannel)
		{
			const float fScale = thisPath.inChannel[nChannel].fScale;
			const WORD thisChannel = thisPath.inChannel[nChannel].nChannel;
			const ChannelBuffer& InputSamples = InputBuffer[thisChannel];

			// Need to accumulate the whole partition, even though the input buffer contains the previous half-partition
			// because FFTW destroys its inputs.

			// untangle [Xn, Xn-1] and [Xn-1,Xn] -> [Yn-1,Yn]

			ChannelBuffer::const_iterator pInputSamples = InputSamples.begin();
			const ChannelBuffer::const_iterator pInputSamplesEnd = InputSamples.begin() + nInputBufferIndex_;
			ChannelBuffer::iterator pInputBufferAccumulator = InputBufferAccumulator.begin() + nHalfPartitionLength;

			// TODO: Do this by pointers to make it faster
			if(fScale == 1.0f)
			{
				while(pInputSamples != pInputSamplesEnd)
				{
					*pInputBufferAccumulator++ += *pInputSamples++;
				}
				pInputSamples = InputSamples.begin() + nInputBufferIndex_;
				const ChannelBuffer::const_iterator pInputSamplesEnd = InputSamples.begin() + nPartitionLength;
				pInputBufferAccumulator = InputBufferAccumulator.begin();
				while(pInputSamples != pInputSamplesEnd)
				{
					*pInputBufferAccumulator++ += *pInputSamples++;
				}

				//ChannelBuffer::size_type j = nHalfPartitionLength;
				//for(ChannelBuffer::size_type i=0; i<nInputBufferIndex_; ++i)
				//{
				//	InputBufferAccumulator[j++] += InputSamples[i];
				//}
				//j = 0;
				//for(ChannelBuffer::size_type i=nInputBufferIndex_; i<nPartitionLength;++i)
				//{
				//	InputBufferAccumulator[j++] += InputSamples[i];
				//}
			}
			else
			{
				// TODO:: performance killer

				while(pInputSamples != pInputSamplesEnd)
				{
					*pInputBufferAccumulator++ += *pInputSamples++ * fScale;
				}
				pInputSamples = InputSamples.begin() + nInputBufferIndex_;
				const ChannelBuffer::const_iterator pInputSamplesEnd = InputSamples.begin() + nPartitionLength;
				pInputBufferAccumulator = InputBufferAccumulator.begin();
				while(pInputSamples != pInputSamplesEnd)
				{
					*pInputBufferAccumulator++ += *pInputSamples++ * fScale;
				}

				//ChannelBuffer::size_type j = nHalfPartitionLength;
				//for(ChannelBuffer::size_type i=0; i<nInputBufferIndex_; ++i)
				//{
				//	InputBufferAccumulator[j++] += InputSamples[i] * fScale;
				//}
				//j = 0;
				//for(ChannelBuffer::size_type i=nInputBufferIndex_; i<nPartitionLength;++i)
				//{
				//	InputBufferAccumulator[j++] += InputSamples[i] * fScale;
				//}

//				const T* pInputBufferAccumulatorEnd = InputBufferAccumulator.end();
//#pragma loop count(65536)
//#pragma ivdep
//#pragma vector aligned
//#pragma prefetch pInputSamples
//				for(T* pInputBufferAccumulator = InputBufferAccumulator.begin(), *pInputSamples = const_cast<T*>(InputSamples.begin());
//					pInputBufferAccumulator != pInputBufferAccumulatorEnd;)
//				{
//					*pInputBufferAccumulator++ += *pInputSamples++ * fScale;
//					//PreFetchCacheLine(PF_NON_TEMPORAL_LEVEL_ALL, pInputSamples+16);
//					//PreFetchCacheLine(PF_TEMPORAL_LEVEL_1, pInputBufferAccumulator+16);
//				}


				//for(ChannelBuffer::iterator iterInputSamples = const_cast<ChannelBuffer::iterator>(InputSamples.begin()),
				//	iterInputBufferAccumulator = InputBufferAccumulator_.begin();
				//	iterInputSamples != InputSamples.end();)
				//{
				//	*iterInputBufferAccumulator++ += fScale * *iterInputSamples++;
				//}
			}
		}
	}
}

template <typename T>
void Convolution<T>::mix_output(const ChannelPaths::ChannelPath& restrict thisPath, SampleBuffer& restrict Accumulator, 
								const ChannelBuffer& restrict Output, const DWORD to)
{
	// Don't zero the Accumulator as it can accumulate from more than one output
	const ChannelPaths::ChannelPath::size_type nChannels = thisPath.outChannel.size();
	const DWORD nPartitionLength = Mixer.nPartitionLength();
	const DWORD nHalfPartitionLength = Mixer.nHalfPartitionLength();

	assert(to == 0 || to == nHalfPartitionLength);

#pragma loop count(6)
#pragma ivdep
	for(SampleBuffer::size_type nChannel=0; nChannel<nChannels; ++nChannel)
	{
		const float fScale = thisPath.outChannel[nChannel].fScale;
		const WORD thisChannel = thisPath.outChannel[nChannel].nChannel;
		ChannelBuffer& thisAccumulator = Accumulator[thisChannel];

		ChannelBuffer::iterator pAccumulator = thisAccumulator.begin() + to;
		ChannelBuffer::const_iterator pOutput = Output.begin() + nHalfPartitionLength;
		const ChannelBuffer::const_iterator pOutputEnd = Output.begin() + nPartitionLength;

		if(fScale == 1.0f)
		{
			// the output is in the second half of Output; Accumulate to the specified part of thisAccumulator
			while(pOutput != pOutputEnd)
			{
				*pAccumulator++ += *pOutput++;
			}
//		DWORD j = to;
//#pragma loop count(65536)
//#pragma ivdep
//			for(ChannelBuffer::size_type i=nHalfPartitionLength; i < nPartitionLength; ++i)
//			{
//				thisAccumulator[j++] += Output[i];
//			} // i
		}
		else
		{
			while(pOutput != pOutputEnd)
			{
				*pAccumulator++ += *pOutput++ * fScale;
			}
//			DWORD j = to;
//#pragma loop count(65536)
//#pragma ivdep
//			for(ChannelBuffer::size_type i=nHalfPartitionLength; i < nPartitionLength; ++i)
//			{
//				thisAccumulator[j++] += Output[i] * fScale;
//			} // i
		}
	} // nChannel
}

#ifdef FFTW
template <typename T>
void inline Convolution<T>::complex_mul(const fftwf_complex* restrict in1, const fftwf_complex* restrict in2,
										fftwf_complex* restrict result, const ChannelBuffer::size_type count)
{
#pragma ivdep
#pragma loop count (65536)
#pragma vector aligned
	for (ChannelBuffer::size_type index = 0; index < count/2+1; ++index)
	{
		//result[index][0] = in1[index][0] * in2[index][0] - in1[index][1] * in2[index][1];
		//result[index][1] = in1[index][0] * in2[index][1] + in1[index][1] * in2[index][0];

		const __declspec(align( 16 )) float T1 = in1[index][0] * in2[index][0];
		const __declspec(align( 16 )) float T2 = in1[index][1] * in2[index][1];
		result[index][0] = T1 - T2;
		result[index][1] = ((in1[index][0] + in1[index][1]) * (in2[index][0] + in2[index][1])) - (T1 + T2);
	}
}

template <typename T>
void inline Convolution<T>::complex_mul_add(const fftwf_complex* restrict in1, const fftwf_complex* restrict in2,
											fftwf_complex* restrict result, const ChannelBuffer::size_type count)
{
#pragma ivdep
#pragma loop count (65536)
#pragma vector aligned
	for (ChannelBuffer::size_type index = 0; index < count/2+1; ++index)
	{
		//result[index][0] += in1[index][0] * in2[index][0] - in1[index][1] * in2[index][1];
		//result[index][1] += in1[index][0] * in2[index][1] + in1[index][1] * in2[index][0];

		const __declspec(align( 16 )) float T1 = in1[index][0] * in2[index][0];
		const __declspec(align( 16 )) float T2 = in1[index][1] * in2[index][1];
		result[index][0] += T1 - T2;
		result[index][1] += ((in1[index][0] + in1[index][1]) * (in2[index][0] + in2[index][1])) - (T1 + T2);
	}
}

#elif !(defined(__ICC) || defined(__INTEL_COMPILER))
// Non-vectorizable versions

/* Complex multiplication */
template <typename T>
void inline Convolution<T>::cmult(const ChannelBuffer& restrict A, const ChannelBuffer& restrict B, ChannelBuffer& restrict C, const ChannelBuffer::size_type N)
{
	//__declspec(align( 16 )) float T1;
	//__declspec(align( 16 )) float T2;

	// a[2*k] = R[k], 0<=k<n/2
	// a[2*k+1] = I[k], 0<k<n/2
	// a[1] = R[n/2]

	// (R[R[a] + I[a]) * (R[b]+I[b]] = R[a]R[b] - I[a]I[b]
	// (I[R[a] + I[a]) * (R[b]+I[b]] = R[a]I[b] + I[a]R[b]

	// 0<k<n/2
	//								  A[R]*B[R]      - A[I]*B[I]
	// R[R[a] + I[a]) * (R[b]+I[b]] = a[2*k]b[2*k]   - a[2*k+1]b[2*k+1]
	//								  A[R]*B[I]      + A{I]*B[R]
	// I[R[a] + I[a]) * (R[b]+I[b]] = a[2*k]b[2*k+1] + a[2*k+1]b[2*k]

	// tempt the optimizer
	assert(N % 4 == 0);

	C[0] = A[0] * B[0];
	C[1] = A[1] * B[1];
	for(ChannelBuffer::size_type R = 2, I = 3; R < N ;R += 2, I += 2)
	{
		C[R] = A[R]*B[R] - A[I]*B[I];
		C[I] = A[R]*B[I] + A[I]*B[R];
		//T1 = A[R] * B[R];
		//T2 = A[I] * B[I];
		//C[I] = ((A[R] + A[I]) * (B[R] + B[I])) - (T1 + T2);
		//C[R] = T1 - T2;
	}
}


/* Complex multiplication with addition */
template <typename T>
void inline Convolution<T>::cmultadd(const ChannelBuffer& restrict A, const ChannelBuffer& restrict B, ChannelBuffer& restrict C, const ChannelBuffer::size_type N)
{
	//__declspec(align( 16 )) float T1;
	//__declspec(align( 16 )) float T2;

	// a[2*k] = R[k], 0<=k<n/2
	// a[2*k+1] = I[k], 0<k<n/2
	// a[1] = R[n/2]

	// R[R[a] + I[a]) * (R[b]+I[b]] = R[a]R[b] - I[a]I[b]
	// I[R[a] + I[a]) * (R[b]+I[b]] = R[a]I[b] + I[a]R[b]

	// 0<k<n/2
	//								  A[R]*B[R]      - A[I]*B[I]
	// R[R[a] + I[a]) * (R[b]+I[b]] = a[2*k]b[2*k]   - a[2*k+1]b[2*k+1]
	//								  A[R]*B[I]      + A{I]*B[R]
	// I[R[a] + I[a]) * (R[b]+I[b]] = a[2*k]b[2*k+1] + a[2*k+1]b[2*k]

	// tempt the optimizer
	assert(N % 4 == 0);

	C[0] += A[0] * B[0];
	C[1] += A[1] * B[1];

	for (ChannelBuffer::size_type R = 2, I = 3; R < N ;R += 2, I += 2)
	{
		C[R] += A[R]*B[R] - A[I]*B[I];
		C[I] += A[R]*B[I] + A[I]*B[R];
		//T1 = A[R] * B[R];
		//T2 = A[I] * B[I];
		//C[I] += ((A[R] + A[I]) * (B[R] + B[I])) - (T1 + T2);
		//C[R] += T1 - T2;
	}
}

#endif

#if defined(DEBUG) | defined(_DEBUG)
template <typename T>
T Convolution<T>::verify_convolution(const ChannelBuffer& X, const ChannelBuffer& H, const ChannelBuffer& Y, 
									 const ChannelBuffer::size_type from, const ChannelBuffer::size_type to) const
{
	ChannelBuffer y(2 * Mixer.nPartitionLength());
	y = 0;
	// Direct calculation, usint input side algorithm
	for (ChannelBuffer::size_type n = 0; n < Mixer.nPartitionLength(); ++n)
	{
		for (DWORD k = 0; k < Mixer.nPartitionLength(); ++k)
			y[n+k] += H[k] * X[n];
	}

	// Only compare the valid part of the output buffer
	float diff = 0, diff2 = 0;
	for(ChannelBuffer::size_type n = from; n < to; ++n)
	{
		diff = abs(Y[n] - y[n]);
		diff2 += diff * diff;
	}

	//if (diff > 0.001 + 0.01 * (abs(Y[n]) + abs(yn)))
	//	cdebug << n << ": " << Y[n] << ", " << yn << std::endl;

	if(diff2 > 1e-7)
	{
		cdebug << "Bad convolution: ";
		cdebug << "nInputBufferIndex_:" << nInputBufferIndex_ << " nPartitionIndex_:" << nPartitionIndex_ << 
			" PartitionLength: " << Mixer.nPartitionLength() << " from " << from << " to " << to << std:: endl;

		cdebug << "X "; DumpChannelBuffer(X);  cdebug << std::endl;
		cdebug << "H "; DumpChannelBuffer(H);  cdebug << std::endl;
		cdebug << "Y "; DumpChannelBuffer(Y);  cdebug << std::endl;
		cdebug << "y "; DumpChannelBuffer(y);  cdebug << std::endl << std::endl;
	}
	return sqrt(diff);
}
#endif

// Convolve the filter with white noise to get the maximum output, from which we calculate the maximum attenuation
template <typename T>
HRESULT Convolution<T>::calculateOptimumAttenuation(T& fAttenuation, const bool overlapsave)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "calculateOptimumAttenuation" << std::endl;)
#endif
		HRESULT hr = S_OK;

	assert(!overlapsave || nPartitions_ == 1);

	Flush();	// Start from a known point

	const WORD nSamples = NSAMPLES; // length of the test sample, in filter lengths
	// So if you are using a 44.1kHz sample rate and it takes 1s to calculate optimum attenuation,
	// that is equivalent to 0.1s to process a filter
	// which means that the filter must be less than 44100 / .1 samples in length to keep up
	const DWORD nBlocks = nSamples * Mixer.nFilterLength();
	const DWORD nInputBufferLength = nBlocks * Mixer.nInputChannels();	// Channels are interleaved
	const DWORD nOutputBufferLength = nBlocks * Mixer.nOutputChannels();

	std::vector<T>InputSamples(nInputBufferLength);
	std::vector<T>OutputSamples(nOutputBufferLength);

	Holder< ConvertSample<T> > convertor(new ConvertSample_ieeefloat<T>());

	// This is a typedef for a random number generator.
	// Try boost:: minstd_rand or boost::ecuyer1988 instead of boost::mt19937
	typedef boost::lagged_fibonacci607 base_generator_type;

	// Seeded generator
	base_generator_type generator(static_cast<unsigned int>(std::time(NULL)));

	// Define a uniform random number distribution which produces "float"
	// values between -1 and 1 (-1 inclusive, 1 exclusive).
	typedef boost::uniform_real<float> distribution_type;
	typedef boost::variate_generator<base_generator_type&, distribution_type> gen_type;
	distribution_type uni_dist(-1, 1);
	gen_type uni(generator, uni_dist);

	//srand( (unsigned)time( NULL ) );

	bool again = TRUE;
	float maxSample = 0;

	// TODO: don't keep repeating, as this messes up the rate calculation which does not know how many iterations
	// have been used
	//do
	//{

	std::generate_n(InputSamples.begin(), nInputBufferLength, uni);
	//for(DWORD i = 0; i < nInputBufferLength; ++i)
	//{
	////	InputSamples[i] = (2.0f * static_cast<T>(rand()) - static_cast<T>(RAND_MAX)) / static_cast<T>(RAND_MAX); // -1..1
	//	InputSamples[i] = 1.0 / (i / 8.0 + 1.0);  // For testing algorithm
	//}

	std::fill_n(OutputSamples.begin(), nOutputBufferLength, 0.0f);

#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(4, 
		cdebug << "InputSamples (" << nInputBufferLength << ") ";
	std::copy(InputSamples.begin(), InputSamples.end(), std::ostream_iterator<T>(cdebug, " "));
	cdebug << std::endl;);
#endif
	// nPartitions == 0 => use overlap-save version
	DWORD nBytesGenerated = overlapsave && nPartitions_ == 1 ?
		doConvolution(reinterpret_cast<BYTE*>(&InputSamples[0]), reinterpret_cast<BYTE*>(&OutputSamples[0]),
		convertor.get_ptr(), convertor.get_ptr(),
		/* dwBlocksToProcess */ nBlocks,
		/* fAttenuation_db */ 0)
		: doPartitionedConvolution(reinterpret_cast<BYTE*>(&InputSamples[0]), reinterpret_cast<BYTE*>(&OutputSamples[0]),
		convertor.get_ptr(), convertor.get_ptr(),
		/* dwBlocksToProcess */ nBlocks,
		/* fAttenuation_db */ 0);

		// The output will be missing the last half filter length, the first time around
		assert (nBytesGenerated % sizeof(T) == 0);
	assert (nBytesGenerated <= nOutputBufferLength * sizeof(T));

#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(4,
		cdebug << "OutputSamples(" << nBytesGenerated/sizeof(T) << ") " ;
	std::copy(&OutputSamples[0], &OutputSamples[nBytesGenerated/sizeof(T)+1], std::ostream_iterator<T>(cdebug, " "));
	cdebug << std::endl << std::endl;);
#endif

	// Scan the output coeffs for larger output samples
	again = FALSE;
	for(DWORD i = 0; i  < nBytesGenerated / sizeof(T); ++i)
	{
		if (abs(OutputSamples[i]) > maxSample)
		{
			maxSample = abs(OutputSamples[i]);
			again = TRUE; // Keep convolving until find no larger output samples
		}
	}
	//} while (again);

	// maxSample * 10 ^ (fAttenuation_db / 20) = 1
	// Limit fAttenuation to +/-MAX_ATTENUATION dB
	fAttenuation = -20.0f * log10(maxSample);

	if (fAttenuation > MAX_ATTENUATION)
	{
		fAttenuation = MAX_ATTENUATION;
	}
	else if (-fAttenuation > MAX_ATTENUATION)
	{
		fAttenuation = -1.0L * MAX_ATTENUATION;
	}

#if defined(DEBUG) | defined(_DEBUG)
	cdebug << "fAttenuation:" << fAttenuation << " maxSample: " << maxSample << std::endl;
#endif

	Flush(); // Reset, so that residual noise cleared

	return hr;
}

template <typename T>
ConvolutionList<T>::ConvolutionList(const TCHAR szConfigFileName[MAX_PATH], const DWORD& nPartitions, const unsigned int& nPlanningRigour) :
config_(szConfigFileName),
state_(Unselected),
selectedConvolutionIndex_(0),
ConvolutionList_(0),
nConvolutionList_(0),
nPartitions_(nPartitions),
bNeedsUpdating(false)
{
	USES_CONVERSION;

#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "ConvolutionList::ConvolutionList " << T2A(szConfigFileName) << " " << nPartitions << " " << std::endl;);
#endif

	try
	{
#ifdef LIBSNDFILE
		SF_INFO sf_info; ::ZeroMemory(&sf_info, sizeof(sf_info));
		CWaveFileHandle wav(szConfigFileName, SFM_READ, &sf_info, 44100); // Will throw if not a sound file
		// TODO: should not guess raw pcm file sample rate
		// check for pcm file and throw exception

#else
		WAVEFORMATEX wfex; ::ZeroMemory(&wfex, sizeof(WAVEFORMATEX));
		CWaveFileHandle wav;

		if( FAILED(pFilterWave->Open( szConfigFileName, NULL, WAVEFILE_READ )) )
		{
			throw wavfileException("Failed to open WAV file", szConfigFileName, "test open failed");
		}
#endif

		// We have a single sound impulse file, so pick it up
		ConvolutionList_.push_back(new Convolution<T>(szConfigFileName, nPartitions_, nPlanningRigour));
		++nConvolutionList_;
	}
	catch(const wavfileException&)
	{
		// Assume that we have a text config file
		try
		{
			// if the first character is a number, we have a config containing a list of filter paths,
			// rather than config containing list of config files and filter sound files
			std::basic_ifstream<TCHAR>::int_type nextchar = config_().peek();
			if (std::isdigit<TCHAR>(nextchar, std::locale()))
			{
				ConvolutionList_.push_back(new Convolution<T>(szConfigFileName, nPartitions_, nPlanningRigour));
				++nConvolutionList_;
			}
			else
			{
				// we have a config file containing a list of config files / filter sound files
				// TODO:: should do this by unsetting the eof exception bit
				TCHAR szConvolutionListFilename[MAX_PATH];
				szConvolutionListFilename[0] = 0;
				while(!config_().eof())
				{
					try
					{
						szConvolutionListFilename[0] = 0;
						while(szConvolutionListFilename[0] == 0)
						{
							config_().getline(szConvolutionListFilename, MAX_PATH);
						}
					}
					catch(const std::ios_base::failure& error)
					{
						if(!config_().eof())
							throw;
					}
					if(!config_().eof())
					{
#if defined(DEBUG) | defined(_DEBUG)
						cdebug << "Reading ConvolutionList from " << szConvolutionListFilename << std::endl;
#endif
						ConvolutionList_.push_back(new Convolution<T>(szConvolutionListFilename, nPartitions, nPlanningRigour));
						++nConvolutionList_;
					}
				}
			}
		}
		catch(const std::ios_base::failure& error)
		{
			if(config_().eof())
			{
				if(nConvolutionList_ == 0)
				{
					throw convolutionListException("At least one filter path configuration file must be specified. Missing final blank line?", szConfigFileName);
				}
#if defined(DEBUG) | defined(_DEBUG)
				Dump();
#endif
			}
			else if (config_().fail())
			{
				throw convolutionListException("Bad sound file or config file syntax is incorrect", szConfigFileName);
			}
			else if (config_().bad())
			{
				throw convolutionListException("Fatal error opening/reading config file", szConfigFileName);
			}
			else
			{
#if defined(DEBUG) | defined(_DEBUG)
				cdebug << "I/O exception: " << error.what() << std::endl;
#endif
				throw convolutionListException(error.what(), szConfigFileName);
			}
		}
		catch(const convolutionException& ex)	// self-generated exception
		{
			throw convolutionListException(ex.what(), szConfigFileName);
		}
		catch(const std::exception& error)
		{
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "Standard exception: " << error.what() << std::endl;
#endif
			throw convolutionListException(error.what(), szConfigFileName);
		}
		catch (...)
		{
			throw convolutionListException("Unexpected exception", szConfigFileName);
		}
	}
	catch(const std::exception& error)
	{
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "Standard exception: " << error.what() << std::endl;
#endif
		throw convolutionListException(error.what(), szConfigFileName);
	}
	catch (...)
	{
		throw convolutionListException("Unexpected exception", szConfigFileName);
	}

#if defined(DEBUG) | defined(_DEBUG)
	Dump();
#endif

}

template <typename T>
HRESULT ConvolutionList<T>::CheckConvolutionList(const WAVEFORMATEX* pWaveIn, const WAVEFORMATEX* pWaveOut, 
												 bool select /*=false*/)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "ConvolutionList::CheckConvolutionList " << select << std::endl;);
#endif

	// The complexity here is because the calling routines may set/check input and output formats separately, and
	// a pWave of NULL can be used to not set/check

	if(pWaveIn==NULL && pWaveOut==NULL)
	{
		// take the first Path (0), if there is one
		if(nConvolutionList() > 0)
		{
			if(select)
			{
				selectedConvolutionIndex_ = 0;
				state_ = Selected;
			}
			return S_OK;
		}
		else
		{
			if(select)
			{
				state_ = Unselected;
			}
			return DMO_E_TYPE_NOT_ACCEPTED; // No Paths have been set
		}
	}
	else if(pWaveIn==NULL)	// operate on output, leave input alone
	{
		switch(state_)
		{
		case Unselected:
		case OutputSelected:
			{
				// Is there a path with compatible output type?
				for(size_type i = 0; i < nConvolutionList(); ++i)
				{
					if(	ConvolutionList_[i].Mixer.nOutputChannels() == pWaveOut->nChannels &&
						ConvolutionList_[i].Mixer.nSamplesPerSec() == pWaveOut->nSamplesPerSec )
					{
						if(select)
						{
							selectedConvolutionIndex_ = i;
							state_ = OutputSelected;
						}
						return S_OK;
					}
				}
				if(select)
				{
					state_ = Unselected;
				}
				return DMO_E_TYPE_NOT_ACCEPTED;
			}

		case InputSelected:
		case Selected:
			{
				// look for a Path with the same characteristics as the currently
				// selected input path
				const DWORD nInputChannels = ConvolutionList_[selectedConvolutionIndex_].Mixer.nInputChannels();
				const DWORD nSamplesPerSec = ConvolutionList_[selectedConvolutionIndex_].Mixer.nSamplesPerSec();

				for(size_type i = 0; i < nConvolutionList(); ++i)
				{
					if(	ConvolutionList_[i].Mixer.nInputChannels() == nInputChannels &&
						ConvolutionList_[i].Mixer.nOutputChannels() == pWaveOut->nChannels &&
						ConvolutionList_[i].Mixer.nSamplesPerSec() == nSamplesPerSec &&
						ConvolutionList_[i].Mixer.nSamplesPerSec() == pWaveOut->nSamplesPerSec )
					{
						// The currently selected Path is OK
						if(select)
						{
							selectedConvolutionIndex_ = i;
							state_ = Selected;
						}
						return S_OK;
					}
				}
				if(select)
				{
					state_ = InputSelected;
				}
				return DMO_E_TYPE_NOT_ACCEPTED;
			}

		default:
			assert(false);
			return DMO_E_TYPE_NOT_ACCEPTED;
		} // switch
	}
	else if(pWaveOut==NULL)	// operate on input, leave output alone
	{
		switch(state_)
		{
		case Unselected:
		case InputSelected:
			{
				// Is there a path with compatible input type?
				for(size_type i = 0; i < nConvolutionList(); ++i)
				{
					if(	ConvolutionList_[i].Mixer.nInputChannels() == pWaveIn->nChannels &&
						ConvolutionList_[i].Mixer.nSamplesPerSec() == pWaveIn->nSamplesPerSec)
					{
						if(select)
						{
							selectedConvolutionIndex_ = i;
							state_ = InputSelected;
						}
						return S_OK;
					}
				}
				if(select)
				{
					state_ = Unselected;
				}
				return DMO_E_TYPE_NOT_ACCEPTED;
			}

		case OutputSelected:
		case Selected:
			{
				// look for another Path with the same characteristics as the currently
				// selected output path
				const DWORD nOutputChannels = ConvolutionList_[selectedConvolutionIndex_].Mixer.nOutputChannels();
				const DWORD nSamplesPerSec = ConvolutionList_[selectedConvolutionIndex_].Mixer.nSamplesPerSec();

				for(size_type i = 0; i < nConvolutionList(); ++i)
				{
					if(	ConvolutionList_[i].Mixer.nOutputChannels() == nOutputChannels &&
						ConvolutionList_[i].Mixer.nInputChannels() == pWaveIn->nChannels &&
						ConvolutionList_[i].Mixer.nSamplesPerSec() == nSamplesPerSec &&
						ConvolutionList_[i].Mixer.nSamplesPerSec() == pWaveIn->nSamplesPerSec )
					{
						// The currently selected Path is OK
						if(select)
						{
							selectedConvolutionIndex_ = i;
							state_ = Selected;
						}
						return S_OK;
					}
				}
				if(select)
				{
					state_ = OutputSelected;
				}
				return DMO_E_TYPE_NOT_ACCEPTED;
			}

		default:
			assert(false);
			return DMO_E_TYPE_NOT_ACCEPTED;
		} // switch
	}
	else // have both pWaveIn and pWaveOut
	{
		// Is there a path with compatible output type?
		for(size_type i = 0; i < nConvolutionList(); ++i)
		{
			if(ConvolutionList_[i].Mixer.nInputChannels() == pWaveIn->nChannels && 
				ConvolutionList_[i].Mixer.nOutputChannels() == pWaveOut->nChannels &&
				ConvolutionList_[i].Mixer.nSamplesPerSec() == pWaveIn->nSamplesPerSec &&
				ConvolutionList_[i].Mixer.nSamplesPerSec() == pWaveOut->nSamplesPerSec)
			{
				if(select)
				{
					state_ = Selected;
					selectedConvolutionIndex_ = i;
				}
				return S_OK;
			}
		}
		if(select)
		{
			state_ = Unselected;
		}
		return DMO_E_TYPE_NOT_ACCEPTED;
	}
}

template <typename T>
HRESULT ConvolutionList<T>::SelectConvolution(const WAVEFORMATEX* pWaveIn, const WAVEFORMATEX* pWaveOut)
{
	return CheckConvolutionList(pWaveIn, pWaveOut, true);
}

template <typename T>
const std::string ConvolutionList<T>::DisplayConvolutionList() const
{
	std::string result = "";
	if(nConvolutionList_ > 0)
	{
		for(size_type i=1; i<nConvolutionList_ - 1; ++i)
			result += ConvolutionList_[i-1].Mixer.DisplayChannelPaths() + "\n";

		result += ConvolutionList_[nConvolutionList_ - 1].Mixer.DisplayChannelPaths();
	}
	else
	{
		result = "Invalid or empty filter";
	}
	return result;
}


#if defined(DEBUG) | defined(_DEBUG)
template <typename T>
void ConvolutionList<T>::Dump() const
{
	for(ConvolutionList::size_type i = 0; i < nConvolutionList_; ++i)
	{
		cdebug << "Convolution " << i << ":" << std::endl;
		ConvolutionList_[i].Mixer.Dump();
	}
}
#endif
