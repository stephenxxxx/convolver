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

#include  "convolution.h"
#include  "filter.h"

// CConvolution Constructor
CConvolution::CConvolution(TCHAR szFilterFileName[MAX_PATH], const DWORD nPartitions, const BYTE nContainerSize) :
FIR(Filter(szFilterFileName, nPartitions)),
nContainerSize_(nContainerSize), 
InputBuffer_(SampleBuffer(FIR.nChannels, ChannelBuffer(FIR.nPartitionLength))),
OutputBuffer_(SampleBuffer(FIR.nChannels, ChannelBuffer(FIR.nPartitionLength))), // NB. Actually, only need half partition length for DoParititionedConvolution
InputBufferCopy_(SampleBuffer(FIR.nChannels, ChannelBuffer(FIR.nPartitionLength))),
ComputationCircularBuffer_(PartitionedBuffer(nPartitions, SampleBuffer(FIR.nChannels, ChannelBuffer(FIR.nPartitionLength)))),
MultipliedFFTBuffer_(SampleBuffer(FIR.nChannels, ChannelBuffer(FIR.nPartitionLength))),
nInputBufferIndex_(0),
nPartitionIndex_(0),
nOutputPartitionIndex_(0),
bStartWritingOutput_(false)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "CConvolution" << std::endl;)
#endif

		// This should not be necessary, as ChannelBuffer should be zero'd on construction.  But it is not for valarray
		Flush();
};

// CConvolution Destructor
CConvolution::~CConvolution(void)
{
}

// Reset various buffers and pointers
void CConvolution::Flush()
{
	for(int nChannel = 0; nChannel < FIR.nChannels; ++nChannel)
	{
		InputBuffer_[nChannel] = 0;
		OutputBuffer_[nChannel] = 0;
		MultipliedFFTBuffer_[nChannel] = 0;
		for (int nPartition = 0; nPartition < FIR.nPartitions; ++nPartition)
			ComputationCircularBuffer_[nPartition][nChannel] = 0;
	}

	nInputBufferIndex_ = 0;			// placeholder
	nPartitionIndex_ = 0;			// for partitioned convolution
	nOutputPartitionIndex_ = 0;		// lags nPartitionIndex_ by 1
	bStartWritingOutput_ = false;	// don't start outputting until we have some convolved output
}


// This version of the convolution routine does partitioned convolution
DWORD
CConvolution::doPartitionedConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
									   DWORD dwBlocksToProcess, // A block contains a sample for each channel
									   const double fAttenuation_db,
									   const double fWetMix,
									   const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
									   , CWaveFileHandle & CWaveFileTrace
