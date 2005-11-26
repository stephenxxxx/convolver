#ifndef WINVER
#define WINVER         0x0410
#endif
#ifndef _WIN32_WINDOWS
#define _WIN32_WINDOWS 0x0410 
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT   0x0500 
#endif
#include <windows.h>
#include <windowsx.h>
#include <streams.h>
#include <commctrl.h>
#include <olectl.h>
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <tchar.h>
#include "resource.h"
#include <string>
#include "iconvolverFilter.h"
#include "convolverFilter.h"
#include "convolverFilterprop.h"
#include "debugging\fastTiming.h"
#include "convolverWMP\version.h"

//
// CreateInstance
//
// Used by the DirectShow base classes to create instances
//
CUnknown *CconvolverFilterProperties::CreateInstance(LPUNKNOWN lpunk, HRESULT *phr)
{
	CUnknown *punk = new CconvolverFilterProperties(lpunk, phr);
	if (punk == NULL) {
		*phr = E_OUTOFMEMORY;
	}
	return punk;

}

//
// Constructor
//
CconvolverFilterProperties::CconvolverFilterProperties(LPUNKNOWN pUnk, HRESULT *phr) :
CBasePropertyPage(NAME("convolverFilter Property Page"),
				  pUnk,IDD_convolverFilterPROP,IDS_TITLE),
				  m_pIconvolverFilter(NULL),
				  m_bIsInitialized(FALSE)
{
	ASSERT(phr);
}

HRESULT CconvolverFilterProperties::DisplayFilterFormat(HWND hwnd, TCHAR* szFilterFileName)
{
	HRESULT hr = ERROR_SUCCESS;

	SetDlgItemText( hwnd, IDC_CONFIGFILE, szFilterFileName );

	if (m_pIconvolverFilter)
	{
		try
		{
			std::string description = "";
			hr = m_pIconvolverFilter->get_filter_description(&description);
			if (FAILED(hr))
			{
				SetDlgItemText( hwnd, IDS_STATUS, TEXT("No filter set"));
				return hr;
			}
			SetDlgItemText( hwnd, IDS_STATUS, CA2CT(description.c_str()));
		}
		catch (std::exception& error)
		{
			SetDlgItemText( hwnd, IDS_STATUS, CA2CT(error.what()));
			hr = E_FAIL;
		}
		catch (...) // creating m_Convolution might throw
		{

			SetDlgItemText( hwnd, IDS_STATUS, TEXT("Failed to load filter.") );
			hr = E_FAIL;
		}
	}
	else
	{
		SetDlgItemText( hwnd, IDS_STATUS, TEXT("Convolution filter initialization failed.") );
	}

	return hr;
}

