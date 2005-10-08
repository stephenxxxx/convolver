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

// CConvolution Constructor
CConvolution::CConvolution(TCHAR szConfigFileName[MAX_PATH], const int& nChannels, const int& nPartitions, const int nContainerSize):
Mixer(szConfigFileName, nChannels, nPartitions),
nContainerSize_(nContainerSize), 
InputBuffer_(Mixer.nChannels, ChannelBuffer(Mixer.nPartitionLength)),
InputBufferAccumulator_(ChannelBuffer(Mixer.nPartitionLength)),
OutputBuffer_(ChannelBuffer(Mixer.nPartitionLength)), // NB. Actually, only need half partition length for DoParititionedConvolution
ComputationCircularBuffer_(nPartitions, SampleBuffer(Mixer.Paths.size(), ChannelBuffer(Mixer.nPartitionLength))),
OutputBufferAccumulator_(Mixer.nChannels, ChannelBuffer(Mixer.nPartitionLength)),
nInputBufferIndex_(0),
nPartitionIndex_(0),
nOutputPartitionIndex_(Mixer.nPartitions-1),
bStartWritingOutput_(false)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "CConvolution" << std::endl;)
#endif

		// This should not be necessary, as ChannelBuffer should be zero'd on construction.  But it is not for valarray
		Flush();
}

//CConvolution Destructor
CConvolution::~CConvolution(void)
{
}

// Reset various buffers and pointers
void CConvolution::Flush()
{
	for(int nChannel = 0; nChannel < Mixer.nChannels; ++nChannel)
	{
		InputBuffer_[nChannel] = 0;
	}

	for(int nPath=0; nPath < Mixer.Paths.size(); ++ nPath)
	{
		for (int nPartition = 0; nPartition < Mixer.nPartitions; ++nPartition)
			ComputationCircularBuffer_[nPartition][nPath] = 0;
	}

	nInputBufferIndex_ = 0;							// placeholder
	nPartitionIndex_ = 0;							// for partitioned convolution
	nOutputPartitionIndex_ = Mixer.nPartitions-1;	// lags nPartitionIndex_ by 1
	bStartWritingOutput_ = false;					// don't start outputting until we have some convolved output
}


