// 
// Notice Regarding Standards.  AMD does not provide a license or sublicense to
// any Intellectual Property Rights relating to any standards, including but not
// limited to any audio and/or video codec technologies such as MPEG-2, MPEG-4;
// AVC/H.264; HEVC/H.265; AAC decode/FFMPEG; AAC encode/FFMPEG; VC-1; and MP3
// (collectively, the "Media Technologies"). For clarity, you will pay any
// royalties due for such third party technologies, which may include the Media
// Technologies that are owed as a result of AMD providing the Software to you.
// 
// MIT license 
// 
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
// DVR.cpp : Defines the entry point for the application.
//

#include "targetver.h"

//#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>

// C RunTime Header Files
#include <atlbase.h>
#include <d3d11.h>
#include <stdlib.h>
#include <tchar.h>
#include <objbase.h>

#include "DVR.h"

#include "public/common/AMFFactory.h"

#include "public/samples/CPPSamples/common/CmdLineParser.h"
#include "public/samples/CPPSamples/common/CmdLogger.h"
#include "public/common/AMFFactory.h"
#include "public/samples/CPPSamples/common/ParametersStorage.h"
#include "public/samples/CPPSamples/common/DisplayDvrPipeline.h"
#include "public/samples/CPPSamples/common/CmdLineParser.h"
#include "public/include/core/Debug.h"

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
TCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Resolution of the desktops that could be captured
struct CaptureSource 
{
	int			ResolutionX;
	int			ResolutionY;
	unsigned	deviceIdx;
	unsigned	adapterIdx;
};

static std::vector<CaptureSource> s_vCaptures;
static int s_iNumDevices = 0, s_iNumCaptures = 0;

static bool s_bCurrentlyRecording = false;
static UINT_PTR s_uiFpsTimerId = 0;

static DisplayDvrPipeline* s_pPipeline = NULL;

static std::wstring s_PipelineMsg;

static unsigned kDefaultGPUIdx = 0;

static bool s_bOCLConverter = false;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, HWND*, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    Dialog(HWND, UINT, WPARAM, LPARAM);
void                UpdateMenuItems();
void                PopulateMenus(HMENU hMenu);
void                ChangeFileLocation(HWND hWnd);
void                GetDirectoryFileLocation(TCHAR* szDirectory, TCHAR* szFile, bool generic = false);
void                GetDefaultFileLocation(TCHAR* szFile, bool generic = false);
void                StartRecording(HWND hWnd);
void                StopRecording(HWND hWnd);
void                UpdateFps(HWND hWnd);
void                UpdateMessage(const wchar_t *msg);

HWND                hClientWindow = NULL;
HMENU               hDevices, hCaptureSources;

//-------------------------------------------------------------------------------------------------
int APIENTRY _tWinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPTSTR    lpCmdLine,
	int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// Parse parameters
	s_bOCLConverter = false;
	LPWSTR *szArgList;
	int argCount = 0;
	szArgList = CommandLineToArgvW(GetCommandLine(), &argCount);
	for (int i = 0; i < argCount; i++)
	{
		if (wcscmp(L"-oclConverter", szArgList[i]) == 0)
		{
			// Enable an optimization to push converter work onto
			// the OCL queue
			s_bOCLConverter = true;
		}
	}

	// Main
	MSG msg = {};
	{
		CoInitializeEx(NULL, COINIT_MULTITHREADED);
		//-------------------------------------------------------------------------------------------------
		// AMF Initialization
		AMF_RESULT res = AMF_OK;
		res = g_AMFFactory.Init();
		if (res != AMF_OK)
		{
			wprintf(L"AMF failed to initialize");
			g_AMFFactory.Terminate();
			return 1;
		}

		AMFCustomTraceWriter writer(AMF_TRACE_WARNING);
#ifdef _DEBUG
		g_AMFFactory.GetDebug()->AssertsEnable(true);
		g_AMFFactory.GetTrace()->SetGlobalLevel(AMF_TRACE_INFO);
		g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_DEBUG_OUTPUT, AMF_TRACE_INFO);
		g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_CONSOLE, AMF_TRACE_INFO);