//
// OnReceiveMessage
//
// Handles the messages for our property window
//
BOOL CconvolverFilterProperties::OnReceiveMessage(HWND hwnd,
												  UINT uMsg,
												  WPARAM wParam,
												  LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_COMMAND:
		{
			if (m_bIsInitialized)
			{
				SetDirty();
			}
			else
			{
				break;
			}

			switch(LOWORD(wParam))
			{
			case IDC_GETCONFIG:
				{
					if(HIWORD(wParam) == BN_CLICKED)
					{
						TCHAR szFilterFileName[MAX_PATH]	= TEXT("");
						TCHAR szFilterPath[MAX_PATH]		= TEXT("");
						HRESULT hr = ERROR_SUCCESS;
#if defined(DEBUG) | defined(_DEBUG)
						cdebug << std::hex << LOWORD(wParam) << " " << HIWORD(wParam) << " " << LOWORD(lParam) << " " << HIWORD(lParam) << std::endl;
#endif
						try
						{
							// Setup the OPENFILENAME structure
							OPENFILENAME ofn = { sizeof(OPENFILENAME), hwnd, NULL,
								TEXT("Text files\0*.txt\0Config Files\0*.cfg\0All Files\0*.*\0\0"), NULL,
								0, 1, szFilterFileName, MAX_PATH, NULL, 0, szFilterPath,
								TEXT("Get filter file"),
								OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_READONLY, 
								0, 0, TEXT(".txt"), 0, NULL, NULL };

							SetDlgItemText( hwnd, IDS_STATUS, TEXT("Configuration filename...") );

							// Display the SaveFileName dialog. Then, try to load the specified file
							if( TRUE != GetOpenFileName( &ofn ) )
							{
								SetDlgItemText( hwnd, IDS_STATUS, TEXT("Get filter aborted.") );
								return (INT_PTR)TRUE;
							}

							SetDlgItemText( hwnd, IDC_CONFIGFILE, szFilterFileName );

							hr = m_pIconvolverFilter->put_filterfilename(szFilterFileName);
							if(FAILED(hr))
							{
								return (INT_PTR)TRUE;
							}

							hr = DisplayFilterFormat(hwnd, szFilterFileName);
						}
						catch (std::exception& error)
						{
							SetDlgItemText( hwnd, IDS_STATUS, CA2CT(error.what()));
							return (INT_PTR)TRUE;
						}
						catch (...) 
						{

							SetDlgItemText( hwnd, IDS_STATUS, TEXT("Failed to load filter config file.") );
							return (INT_PTR)TRUE;
						}

						hr = Apply(); // apply the filter immediately; don't wait for apply button to be set
						if(FAILED(hr))
						{
							SetDlgItemText( hwnd, IDS_STATUS, TEXT("Failed to apply new filter filename.") );
							return (INT_PTR)TRUE;
						}

						return (INT_PTR)TRUE;
					}
				}
				break;

			case IDC_CALCULATEOPTIMUMATTENUATION:
				{
					float	fAttenuation = 0;
					DWORD	nPartitions = 1;
					TCHAR*  szFilterFileName	= TEXT("");
					HRESULT hr S_OK;

					try
					{
						if(IsPageDirty() == S_OK) // Make sure that the currently-visible settings are used
						{
							hr = Apply();
							if (FAILED(hr))
							{
								SetDlgItemText( hwnd, IDS_STATUS, TEXT("Failed to apply settings.") ); // Apply supplies its own diagnostics
								return (INT_PTR)TRUE;
							}
						}

						hr = m_pIconvolverFilter->get_filterfilename(&szFilterFileName);
						if (FAILED(hr))
						{
							SetDlgItemText( hwnd, IDS_STATUS, TEXT("Failed to get filter filename.") ); // TODO: internationalize
							return (INT_PTR)TRUE;
						}

						hr = m_pIconvolverFilter->get_partitions(&nPartitions);
						if (FAILED(hr))
						{
							SetDlgItemText( hwnd, IDS_STATUS, TEXT("Failed to get number of partitions.") ); // TODO: internationalize
							return (INT_PTR)TRUE;
						}

						double fElapsed = 0;
						apHiResElapsedTime t;
						// TODO: the 8 should be derived
						hr = calculateOptimumAttenuation(fAttenuation, szFilterFileName, nPartitions);
						fElapsed = t.sec();
						if (FAILED(hr))
						{
							SetDlgItemText( hwnd,  IDS_STATUS, TEXT("Failed to calculate optimum attenuation. Check filter filename.") );  // TODO: internationalize
							return (INT_PTR)TRUE;
						}

						// Display the attenuation.
						TCHAR   szStr[MAXSTRING] = { 0 };
						_stprintf(szStr, _T("%.1f"), fAttenuation);
						SetDlgItemText( hwnd, IDC_ATTENUATION, szStr);

						// Display calculation time
						_stprintf(szStr, _T("Elapsed calculation time: %.2f seconds"), fElapsed);
						SetDlgItemText( hwnd, IDS_STATUS, szStr);
					}
					catch (std::exception& error)
					{
						SetDlgItemText( hwnd, IDS_STATUS, CA2CT(error.what()));
						return (INT_PTR)TRUE;
					}
					catch (...) 
					{

						SetDlgItemText( hwnd, IDS_STATUS, TEXT("Failed to calculate optimum attenuation.") );
						return (INT_PTR)TRUE;
					}

					SetDirty();

					return (INT_PTR)TRUE;
				}
				break;

			default:
				{
#if defined(DEBUG) | defined(_DEBUG)
					cdebug << "LOWORD(wParam) " << LOWORD(wParam) << std::endl;
#endif
				}
			} //switch wParam
		}
		break;

	case WM_NOTIFY:
		{
#if defined(DEBUG) | defined(_DEBUG)
			SetDlgItemText( hwnd, IDS_STATUS, TEXT("Unexpected notification.") );
#endif
		}

	default:
		{
		}
		break;

	} // switch uMsg

	return CBasePropertyPage::OnReceiveMessage(hwnd,uMsg,wParam,lParam);

}

