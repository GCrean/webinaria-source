#include "StdAfx.h"

#include <shlwapi.h>
#include <math.h>

#include "CustomWindow.h"
#include "utils.h"
#include "Folders.h"
#include "Web.h"
#include "Branding.h"
#include "FileSelector.h"
#include "TextElement.h"

using namespace EditorApplication::EditorUserInterface;
using namespace EditorApplication::EditorLogical;

const TCHAR *g_CutExtension = _T(".rcut");
const TCHAR *g_VideoExtension = _T(".avi");

HINSTANCE			CCustomWindow::HInstance = 0;
IMediaEventEx *		CCustomWindow::pME = NULL;
wchar_t *			CCustomWindow::Caption = NULL;
CViewer *			CCustomWindow::Viewer = NULL;
unsigned short		CCustomWindow::iter = 0;
WNDPROC				CCustomWindow::OldProc = NULL;
WNDPROC				CCustomWindow::OldPreviewWndProc = NULL;
CParameters			CCustomWindow::params;
HWND				CCustomWindow::HWnd = 0;
bool				CCustomWindow::VideoDown = false;
int					CCustomWindow::DownX = 0;
int					CCustomWindow::DownY = 0;
int					CCustomWindow::Left = 0;
int					CCustomWindow::Top = 0;
bool				CCustomWindow::NewArrow = false;
bool				CCustomWindow::NewArrowFirstPoint = false;
OverlayControl		CCustomWindow::overlayControl;
bool				CCustomWindow::freezePreview = false;

const wchar_t CLASSNAME[] = L"EUI::CustomWindow";

//////////////////////////////////////////////////////////////////////////
//							Public methods								//
//////////////////////////////////////////////////////////////////////////

// Конструирует окно в заданной точке с заданными размерами и заголовком
CCustomWindow::CCustomWindow(const int& X, const int& Y, const int& Width, const int& Height, const wchar_t* CaptionText)
	: event_handler(HANDLE_BEHAVIOR_EVENT)
	, HPictureWnd(0)
	, HMainSliderWnd(0)
	, Previewer(0)
	, m_WebcamControl(0)
	, convertingFile(false)
	, m_SelectionStart(0)
	, m_SelectionEnd(0)
	, hasVideo(false)
	, hasSound(false)
{
	int iLen = wcslen(CaptionText)+1;
	Caption = new wchar_t[iLen];
	wcscpy_s(Caption, iLen, CaptionText);

	// Set window style
	UINT style = WS_POPUP | WS_SYSMENU | WS_EX_LAYERED;

	// Create main window
	HWnd = CreateWindowW(CLASSNAME, CaptionText, style ,X, Y, Width, Height, NULL, NULL, HInstance, NULL);

	// Save window associated data
	self(HWnd,this);

	// Callback for HTMLayout document elements
	HTMLayoutSetCallback(HWnd,&callback,this);

    TCHAR ui[MAX_PATH];
    Folders::FromEditorUI(ui, _T("default.htm"));

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

		OpenButton				= r.get_element_by_id("Open");
		SaveButton				= r.get_element_by_id("Save");
		PreviewButton			= r.get_element_by_id("Preview");
		MakeFlashVideoButton	= r.get_element_by_id("ConvertToFLV");
		SoundControlButton		= r.get_element_by_id("SoundControl");
		WebcamControlButton		= r.get_element_by_id("WebcamControl");
		VideoPreview			= r.get_element_by_id("videoPreview");
		AddTextButton			= r.get_element_by_id("AddText");

		AddIntervalButton		= r.get_element_by_id("AddInterval");
		DeleteOverlayButton		= r.get_element_by_id("DeleteInterval");
		DeleteContentButton		= r.get_element_by_id("DeleteContent");
		NextFrameButton			= r.get_element_by_id("NextFrame");
		PrevFrameButton			= r.get_element_by_id("PreviousFrame");

		PublishButton			= r.get_element_by_id("Publish");
		StatusReporter			= r.get_element_by_id("StatusReporter");
		ConversionMessage		= r.get_element_by_id("ConversionMessage");

		htmlayout::attach_event_handler(r, this);

		SetWindowCaption();
	}

	// Initialize global window class for Previewer
	CPreviewerWindow::RegisterWindowClass(HInstance);

	// Creating Previewer window
	Previewer = new CPreviewerWindow(100, 50, 1026, 925, L"Preview");

	UpdateAllControls(false, false);

	__hook(&Timeline::Changed, &timeline, &CCustomWindow::TimelineChanged);
	__hook(&Timeline::ActiveOverlayChanged, &timeline, &CCustomWindow::TimelineActiveOverlayChanged);
	__hook(&Timeline::CurrentFrameChanged, &timeline, &CCustomWindow::TimelineCurrentFrameChanged);
	__hook(&OverlayControl::ActiveOverlayElementChanged, &overlayControl, &CCustomWindow::OverlayActiveElementChanged);
	__hook(&OverlayControl::RightMouseButtonClicked, &overlayControl, &CCustomWindow::OverlayRightMouseButtonClicked);
	__hook(&OverlayElementEditorManager::ElementChanged, &overlayElementEditorManager, &CCustomWindow::OverlayElementChanged, this);
}