#else
		g_AMFFactory.GetDebug()->AssertsEnable(false);
		g_AMFFactory.GetTrace()->SetGlobalLevel(AMF_TRACE_WARNING);
#endif
		amf_increase_timer_precision();

		//-------------------------------------------------------------------------------------------------
		// DisplayDvrPipeline Initialization
		DisplayDvrPipeline pipeline;
		s_pPipeline = &pipeline;

		std::wstring codec = L"AMFVideoEncoderVCE_AVC";
		s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_CODEC, L"AMFVideoEncoderVCE_AVC");
		RegisterEncoderParamsAVC(s_pPipeline);

		s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_ADAPTERID, kDefaultGPUIdx);
		s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_MONITORID, 0);

		//-------------------------------------------------------------------------------------------------
		// Initialize global strings
		LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
		LoadString(hInstance, IDC_DVR, szWindowClass, MAX_LOADSTRING);
		MyRegisterClass(hInstance);
		HWND hWnd = NULL;

		// Perform application initialization:
		if (!InitInstance(hInstance, &hWnd, nCmdShow))
		{
			CoUninitialize();
			g_AMFFactory.Terminate();
			return FALSE;
		}
		
		HACCEL hAccelTable;
		hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_DVR));

		// Main message loop:
		while (GetMessage(&msg, NULL, 0, 0))
		{
			if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		CoUninitialize();
		//-------------------------------------------------------------------------------------------------
	}
	g_AMFFactory.Terminate();

	return (int)msg.wParam;
}

//-------------------------------------------------------------------------------------------------
//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//  COMMENTS:
//
//    This function and its usage are only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DVR));
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCE(IDC_DVR);
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_DVR));

	return RegisterClassEx(&wcex);
}

//-------------------------------------------------------------------------------------------------
//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, HWND* phWnd, int nCmdShow)
{
	hInst = hInstance; // Store instance handle in our global variable

	HWND hWnd = NULL;

	hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, 600, 400, NULL, NULL, hInstance, NULL);

	if (!hWnd)
	{
		return FALSE;
	}
	hClientWindow = hWnd;

	s_uiFpsTimerId = SetTimer(hWnd, 10000, 1000, NULL);

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	PopulateMenus(GetMenu(hWnd));

	*phWnd = hWnd;

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void UpdateMenuItems()
{
	int iSelectedDevice = 0, iSelectedCapture = 0;
	s_pPipeline->GetParam(DisplayDvrPipeline::PARAM_NAME_ADAPTERID, iSelectedDevice);
	s_pPipeline->GetParam(DisplayDvrPipeline::PARAM_NAME_MONITORID, iSelectedCapture);

	for (int i = 0; i < s_iNumDevices; ++i)
	{
		CheckMenuItem(hDevices, ID_DEVICE_START + i, MF_BYCOMMAND | (i == iSelectedDevice ? MF_CHECKED : MF_UNCHECKED));
	}

	unsigned iSelectedCaptureIdx = 0;
	for (int i = 0; i < s_iNumCaptures; ++i)
	{
		bool enableItem = s_vCaptures[i].deviceIdx == iSelectedDevice;
		bool checkItem = enableItem && (s_vCaptures[i].adapterIdx == iSelectedCapture);
		CheckMenuItem(hCaptureSources, ID_CAPTURE_SOURCE_START + i, MF_BYCOMMAND | (checkItem ? MF_CHECKED : MF_UNCHECKED));
		EnableMenuItem(hCaptureSources, ID_CAPTURE_SOURCE_START + i, enableItem ? MF_ENABLED : MF_DISABLED);
		//
		if (checkItem)
		{
			iSelectedCaptureIdx = i;
		}
	}

	s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_VIDEO_WIDTH, s_vCaptures[iSelectedCaptureIdx].ResolutionX);
	s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_VIDEO_HEIGHT, s_vCaptures[iSelectedCaptureIdx].ResolutionY);
	s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_OPENCL_CONVERTER, s_bOCLConverter);
}

