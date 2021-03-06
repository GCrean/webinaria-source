#include "StdAfx.h"
#include "Previewer.h"
#include "utils.h"
#include "folders.h"

//using namespace std;
using namespace  EditorApplication::EditorUserInterface;
using namespace  EditorApplication::EditorLogical;

HINSTANCE	CPreviewerWindow::HInstance = 0;
wchar_t*	CPreviewerWindow::Caption = NULL;
CParameters* CPreviewerWindow::params = NULL;
HWND		CPreviewerWindow::HWnd = 0;
wchar_t*	CPreviewerWindow::SourceFileName = NULL;
WNDPROC		CPreviewerWindow::OldPreviewWndProc = NULL;
IGraphBuilder *			CPreviewerWindow::pGB = NULL;
IMediaControl *			CPreviewerWindow::pMC = NULL;
IMediaSeeking *			CPreviewerWindow::pMS = NULL;
IMediaEventEx *			CPreviewerWindow::pME = NULL;
IPin *					CPreviewerWindow::pOutputPin = NULL;
IPin *					CPreviewerWindow::pInputPin = NULL;
IBaseFilter *			CPreviewerWindow::pSource = NULL;
IBaseFilter *			CPreviewerWindow::pAVISplitter = NULL;
IBaseFilter *			CPreviewerWindow::pVR= NULL;
IVMRWindowlessControl9 * CPreviewerWindow::pWC= NULL;
//IVMRMixerControl9 *		CPreviewerWindow::pMix = NULL;
IBaseFilter *			CPreviewerWindow::pAR= NULL;
IBaseFilter *			CPreviewerWindow::pAdder = NULL;
IAdder *				CPreviewerWindow::pAdderSettings = NULL;
DWORD					CPreviewerWindow::dwRotReg = 0;

const wchar_t CLASSNAME[] = L"PUI::CustomWindow";
		
//////////////////////////////////////////////////////////////////////////
//							Public methods								//
//////////////////////////////////////////////////////////////////////////

// Construct window in specified point with specified dimensions and caption
CPreviewerWindow::CPreviewerWindow(const int& X, const int& Y, const int& Width, const int& Height, const wchar_t* CaptionText)
	: event_handler(HANDLE_BEHAVIOR_EVENT)
	, HPictureWnd(NULL)
	, VideoStreamWidth(0)
	, VideoStreamHeight(0)
	, maximized(false)
	, paused(false)
	, timeline(0)
{
	int iLen = wcslen(CaptionText)+1;
	Caption = new wchar_t[iLen];
	wcscpy_s(Caption, iLen, CaptionText);

	// Set window style
	UINT style = WS_POPUP | WS_SYSMENU | WS_SIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	// Create main window
	HWnd = CreateWindowW(CLASSNAME, CaptionText, style ,X, Y, Width, Height, NULL, NULL, HInstance, NULL);

	// Save window associated data
	self( HWnd, this);

	// Callback for HTMLayout document elements
	HTMLayoutSetCallback(HWnd,&callback,this);

	// Loading document elements
	TCHAR ui[MAX_PATH];
    Folders::FromEditorUI(ui, _T("p_default.htm"));

	// Loading document elements
    if (HTMLayoutLoadFile(HWnd, ui))
	{
		htmlayout::dom::element r = Root();

		Body					= r.find_first("body");
		WindowCaption			= r.get_element_by_id("caption");
		MinimizeButton			= r.get_element_by_id("minimize");
		MaximizeButton			= r.get_element_by_id("maximize");
		IconButton				= r.get_element_by_id("icon");
		CloseButton				= r.get_element_by_id("close");
		Corner					= r.get_element_by_id("corner");

		OpenButton				= r.get_element_by_id("Open");
		SaveButton				= r.get_element_by_id("Save");
		PlayButton				= r.get_element_by_id("Play");
		PauseButton				= r.get_element_by_id("Pause");
		StopButton				= r.get_element_by_id("Stop");

		htmlayout::attach_event_handler(r, this);

		SetWindowCaption();

		UpdatePlaybackControls(false, true, true);
	}
}