// This version of the convolution routine does partitioned convolution
DWORD
CConvolution::doPartitionedConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
									   DWORD dwBlocksToProcess, // A block contains a sample for each channel
									   const float fAttenuation_db,
									   const float fWetMix,
									   const float fDryMix)  // Returns bytes processed
{
	assert(isPowerOf2(Mixer.nPartitionLength));

	DWORD cbInputBytesProcessed = 0;
	DWORD cbOutputBytesGenerated = 0;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	const float fAttenuationFactor = powf(10, fAttenuation_db / 20.0f);

	while (dwBlocksToProcess--)	// Frames to process
	{
		// Channels are stored sequentially in a frame (ie, they are interleaved on the channel)

#pragma loop count(6)
		// Get the next frame from pbInputBuffer_ to InputBuffer_, copying the previous frame from OutputBuffer_ to pbOutputData
		for (int nChannel = 0; nChannel<Mixer.nChannels; ++nChannel)
		{
			// Mix the processed signal with the dry signal
			// TODO: remove support for wet/drymix
			float outputSample = InputBuffer_[nChannel][nInputBufferIndex_] * fDryMix + 
				OutputBufferAccumulator_[nChannel][nInputBufferIndex_] * fWetMix;

#if defined(DEBUG) | defined(_DEBUG)
#if defined(DIRAC_DELTA_FUNCTIONS)
			// Only for testing with perfect Dirac Delta filters, where the output is supposed to be the same as the input
			// TODO: not a good test, if one of the values is 0
			if (abs(OutputBufferAccumulator_[nChannel][nInputBufferIndex_] - InputBuffer_[nChannel][nInputBufferIndex_]) >
				0.001 + 0.01 * (abs(OutputBufferAccumulator_[nChannel][nInputBufferIndex_]) + abs(InputBuffer_[nChannel][nInputBufferIndex_])))
			{
				cdebug 
					<< dwBlocksToProcess << ","
					<< nInputBufferIndex_ << ","
					<< nPartitionIndex_ << ", "
					<< nOutputPartitionIndex_ << ", "
					<< InputBuffer_[nChannel][nInputBufferIndex_] << ","
					<< OutputBufferAccumulator_[nChannel][nInputBufferIndex_] << ", "
					<< (OutputBufferAccumulator_[nChannel][nInputBufferIndex_] - InputBuffer_[nChannel][nInputBufferIndex_]) << std::endl;
			}
#endif
#endif
			// Get the input sample and convert it to a float, and scale it
			InputBuffer_[nChannel][nInputBufferIndex_] = fAttenuationFactor * GetSample(pbInputDataPointer);
			pbInputDataPointer += nContainerSize_;
			cbInputBytesProcessed += nContainerSize_;

			// Write to OutputData.  Channels are interleaved in the output
			if(bStartWritingOutput_)
			{
				cbOutputBytesGenerated += NormalizeSample(pbOutputDataPointer, outputSample);
				pbOutputDataPointer += nContainerSize_;
			}
		} // nChannel

		// Got a frame
		if (nInputBufferIndex_ == Mixer.nHalfPartitionLength - 1) // Got half a partition-length's worth of frames
		{	
			// Apply the filter paths

#pragma loop count(2)
			for(int nChannel=0; nChannel<Mixer.nChannels; ++nChannel)
			{
				OutputBufferAccumulator_[nChannel] = 0;
			}

#pragma loop count(6)
			for (int nPath = 0; nPath < Mixer.Paths.size(); ++nPath)
			{
				// Zero the partition from circular buffer that we have just used, for the next cycle
				ComputationCircularBuffer_[nOutputPartitionIndex_][nPath] = 0;

				// Mix the input samples for this filter path
				InputBufferAccumulator_ = 0;
#pragma loop count(6)
				for(int nChannel = 0; nChannel < Mixer.Paths[nPath].inChannel.size(); ++nChannel)
				{
#pragma loop count(65536)
					for(int i=0; i<Mixer.nPartitionLength; ++i)
					{
						InputBufferAccumulator_[i] += InputBuffer_[Mixer.Paths[nPath].inChannel[nChannel].nChannel][i] *
							Mixer.Paths[nPath].inChannel[nChannel].fScale;
					}
				}

				// get DFT of InputBufferAccumulator_
#ifdef OOURA_SIMPLE
				rdft(Mixer.nPartitionLength, OouraRForward, &InputBufferAccumulator_[0]);
#else
				// TODO: rationalize the ip, w references
				rdft(Mixer.nPartitionLength, OouraRForward, &InputBufferAccumulator_[0], 
					&Mixer.Paths[0].filter.ip[0], &Mixer.Paths[0].filter.w[0]);
#endif

#pragma loop count(4)
				for (int nPartitionIndex = 0; nPartitionIndex < Mixer.nPartitions; ++nPartitionIndex)
				{
					// Complex vector multiplication of InputBufferAccumulator_ and Mixer.Paths[nPath].filter,
					// added to ComputationCircularBuffer_
#if (defined(__ICC) || defined(__ICL) || defined(__ECC) || defined(__ECL))
					// Vectorizable
					cmuladd(&InputBufferAccumulator_[0], &Mixer.Paths[nPath].filter.buffer[nPartitionIndex][0][0],
						&ComputationCircularBuffer_[nPartitionIndex_][nPath][0], Mixer.nPartitionLength);
#else
					// Non-vectorizable
					cmultadd(InputBufferAccumulator_, Mixer.Paths[nPath].filter.buffer[nPartitionIndex][0], //TODO: 0=assume only one channel for filter
						ComputationCircularBuffer_[nPartitionIndex_][nPath], Mixer.nPartitionLength);
#endif
					nPartitionIndex_ = (nPartitionIndex_ + 1) % Mixer.nPartitions;	// circular
				} // nPartitionIndex

				//get back the yi: take the Inverse DFT. Not necessary to scale here, as did so when reading filter
#ifdef SIMPLE_OOURA
				rdft(Mixer.nPartitionLength, OouraRBackward, &ComputationCircularBuffer_[nPartitionIndex_][nPath][0]);
#else
				rdft(Mixer.nPartitionLength, OouraRBackward, &ComputationCircularBuffer_[nPartitionIndex_][nPath][0],
					&Mixer.Paths[0].filter.ip[0], &Mixer.Paths[0].filter.w[0]);
#endif
				// Mix the outputs
#pragma loop count(6)
				for(int nChannel=0; nChannel<Mixer.Paths[nPath].outChannel.size(); ++nChannel)
				{
#pragma loop count(65536)
					for(int i=0; i < Mixer.nHalfPartitionLength; ++i)
					{
						OutputBufferAccumulator_[Mixer.Paths[nPath].outChannel[nChannel].nChannel][i] +=
							ComputationCircularBuffer_[nPartitionIndex_][nPath][i] * Mixer.Paths[nPath].outChannel[nChannel].fScale;
					}
				}
			} // nPath

			// Save the previous half partition; the first half will be read in over the next cycle
			// TODO: avoid this copy by rotating the filter when it is read in?
#pragma loop count(6)
			for(int nChannel = 0; nChannel < Mixer.nChannels; ++nChannel)
			{
				InputBuffer_[nChannel].shiftright(Mixer.nHalfPartitionLength);
			}

			// Save the partition to be used for output and move the circular buffer on for the next cycle
			nOutputPartitionIndex_ = nPartitionIndex_;
			nPartitionIndex_ = (nPartitionIndex_ + 1) % Mixer.nPartitions;	// circular
			bStartWritingOutput_ = true;

			nInputBufferIndex_ = 0;		// start to refill the InputBuffer_
		}
		else // keep filling InputBuffer_
			++nInputBufferIndex_;
	} // while

	return cbOutputBytesGenerated;
}