// Виртуальный деструктор
CCustomWindow::~CCustomWindow(void)
{
	__unhook(&Timeline::Changed, &timeline, &CCustomWindow::TimelineChanged);
	__unhook(&Timeline::ActiveOverlayChanged, &timeline, &CCustomWindow::TimelineActiveOverlayChanged);
	__unhook(&Timeline::CurrentFrameChanged, &timeline, &CCustomWindow::TimelineCurrentFrameChanged);
	__unhook(&OverlayControl::ActiveOverlayElementChanged, &overlayControl, &CCustomWindow::OverlayActiveElementChanged);
	__unhook(&OverlayControl::RightMouseButtonClicked, &overlayControl, &CCustomWindow::OverlayRightMouseButtonClicked);
	__unhook(&OverlayElementEditorManager::ElementChanged, &overlayElementEditorManager, &CCustomWindow::OverlayElementChanged, this);

	if (Caption != NULL)
		delete Caption;

	if (m_WebcamControl)
		delete m_WebcamControl;

	SAFE_RELEASE(pME);
	Viewer = NULL;
}

// Set viewer handler
void CCustomWindow::SetViewer(CViewer * NewViewer)
{
	Viewer = NewViewer;
}

// Set media event handler
void CCustomWindow::SetME(IMediaEventEx * NewME)
{
	pME = NewME;
}

// Return window handle
HWND CCustomWindow::GetHWND(const ApplicationWindow & AW) const
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