CPreviewerWindow::~CPreviewerWindow(void)
{
	if (Caption != NULL)
		delete Caption;
}

void CPreviewerWindow::SetTimeline(Timeline *value)
{
	timeline = value;
}

void CPreviewerWindow::SetParameters( const CParameters &NewParams, const wchar_t * Source, Timeline *value)
{
	params = (CParameters*) &NewParams;
	if (NULL != SourceFileName)
			delete[] SourceFileName;
	int iLen = wcslen(Source) + 1;
	SourceFileName = new wchar_t[iLen];
	wcscpy_s(SourceFileName, iLen, Source);

    SetTimeline(value);
	CreateGraph();

	RECT rc;
	GetWindowRect(HWnd, &rc);
	if (VideoStreamWidth >= 332)
		MoveWindow(HWnd, rc.left, rc.top, VideoStreamWidth + 2, VideoStreamHeight + 157, TRUE);
	else
		MoveWindow(HWnd, rc.left, rc.top, 334, VideoStreamHeight + 157, TRUE);	
}

// Return window handle
HWND CPreviewerWindow::GetHWND(const ApplicationWindow & AW) const
{
	switch (AW)
	{
		case AW_MAIN:
			return HWnd;
		case AW_CLIP:
			return HPictureWnd;
		default:
			return NULL;
	}
}

// Register customer window class
ATOM CPreviewerWindow::RegisterWindowClass(HINSTANCE hInstance)
{
	// Window instance handle initialize
	HInstance = hInstance;
	WNDCLASSEXW wcex;

	// Set extension window options 
	wcex.cbSize			= sizeof(WNDCLASSEX); 
	wcex.style			= CS_VREDRAW | CS_HREDRAW;
	wcex.lpfnWndProc	= (WNDPROC)CustomWindowProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, (LPCTSTR)IDI_WEBINARIA_ICON);
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_DESKTOP);
	wcex.lpszMenuName	= 0;
	wcex.lpszClassName	= CLASSNAME;
	wcex.hIconSm		= LoadIcon(wcex.hInstance, (LPCTSTR)IDI_WEBINARIA_ICON);

	// Register window class
	return RegisterClassExW(&wcex);
}