//
// OnConnect
//
// Called when we connect to a transform filter
//
HRESULT CconvolverFilterProperties::OnConnect(IUnknown *pUnknown)
{
	ASSERT(m_pIconvolverFilter == NULL);

	HRESULT hr = pUnknown->QueryInterface(IID_IconvolverFilter, (void **) &m_pIconvolverFilter);
	if (FAILED(hr))
	{
		return E_NOINTERFACE;
	}

	ASSERT(m_pIconvolverFilter);

	m_bIsInitialized = FALSE ;
	return NOERROR;
}

//
// OnDisconnect
//
// Likewise called when we disconnect from a filter
//
HRESULT CconvolverFilterProperties::OnDisconnect()
{
	CheckPointer(m_pIconvolverFilter, E_UNEXPECTED);
	m_pIconvolverFilter->Release();
	m_pIconvolverFilter = NULL;
	return NOERROR;
}

//
// OnActivate
//
// We are being activated
//
HRESULT CconvolverFilterProperties::OnActivate()
{
	CheckPointer(m_pIconvolverFilter, E_POINTER);
	TCHAR sz[MAX_PATH] = {0};
	TCHAR *szFilterFileName = NULL;

			// May throw
	version v(TEXT("convolverFilter.ax"));
	SetDlgItemText(m_Dlg, IDS_Version, (TEXT("Version ") + v.get_product_version()).c_str());

	m_pIconvolverFilter->get_filterfilename(&szFilterFileName);
	Edit_SetText(GetDlgItem(m_Dlg, IDC_CONFIGFILE), szFilterFileName);
	DisplayFilterFormat(m_Dlg, szFilterFileName);

	float fAttenuation_db = 0;
	m_pIconvolverFilter->get_attenuation(&fAttenuation_db);
	_stprintf(sz, TEXT("%.1f"), fAttenuation_db);
	Edit_SetText(GetDlgItem(m_Dlg, IDC_ATTENUATION), sz);

	DWORD nPartitions = 0;
	m_pIconvolverFilter->get_partitions(&nPartitions);
	_stprintf(sz, TEXT("%d"), nPartitions);
	Edit_SetText(GetDlgItem(m_Dlg, IDC_NOOFPARTITIONS), sz);

	m_bIsInitialized = TRUE;

	return NOERROR;
}

//
// OnDeactivate
//
// We are being deactivated
//
HRESULT CconvolverFilterProperties::OnDeactivate(void)
{
	ASSERT(m_pIconvolverFilter);
	m_bIsInitialized = FALSE;

	float	fAttenuation = 0; 
	TCHAR szFilterFileName[MAX_PATH] = {0};
	DWORD nPartitions = 0;
	GetControlValues(fAttenuation, szFilterFileName, nPartitions);

	return NOERROR;
}

//
// OnApplyChanges
//
// Apply any changes so far made
//
HRESULT CconvolverFilterProperties::OnApplyChanges()
{
	float	fAttenuation = 0; 
	TCHAR szFilterFileName[MAX_PATH] = {0};
	DWORD nPartitions = 0;

	GetControlValues(fAttenuation, szFilterFileName, nPartitions);

	// update the plug-in
	if (m_pIconvolverFilter != NULL)
	{
		try
		{
			HRESULT hr;

			hr = m_pIconvolverFilter->put_attenuation(fAttenuation);
			if (FAILED(hr))
			{
				SetDlgItemText( m_Dlg, IDS_STATUS, TEXT("Failed to put attenuation"));
				return hr;
			}

			hr = m_pIconvolverFilter->put_partitions(nPartitions);
			if (FAILED(hr))
			{
				SetDlgItemText( m_Dlg, IDS_STATUS, TEXT("Failed to put partitions"));
				return hr;
			}

			hr = m_pIconvolverFilter->put_filterfilename(szFilterFileName);
			if (FAILED(hr))
			{
				SetDlgItemText( m_Dlg, IDS_STATUS, TEXT("Failed to put config filename"));
				return hr;
			}
		}
		catch (std::exception& error)
		{
			SetDlgItemText( m_Dlg, IDS_STATUS, CA2CT(error.what()));
			return E_FAIL;
		}
		catch (...) 
		{

			SetDlgItemText( m_Dlg, IDS_STATUS, TEXT("Failed to apply settings."));
			return E_FAIL;
		}
	}
	return S_OK;

}