ATOM CCustomWindow::RegisterWindowClass(HINSTANCE hInstance)
{
	// Window instance handle initialize
	HInstance = hInstance;

	WNDCLASSEXW wcex = { 0 };

	// Set extension window options 
	wcex.cbSize			= sizeof(WNDCLASSEX); 
	wcex.style			= CS_VREDRAW | CS_HREDRAW;
	wcex.lpfnWndProc	= (WNDPROC)CustomWindowProc;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= 
		(HICON)LoadImage(0, Branding::Instance()->MainIconPath(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_DESKTOP);
	wcex.lpszClassName	= CLASSNAME;

	// Register window class
	return RegisterClassExW(&wcex);
}

void CCustomWindow::SetWindowCaption( )
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

int CCustomWindow::HitTest(	const int & x,
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
HELEMENT CCustomWindow::Root() const
{
	return htmlayout::dom::element::root_element(HWnd);
}

// Get true if window is minimized
// otherwise - false
bool CCustomWindow::IsMin() const
{
	WINDOWPLACEMENT wp;
	// Get window placement state
	GetWindowPlacement(HWnd,&wp);
	return wp.showCmd == SW_SHOWMINIMIZED;
}

BOOL CCustomWindow::on_event(HELEMENT he, HELEMENT target, BEHAVIOR_EVENTS type, UINT reason )
{
	// If minimize button click
	if (target == MinimizeButton && type == BUTTON_PRESS)
	{
		// Minimize window
		::ShowWindow(HWnd,SW_MINIMIZE); 
		return TRUE;
	}

	// If close button click
	if( target == CloseButton && type == BUTTON_PRESS)
	{
		// Post close message
		::PostMessage(HWnd, WM_CLOSE, 0,0); 
		return TRUE;
	}

	long long SelStartFrame = 0;//SendMessage(HMainSliderWnd, TBM_GETSELSTART, 0, 0);
	long long SelEndFrame = 0;//SendMessage(HMainSliderWnd, TBM_GETSELEND, 0, 0);
	long long CurrentPos = 0;//SendMessage(HMainSliderWnd, TBM_GETPOS, 0, 0);

	if ( target == OpenButton && type == BUTTON_PRESS )
	{
		OpenFile(0);

		return TRUE;
	}

	if ( target == SaveButton && type == BUTTON_PRESS )
	{
		bool saveAs = (GetAsyncKeyState(VK_SHIFT) & 0x8000);
		Save(saveAs);

		return TRUE;
	}

	if ( target == PreviewButton && type == BUTTON_PRESS )
	{
		if (Viewer->Loaded())
		{
			Previewer->SetParameters(params, Viewer->GetFileName(), &timeline);
			//Previewer->SetTimeline(&timeline);

			//SendMessage(HMainSliderWnd, TBM_SETPOS, (WPARAM) TRUE, (LPARAM)0);

			HWND hMainWnd = Previewer->GetHWND(AW_MAIN);

			// If custom window is available
			if (hMainWnd)
			{
				// Show custom window
				ShowWindow(hMainWnd, SW_NORMAL);
				// Update custom window
				UpdateWindow(hMainWnd);
			}
		}
		return TRUE;				
	}

	if ( target == AddTextButton && type == BUTTON_PRESS )
	{
		if (timeline.ActiveOverlay)
		{
			RECT rc;
			GetClientRect(HPictureWnd, &rc);

			overlayControl.ActivateElement(HPictureWnd, timeline.ActiveOverlay->CreateElement<TextElement>(&rc));
		}

		return TRUE;
	}

	if ( target == SoundControlButton && type == BUTTON_PRESS )
	{
		if (timeline.ActiveOverlay)
		{
			timeline.ActiveOverlay->EnableSound = !timeline.ActiveOverlay->EnableSound;
			UpdateSelectionControls();
		}

		return TRUE;
	}

	if ( target == WebcamControlButton && type == BUTTON_PRESS )
	{
		if (timeline.ActiveOverlay)
		{
			timeline.ActiveOverlay->EnableWebcam = !timeline.ActiveOverlay->EnableWebcam;
			UpdateSelectionControls();
		}

		return TRUE;
	}

	if ( target == AddIntervalButton && type == BUTTON_PRESS )
	{
		int spanLength = (int)ceil((double)timeline.Length * 0.1);
		timeline.CreateOverlay(spanLength);

		return TRUE;
	}

	if ( target == DeleteOverlayButton && type == BUTTON_PRESS )
	{
		if (timeline.ActiveOverlay)
			timeline.Delete(timeline.ActiveOverlay);

		return TRUE;
	}

	if ( target == DeleteContentButton && type == BUTTON_PRESS )
	{
		if (timeline.ActiveOverlay)
		{
			timeline.ActiveOverlay->Deleted = true;
			timelineControl.RepaintAll();
		}

		return TRUE;
	}

	if ( target == MakeFlashVideoButton && type == BUTTON_PRESS )
	{
		if (Viewer->Loaded())
		{
			//SendMessage(HMainSliderWnd, TBM_SETPOS, (WPARAM) TRUE, (LPARAM)0);
			SAFE_RELEASE(pME);
			if (EditorLogical::CFLVConverter::PreSaveToFLV(HWnd, params, Viewer->GetFileName(), &pME, &timeline))
			{
				htmlayout::dom::element r = Root();
				unsigned char text[] = "Converting AVI to FLV, please wait...";
				ConversionMessage.set_html(text,strlen((char*)text));
				r.update(true);

				ChangeConversionProcessState(true);
			}
		}
		return TRUE;
	}

	if ( target == NextFrameButton && type == BUTTON_PRESS )
	{
		if (timeline.CurrentFrame < timeline.Length)
			timeline.CurrentFrame++;

		return TRUE;
	}

	if ( target == PrevFrameButton && type == BUTTON_PRESS )
	{
		if (timeline.CurrentFrame > 0)
			timeline.CurrentFrame--;

		return TRUE;
	}

	if ( target == PublishButton && type == BUTTON_PRESS )
	{
		Web::OpenUploads();
		return TRUE;
	}

	return FALSE;
}

void CCustomWindow::ChangeConversionProcessState(bool started)
{
	convertingFile = started;

	HCURSOR hCur = LoadCursor(NULL, (started) ? IDC_WAIT : IDC_ARROW);
	SetCursor(hCur);

	UpdateAllControls(!started, true);
	ShowWindow(HPictureWnd, (started) ? SW_HIDE : SW_NORMAL);

	VideoPreview.set_style_attribute("display", (!started) ? L"block" : L"none");
	VideoPreview.update(true);

	StatusReporter.set_style_attribute("display", (started) ? L"block" : L"none");
	StatusReporter.update(true);
}

// Обрабатывает события, происходящие с элементами окна
LRESULT CALLBACK CCustomWindow::CustomWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
LRESULT lResult;
BOOL    bHandled;

	CCustomWindow* me = self(hWnd);


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
	case WM_KEYDOWN:
		{
			if ((UINT)wParam == VK_ESCAPE && me->convertingFile)
			{
				EditorLogical::CFLVConverter::Stop();

				MessageBoxW(hWnd, L"File conversion was interrupted.", L"Information", MB_ICONERROR);

				me->ChangeConversionProcessState(false);
			}
		}
		break;

	case WM_SYSCOMMAND:
		{
			switch (wParam)
			{
			case SC_MINIMIZE:
				{
					::ShowWindow(hWnd,SW_MINIMIZE);
				}
				return 0;
			}
		}
		break;
	case WM_MOUSEMOVE:
		{
			int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);
			if ((x >= 15) && (x <= 335) && (y >= 110) && (y <= 350))
			{
				float Reduction = Viewer->GetReduction();
				if (NewArrowFirstPoint)
				{
					x -= 15 + Viewer->GetMainStreamLeftShift();
					y -= 110 + Viewer->GetMainStreamTopShift();
					params.CurrentArrowInterval->x2 = (int)((float)x / Reduction);
					params.CurrentArrowInterval->y2 = (int)((float)y / Reduction);
					Viewer->Update();
				}
				else if (VideoDown)
				{
					if (NULL != params.CurrentTextInterval)
						if (params.CurrentTextInterval->IsSelected)
						{
							params.CurrentTextInterval->Left = Left + (int)((float)(x - DownX)/Reduction); 
							params.CurrentTextInterval->Top = Top + (int)((float)(y - DownY)/Reduction); 
						}

					if (NULL != params.CurrentArrowInterval)
						if (params.CurrentArrowInterval->IsSelected)
						{
							params.CurrentArrowInterval->x2 =	params.CurrentArrowInterval->x2  - 
																params.CurrentArrowInterval->x1 + 
																Left + (int)((float)(x - DownX)/Reduction); 
							params.CurrentArrowInterval->y2 =	params.CurrentArrowInterval->y2  - 
																params.CurrentArrowInterval->y1 + 
																Top + (int)((float)(y - DownY)/Reduction); 
							params.CurrentArrowInterval->x1 = Left + (int)((float)(x - DownX)/Reduction); 
							params.CurrentArrowInterval->y1 = Top + (int)((float)(y - DownY)/Reduction); 
						}							
				}
			}
			return 0;
		}
	case WM_LBUTTONUP:
		{
			int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);
			if ((x >= 15) && (x <= 335) && (y >= 110) && (y <= 350))
				if (NewArrow)
				{
					NewArrowFirstPoint = true;
					NewArrow = false;
					float Reduction = Viewer->GetReduction();
					x -= 15 + Viewer->GetMainStreamLeftShift();
					y -= 110 + Viewer->GetMainStreamTopShift();
					params.CurrentArrowInterval->x1 = (int)((float)x / Reduction);
					params.CurrentArrowInterval->y1 = (int)((float)y / Reduction);
				}
				else if (NewArrowFirstPoint)
				{
					NewArrowFirstPoint = false;
				}

			if (VideoDown)
			{
				VideoDown = false;
				Viewer->Update();
				DownX = 0;
				DownY = 0;
			}

			return 0;
		}
	case WM_LBUTTONDOWN:
		{
			int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);
			if (Viewer->Loaded() && (x >= 15) && (x <= 335) && (y >= 110) && (y <= 350))
			{
				DownX = x;
				DownY = y;
				x -= 15 + Viewer->GetMainStreamLeftShift();
				y -= 110 + Viewer->GetMainStreamTopShift();

				if (x >= 0 && y >= 0)
				{
					if (!NewArrow)
					{
						float Reduction = Viewer->GetReduction();
						STextInterval * NewCurrentText = params.HitText(x, y, Reduction);
						SArrowInterval * NewCurrentArrow = params.HitArrow(x, y, Reduction);
						if (NULL != NewCurrentText)
						{
							if (NULL != params.CurrentTextInterval)
								params.CurrentTextInterval->IsSelected = false;
							NewCurrentText->IsSelected = true;
							params.CurrentTextInterval = NewCurrentText;
							VideoDown = true;
							Left = params.CurrentTextInterval->Left;
							Top = params.CurrentTextInterval->Top;
							if (NULL != params.CurrentArrowInterval)
									params.CurrentArrowInterval->IsSelected = false;
						}
						else
						{
							if (NULL != params.CurrentTextInterval)
								params.CurrentTextInterval->IsSelected = false;
							if (NULL != NewCurrentArrow)
							{
								if (NULL != params.CurrentArrowInterval)
									params.CurrentArrowInterval->IsSelected = false;
								NewCurrentArrow->IsSelected = true;
								params.CurrentArrowInterval = NewCurrentArrow;
								VideoDown = true;
								Left = params.CurrentArrowInterval->x1;
								Top = params.CurrentArrowInterval->y1;
							}
							else
								if (NULL != params.CurrentArrowInterval)
									params.CurrentArrowInterval->IsSelected = false;
						}
					}
				}
				else
				{
					if (NULL != params.CurrentTextInterval)
						params.CurrentTextInterval->IsSelected = false;
					if (NULL != params.CurrentArrowInterval)
						params.CurrentArrowInterval->IsSelected = false;
				}
			}

			Viewer->Update();
			return 0;
		}
	case WM_NCHITTEST:
		{
			// Test hit
			if(me)
				return me->HitTest( GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) );
		}
	
	case WM_ACTIVATE:
		return 0;

	case WM_NCCALCSIZE:   
		// We have no non-client areas
		return 0; 

	case WM_GETMINMAXINFO:
		{
			LRESULT lr = DefWindowProc(hWnd, message, wParam, lParam);
			MINMAXINFO* pmmi = (MINMAXINFO*)lParam;
			pmmi->ptMinTrackSize.x = ::HTMLayoutGetMinWidth(hWnd);
			RECT rc; 
			GetWindowRect(hWnd,&rc);
			pmmi->ptMinTrackSize.y = ::HTMLayoutGetMinHeight(hWnd, rc.right - rc.left);
			return lr;
		}
		return 0;

	//case WM_FGNOTIFY:
	case WM_FGNOTIFY2:
		{
			LONG evCode, evParam1, evParam2;
			
			if (pME)
			{
				// Process all queued events
				if(SUCCEEDED(pME->GetEvent(&evCode, (LONG_PTR *)&evParam1, (LONG_PTR *)&evParam2, 0)))
				{
					// Free memory associated with callback, since we're not using it
					pME->FreeEventParams(evCode, evParam1, evParam2);
					
					//Our custom event to progress update
					if(EC_PROGRESS == evCode)
					{
						LONG lPos  = evParam1;
						LONG lTotal = (LONG) EditorLogical::CFLVConverter::GetDuration();

                        if (lTotal > 0)
                        {
                            wchar_t progressValue[10];
                            _itow((lPos * 100) / lTotal, progressValue, 10);

                            htmlayout::dom::element r = htmlayout::dom::element::root_element(hWnd);

                            //  Ensure progress bar is visible and update progress.
                            htmlayout::dom::element progress = r.get_element_by_id("ConversionProgress");
                            progress.set_style_attribute("display", L"block");
                            progress.set_attribute("value", progressValue);

                            r.update(true);
                        }
					}							
					else if(EC_COMPLETE == evCode)
					{
						htmlayout::dom::element r = htmlayout::dom::element::root_element(hWnd);
						htmlayout::dom::element RI = r.get_element_by_id("ConversionMessage");
						unsigned char text[] = "Converting recording to Flash Video format, please wait...";
						RI.set_html(text,strlen((char*)text));

                        //  Hide progress bar and reset its value.
                        htmlayout::dom::element progress = r.get_element_by_id("ConversionProgress");
                        progress.set_style_attribute("display", L"none");
                        progress.set_attribute("value", L"0");

                        r.update(true);

						int bitrateValue = 200;
						switch (EditorLogical::CFLVConverter::SelectedFileQuality)
						{
						case FileQualityLow:
							bitrateValue = 200;
							break;

						case FileQualityMedium:
							bitrateValue = 400;
							break;

						case FileQualityHigh:
							bitrateValue = 700;
							break;
						}

						if (EditorLogical::CFLVConverter::PostSaveToFLV(bitrateValue))
						{
							MessageBoxW(hWnd, L"Your recording was successfully converted to Flash Video format and is ready to publish.", L"Information", 0);
						}
						else
							CError::ErrMsg(hWnd, L"Error occured during file conversion process.");

						RI.set_html((unsigned char *)"",1);
						r.update(true);

						me->ChangeConversionProcessState(false);
					}
					else if(EC_ERRORABORT == evCode)
					{
						WCHAR wcMsg[128];
						StringCbPrintf(wcMsg, sizeof(wcMsg), L"File conversion aborted due to DirectShow error. Code 0x%08X", evParam1);
						MessageBoxW(hWnd, wcMsg, L"Information", MB_ICONERROR);

						me->ChangeConversionProcessState(false);
					}
				}
			}
		}
		return 0;

	case WM_NOTIFY:
		if (IDD_WEBCAM_CONTROL == wParam)
		{
			NMHDR *pnmhdr = (NMHDR*)lParam;

			if (WebcamPositionUpdated == pnmhdr->code && me->timeline.ActiveOverlay)
			{
				int horzShift = me->m_WebcamControl->HorzShift(), vertShift = me->m_WebcamControl->VertShift();
				Viewer->MoveWebStream(horzShift, vertShift);
				me->timeline.ActiveOverlay->WebcamX = horzShift;
				me->timeline.ActiveOverlay->WebcamY = vertShift;
			}
		}
		return 0;

	case WM_CLOSE:
		//SetWindowLong(me->HMainSliderWnd, GWL_WNDPROC, (LONG)OldProc); 
		SetWindowLong(me->HPictureWnd, GWL_WNDPROC, (LONG)OldPreviewWndProc);
		// Destroy window
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
void CCustomWindow::DrawOverlay()
{
    if(Viewer)
    {
        Viewer->OverlayClear();
        if(overlayControl.ActiveOverlay)
        {
            RECT rcFrame;
            SetRect(&rcFrame, 0, 0, Viewer->GetMainWidth(), Viewer->GetMainHeight());              
            overlayControl.ActiveOverlay->Paint(Viewer->GetOverlayHdc(), &rcFrame);
        }
        Viewer->OverlayCommit();
    }
}
// Handles Preview holder window's events
LRESULT CALLBACK CCustomWindow::PreviewWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) 
	{
		case WM_PAINT:     
		{
			if (freezePreview)
				return 0;
            
            DrawOverlay();

			PAINTSTRUCT ps = { 0 };
			HDC         hdc;
			hdc = BeginPaint(hWnd, &ps); 

			if (hdc)
			{

                

				// When using VMR Windowless mode, you must explicitly tell the
				// renderer when to repaint the video in response to WM_PAINT
				// messages.  This is most important when the video is stopped
				// or paused, since the VMR won't be automatically updating the
				// window as the video plays.
				if (Viewer)
				{
					//Viewer->MoveVideoWindow();
					Viewer->RepaintVideo(hdc);
				}
				EndPaint(hWnd, &ps);
			}
		}
		return 0;

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
			overlayControl.MouseDown(hWnd, wParam, lParam);
            DrawOverlay();

			return 0;

		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
			overlayControl.MouseUp(hWnd, wParam, lParam);

			return 0;

		case WM_MOUSEMOVE:
			overlayControl.MouseMove(hWnd, wParam, lParam);
			return 0;

		case WM_ERASEBKGND:
			return 1;
	}

	return CallWindowProc(OldPreviewWndProc, hWnd, message, wParam, lParam); 
}