//-------------------------------------------------------------------------------------------------
// Populate devices and displays menus
//-------------------------------------------------------------------------------------------------
void PopulateMenus(HMENU hMenu)
{
	HRESULT hr = S_OK;
	ATL::CComPtr<IDXGIFactory> pFactory;
	hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);
	if (FAILED(hr))
	{
		wprintf(L"CreateDXGIFactory failed. Error: %d", hr);
		return;
	}

	hDevices = GetSubMenu(hMenu, 1);
	hCaptureSources = GetSubMenu(hMenu, 2);

	std::wstring adapterMsg;
	UINT device = 0, output = 0;
	// Enumerate display devices
	while (true)
	{
		ATL::CComPtr<IDXGIAdapter> pDevice;
		if (pFactory->EnumAdapters(device, &pDevice) == DXGI_ERROR_NOT_FOUND)
		{
			break;
		}

		DXGI_ADAPTER_DESC descAdapter;
		hr = pDevice->GetDesc(&descAdapter);
		//
#ifdef _DEBUG
		adapterMsg += L"Found adapter: ";
		adapterMsg += descAdapter.Description;
#endif
		//
		if (descAdapter.VendorId != 0x1002) // Ensure only AMD GPUs are listed
		{
#ifdef _DEBUG
			adapterMsg += L": Non AMD";
			adapterMsg += L"\n";
#endif
			//
			++device;
			continue;
		}
#ifdef _DEBUG
		adapterMsg += L": AMD";
		adapterMsg += L"\n";
#endif

		if (SUCCEEDED(hr))
		{
			// Add to the Devices menu
			static int deviceId = ID_DEVICE_START;
			if (AppendMenu(hDevices, MF_BYPOSITION, deviceId++, descAdapter.Description) == false)
			{
				wprintf(L"Could not insert device menu item.");
				break;
			}
			++s_iNumDevices;
			// Enumerate monitors
			unsigned adapterIdx = 0;
			while (true)
			{
				ATL::CComPtr<IDXGIOutput> pOutput;
				if (pDevice->EnumOutputs(output, &pOutput) == DXGI_ERROR_NOT_FOUND)
				{
					break;
				}
				
				DXGI_OUTPUT_DESC descOutput;
				hr = pOutput->GetDesc(&descOutput);
				if (SUCCEEDED(hr))
				{
					// Get this output's screen resolution
					HMONITOR hMonitor = descOutput.Monitor;
					MONITORINFOEX monitorInfo;
					monitorInfo.cbSize = sizeof(MONITORINFOEX);
					GetMonitorInfo(hMonitor, &monitorInfo);
					DEVMODE devMode;
					devMode.dmSize = sizeof(DEVMODE);
					devMode.dmDriverExtra = 0;
					EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode);
					// Add to the Capture Sources list
					CaptureSource cs;
					cs.ResolutionX = devMode.dmPelsWidth;
					cs.ResolutionY = devMode.dmPelsHeight;
					cs.deviceIdx = device;		// GPU
					cs.adapterIdx = adapterIdx;	// ADAPTER on GPU
					s_vCaptures.push_back(cs);
					// Add to the Capture Sources menu
					static int outputId = ID_CAPTURE_SOURCE_START;
					TCHAR outputName[128];
					swprintf_s(outputName, 128, L"Monitor %d (%d x %d)", outputId - ID_CAPTURE_SOURCE_START, cs.ResolutionX, cs.ResolutionY);
					if (AppendMenu(hCaptureSources, MF_BYPOSITION, outputId++, outputName) == false)
					{
						wprintf(L"Could not insert capture source menu item.");
						break;
					}
					if (EnableMenuItem(hCaptureSources, (outputId - 1), (device == kDefaultGPUIdx) ? MF_ENABLED : MF_DISABLED))
					{
						wprintf(L"Could not enable/disable capture source menu item.");
						break;
					}
					++adapterIdx;
					++s_iNumCaptures;
				}
				++output;
			}
		}
		output = 0;
		++device;
	}
	//
	UpdateMessage(adapterMsg.c_str());
	//
	if (s_iNumDevices == 0 || s_iNumCaptures == 0)
	{
		// No devices detected, close application
		PostMessage(GetActiveWindow(), WM_USER + 1000, 0, 0);
	}
	else
	{
		// Remove placeholder separators (needed because GetSubMenu returns NULL if menu is initially empty)
		RemoveMenu(hDevices, 0, MF_BYPOSITION);
		RemoveMenu(hCaptureSources, 0, MF_BYPOSITION);
		// Place default checkmarks at first item in each menu
		UpdateMenuItems();
	}
}