// Set window caption
void CPreviewerWindow::SetWindowCaption( )
{
	// If text is not null
	if(Caption)
	{
		// Set window caption
		::SetWindowTextW(HWnd,Caption);
		// If HTMLayout document contains caption
		if( WindowCaption.is_valid() )
		{
			// Set HTMLayout document caption text
			WindowCaption.set_text(Caption);
			// Update HTMLayout document caption
			WindowCaption.update(true);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
//							Protected methods							//
//////////////////////////////////////////////////////////////////////////

// Hit mouse pointer
int CPreviewerWindow::HitTest(	const int & x,
								const int & y ) const
{
	POINT pt; 
	pt.x = x; 
	pt.y = y;
	// Convert desktop mouse point to window mouse point
	::MapWindowPoints(HWND_DESKTOP,HWnd,&pt,1);

	// If mouse pointer is over window caption
	if( WindowCaption.is_valid() && WindowCaption.is_inside(pt) )
		return HTCAPTION;

	// If mouse pointer is over system menu icon
	if( IconButton.is_valid() && IconButton.is_inside(pt) )
		return HTSYSMENU;

	/*if( CornerButton.is_valid() && CornerButton.is_inside(pt) )
		return HTBOTTOMRIGHT;*/

	RECT body_rc = Body.get_location(ROOT_RELATIVE | CONTENT_BOX);

	// If mouse pointer is over Body
	if( PtInRect(&body_rc, pt) )
		return HTCLIENT;

	if( pt.y < body_rc.top + 10 ) 
	{
		// If mouse pointer is over LeftTop coner
		if( pt.x < body_rc.left + 10 ) 
			return HTTOPLEFT;
		// If mouse pointer is over RightTop coner
		if( pt.x > body_rc.right - 10 ) 
			return HTTOPRIGHT;
	}
	else 
	if( pt.y > body_rc.bottom - 10 ) 
	{
		// If mouse pointer is over LeftBottom coner
		if( pt.x < body_rc.left + 10 ) 
			return HTBOTTOMLEFT;
		// If mouse pointer is over RightBottom coner
		if( pt.x > body_rc.right - 10 ) 
			return HTBOTTOMRIGHT;
	}

	// If mouse pointer is above top border
	if( pt.y < body_rc.top ) 
		return HTTOP;
	// If mouse pointer is below bottom border
	if( pt.y > body_rc.bottom ) 
		return HTBOTTOM;
	// If mouse pointer is outer of the left border
	if( pt.x < body_rc.left ) 
		return HTLEFT;
	// If mouse pointer is outer of the right border
	if( pt.x > body_rc.right ) 
		return HTRIGHT;

	// If mouse pointer is in the client area
	return HTCLIENT;	
}

// Get root DOM element of the HTMLayout document
HELEMENT CPreviewerWindow::Root() const
{
	return htmlayout::dom::element::root_element(HWnd);
}

// Get true if window is minimized
// otherwise - false
bool CPreviewerWindow::IsMin() const
{
	WINDOWPLACEMENT wp;
	// Get window placement state
	GetWindowPlacement(HWnd,&wp);
	return wp.showCmd == SW_SHOWMINIMIZED;
}

bool CPreviewerWindow::IsMax() const
{
	return maximized;
}

// Handle events of HTMLayout document
BOOL CPreviewerWindow::on_event(HELEMENT he, HELEMENT target, BEHAVIOR_EVENTS type, UINT reason )
{
	MoveVideoWindow();
	/*Viewer->Stop();
	Viewer->Play();*/
	// If minimize button click
	if (target == MinimizeButton && type == BUTTON_PRESS)
	{
		// Minimize window
			ShowWindow(HWnd,SW_MINIMIZE); 
		return TRUE;
	}

	if( target == MaximizeButton && type == BUTTON_PRESS)
	{
		if( IsMax())
		{
			MoveWindow(HWnd, oldWindowSize.left, oldWindowSize.top, RectWidth(oldWindowSize), RectHeight(oldWindowSize), TRUE);
			MoveVideoWindow();

			maximized = false;
		}
		else
		{
			GetWindowRect(HWnd, &oldWindowSize);

			//	Determine active monitor information.
			HMONITOR monitor = MonitorFromWindow(HWnd, MONITOR_DEFAULTTONEAREST);
			if (monitor)
			{
				MONITORINFO mi = { 0 };
				mi.cbSize = sizeof(MONITORINFO);
				if (GetMonitorInfo(monitor, &mi))
				{
					MoveWindow(HWnd, mi.rcWork.left, mi.rcWork.top, RectWidth(mi.rcWork), RectHeight(mi.rcWork), TRUE);
					MoveVideoWindow();
				}
			}

			maximized = true;
		}

		return TRUE;
	}

	// If close button click
	if( target == CloseButton && type == BUTTON_PRESS)
	{
		FreeWholeGraph();
		ShowWindow(HWnd, SW_HIDE);
		return TRUE;
	}

	if ( target == PlayButton && type == BUTTON_PRESS )
	{
		if (pMC && paused)
		{
			pMC->Run();
			UpdatePlaybackControls(false, true, true);

			paused = false;
		}
		else
		{
			FreeWholeGraph();
			CreateGraph();
			UpdatePlaybackControls(false, true, true);
			MoveVideoWindow();
		}
		return TRUE;
	}

	if ( target == StopButton && type == BUTTON_PRESS )
	{
		if (pMC)
			pMC->Stop();

		UpdatePlaybackControls(true, false, false);
		paused = false;

		return TRUE;	
	}

	if ( target == PauseButton && type == BUTTON_PRESS )
	{
		if (pMC)
		{
			pMC->Pause();
			UpdatePlaybackControls(true, false, true);

			paused = true;
		}

		return TRUE;
	}

	if ( target == SaveButton && type == BUTTON_PRESS )
	{
		return TRUE;
	}

	return FALSE;
}


bool CPreviewerWindow::CreateGraph()
{
	FreeWholeGraph();

	m_llAudioSamplesCount = 0;
	m_llVideoFramesCount = 0;
	// Create a DirectShow GraphBuilder object
	HRESULT hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void **)&pGB);
	if (FAILED(hr))
	{
		CError::ErrMsg(HWnd, TEXT("Error %x: Filter graph built failed"), hr);
		FreeWholeGraph();
		return false;
	}

	// Get DirectShow interfaces
	hr = pGB->QueryInterface(IID_IMediaControl, (void **)&pMC);
	if (FAILED(hr))
	{
		CError::ErrMsg(HWnd, TEXT("Error %x: Create filter graph control"), hr);
		FreeWholeGraph();
		return false;
	}

	hr = pGB->QueryInterface(IID_IMediaEventEx, (void **)&pME);
	if (FAILED(hr))
	{
		CError::ErrMsg(HWnd, TEXT("Error %x: Create filter graph event handler"), hr);
		FreeWholeGraph();
		return false;
	}

	// Have the graph signal event via window callbacks
	hr = pME->SetNotifyWindow((OAHWND)HWnd, WM_FGNOTIFY, 0);

	// Add the splitter filter to the graph
	hr = CreateAndInsertFilter(pGB, CLSID_AviSplitter, &pAVISplitter, L"AVI Splitter");
	if (FAILED(hr))
	{
		CError::ErrMsg(HWnd, TEXT("Error %x: Failed creating splitter filter"), hr);
		FreeWholeGraph();
		return false;
	}
	// Add the source filter to the graph
	hr = pGB->AddSourceFilter(SourceFileName, SourceFileName, &pSource);
	if (FAILED(hr))
	{
		CError::ErrMsg(HWnd, TEXT("Error %x: Failed adding source filter"), hr);
		FreeWholeGraph();
		return false;
	}


		// Add the mixer filter to the graph
		hr = CreateAndInsertFilter(pGB, CLSID_CAdder, &pAdder, L"Adder");
		if (FAILED(hr))
		{
			CError::ErrMsg(HWnd, TEXT("Error %x: Failed creating adder filter"), hr);
			FreeWholeGraph();
			return false;
		}
		
		hr = pAdder->QueryInterface(IID_IAdder, (void **)&pAdderSettings);
		if (FAILED(hr))
		{
			CError::ErrMsg(HWnd, TEXT("Error %x: Failed creating adder settings interface"), hr);
			FreeWholeGraph();
			return false;
		}				
		
		pAdderSettings->SetIntervals((void*)params->GetAllDeletedInterval(),
									 (void*)params->GetWebDeletedInterval(),
									 (void*)params->GetWebPosInterval(),
									 (void*)params->GetArrowInterval(),
									 (void*)params->GetTextInterval());

        if(timeline)
        {
	        CComPtr <ITimelineFilter> cpTimelineFilter;
		    hr = pAdder->QueryInterface(IID_ITimelineFilter, (void **)&cpTimelineFilter);
		    if (FAILED(hr))
		    {
			    CError::ErrMsg(HWnd, TEXT("Error %x: Failed creating ITimelineFilter interface"), hr);
			    FreeWholeGraph();
			    return false;
		    }
            CComPtr <ITimeline> cpTimeline;
            hr = timeline->QueryInterface(IID_ITimeline, (void**) &cpTimeline);
            if(SUCCEEDED(hr))
            {
                hr = cpTimelineFilter->AssignTimeline(cpTimeline);
                if(FAILED(hr))
                {
			        CError::ErrMsg(HWnd, TEXT("Error %x: Failed assigning ITimeline instance to filter"), hr);
			        FreeWholeGraph();
			        return false;
                }
            }
            else
            {
			    CError::ErrMsg(HWnd, TEXT("Error %x: Failed creating ITimeline interface"), hr);
			    FreeWholeGraph();
			    return false;
            }
        }
		// Add the video renderer to the graph
		hr = CreateAndInsertFilter(pGB, CLSID_VideoMixingRenderer9, &pVR, L"VMR9");
		if (FAILED(hr))
		{
			CError::ErrMsg(HWnd, TEXT("Error %x: Failed creating video renderer"), hr);
			FreeWholeGraph();
			return false;
		}
		// Set the rendering mode and number of streams
		CComPtr <IVMRFilterConfig9> pConfig;

		hr = pVR->QueryInterface(IID_IVMRFilterConfig9, (void**)&pConfig);
		if (FAILED(hr))
		{
			CError::ErrMsg(HWnd, TEXT("Error %x: Cannot config VMR"), hr);
			FreeWholeGraph();
			return false;
		}
#if 1 //!!!do not forget to remove it
		hr = pConfig->SetRenderingMode(VMR9Mode_Windowless);
		if (FAILED(hr))
		{
			CError::ErrMsg(HWnd, TEXT("Error %x: Set rendering mode"), hr);
			FreeWholeGraph();
			return false;
		}

		// Set the bounding window and border for the video
		hr = pVR->QueryInterface(IID_IVMRWindowlessControl9, (void**)&pWC);
		if (FAILED(hr))
		{
			CError::ErrMsg(HWnd, TEXT("Error %x: Initialize windowless control"), hr);
			FreeWholeGraph();
			return false;
		}
		hr = pWC->SetVideoClippingWindow(HPictureWnd);
		if (FAILED(hr))
		{
			CError::ErrMsg(HWnd, TEXT("Error %x: Initialize clipping window"), hr);
			FreeWholeGraph();
			return false;
		}

		pWC->SetBorderColor(RGB(63,63,63));    // Black border
#endif
		// Get the mixer control interface for later manipulation of video 
		// stream output rectangles and alpha values
/*
		hr = pVR->QueryInterface(IID_IVMRMixerControl9, (void**)&pMix);
		if (FAILED(hr))
		{
			CError::ErrMsg(HWnd, TEXT("Error %x: Initialize mixer control"), hr);
			FreeWholeGraph();
			return false;
		}
*/
		// Create an instance of the DirectSound renderer (for each media file).
		hr = CreateAndInsertFilter(pGB, CLSID_DSoundRender, &pAR, L"Audio Renderer");
		if (FAILED(hr))
		{
			CError::ErrMsg(HWnd, TEXT("Error %x: Failed audio renderer create"), hr);
			FreeWholeGraph();
			return false;
		}
		// Get the interface for the first output pin
		hr = GetUnconnectedPin(pSource, PINDIR_OUTPUT, &pOutputPin);
		if (SUCCEEDED(hr))
		{
			hr = JoinFilterToChain(pGB, pAVISplitter, &pOutputPin);
			if (SUCCEEDED(hr))
			{

				hr = pOutputPin->QueryInterface(IID_IMediaSeeking, (void **)&pMS);
				if (FAILED(hr))
				{
					CError::ErrMsg(HWnd, TEXT("Error %x: Create filter graph frame seeking"), hr);
					FreeWholeGraph();
					return false;
				}
				hr = pMS->SetTimeFormat( &TIME_FORMAT_FRAME );
				if(SUCCEEDED(hr))
				{
					pMS->GetDuration( &m_llVideoFramesCount );
				}

				hr = JoinFilterToChain(pGB, pAdder, &pOutputPin);
				if (SUCCEEDED(hr))
				{
					hr = JoinFilterToChain(pGB, pVR, &pOutputPin);
				}
			}
		}
		SAFE_RELEASE(pOutputPin);
		if (FAILED(hr))
		{
			CError::ErrMsg(HWnd, TEXT("Error %x: Can not render main video stream"), hr);
			FreeWholeGraph();
			return false;
		}
		//try to connect optional streams:
		for(int i=0; i<2; i++)
		{
			GetUnconnectedPin(pAVISplitter, PINDIR_OUTPUT, &pOutputPin);
			if(!pOutputPin)
				break;
			if(ptVideo==GetPinType(pOutputPin ))
			{//second video stream (webcam)
				GetUnconnectedPin(pAdder, PINDIR_INPUT, &pInputPin);
				if(pInputPin)
				{
					hr = pGB->Connect(pOutputPin, pInputPin);
					SAFE_RELEASE(pInputPin);
				}
			}
			else if(ptAudio==GetPinType(pOutputPin ))
			{// audio stream
				CComPtr<IBaseFilter> cpAudioSkipFilter;
				hr = CreateAndInsertFilter(pGB, CLSID_CAudioSkipper, &cpAudioSkipFilter, L"AudioSkipper");
				if(SUCCEEDED(hr))
				{

					CComPtr<IMediaSeeking> cpMediaSeeking;
					hr = pOutputPin->QueryInterface(IID_IMediaSeeking, (void **)&cpMediaSeeking);
					if(SUCCEEDED(hr))
					{
						hr = cpMediaSeeking->SetTimeFormat(&TIME_FORMAT_SAMPLE);
						if(SUCCEEDED(hr))
						{
							cpMediaSeeking->GetDuration(&m_llAudioSamplesCount);
						}
					}	

					hr = JoinFilterToChain(pGB, cpAudioSkipFilter, &pOutputPin);

					CComPtr<IAudioSkip> cpAudioSkip;
					hr = cpAudioSkipFilter->QueryInterface(IID_IAudioSkip, (void**) &cpAudioSkip);
					if(SUCCEEDED(hr))
					{
						cpAudioSkip->SetSamplesCount(m_llVideoFramesCount, m_llAudioSamplesCount);
						cpAudioSkip->SetIntervals((void*)params->GetAllDeletedInterval(), (void*)params->GetAudioDeletedInterval());
					}
				}
				hr = JoinFilterToChain(pGB, pAR, &pOutputPin);
			}
			SAFE_RELEASE(pOutputPin);
			if(FAILED(hr))
			{
				CError::ErrMsg(HWnd, TEXT("Error %x: Can not render optional stream %d"), hr, i);
				break;
			}
		}
		if(FAILED(hr))
		{			
			FreeWholeGraph();
			return false;
		}
		if(pWC)
			pWC->GetNativeVideoSize(&VideoStreamWidth, &VideoStreamHeight, &ArVideoStreamWidth, &ArVideoStreamHeight);


	AddGraphToRot(pGB, &dwRotReg);
	pMC->Run();
	return true;
}

// Tear down everything downstream of a given filter

void CPreviewerWindow::FreeWholeGraph()
{
	if (!pMC)
		return;

	if (pMC)
		pMC->Stop();

	RemoveGraphFromRot(dwRotReg);

	DemolishGraphFilters(pGB);

	SAFE_RELEASE(pOutputPin);
	SAFE_RELEASE(pInputPin);
	SAFE_RELEASE(pAR);
//	SAFE_RELEASE(pMix);
	SAFE_RELEASE(pWC);
	SAFE_RELEASE(pVR);
	SAFE_RELEASE(pAdderSettings);
	SAFE_RELEASE(pAdder);
	SAFE_RELEASE(pAVISplitter);
	SAFE_RELEASE(pSource);
	SAFE_RELEASE(pME);
	SAFE_RELEASE(pMS);
	SAFE_RELEASE(pMC);
	SAFE_RELEASE(pGB);
}

void CPreviewerWindow::MoveVideoWindow()
{
	if (pWC)
	{
		RECT rcDest;
		GetWindowRect(HPictureWnd, &rcDest);
		rcDest.right -= rcDest.left;
		rcDest.bottom -= rcDest.top;
		rcDest.left = 0;
		rcDest.top = 0;
		pWC->SetVideoPosition(NULL, &rcDest);
	}
}

// Handle window's events
LRESULT CALLBACK CPreviewerWindow::CustomWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT lResult;
	BOOL    bHandled;
	CPreviewerWindow * me = self(hWnd);
// HTMLayout +
	// HTMLayout could be created as separate window using CreateWindow API.
	// But in this case we are attaching HTMLayout functionality
	// to the existing window delegating windows message handling to 
	// HTMLayoutProcND function.
	lResult = HTMLayoutProcND(hWnd,message,wParam,lParam, &bHandled);
	if(bHandled)
	{
	  return lResult;
	}
// HTMLayout -

	switch (message) 
	{
		case WM_FGNOTIFY:
			if(pME)
			{
				LONG lEvent, l1, l2;
				while(pME->GetEvent(&lEvent, (LONG_PTR*)&l1, (LONG_PTR*)&l2, 0) == S_OK)
				{
					pME->FreeEventParams(lEvent, l1, l2);
					if(EC_ERRORABORT==lEvent)
					{
						//todo: displayu error message here
						CError::ErrMsg(HWnd, TEXT("EC_ERRORABORT event arrived, code %x"), l1);
						me->on_event(NULL, me->StopButton, BUTTON_PRESS, 0);
						continue;
					}
					else if(EC_COMPLETE==lEvent)
					{
						me->UpdatePlaybackControls(true, false, false);
						continue;
					}
				}
			}
			return 0;
		case WM_KEYDOWN:
		{
			if ((UINT)wParam == 82)
				me->on_event(NULL, me->PlayButton, BUTTON_PRESS, 0);
			else
			if ((UINT)wParam == 83)
				me->on_event(NULL, me->StopButton, BUTTON_PRESS, 0);
			else
			if ((UINT)wParam == 80)
				me->on_event(NULL, me->PauseButton, BUTTON_PRESS, 0);

		}
		break;

		case WM_NCHITTEST:
		{
			// Test hit
			if(me)
				return me->HitTest( GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) );
		}
	
		case WM_ACTIVATE:
			InvalidateRect(me->HPictureWnd, NULL, TRUE);
			me->MoveVideoWindow();
			return 0;

		case WM_CLOSE:
			// Destroy window
			SetWindowLong(me->HPictureWnd, GWL_WNDPROC, (LONG)OldPreviewWndProc);
			::DestroyWindow(hWnd);
			return 0;

		case WM_DESTROY:
			// Delete window class
			delete me; 
			me = NULL;
			self(hWnd,0);
			PostQuitMessage(0);
			return 0;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}