LRESULT CCustomWindow::on_create_control(LPNMHL_CREATE_CONTROL pnmcc)
{
	htmlayout::dom::element control = pnmcc->helement;
	const wchar_t *id = control.get_attribute("id");

	if (id)
	{
		if (!wcscmp(L"webcamControl", id))
		{
			//	Create webcam positioning control.
			m_WebcamControl = new WebcamControl(HInstance, pnmcc->inHwndParent, IDD_WEBCAM_CONTROL);

			pnmcc->outControlHwnd = *m_WebcamControl;
			return (LRESULT)pnmcc->outControlHwnd;
		}
		else if (!wcscmp(L"videoPreview", id))
		{
			UINT style = WS_VISIBLE | WS_CHILD | SS_NOTIFY;

			//	Create video preview control.
			HPictureWnd = CreateWindowW(L"Static", 0, style , 15, 110, 320, 240, pnmcc->inHwndParent, NULL, NULL, NULL);
			OldPreviewWndProc = (WNDPROC)SetWindowLong(HPictureWnd, GWL_WNDPROC, (LONG)PreviewWindowProc);

			pnmcc->outControlHwnd = HPictureWnd;
			return (LRESULT)pnmcc->outControlHwnd;
		}
		else if (!wcscmp(L"timeline", id))
		{
			pnmcc->outControlHwnd = 
				timelineControl.Create(HInstance, pnmcc->inHwndParent, &NewRect(15, 400, 720, 18), &timeline);
			return (LRESULT)pnmcc->outControlHwnd;
		}
	}

	return 0;
}