//-------------------------------------------------------------------------------------------------
//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND    - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY    - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message)
	{
	case WM_USER + 1000:
		// If no AMD devices are recognized the application closes
		if (DialogBox(hInst, MAKEINTRESOURCE(IDD_NODEVICEBOX), hWnd, Dialog) != IDOK)
		{
			PostQuitMessage(0);
		}
		break;
	case WM_COMMAND:
		wmId = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// Parse the menu selections:
		if (wmId == IDM_ABOUT)
		{
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, Dialog);
		}
		else if (wmId == IDM_EXIT)
		{
			DestroyWindow(hWnd);
		}
		else if (wmId == ID_RECORD)
		{
			StartRecording(hWnd);
		}
		else if (wmId == ID_STOP)
		{
			StopRecording(hWnd);
		}
		else if (wmId == IDM_SAVEFILE)
		{
			ChangeFileLocation(hWnd);
		}
		// Checking/unchecking dynamically added gpu devices
		else if ((wmId >= ID_DEVICE_START) && (wmId <= ID_DEVICE_START + s_iNumDevices))
		{
			int id = wmId - ID_DEVICE_START;
			s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_ADAPTERID, id);
			UpdateMenuItems();
		}
		// Checking/unchecking dynamically added capture source monitors
		else if (wmId >= ID_CAPTURE_SOURCE_START && wmId <= ID_CAPTURE_SOURCE_START + s_iNumCaptures)
		{
			// Find the id of the selected adapter in the menu
			int id = wmId - ID_CAPTURE_SOURCE_START;
			// Find the adapter in the vector list
			unsigned adapterIdx = s_vCaptures[id].adapterIdx;
			// Set on the pipeline
			s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_MONITORID, adapterIdx);
			UpdateMenuItems();
		}
		else
		{
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		if (!s_PipelineMsg.empty())
		{
			std::wstring line;
			unsigned lineCount = 0;
			size_t len = s_PipelineMsg.length();
			for (unsigned i = 0; i < len; i++)
			{
				if (s_PipelineMsg[i] == L'\n' || ((i+1)==len))
				{
					TextOut(hdc, 5, 5 + i, line.c_str(), (int)line.length());
					//
					lineCount = 0;
					line.clear();
				}
				else
				{
					line += s_PipelineMsg[i];
				}
			}
		}
		EndPaint(hWnd, &ps);
		break;
	case WM_CREATE:
		break;
	case WM_SIZE:
		break;
	case WM_TIMER:
		// UpdateFps(hWnd);
		break;
	case WM_DESTROY:
		s_pPipeline->Terminate();
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

//-------------------------------------------------------------------------------------------------
// Handle save file dialog
void ChangeFileLocation(HWND hWnd)
{
	TCHAR szDefaultFile[1024];
	GetDefaultFileLocation(szDefaultFile, true /* generic */); // Even though we're changing it, get it here to show default in dialog

	std::wstring szFileOld = L"";
	s_pPipeline->GetParamWString(DisplayDvrPipeline::PARAM_NAME_OUTPUT, szFileOld);
	// If the file location is not set, or is default, then we use the default directory
	std::wstring::size_type foundAtIndex = szFileOld.find(szDefaultFile);
	if (0 == szFileOld.size() || 0 == foundAtIndex)
	{
		// Use the default we have
	}
	else if (std::wstring::npos == foundAtIndex)
	{
		// The user has changed the default path so find the last forward or
		// backwards slash
		std::wstring::size_type slashFoundAtIndex = szFileOld.rfind(L"/");
		if (std::wstring::npos == slashFoundAtIndex)
		{
			slashFoundAtIndex = szFileOld.rfind(L"\\");
		}
		if (std::wstring::npos != slashFoundAtIndex)
		{
			std::wstring dpath = szFileOld.substr(0, slashFoundAtIndex);
			GetDirectoryFileLocation((TCHAR*)dpath.c_str(), szDefaultFile, true /* generic */);
		}
	}

	OPENFILENAME ofn = {};
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.lpstrFile = szDefaultFile;
	ofn.nMaxFile = _countof(szDefaultFile);
	ofn.lpstrFilter = L"Videos\0*.WMV;*.WMA;*.AVI;*.ASF;*.FLV;*.BFI;*.CAF;*.GXF;*.IFF;*.RL2;*.MP4;*.3GP;*.QTFF;*.MKV;*.MK3D;*.MKA;*.MKS;*.MPG;*.MPEG;*.PS;*.TS;*.MXF;*.OGV;*.OGA;*.OGX;*.OGG;*.SPX;*.FLV;*.F4V;*.F4P;*.F4A;*.F4B;*.JSV;*.h264;*.264;*.vc1;*.mov;*.mvc;*.m1v;*.m2v;*.m2ts;*.vpk;*.yuv;*.rgb;*.nv12;*.h265;*.265\0All\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrInitialDir = L"%USERPROFILE%\\Videos";
	ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

	if (GetSaveFileName(&ofn) == TRUE)
	{
		s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_OUTPUT, ofn.lpstrFile);
	}
}