#endif	
									   )  // Returns bytes processed
{
	assert(isPowerOf2(FIR.nPartitionLength));

	DWORD cbInputBytesProcessed = 0;
	DWORD cbOutputBytesGenerated = 0;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	while (dwBlocksToProcess--)
	{
		for (int nChannel = 0; nChannel < FIR.nChannels; ++nChannel)
		{
			// Mix the processed signal with the dry signal
			double outputSample = InputBuffer_[nChannel][nInputBufferIndex_] * fDryMix + 
				ComputationCircularBuffer_[nOutputPartitionIndex_][nChannel][nInputBufferIndex_] * fWetMix;

#if defined(DEBUG) | defined(_DEBUG)
#if defined(DIRAC_DELTA_FUNCTIONS)
			// Only for testing with perfect Dirac Delta filters, where the output is supposed to be the same as the input
			// TODO: not a good test, if one of the values is 0
			if (abs(ComputationCircularBuffer_[nOutputPartitionIndex_][nChannel][nInputBufferIndex_] - InputBuffer_[nChannel][nInputBufferIndex_]) >
				0.001 + 0.01 * (abs(ComputationCircularBuffer_[nOutputPartitionIndex_][nChannel][nInputBufferIndex_]) + abs(InputBuffer_[nChannel][nInputBufferIndex_])))
			{
				cdebug 
					<< dwBlocksToProcess << ","
					<< nInputBufferIndex_ << ","
					<< nPartitionIndex_ << ", "
					<< nOutputPartitionIndex_ << ", "
					<< nChannel << ", "
					<< InputBuffer_[nChannel][nInputBufferIndex_] << ","
					<< ComputationCircularBuffer_[nOutputPartitionIndex_][nChannel][nInputBufferIndex_] << ", "
					<< (ComputationCircularBuffer_[nOutputPartitionIndex_][nChannel][nInputBufferIndex_] - InputBuffer_[nChannel][nInputBufferIndex_]) << std::endl;
			}
#endif
#endif
			// Get the input sample and convert it to a FFT_type (eg, float)
			InputBuffer_[nChannel][nInputBufferIndex_] = AttenuatedSample(fAttenuation_db, GetSample(pbInputDataPointer)); // Channels are interleaved
			pbInputDataPointer += nContainerSize_;
			cbInputBytesProcessed += nContainerSize_;

			// Write to OutputData
			if(bStartWritingOutput_)
			{
#if defined(DEBUG) | defined(_DEBUG)
				DWORD containerSize = NormalizeSample(pbOutputDataPointer, outputSample);
				assert(containerSize == nContainerSize_);
				cbOutputBytesGenerated += containerSize; // input container size == output container size
#else
				cbOutputBytesGenerated += NormalizeSample(pbOutputDataPointer, outputSample);
#endif

#if defined(DEBUG) | defined(_DEBUG)
				{
					// This does not quite work, because AllocateStreamingResources seems to be called after IEEE FFT_type playback, which rewrites the file
					// It is not clear why this happens, as it does not occur after PCM playback
					UINT nSizeWrote;
					HRESULT hr = CWaveFileTrace->Write(nContainerSize_, pbOutputDataPointer, &nSizeWrote);
					if (FAILED(hr) || nSizeWrote != nContainerSize_)
					{
						return DXTRACE_ERR_MSGBOX(TEXT("Failed to write to trace file"), hr);
					}
				}
#endif
				pbOutputDataPointer += nContainerSize_;
			}
		} // nChannel

		// Got a block
		if (nInputBufferIndex_ == FIR.nHalfPartitionLength - 1) // Got half a partition-length's worth
		{	

			//cdebug << "InputBuffer_: " << nInputBufferIndex_; DumpSampleBuffer(InputBuffer_); cdebug << std::endl;

			for (int nChannel = 0; nChannel < FIR.nChannels; ++nChannel)
			{
				// Copy the sample buffer partition as the rdft routine overwrites it
				InputBufferCopy_[nChannel] = InputBuffer_[nChannel];

				// get DFT of InputBuffer_
				rdft(FIR.nPartitionLength, OouraRForward, &InputBufferCopy_[nChannel][0]);

				// Zero the partition from circular buffer that we have just used, for the next cycle
				ComputationCircularBuffer_[nOutputPartitionIndex_][nChannel] = 0;

				for (int nPartitionIndex = 0; nPartitionIndex < FIR.nPartitions; ++nPartitionIndex)
				{
					// Complex vector multiplication
					cmult(InputBufferCopy_[nChannel], FIR.buffer[nPartitionIndex][nChannel],
						MultipliedFFTBuffer_[nChannel], FIR.nPartitionLength);	

					ComputationCircularBuffer_[nPartitionIndex_][nChannel] += MultipliedFFTBuffer_[nChannel];
					nPartitionIndex_ = (nPartitionIndex_ + 1) % FIR.nPartitions;	// circular
				}

				//get back the yi: take the Inverse DFT
				rdft(FIR.nPartitionLength, OouraRBackward, &ComputationCircularBuffer_[nPartitionIndex_][nChannel][0]);
				// Not necessary to scale here, as did so when reading filter

				// Save the previous half partition; the first half will be read in over the next cycle
				::CopyMemory(&InputBuffer_[nChannel][FIR.nHalfPartitionLength], &InputBuffer_[nChannel][0],
					FIR.nHalfPartitionLength * sizeof(float));
				InputBuffer_[nChannel].shiftright(FIR.nHalfPartitionLength);

			} // nChannel

			// Save the partition to be used for output and move the circular buffer on for the next cycle
			nOutputPartitionIndex_ = nPartitionIndex_;
			nPartitionIndex_ = (nPartitionIndex_ + 1) % FIR.nPartitions;	// circular

			//cdebug << "nOutputPartitionIndex_, nPartitionIndex_: " << nOutputPartitionIndex_ << " " << nPartitionIndex_ << " ";
			//DumpPartitionedBuffer(ComputationCircularBuffer_); cdebug << std::endl;

			nInputBufferIndex_ = 0;		// start to refill the InputBuffer_
			bStartWritingOutput_ = true;
		}
		else // keep filling InputBuffer_
			nInputBufferIndex_++;
	} // while

	//#if defined(DEBUG) | defined(_DEBUG)
	//	cdebug
	//		<< "nInputBufferIndex_ " << nInputBufferIndex_
	//		<< ", bytes processed: " << cbInputBytesProcessed << " bytes generated: " << cbOutputBytesGenerated << std::endl;
	//#endif

	return cbOutputBytesGenerated;
};