void CCustomWindow::UpdateAllControls(bool enable, bool includingOpenButton)
{
	//	Traverse all known controls.
	htmlayout::dom::element* elements[12] =
	{ 
		&SaveButton, &PreviewButton, &MakeFlashVideoButton, &SoundControlButton, &WebcamControlButton, 
		&AddIntervalButton, &DeleteOverlayButton, &DeleteContentButton, &NextFrameButton, &PrevFrameButton, &PublishButton,
		&AddTextButton
	};
	for (int i = 0; i < _countof(elements); i++)
		elements[i]->set_state((!enable) ? STATE_DISABLED : 0, (enable) ? STATE_DISABLED : 0, true);

	if (includingOpenButton)
		OpenButton.set_state((!enable) ? STATE_DISABLED : 0, (enable) ? STATE_DISABLED : 0, true);

	//	Update custom controls.
	EnableWindow(*m_WebcamControl, enable);
	EnableWindow(timelineControl, enable);

	//	If controls are to be enabled, update selection controls properly.
	if (enable)
		UpdateSelectionControls();
}

void CCustomWindow::UpdateSelectionControls()
{
	Overlay *overlay = timeline.ActiveOverlay;
	if (!overlay)
		overlay = timeline.GetOverlayForFrame(timeline.CurrentFrame);

	bool enable = (0 != overlay);

	DeleteOverlayButton.set_state((!enable) ? STATE_DISABLED : 0, (enable) ? STATE_DISABLED : 0, true);
	DeleteContentButton.set_state((!enable) ? STATE_DISABLED : 0, (enable) ? STATE_DISABLED : 0, true);
	AddTextButton.set_state((!enable) ? STATE_DISABLED : 0, (enable) ? STATE_DISABLED : 0, true);

	bool enableAudioOperations = hasSound && enable;
	SoundControlButton.set_state((!enableAudioOperations) ? STATE_DISABLED : 0, 
		(enableAudioOperations) ? STATE_DISABLED : 0, true);
	if (hasSound && enable)
	{
		if (overlay->EnableSound)
			SoundControlButton.set_text(L" Sound On ");
		else
			SoundControlButton.set_text(L" Sound Off ");
		SoundControlButton.update(true);
	}

	bool enableVideoOperations = hasVideo && enable;
	WebcamControlButton.set_state((!enableVideoOperations) ? STATE_DISABLED : 0, 
		(enableVideoOperations) ? STATE_DISABLED : 0, true);
	EnableWindow(*m_WebcamControl, enableVideoOperations);
	if (hasVideo)
	{
		WebcamControlButton.update(true);
		Viewer->SetWebcamVideo(!overlay || overlay->EnableWebcam);
		if (enable)
		{
			if (overlay->EnableWebcam)
				WebcamControlButton.set_text(L" Camera On ");
			else
				WebcamControlButton.set_text(L" Camera Off ");

			Viewer->MoveWebStream(overlay->WebcamX, overlay->WebcamY);
		}
		else
			Viewer->MoveWebStream(g_DefaultWebcamX, g_DefaultWebcamY);
	}
}