// Handles Preview holder window's events
LRESULT CALLBACK CPreviewerWindow::PreviewWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	CPreviewerWindow * me = self(GetParent(hWnd));

	switch (message) 
	{
		case WM_PAINT:     
		{
			PAINTSTRUCT ps; 
			HDC         hdc; 
			hdc = BeginPaint(hWnd, &ps);
			HRESULT hr;
			if (pWC)
			{
				hr = pWC->RepaintVideo(hWnd, hdc);
			}
			EndPaint(hWnd, &ps);
		}
		return 0;

		case WM_ERASEBKGND:
			return 1;

		case WM_SIZE:
			me->MoveVideoWindow();
			return 0;
	}

	return CallWindowProc(OldPreviewWndProc, hWnd, message, wParam, lParam); 
}

LRESULT CPreviewerWindow::on_create_control(LPNMHL_CREATE_CONTROL pnmcc)
{
	htmlayout::dom::element control = pnmcc->helement;
	const wchar_t *id = control.get_attribute("id");

	if (id && !wcscmp(L"VideoPreview", id))
	{
		// Set picture box style
		UINT style = WS_VISIBLE | WS_CHILD | WS_EX_LAYERED;

		// Create static picture box
		HPictureWnd = CreateWindowW(L"Static", NULL, style , 1, 1, 100, 100, pnmcc->inHwndParent, NULL, NULL, NULL);
		OldPreviewWndProc = (WNDPROC)SetWindowLong(HPictureWnd, GWL_WNDPROC, (LONG)PreviewWindowProc);

		pnmcc->outControlHwnd = HPictureWnd;
		return (LRESULT)HPictureWnd;
	}

	return 0;
}

void CPreviewerWindow::UpdatePlaybackControls(bool canPlay, bool canPause, bool canStop)
{
	PlayButton.set_attribute("disabled", (canPlay) ? 0 : L"true");
	PauseButton.set_attribute("disabled", (canPause) ? 0 : L"true");
	StopButton.set_attribute("disabled", (canStop) ? 0 : L"true");

	PlayButton.update(true);
	PauseButton.update(true);
	StopButton.update(true);
}
