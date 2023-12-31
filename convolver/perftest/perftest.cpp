// perftest.cpp	: Defines the entry	point for the console application.
//

#include "stdafx.h"
#include "convolution\config.h"

#ifdef LIBSNDFILE
#include "libsndfile\sndfile.h"
#else
#include "Common\dxstdafx.h"
#endif
#if defined(DEBUG) || defined(_DEBUG)
#include "debugging\debugging.h"
#endif
#include "debugging\fasttiming.h"
#include "convolution\wavefile.h"
#include "convolution\convolution.h"
#include <iostream>
#include <sstream>

#ifdef MINGW_FFTW
// For MinGW-compile FFTW, which does not do its own initialization
static void my_fftwf_write_char(char c, void *f) { fputc(c, (FILE *) f); }
#define fftwf_export_wisdom_to_file(f) fftwf_export_wisdom(my_fftwf_write_char, (void*) (f))

static int my_fftwf_read_char(void *f) { return fgetc((FILE *) f); } 
#define fftwf_import_wisdom_from_file(f) fftwf_import_wisdom(my_fftwf_read_char, (void*) (f))
#endif


int	_tmain(int argc, _TCHAR* argv[])
{
#if defined(DEBUG) | defined(_DEBUG)
	const int	mode =   (1 * _CRTDBG_MODE_DEBUG) | (0 * _CRTDBG_MODE_WNDW);
	::_CrtSetReportMode (_CRT_WARN, mode);
	::_CrtSetReportMode (_CRT_ERROR, mode);
	::_CrtSetReportMode (_CRT_ASSERT, mode);
	const int	old_flags = ::_CrtSetDbgFlag (_CRTDBG_REPORT_FLAG);
	::_CrtSetDbgFlag (  old_flags
		| (1 * _CRTDBG_LEAK_CHECK_DF)
		| (1 * _CRTDBG_CHECK_ALWAYS_DF)
		| (1 * _CRTDBG_ALLOC_MEM_DF));

	// 3 = function call trace
	apDebug::gOnly().set(4);

	debugstream.sink (apDebugSinkConsole::sOnly);


	_clear87();
	cdebug << std::hex <<_control87(_EM_INVALID | _EM_DENORMAL | _EM_ZERODIVIDE | _EM_OVERFLOW | _EM_UNDERFLOW | _EM_INEXACT, _MCW_EM ) << std::dec << std::endl;

	_clearfp();
	cdebug << std::hex <<_controlfp(_EM_INVALID | _EM_DENORMAL | _EM_ZERODIVIDE | _EM_OVERFLOW | _EM_UNDERFLOW | _EM_INEXACT, _MCW_EM ) << std::dec << std::endl;


#endif

	HRESULT	hr = S_OK;
	double fElapsedLoad	= 0;
	double fTotalElapsedLoad = 0;
	double fElapsedCalc = 0;
	double fTotalElapsedCalc = 0;
	apHiResElapsedTime t;

	//CWaveFileHandle	FilterWav;

	if (argc !=	5)
	{
		std::wcerr << "Usage: perftest MaxnPartitions nIterations nTuningRigour config.txt|IR.wav" << std::endl;
		return 1;
	}

	try
	{

#ifdef MINGW_FFTW
// For MinGW-compile FFTW, which does not do its own initialization
#define WISDOM_FILENAME	"wisdom.fftw"

		if (fftwf_init_threads() == 0)
			return FALSE;	// failed to initialize threads

		FILE* wisdom = fopen (WISDOM_FILENAME,"r");
		if (wisdom!=NULL)
		{
			if(fftwf_import_wisdom_from_file(wisdom) != 1)
			{
				std::wcerr << "Failed to import wisdom" << std::endl;
			}
			fclose (wisdom);
		}
#endif

		std::wistringstream	szPartitions(argv[1]);
		unsigned int max_nPartitions;	// Can't use WORD as that seems to break >>
		szPartitions >>	max_nPartitions;
		if (max_nPartitions	< 0 || max_nPartitions > 1000)
		{
			std::cerr << "max_nPartitions (" << max_nPartitions << ") must be between 0 and 1000" << std::endl;
			throw(std::length_error("invalid max_nPartitions"));
		}

		std::wistringstream	szIterations(argv[2]);
		int nIterations;
		szIterations >>	nIterations;
		if (nIterations	< 1 || nIterations > 1000)
		{
			std::wcerr << "nIterations (" << nIterations << ") must be between 1 and 1000" << std::endl;
			throw(std::length_error("invalid nIterations"));
		}

		//PlanningRigour pr;
		std::wistringstream	szPlanningRigour(argv[3]);
		unsigned int nPlanningRigour;
		szPlanningRigour >>	nPlanningRigour;
		if (nPlanningRigour	< 0 || nPlanningRigour > PlanningRigour::nDegrees - 1)
		{
			std::wcerr << "nTuningRigour (" << nPlanningRigour << ") must be between 0 and " << PlanningRigour::nDegrees - 1 << std::endl;
			throw(std::length_error("invalid nTuningRigour"));
		}


		//#ifdef LIBSNDFILE
		//		SF_INFO sfinfo;
		//		ZeroMemory(&sfinfo, sizeof(SF_INFO));
		//		CWaveFileHandle FilterWav(argv[4], SFM_READ, &sfinfo);
		//		std::cerr << waveFormatDescription(sfinfo, 	"Filter file format: ") << std::endl;
		//#else
		//		std::basic_string< _TCHAR >FilterFileName(argv[4],	_tcslen(argv[4]));
		//		if(	FAILED(	hr = FilterWav->Open( argv[4], NULL, WAVEFILE_READ ) ) )
		//		{
		//			std::wcerr << "Failed to open "	<< FilterFileName << " for reading"	<< std::endl;
		//			throw(hr);
		//		}
		//
		//		WAVEFORMATEX *pWave	= FilterWav->GetFormat();
		//		std::cerr << waveFormatDescription(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pWave), 
		//			FilterWav->GetSize() / FilterWav->GetFormat()->nBlockAlign,	"Filter file format: ") << std::endl;
		//#endif

		ConvolutionList<float> conv(argv[4], 1, nPlanningRigour);
		std::cerr << conv.DisplayConvolutionList() << std::endl;

		float fAttenuation	= 0;

		for(int i=0; i<conv.nConvolutionList(); ++i)
		{

			conv.selectConvolutionIndex(i);
			std::cerr << std::endl << conv.SelectedConvolution().Mixer.DisplayChannelPaths();

#ifdef LIBSNDFILE
			std::cout << std::endl << "Partitions\tRate\tSecCalc\tSecLoad\tAttenuation\tFilter Length\tPartition Length\tIteration" << std::endl;
#else
			std::cout << std::endl << "Partitions\tSecCalc\tSecLoad\tAttenuation\tFilter Length\tPartition Length\tIteration" << std::endl;
#endif

			fTotalElapsedLoad = 0;
			fTotalElapsedCalc = 0;

			for (WORD nPartitions = 0; nPartitions <= max_nPartitions; ++nPartitions)
			{
				for (WORD nIteration = 1; nIteration<=nIterations; ++nIteration)
				{
					t.reset();
					ConvolutionList<float> convp(argv[4], 
						nPartitions == 0 ? 1 : nPartitions, 
						nPlanningRigour); // Used to calculate nPartitionLength
					fElapsedLoad = t.sec();
					fTotalElapsedLoad += fElapsedLoad;

					convp.selectConvolutionIndex(i);

					t.reset();
					hr = convp.SelectedConvolution().calculateOptimumAttenuation(fAttenuation, nPartitions == 0);
					fElapsedCalc = t.sec();
					fTotalElapsedCalc += fElapsedCalc;

					if (FAILED(hr))
					{
						std::wcerr << "Failed to calculate optimum attenuation (" << std::hex << hr	<< std::dec << ")" << std::endl;
						throw (hr);
					}
					std::cout  << std::setprecision(3) << nPartitions << "\t"
#ifdef LIBSNDFILE
						<< (static_cast<float>(convp.SelectedConvolution().Mixer.Paths()[0].filter.sf_FilterFormat().frames * NSAMPLES) / fElapsedCalc) /
						static_cast<float>(convp.SelectedConvolution().Mixer.Paths()[0].filter.sf_FilterFormat().samplerate) << "\t"
#endif
						<< fElapsedCalc << "\t" << fElapsedLoad << "\t" 
						<< fAttenuation << "\t" << convp.SelectedConvolution().Mixer.nFilterLength() << "\t" 
						<< convp.SelectedConvolution().Mixer.nPartitionLength() << "\t" << nIteration  << std::endl;
				}
			}

			std::cout << std::endl << "Total load time: " << fTotalElapsedLoad  << "s" << std::endl;
			std::cout << "Total execution time: " << fTotalElapsedCalc << "s" << std::endl;

		}

#ifdef MINGW_FFTW
// For MinGW-compile FFTW, which does not do its own initialization
		wisdom = fopen (WISDOM_FILENAME,"w");
		if (wisdom!=NULL)
		{
			fftwf_export_wisdom_to_file(wisdom);
			fflush(wisdom);		// needed as WIN32 fclose does not do it as it should
			fclose (wisdom);
		}
		else
		{
			std::wcerr << "Failed to export wisdom" << std::endl;
		}
		fftwf_cleanup_threads();
#endif

	}

	catch (convolutionException& error)
	{
		std::wcerr << "Convolver error: " << error.what() << std::endl;
	}
	catch (HRESULT& hr) // from Convolution
	{
		std::wcerr << "Failed to calculate optimum attenuation (" << std::hex << hr	<< std::dec << ")" << std::endl;
		return hr;
	}
	catch(const std::exception& error)
	{
		std::wcerr << "Standard exception: " << error.what() << std::endl;
		return E_OUTOFMEMORY;
	}
	catch (const char* what)
	{
		std::wcerr << "Failed: " << what << std::endl;
		return E_OUTOFMEMORY;
	}
	catch (const TCHAR* what)
	{
		std::wcerr << "Failed: " << what << std::endl;
		return E_OUTOFMEMORY;
	}
	catch (...)
	{
		std::wcerr << "Failed." <<std::endl;
	}

#if defined(DEBUG) | defined(_DEBUG)
	_CrtDumpMemoryLeaks();
#endif

	return hr;

}