void CCustomWindow::TimelineChanged()
{
}

void CCustomWindow::TimelineCurrentFrameChanged()
{
	Viewer->SetPFrame(params.GetRealFrameNumber(timeline.CurrentFrame));
	UpdateSelectionControls();
}

void CCustomWindow::TimelineActiveOverlayChanged()
{
	if (timeline.ActiveOverlay)
		timeline.SetCurrentFrameDontActivateOverlay(timeline.ActiveOverlay->Start);

	//	Change overlay in overlay control.
	overlayControl.ActiveOverlay = timeline.ActiveOverlay;
	InvalidateRect(HPictureWnd, 0, TRUE);
}

void CCustomWindow::OverlayActiveElementChanged()
{
	if (timeline.ActiveOverlay)
		overlayElementEditorManager.ShowEditorFor(HWnd, timeline.ActiveOverlay->ActiveElement);
	else
		overlayElementEditorManager.ShowEditorFor(HWnd, 0);
}

void CCustomWindow::OverlayRightMouseButtonClicked(OverlayElement *element, const POINT *pt)
{
	if (!element)
		return;

	HMENU menu = LoadMenu(HInstance, MAKEINTRESOURCE(IDR_OVERLAY_MENU));
	if (!menu)
		return;

	HMENU overlayMenu = GetSubMenu(menu, 0);
	if (!overlayMenu)
		return;

	BOOL menuResult = TrackPopupMenu(overlayMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY | TPM_RETURNCMD, 
		pt->x, pt->y, 0, HWnd, 0);

	switch (menuResult)
	{
	case ID_OVERLAYMENU_PROPERTIES:
		overlayElementEditorManager.ShowCurrentEditor();
		break;

	case ID_OVERLAYMENU_DELETE:
		if (timeline.ActiveOverlay)
		{
			overlayElementEditorManager.DestroyCurrentEditor();
			timeline.ActiveOverlay->Delete(element);
		}
		break;
	}
}