//-------------------------------------------------------------------------------------------------
// Get file name based on system time (e.g. DVRRecording-2017-09-26-12-30-00.mp4)
void GetDirectoryFileLocation(TCHAR* szDirectory, TCHAR* szFile, bool generic )
{
	assert(szDirectory);
	assert(szFile);
	//
	SYSTEMTIME lt;
	ZeroMemory(&lt, sizeof(lt));
	if (!generic)
	{
		GetLocalTime(&lt);
	}
	swprintf_s(szFile, 1024, L"%s\\DVRRecording-%d-%02d-%02d-%02d-%02d-%02d.mp4",
		szDirectory, lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
}

//-------------------------------------------------------------------------------------------------
// Get file name based on system time (e.g. DVRRecording-2017-09-26-12-30-00.mp4)
void GetDefaultFileLocation(TCHAR* szFile, bool generic )
{
	TCHAR szDirectory[1024];
	swprintf_s(szDirectory, 1024, L"%s\\Videos", _wgetenv(L"USERPROFILE"));
	GetDirectoryFileLocation(szDirectory, szFile, generic);
}

//-------------------------------------------------------------------------------------------------
// Handle Record button press
void StartRecording(HWND hWnd)
{
	// Enable stop button and disable record button to prevent repeated commands
	HMENU hMenu = GetMenu(hWnd);
	EnableMenuItem(hMenu, ID_RECORD, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(hMenu, ID_STOP, MF_BYCOMMAND | MF_ENABLED);
	DrawMenuBar(hWnd);

	// Initialize the pipeline and create the record file
	// Set record file location
	TCHAR szFileDefaultStart[1024];
	swprintf_s(szFileDefaultStart, 1024, L"%s\\Videos\\DVRRecording", _wgetenv(L"USERPROFILE"));
	std::wstring szFileOld = L"";
	s_pPipeline->GetParamWString(DisplayDvrPipeline::PARAM_NAME_OUTPUT, szFileOld);
	// If the file location is not set, or is default, overwrite it with an updated default location based on date
	std::wstring::size_type foundAtIndex = szFileOld.find(szFileDefaultStart);
	if (0 == szFileOld.size() || 0 == foundAtIndex)
	{
		TCHAR szFileNew[1024];
		GetDefaultFileLocation(szFileNew);
		s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_OUTPUT, szFileNew);
	}
	else if (std::wstring::npos == foundAtIndex)
	{
		// The user has changed the default path so find the last forward or
		// backwards slash
		std::wstring::size_type slashFoundAtIndex = szFileOld.rfind(L"/");
		if (std::wstring::npos == slashFoundAtIndex)
		{
			slashFoundAtIndex = szFileOld.rfind(L"\\");
		}
		if (std::wstring::npos != slashFoundAtIndex)
		{
			TCHAR szFileNew[1024];
			std::wstring dpath = szFileOld.substr(0, slashFoundAtIndex);
			GetDirectoryFileLocation((TCHAR*) dpath.c_str(), szFileNew);
			s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_OUTPUT, szFileNew);
		}
	}

	AMF_RESULT res = s_pPipeline->Init();
	if (res != AMF_OK)
	{
		wprintf(L"DisplayDvrPipeline failed to initialize");
		// Get the pipeline message if there is one
		std::wstring initMsg = s_pPipeline->GetErrorMsg();
		if (initMsg.empty())
		{
			initMsg = L"Failed to initialize pipeline";
		}
		UpdateMessage(initMsg.c_str());
		// Grey out the Record and Stop buttons
		EnableMenuItem(hMenu, ID_STOP, MF_BYCOMMAND | MF_GRAYED);
		EnableMenuItem(hMenu, ID_RECORD, MF_BYCOMMAND | MF_GRAYED);
		DrawMenuBar(hWnd);
		// 
		g_AMFFactory.Terminate();
		return;
	}
	
	// Start recording through pipeline
	s_pPipeline->Start();
	// Start drawing "Recording..." in window
	s_bCurrentlyRecording = true;
	// Update message
	std::wstring str = L"Recording";
#ifdef _DEBUG
	if (s_bOCLConverter)
	{
		str += L" (using OCL) ";
	}
#endif
	str += L"...";
	UpdateMessage(str.c_str());
}