// This version of the convolution routine is just plain overlap-save.
DWORD
CConvolution::doConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
							DWORD dwBlocksToProcess, // A block contains a sample for each channel
							const float fAttenuation_db,
							const float fWetMix,
							const float fDryMix)  // Returns bytes processed
{
	// This convolution algorithm assumes that the filter is stored in the first, and only, partition
	if(Mixer.nPartitions != 1)
	{
		return 0;
	}

	assert(isPowerOf2(Mixer.nPartitionLength));
	assert(Mixer.nPartitionLength == Mixer.nFilterLength);

	DWORD cbInputBytesProcessed = 0;
	DWORD cbOutputBytesGenerated = 0;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	const float fAttenuationFactor = powf(10, fAttenuation_db / 20.0f);

	while (dwBlocksToProcess--)
	{
		// Channels are stored sequentially in a frame (ie, they are interleaved on the channel)

#pragma loop count(6)
		// Get the next frame from pbInputBuffer_ to InputBuffer_, copying the previous frame from OutputBuffer_ to pbOutputData
		for (int nChannel = 0; nChannel<Mixer.nChannels; ++nChannel)
		{
			// Mix the processed signal with the dry signal
			// TODO: remove support for wet/drymix
			float outputSample = InputBuffer_[nChannel][nInputBufferIndex_] * fDryMix + 
				OutputBufferAccumulator_[nChannel][nInputBufferIndex_] * fWetMix;

#if defined(DEBUG) | defined(_DEBUG)
#if defined(DIRAC_DELTA_FUNCTIONS)
			// Only for testing with perfect Dirac Delta filters, where the output is supposed to be the same as the input
			// TODO: not a good test, if one of the values is 0
			if (abs(OutputBufferAccumulator_[nChannel][nInputBufferIndex_] - InputBuffer_[nChannel][nInputBufferIndex_]) >
				0.001 + 0.01 * (abs(OutputBufferAccumulator_[nChannel][nInputBufferIndex_]) + abs(InputBuffer_[nChannel][nInputBufferIndex_])))
			{
				cdebug 
					<< dwBlocksToProcess << ","
					<< nInputBufferIndex_ << ","
					<< nPartitionIndex_ << ", "
					<< nOutputPartitionIndex_ << ", "
					<< InputBuffer_[nChannel][nInputBufferIndex_] << ","
					<< OutputBufferAccumulator_[nChannel][nInputBufferIndex_] << ", "
					<< (OutputBufferAccumulator_[nChannel][nInputBufferIndex_] - InputBuffer_[nChannel][nInputBufferIndex_]) << std::endl;
			}
#endif
#endif

			// Get the input sample and convert it to a float, and scale it
			InputBuffer_[nChannel][nInputBufferIndex_] = fAttenuationFactor * GetSample(pbInputDataPointer);
			pbInputDataPointer += nContainerSize_;
			cbInputBytesProcessed += nContainerSize_;

			// Write to OutputData.  Channels are interleaved in the output
			if(bStartWritingOutput_)
			{
				cbOutputBytesGenerated += NormalizeSample(pbOutputDataPointer, outputSample);
				pbOutputDataPointer += nContainerSize_;
			}
		} // nChannel

		// Got a frame
		if (nInputBufferIndex_ == Mixer.nHalfPartitionLength - 1) // Got half a partition-length's worth of frames
		{
			// Apply the filters

#pragma loop count(6)
			for(int nChannel = 0; nChannel < Mixer.nChannels; ++nChannel)
			{
				OutputBufferAccumulator_[nChannel] = 0;
			}

#pragma loop count(6)
			for (int nPath = 0; nPath < Mixer.Paths.size(); ++nPath)
			{
				// Mix the input samples for this filter
				InputBufferAccumulator_ = 0;
				for(int nChannel = 0; nChannel < Mixer.Paths[nPath].inChannel.size(); ++nChannel)
				{
#pragma loop count(65536)
					for(int i=0; i<Mixer.nPartitionLength; ++i)
					{
						InputBufferAccumulator_[i] += InputBuffer_[Mixer.Paths[nPath].inChannel[nChannel].nChannel][i] *
							Mixer.Paths[nPath].inChannel[nChannel].fScale;
					}
				}

				// get DFT of InputBufferAccumulator_
#ifdef OOURA_SIMPLE
				rdft(Mixer.nPartitionLength, OouraRForward, &InputBufferAccumulator_[0]);
#else
				rdft(Mixer.nPartitionLength, OouraRForward, &InputBufferAccumulator_[0], 
					&Mixer.Paths[0].filter.ip[0], &Mixer.Paths[0].filter.w[0]);
#endif

#if (defined(__ICC) || defined(__ICL) || defined(__ECC) || defined(__ECL))
				// vectorized
				cmul(&InputBufferAccumulator_[0], 
					&Mixer.Paths[nPath].filter.buffer[0][0][0],					// use the first partition and channel only
					&OutputBuffer_[0], Mixer.nPartitionLength);	
#else
				// Non-vectorizable
				cmult(InputBufferAccumulator_,
					Mixer.Paths[nPath].filter.buffer[0][0],					// use the first partition and channel only
					OutputBuffer_, Mixer.nPartitionLength);	
#endif
				//get back the yi: take the Inverse DFT. Not necessary to scale here, as did so when reading filter
#ifdef OOURA_SIMPLE
				rdft(Mixer.nPartitionLength, OouraRBackward, &OutputBuffer_[0]);
#else
				rdft(Mixer.nPartitionLength, OouraRBackward, &OutputBuffer_[0],
					&Mixer.Paths[0].filter.ip[0], &Mixer.Paths[0].filter.w[0]);
#endif
				// Mix the outputs
#pragma loop count(6)
				for(int nChannel=0; nChannel<Mixer.Paths[nPath].outChannel.size(); ++nChannel)
				{
#pragma loop count(65536)
					for(int i=0; i<Mixer.nHalfPartitionLength; ++i)
					{
						OutputBufferAccumulator_[Mixer.Paths[nPath].outChannel[nChannel].nChannel][i] += 
							OutputBuffer_[i] * Mixer.Paths[nPath].outChannel[nChannel].fScale;
					}
				}
			} // nPath

				// Save the previous half partition; the first half will be read in over the next cycle
				// TODO: avoid this copy by rotating the filter when it is read in?
#pragma loop count(6)
			for(int nChannel = 0; nChannel < Mixer.nChannels; ++nChannel)
			{
				InputBuffer_[nChannel].shiftright(Mixer.nHalfPartitionLength);
			}

			nInputBufferIndex_ = 0;
			bStartWritingOutput_ = true;
		}
		else // keep filling InputBuffer_
			++nInputBufferIndex_;
	} // while

	return cbOutputBytesGenerated;
}