// This version of the convolution routine is just plain overlap-save.
DWORD
CConvolution::doConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
							DWORD dwBlocksToProcess, // A block contains a sample for each channel
							const double fAttenuation_db,
							const double fWetMix,
							const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
							, CWaveFileHandle & CWaveFileTrace
#endif	
							)  // Returns bytes processed
{
	// This convolution algorithm assumes that the filter is stored in the first, and only, partition
	if(FIR.nPartitions != 1)
	{
		return 0;
	}

	assert(isPowerOf2(FIR.nPartitionLength));

	DWORD cbInputBytesProcessed = 0;
	DWORD cbOutputBytesGenerated = 0;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	while (dwBlocksToProcess--)
	{
		for (int nChannel = 0; nChannel < FIR.nChannels; ++nChannel)
		{
			// Mix the processed signal with the dry signal
			// This will be silence, until we have gathered a filter length a convolved it to produce some output. Hence with lag
			double outputSample = (InputBuffer_[nChannel][nInputBufferIndex_] * fDryMix ) + 
				(OutputBuffer_[nChannel][nInputBufferIndex_] * fWetMix);

#if defined(DEBUG) | defined(_DEBUG)
//#if defined(DIRAC_DELTA_FUNCTIONS)
			// Only for testing with perfect Dirac Delta filters, where the output is supposed to be the same as the input
			// TODO: not a good test, if one of the values is 0
			if (abs(OutputBuffer_[nChannel][nInputBufferIndex_] - InputBuffer_[nChannel][nInputBufferIndex_]) >
				0.001 + 0.01 * (abs(OutputBuffer_[nChannel][nInputBufferIndex_]) + abs(InputBuffer_[nChannel][nInputBufferIndex_])))
			{
				cdebug 
					<< dwBlocksToProcess << ","
					<< nInputBufferIndex_ << ","
					<< nPartitionIndex_ << ", "
					<< nChannel << ", "
					<< InputBuffer_[nChannel][nInputBufferIndex_] << ","
					<< OutputBuffer_[nChannel][nInputBufferIndex_] << ", "
					<< (OutputBuffer_[nChannel][nInputBufferIndex_] - InputBuffer_[nChannel][nInputBufferIndex_]) << std::endl;
			}
//#endif
#endif

			// Get the input sample and convert it to a FFT_type (eg, float)
			InputBuffer_[nChannel][nInputBufferIndex_] = AttenuatedSample(fAttenuation_db, GetSample(pbInputDataPointer)); // Channels are interleaved
			pbInputDataPointer += nContainerSize_;
			cbInputBytesProcessed += nContainerSize_;

			// Write to OutputData
			if(bStartWritingOutput_)
			{
#if defined(DEBUG) | defined(_DEBUG)
				DWORD containerSize = NormalizeSample(pbOutputDataPointer, outputSample);
				assert(containerSize == nContainerSize_);
				cbOutputBytesGenerated += containerSize; // input container size == output container size
#else
				cbOutputBytesGenerated += NormalizeSample(pbOutputDataPointer, outputSample);
#endif

#if defined(DEBUG) | defined(_DEBUG)
				{
					// This does not quite work, because AllocateStreamingResources seems to be called after IEEE FFT_type playback, which rewrites the file
					// It is not clear why this happens, as it does not occur after PCM playback
					UINT nSizeWrote;
					HRESULT hr = CWaveFileTrace->Write(nContainerSize_, pbOutputDataPointer, &nSizeWrote);
					if (FAILED(hr) || nSizeWrote != nContainerSize_)
					{
						return DXTRACE_ERR_MSGBOX(TEXT("Failed to write to trace file"), hr);
					}
				}
#endif
				pbOutputDataPointer += nContainerSize_;
			}
		}

		// Got a block
		if (nInputBufferIndex_ == FIR.nHalfPartitionLength - 1) // Got half a partition-length's worth
		{	
			for (int nChannel = 0; nChannel < FIR.nChannels; ++nChannel)
			{
				// Copy the sample buffer partition as the rdft routine overwrites it

				InputBufferCopy_[nChannel] = InputBuffer_[nChannel];

				rdft(FIR.nPartitionLength, OouraRForward, &InputBufferCopy_[nChannel][0]);  // get DFT of InputBuffer_

				cmult(InputBufferCopy_[nChannel], FIR.buffer[0][nChannel],					// use the first partition only
					OutputBuffer_[nChannel], FIR.nPartitionLength);	

				//get back the yi: take the Inverse DFT
				rdft(FIR.nPartitionLength, OouraRBackward, &OutputBuffer_[nChannel][0]);
				// Not necessary to scale here, as did so when reading filter

				// Save the previous half partition; the first half will be read in over the next cycle
				::CopyMemory(&InputBuffer_[nChannel][FIR.nHalfPartitionLength], &InputBuffer_[nChannel][0],
					FIR.nHalfPartitionLength * sizeof(float));
			}
			nInputBufferIndex_ = 0;
			bStartWritingOutput_ = true;
		}
		else // keep filling InputBuffer_
			nInputBufferIndex_++;
	} // while

	//#if defined(DEBUG) | defined(_DEBUG)
	//	cdebug
	//		<< "nInputBufferIndex_ " << nInputBufferIndex_
	//		<< ", bytes processed: " << cbInputBytesProcessed << " bytes generated: " << cbOutputBytesGenerated << std::endl;
	//#endif

	return cbOutputBytesGenerated;
};