//-------------------------------------------------------------------------------------------------
// Handle Stop button press
void StopRecording(HWND hWnd)
{
	// Enable record button and disable stop button to prevent repeated commands
	HMENU hMenu = GetMenu(hWnd);
	EnableMenuItem(hMenu, ID_STOP, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(hMenu, ID_RECORD, MF_BYCOMMAND | MF_ENABLED);
	DrawMenuBar(hWnd);

	// Stop recording
	s_pPipeline->Stop();
	// Stop drawing "Recording..." in window
	s_bCurrentlyRecording = false;
	UpdateMessage(NULL);
}

//-------------------------------------------------------------------------------------------------
void UpdateFps(HWND hWnd)
{
	if (s_bCurrentlyRecording)
	{
		TCHAR windowText[1000];
		double fps = s_pPipeline->GetFPS();
		swprintf_s(windowText, 1000, L"%s | FPS: %.1f", szTitle, fps);
		SetWindowText(hWnd, windowText);
	}
}

//-------------------------------------------------------------------------------------------------
void UpdateMessage(const wchar_t *msg)
{
	s_PipelineMsg = (msg) ? msg : L"";
	InvalidateRect(hClientWindow, NULL, TRUE); // redraw window
}

//-------------------------------------------------------------------------------------------------
// Message handler for simple dialog boxes.
INT_PTR CALLBACK Dialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCLOSE)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