#if !(defined(__ICC) || defined(__ICL) || defined(__ECC) || defined(__ECL))
// Non-vectorizable versions

/* Complex multiplication */
void CConvolution::cmult(const ChannelBuffer& restrict A, const ChannelBuffer& restrict B, ChannelBuffer& restrict C, const int N)
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
	for(int R = 2, I = 3; R < N ;R += 2, I += 2)
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
void CConvolution::cmultadd(const ChannelBuffer& restrict A, const ChannelBuffer& restrict B, ChannelBuffer& restrict C, const int N)
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

	for (int R = 2, I = 3; R < N ;R += 2, I += 2)
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

// Pick the correct version of the processing class
//
// TODO: build this into the Factory class.  (Probably more trouble than it is worth)
//
// Note that this allows multi-channel and high-resolution WAVE_FORMAT_PCM and WAVE_FORMAT_IEEE_FLOAT.
// Strictly speaking these should be WAVE_FORMAT_EXTENSIBLE, if we want to avoid ambiguity.
// The code below assumes that for WAVE_FORMAT_PCM and WAVE_FORMAT_IEEE_FLOAT, wBitsPerSample is the container size.
// The code below will not work with streams where wBitsPerSample is used to indicate the bits of actual valid data,
// while the container size is to be inferred from nBlockAlign and nChannels (eg, wBitsPerSample = 20, nBlockAlign = 8, nChannels = 2).
// See http://www.microsoft.com/whdc/device/audio/multichaud.mspx
HRESULT SelectConvolution(WAVEFORMATEX* & pWave, Holder<CConvolution>& convolution, TCHAR szConfigFileName[MAX_PATH], const int nPartitions)
{
	HRESULT hr = S_OK;

	if (NULL == pWave)
	{
		return E_POINTER;
	}

	WORD wFormatTag = pWave->wFormatTag;
	WORD wValidBitsPerSample = pWave->wBitsPerSample;
	int nChannels = pWave->nChannels;
	if (wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		WAVEFORMATEXTENSIBLE* pWaveXT = (WAVEFORMATEXTENSIBLE *) pWave;
		wValidBitsPerSample = pWaveXT->Samples.wValidBitsPerSample;
		if (pWaveXT->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
		{
			wFormatTag = WAVE_FORMAT_PCM;
			// TODO: cross-check pWaveXT->Samples.wSamplesPerBlock
		}
		else
			if (pWaveXT->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
			{
				wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			}
			else
			{
				return E_INVALIDARG;
			}
	}

	try
	{
		switch (wFormatTag)
		{
		case WAVE_FORMAT_PCM:
			switch (pWave->wBitsPerSample)
			{
				// Note: for 8 and 16-bit samples, we assume the sample is the same size as
				// the samples. For > 16-bit samples, we need to use the WAVEFORMATEXTENSIBLE
				// structure to determine the valid bits per sample. (See above)
			case 8:
				convolution = new Cconvolution_pcm8(szConfigFileName, nChannels, nPartitions);
				break;

			case 16:
				convolution = new Cconvolution_pcm16(szConfigFileName, nChannels, nPartitions);
				break;

			case 24:
				{
					switch (wValidBitsPerSample)
					{
					case 16:
						convolution = new Cconvolution_pcm24<16>(szConfigFileName, nChannels, nPartitions);
						break;

					case 20:
						convolution = new Cconvolution_pcm24<20>(szConfigFileName, nChannels, nPartitions);
						break;

					case 24:
						convolution = new Cconvolution_pcm24<24>(szConfigFileName, nChannels, nPartitions);
						break;

					default:
						return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
					}
				}
				break;

			case 32:
				{
					switch (wValidBitsPerSample)
					{
					case 16:
						convolution = new Cconvolution_pcm32<16>(szConfigFileName, nChannels, nPartitions);
						break;

					case 20:
						convolution = new Cconvolution_pcm32<20>(szConfigFileName, nChannels, nPartitions);
						break;

					case 24:
						convolution = new Cconvolution_pcm32<24>(szConfigFileName, nChannels, nPartitions);
						break;

					case 32:
						convolution = new Cconvolution_pcm32<32>(szConfigFileName, nChannels, nPartitions);
						break;

					default:
						return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
					}
				}
				break;

			default:  // Unprocessable PCM
				return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
			}
			break;

		case WAVE_FORMAT_IEEE_FLOAT:
			switch (pWave->wBitsPerSample)
			{
			case 32:
				convolution = new Cconvolution_ieeefloat(szConfigFileName, nChannels, nPartitions);
				break;

			default:  // Unprocessable IEEE float
				return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
				//break;
			}
			break;

		default: // Not PCM or IEEE Float
			return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
		}

		return hr;
	}
	catch(HRESULT hr)
	{
		return hr;
	}
}

// Convolve the filter with white noise to get the maximum output, from which we calculate the maximum attenuation
HRESULT calculateOptimumAttenuation(float& fAttenuation, TCHAR szConfigFileName[MAX_PATH], const int& nChannels, const int& nPartitions)
{
	HRESULT hr = S_OK;

	try 
	{
		Cconvolution_ieeefloat c(szConfigFileName, nChannels, nPartitions == 0 ? 1 : nPartitions); // 0 used to signal use of overlap-save

		const int nSamples = 10; // length of the test sample, in filter lengths
		// So if you are using a 44.1kHz sample rate and it takes 1s to calculate optimum attenuation,
		// that is equivalent to 0.1s to process a filter
		// which means that the filter must be less than 44100 / .1 samples in length to keep up
		const int nBlocks = nSamples * c.Mixer.nFilterLength;
		const int nBufferLength = nBlocks * c.Mixer.nChannels;	// Channels are interleaved

		std::vector<float>InputSamples(nBufferLength);
		std::vector<float>OutputSamples(nBufferLength);

		srand( (unsigned)time( NULL ) );

		bool again = TRUE;
		float maxSample = 0;

		do
		{
			for(int i = 0; i < nBufferLength; ++i)
			{
#ifdef USEMT
				InputSamples[i] = (2.0f * r.mt_random() - r.mt_max()) / r.mt_max(); // -1..1
#if defined(DEBUG) | defined(_DEBUG)
				cdebug << InputSamples[i] << " " ;
#endif
#else
				InputSamples[i] = (2.0f * static_cast<float>(rand()) - static_cast<float>(RAND_MAX)) / static_cast<float>(RAND_MAX); // -1..1
#endif
				OutputSamples[i] = 0;  // silence
			}

#if defined(DEBUG) | defined(_DEBUG)
			//copy(InputSamples.begin(), InputSamples.end(), ostream_iterator<float>(cdebug, ", "));
#endif
			// nPartitions == 0 => use overlap-save version
			DWORD nBytesGenerated = nPartitions == 0 ?
				c.doConvolution(reinterpret_cast<BYTE*>(&InputSamples[0]), reinterpret_cast<BYTE*>(&OutputSamples[0]),
				/* dwBlocksToProcess */ nBlocks,
				/* fAttenuation_db */ 0,
				/* fWetMix,*/ 1.0L,
				/* fDryMix */ 0.0L)
				: c.doPartitionedConvolution(reinterpret_cast<BYTE*>(&InputSamples[0]), reinterpret_cast<BYTE*>(&OutputSamples[0]),
				/* dwBlocksToProcess */ nBlocks,
				/* fAttenuation_db */ 0,
				/* fWetMix,*/ 1.0L,
				/* fDryMix */ 0.0L);

				// The output will be missing the last half filter length, the first time around
				assert (nBytesGenerated % sizeof(float) == 0);
			assert (nBytesGenerated <= nBufferLength * sizeof(float));

			// Scan the output buffer for larger output samples
			again = FALSE;
			for(int i = 0; i  < nBytesGenerated / sizeof(float); ++i)
			{
				if (abs(OutputSamples[i]) > maxSample)
				{
					maxSample = abs(OutputSamples[i]);
					again = TRUE; // Keep convolving until find no larger output samples
				}
			}
		} while (again);

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

		return hr;
	}
	catch (HRESULT& hr) // from CConvolution
	{
#if defined(DEBUG) | defined(_DEBUG)	
		cdebug << "Failed to calculate optimum attenuation (" << std::hex << hr	<< std::dec << ")" << std::endl;
#endif
		return hr;
	}
#if defined(DEBUG) | defined(_DEBUG)
	catch(const std::exception& error)
	{
		cdebug << "Standard exception: " << error.what() << std::endl;
		return E_OUTOFMEMORY;
	}
	catch (const char* what)
	{
		cdebug << "Failed: " << what << std::endl;
		return E_OUTOFMEMORY;
	}
	catch (const TCHAR* what)
	{
		cdebug << "Failed: " << what << std::endl;
		return E_OUTOFMEMORY;
	}
#endif
	catch (...)
	{
#if defined(DEBUG) | defined(_DEBUG)	
		cdebug << "Failed to calculate optimum attenuation.  Unknown exception." << std::endl;
#endif
		return E_OUTOFMEMORY;
	}
}