void CCustomWindow::ParseFileName(const TCHAR *fileName)
{
	//	Determine file type.
	TCHAR *type = PathFindExtension(fileName);

	//	Save base file name.
	baseFileName.assign(fileName, type - fileName);
}

void CCustomWindow::OpenFile(TCHAR *file)
{
	const TCHAR *fileName = file;
    FileSelector selector;
	string_t videoFile;

	//	Select file, if not specified.
	if (!file)
	{
		if (selector.Select(HWnd, SelectToOpen, 0, FileTypeCut))
			fileName = selector.FileName();
		else
			return;
	}

	//	Get base file name off the file name.
	ParseFileName(fileName);
	videoFile = baseFileName + g_VideoExtension;

	freezePreview = true;

	if (Viewer->Open(videoFile.c_str()))
	{
		m_WebcamControl->SetCamera(0, 100);

		SIZE main = { 0 }, camera = { 0 };
		if (Viewer->WebcamPresent())
		{
			//	Update streams geometry in camera control.
			main.cx = Viewer->GetMainWidth();
			main.cy = Viewer->GetMainHeight();
			camera.cx = Viewer->GetWebWidth();
			camera.cy = Viewer->GetWebHeight();
		}
		m_WebcamControl->SetGeometry(main, camera);

		hasVideo = Viewer->WebcamPresent();
		hasSound = Viewer->AudioPresent();

		Viewer->SetWebcamVideo(true);
		if (!hasVideo)
			WebcamControlButton.set_text(L" No Camera ");

		if (!hasSound)
			SoundControlButton.set_text(L" No Sound ");

		// Params
		params.Clear();
		VideoDown = false;
		NewArrow = false;
		DownX = 0;
		DownY = 0;

		//	Initialize timeline.
		string_t cutFile = baseFileName + g_CutExtension;
		if (FileExists(cutFile.c_str()))
			timeline.Load(cutFile.c_str());
		else
			timeline.Initialize(Viewer->GetFramesCount());						//	Initialize timeline from scratch.

		Viewer->SetPFrame(params.GetRealFrameNumber(0));

		UpdateAllControls(true, false);
	}

	freezePreview = false;
	InvalidateRect(HPictureWnd, 0, TRUE);
}