//
// GetControlValues
//
void CconvolverFilterProperties::GetControlValues(float& fAttenuation, 
												  TCHAR szFilterFileName[MAX_PATH],
												  DWORD& nPartitions)
{
	// TODO:: redo this in C++ style, with stringstream

	DWORD	dwAttenuation = m_pIconvolverFilter->encode_Attenuationdb(fAttenuation);
	TCHAR   szStr[MAXSTRING] = { 0 };											// Temporary string
#ifdef UNICODE
	CHAR strTmp[2*(sizeof(szStr)+1)];	// SIZE equals (2*(sizeof(tstr)+1)). This ensures enough
	// room for the multibyte characters if they are two
	// bytes long and a terminating null character.
#endif
	fAttenuation = 0.0;													// Initialize a float for the attenuation
	dwAttenuation = m_pIconvolverFilter->encode_Attenuationdb(fAttenuation);  // Encoding necessary as DWORD is an unsigned long
	szFilterFileName[0] = 0;
	nPartitions = 0;													// Initialize a WORD for the number of partitions
	// to be used in the convolution algorithm


	// Get the attenuation value from the dialog box.
	Edit_GetText(GetDlgItem(m_Dlg, IDC_ATTENUATION), szStr, sizeof(szStr) / sizeof(szStr[0]));
	// atof does not work with Unicode
	// TODO:: redo this in C++ style, with stringstream
#ifdef UNICODE
	wcstombs(strTmp, (const wchar_t *) szStr, sizeof(strTmp)); 
	fAttenuation = atof(strTmp); 
#else 
	fAttenuation = atof(szStr);
#endif 

	// Make sure attenuation value is valid
	if ((-fAttenuation > MAX_ATTENUATION) || (fAttenuation > MAX_ATTENUATION))
	{
		SetDlgItemText( m_Dlg, IDS_STATUS, TEXT("Attenuation too high or low.") );
		fAttenuation = 0;
	}
	else
		dwAttenuation = m_pIconvolverFilter->encode_Attenuationdb(fAttenuation); // to ensure that it is unsigned

	// Get the number of partitions value from the dialog box.
	Edit_GetText(GetDlgItem(m_Dlg, IDC_NOOFPARTITIONS), szStr, sizeof(szStr) / sizeof(szStr[0]));

#ifdef UNICODE 
	wcstombs(strTmp, (const wchar_t *) szStr, sizeof(strTmp)); 
	nPartitions = static_cast<DWORD>(atoi(strTmp)); 
#else 
	nPartitions = static_cast<DWORD>(atoi(szStr));
#endif

	// update the registry
	CRegKey key;
	LONG    lResult;

	// Write the attenuation value to the registry.
	lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
	if (ERROR_SUCCESS == lResult)
	{
		lResult = key.SetDWORDValue( kszPrefsAttenuation, dwAttenuation );
		if (lResult != ERROR_SUCCESS)
		{
			SetDlgItemText( m_Dlg, IDS_STATUS, TEXT("Failed to save attenuation to registry.") );
		}
	}

	// Write the filter filename to the registry.
	lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
	if (ERROR_SUCCESS == lResult)
	{
		// Get the filter file name from the dialog box.
		Edit_GetText(GetDlgItem(m_Dlg, IDC_CONFIGFILE), szFilterFileName, MAX_PATH * sizeof(szFilterFileName[0]));

		lResult = key.SetStringValue( kszPrefsFilterFileName, szFilterFileName );
		if (lResult != ERROR_SUCCESS)
		{
			SetDlgItemText( m_Dlg, IDS_STATUS, TEXT("Failed to save filename to registry.") );
		}
	}

	// Write the number of partitions to the registry.
	lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
	if (ERROR_SUCCESS == lResult)
	{
		lResult = key.SetDWORDValue( kszPrefsPartitions, nPartitions );
		if (lResult != ERROR_SUCCESS)
		{
			SetDlgItemText( m_Dlg, IDS_STATUS, TEXT("Failed to save number of partitions to registry") );
		}
	}
}

// Helper function to update the dirty status.
void CconvolverFilterProperties::SetDirty()
{
	m_bDirty = TRUE;
	if (m_pPageSite)
	{
		m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
	}
}