/* Complex multiplication */
#ifdef VALARRAY
inline void CConvolution::cmult(const std::valarray<float>& A, const std::valarray<float>& B, std::valarray<float>& C, const DWORD N)
#endif
#ifdef FASTARRAY
inline void CConvolution::cmult(const fastarray<float>& A, const fastarray<float>& B, fastarray<float>& C, const DWORD N)
#endif
{
	int R;
	int I;
	__declspec(align( 16 )) float T1;
	__declspec(align( 16 )) float T2;

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
	__assume(N % 4 == 0);

	C[0] = A[0] * B[0];
	C[1] = A[1] * B[1];
	for (R = 2,I = 3;R < N;R += 2,I += 2)
	{
		//C[R] = A[R]*B[R] - A[I]*B[I];
		//C[I] = A[R]*B[I] + A[I]*B[R];
		T1 = A[R] * B[R];
		T2 = A[I] * B[I];
		C[I] = ((A[R] + A[I]) * (B[R] + B[I])) - (T1 + T2);
		C[R] = T1 - T2;
	}
};


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
HRESULT SelectConvolution(WAVEFORMATEX* & pWave, CConvolution* & convolution, TCHAR szFilterFileName[MAX_PATH], const DWORD nPartitions)
{
	HRESULT hr = S_OK;

	if (NULL == pWave)
	{
		return E_POINTER;
	}

	WORD wFormatTag = pWave->wFormatTag;
	WORD wValidBitsPerSample = pWave->wBitsPerSample;
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

	//	delete convolution;

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
				convolution = new Cconvolution_pcm8(szFilterFileName, nPartitions);
				break;

			case 16:
				convolution = new Cconvolution_pcm16(szFilterFileName, nPartitions);
				break;

			case 24:
				{
					switch (wValidBitsPerSample)
					{
					case 16:
						convolution = new Cconvolution_pcm24<16>(szFilterFileName, nPartitions);
						break;

					case 20:
						convolution = new Cconvolution_pcm24<20>(szFilterFileName, nPartitions);
						break;

					case 24:
						convolution = new Cconvolution_pcm24<24>(szFilterFileName, nPartitions);
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
						convolution = new Cconvolution_pcm32<16>(szFilterFileName, nPartitions);
						break;

					case 20:
						convolution = new Cconvolution_pcm32<20>(szFilterFileName, nPartitions);
						break;

					case 24:
						convolution = new Cconvolution_pcm32<24>(szFilterFileName, nPartitions);
						break;

					case 32:
						convolution = new Cconvolution_pcm32<32>(szFilterFileName, nPartitions);
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
				convolution = new Cconvolution_ieeefloat(szFilterFileName, nPartitions);
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
};

// Convolve the filter with white noise to get the maximum output, from which we calculate the maximum attenuation
HRESULT calculateOptimumAttenuation(double& fAttenuation, TCHAR szFilterFileName[MAX_PATH], const DWORD nPartitions)
{
	HRESULT hr = S_OK;

	try 
	{
		Cconvolution_ieeefloat c(szFilterFileName, nPartitions == 0 ? 1 : nPartitions); // 0 used to signal use of overlap-save

		const WORD nSamples = 5; // length of the test sample, in filter lengths
		const DWORD nBlocks = nSamples * c.FIR.nFilterLength;
		const DWORD nBufferLength = nBlocks * c.FIR.nChannels;	// Channels are interleaved

#if defined(DEBUG) | defined(_DEBUG)
		CWaveFileHandle CWaveFileTrace;
		if (FAILED(hr = CWaveFileTrace->Open(TEXT("c:\\temp\\AttenuationTrace.wav"), &c.FIR.wfexFilterFormat.Format, WAVEFILE_WRITE)))
		{
			return DXTRACE_ERR_MSGBOX(TEXT("Failed to open trace file for writing"), hr);
		}
#endif

		std::vector<float>InputSamples(nBufferLength);
		std::vector<float>OutputSamples(nBufferLength);

		// Seed the random-number generator with current time so that
		// the numbers will be different every time we run.
		srand( (unsigned)time( NULL ) );

		bool again = TRUE;
		float maxSample = 0;

		do
		{
			for(int i = 0; i < nBufferLength; i++)
			{
				InputSamples[i] = static_cast<float>((2.0 * rand() - RAND_MAX) / RAND_MAX); // -1..1
				OutputSamples[i] = 0;  // silence
			}

			// nPartitions == 0 => use overlap-save version
			DWORD nBytesGenerated = nPartitions == 0 ?
				c.doConvolution(reinterpret_cast<BYTE*>(&InputSamples[0]), reinterpret_cast<BYTE*>(&OutputSamples[0]),
				/* dwBlocksToProcess */ nBlocks,
				/* fAttenuation_db */ 0,
				/* fWetMix,*/ 1.0L,
				/* fDryMix */ 0.0L
#if defined(DEBUG) | defined(_DEBUG)
				, CWaveFileTrace
#endif
				)
				: c.doPartitionedConvolution(reinterpret_cast<BYTE*>(&InputSamples[0]), reinterpret_cast<BYTE*>(&OutputSamples[0]),
				/* dwBlocksToProcess */ nBlocks,
				/* fAttenuation_db */ 0,
				/* fWetMix,*/ 1.0L,
				/* fDryMix */ 0.0L
#if defined(DEBUG) | defined(_DEBUG)
				, CWaveFileTrace
#endif
				);
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
		return hr;
	}
	catch (...)
	{
		return E_OUTOFMEMORY;
	}
}