void CCustomWindow::Save(bool saveAs)
{
	string_t cutFile, oldCutFile, oldVideoFile, videoFile;

	oldVideoFile = baseFileName + g_VideoExtension;								//	NOTE: even if timeline is fresh, the video file IS THERE and base file name is known!
	if (!timeline.Fresh)
		oldCutFile = baseFileName + g_CutExtension;

	if (timeline.Fresh || saveAs)
	{
		string_t defaultCutFile = baseFileName + g_CutExtension;

		FileSelector selector;
		if (selector.Select(HWnd, SelectToSave, defaultCutFile.c_str(), FileTypeCut))
		{
			cutFile = selector.FileName();
			ParseFileName(cutFile.c_str());					//	Get base file name off the file name.
		}
		else
			return;
	}
	else
	{
		cutFile = baseFileName + g_CutExtension;
	}

	if (timeline.Save(cutFile.c_str()))
	{
		if (!oldCutFile.empty() && oldCutFile != cutFile)
			DeleteFile(oldCutFile.c_str());
	}
	else
		goto Error;

	videoFile = baseFileName + g_VideoExtension;
	//	If the file name changed, rename the video file as well.
	if (videoFile != oldVideoFile)
	{
		if (!MoveFileEx(oldVideoFile.c_str(), videoFile.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
			goto Error;
	}

	return;

Error:
	MessageBox(HWnd, _T("An error occured while saving recording cut. Please make sure you are not trying to overwrite read-only file."), 
		_T("Error"), MB_OK | MB_ICONERROR);
}

void CCustomWindow::OverlayElementChanged()
{
	DrawOverlay();
}
