#include "StdAfx.h"
#include "CustomWindow.h"

#include <atlconv.h>

#include "VersionInfo.h"
#include "Folders.h"
#include "RegionSelector.h"
#include "SoundSetup.h"
#include "utils.h"
#include "Web.h"
#include "Branding.h"

namespace WebinariaApplication
{
	namespace WebinariaUserInterface
	{
		HINSTANCE CCustomWindow::HInstance = 0;
		IMediaEventEx * CCustomWindow::pME = NULL;
		wchar_t * CCustomWindow::Caption = NULL;
		CAuthorization * CCustomWindow::authdialog = NULL;

		// Here we import the function from USER32.DLL 
		HMODULE CCustomWindow::hUser32 = GetModuleHandle(_T("USER32.DLL")); 
		lpfnSetLayeredWindowAttributes CCustomWindow::SetLayeredWindowAttributes = 
		(lpfnSetLayeredWindowAttributes)GetProcAddress(hUser32, "SetLayeredWindowAttributes"); 

		const wchar_t CLASSNAME[] = L"WUI::CustomWindow";

		//////////////////////////////////////////////////////////////////////////
		//							Public methods								//
		//////////////////////////////////////////////////////////////////////////

		CCustomWindow::CCustomWindow(	const int & X, 
										const int & Y, 
										const int & Width, 
										const int & Height,
										CDeviceEnumerator * Enum,
										const wchar_t* CaptionText):event_handler(HANDLE_BEHAVIOR_EVENT),
																	TrayIcon(NULL),
																	WebElement(NULL),
																	WebComboBox(0),
																	DevEnum(Enum),
																	WebElementCount(1),
																	AudElementCount(1),
																	curWeb(NULL),
																	curAud(NULL),
																	microphone(NULL),
																	paused(false),
																	skipWMActivate(false)
		{
			Caption = new wchar_t[wcslen(CaptionText)+1];
			wcscpy(Caption, CaptionText);

			UINT style =	WS_POPUP | WS_SYSMENU | WS_EX_LAYERED;

			// Create main window
			HWnd = CreateWindowW(CLASSNAME, CaptionText, style ,X, Y, Width, Height, NULL, NULL, HInstance, NULL);

			// Save window associated data
			self(HWnd,this);

			// Create tray button
			TrayIcon = new WUI::CTrayIcon(HWnd, HInstance);

			microphone = new CMicrophone(HWnd, HInstance);
			
			authdialog = new CAuthorization(HWnd, HInstance, L"Proxy authorization");

			// Callback for HTMLayout document elements
			HTMLayoutSetCallback(HWnd,&callback,this);

			ResetCaptureRegion();

			keyRecord = GlobalAddAtom(_T("Webinaria.Record"));
			RegisterHotKey(HWnd, keyRecord, MOD_ALT | MOD_WIN, (int)'R');
			keyPause = GlobalAddAtom(_T("Webinaria.Pause"));
			RegisterHotKey(HWnd, keyPause, MOD_ALT | MOD_WIN, (int)'P');
			keyStop = GlobalAddAtom(_T("Webinaria.Stop"));
			RegisterHotKey(HWnd, keyStop, MOD_ALT | MOD_WIN, (int)'S');

            TCHAR ui[MAX_PATH];
            Folders::FromRecorderUI(ui, _T("default.htm"));

			// Loading document elements
            if (HTMLayoutLoadFile(HWnd, ui))
			{
				htmlayout::dom::element r = Root();

				Body					= r.find_first("body");
				WindowCaption			= r.get_element_by_id("caption");
				MinimizeButton			= r.get_element_by_id("minimize");
				IconButton				= r.get_element_by_id("icon");
				CloseButton				= r.get_element_by_id("close");
				//СornerButton		= r.get_element_by_id("corner");
				StatusBar				= r.get_element_by_id("statusbar");

				RecordButton			= r.get_element_by_id("Record");
				PauseButton		= r.get_element_by_id("Pause");
				StopButton				= r.get_element_by_id("Stop");
				HideWindowWhenRecording = r.get_element_by_id("HideWindowWhenRecording");

				WebCameraSelectBox		= r.get_element_by_id("Device");
				FormatButton			= r.get_element_by_id("Format");
				SourceButton			= r.get_element_by_id("Source");

				SoundDeviceSelectBox	= r.get_element_by_id("SoundDevice");
				SoundTestButton			= r.get_element_by_id("SoundTest");

				AudioNarrationCheckBox	= r.get_element_by_id("Narration");
				WebcamVideoCheckBox		= r.get_element_by_id("Webcam");
				
				ScreenRadioButton		= r.get_element_by_id("Screen");
				ActiveWindowRadioButton	= r.get_element_by_id("Window");
				CustomAreaRadioButton	= r.get_element_by_id("Custom");
				CustomAreaInfo			= r.get_element_by_id("CustomAreaInfo");
				SelectRegionButton      = r.get_element_by_id("SelectRegion");

				FrameRate				= r.get_element_by_id("FrameRate");

				TextWritingCheckBox		= r.get_element_by_id("Text");
				ArrowPointerCheckBox	= r.get_element_by_id("Arrow");
				CompressQualitySlider	= r.get_element_by_id("QulitySlider");
				CompressQualityEdit		= r.get_element_by_id("QulityEdit");

				HelpForumButton			= r.get_element_by_id("Forum");
				UpdateButton			= r.get_element_by_id("Updates");
				UnlockButton			= r.get_element_by_id("Unlock");

				UnlockButton.set_style_attribute("display", L"none");

				HardwareTab				= r.get_element_by_id("second");

				htmlayout::attach_event_handler(r, this);

				WebComboBox = htmlayout::dom::element::create("select");
				WebElement = new htmlayout::dom::element[1];
				WebElement[0] = htmlayout::dom::element::create("option",L"(No device)");
				WebComboBox.append(WebElement[0]);
				WebCameraSelectBox.append(WebComboBox);

				AudComboBox = htmlayout::dom::element::create("select");
				AudElement = new htmlayout::dom::element[1];
				AudElement[0] = htmlayout::dom::element::create("option",L"(No device)");
				AudComboBox.append(AudElement[0]);
				SoundDeviceSelectBox.append(AudComboBox);

				SetWindowCaption();
				UpdateRecordControls(true, false, false);
			}
		}

		CCustomWindow::~CCustomWindow(void)
		{
			SAFE_RELEASE(pME);
			if (TrayIcon != NULL)
				delete TrayIcon;
			if (Caption != NULL)
				delete[] Caption;
			/*if (microphone != NULL)
				delete microphone;*/
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

				if (WindowCaption.is_valid())
				{
					WindowCaption.set_text(Caption);
					WindowCaption.update(true);
				}

				wchar_t info[150];

				if (StatusBar.is_valid())
				{
					wchar_t info[150];

					_stprintf_s(info, _countof(info), L"Screen area to record: %d x %d", 
						captureRegion.right - captureRegion.left, captureRegion.bottom - captureRegion.top);

					StatusBar.set_text(info);
					StatusBar.update(true);
				}

				if (CustomAreaInfo.is_valid())
				{
					_stprintf_s(info, _countof(info), L"%d, %d; %d x %d", captureRegion.left, captureRegion.top, 
						captureRegion.right - captureRegion.left, captureRegion.bottom - captureRegion.top);

					CustomAreaInfo.set_text(info);
					CustomAreaInfo.update(true);
				}
			}
		}
		
		// Set media event handler
		void CCustomWindow::SetME(IMediaEventEx * NewME)
		{
			pME = NewME;
		}

		// Reset screen capture parameters
		void CCustomWindow::ResetScreenParameters()
		{
			WL::CRecorder::SetTop(captureRegion.top);
			WL::CRecorder::SetLeft(captureRegion.left);
			WL::CRecorder::SetRight(captureRegion.right);
			WL::CRecorder::SetBottom(captureRegion.bottom);

			//	Get selected frame rate.
			htmlayout::value_t selectedRate = htmlayout::get_select_value(FrameRate);
			int frameRate = selectedRate.get(5);
			if (frameRate < 5 || frameRate > 15)
				frameRate = 5;

			WL::CRecorder::SetFrameRate((float)frameRate);

			SetWindowCaption();
		}

		// Flash tray
		void CCustomWindow::FlashTray(const bool & NormalState)
		{
			TrayIcon->Flash(NormalState);
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
		bool CCustomWindow::IsMinimized(HWND hWnd) const
		{
			WINDOWPLACEMENT wp;
			// Get window placement state
			GetWindowPlacement(HWnd,&wp);
			return wp.showCmd == SW_SHOWMINIMIZED;
		}

		// Run default html-browser with Webinaria Help Forum
		void CCustomWindow::HelpForumExecute(void)
		{
			Web::OpenForums();
		}
		
		// Run Webinaria Update Service
		DWORD WINAPI CCustomWindow::UpdateExecute(LPVOID lpParam)
		{
			//	Build own version string.
			TCHAR myVersion[256];
			_stprintf_s(myVersion, _countof(myVersion), _T("%d.%d.%d.%d"), 
				VersionInfo::ModuleVersion().Major, VersionInfo::ModuleVersion().Minor, VersionInfo::ModuleVersion().Release, VersionInfo::ModuleVersion().Build);

			//	Get latest version on the web.
			TCHAR version[256];
			if (Web::GetCurrentVersion(version, _countof(version)))
			{
				if (_tcscmp(myVersion, version))
				{
					if (IDYES == MessageBox(0, _T("There's a new version of the software available for download. Do you want to update?"), 
						_T("Information"), MB_ICONINFORMATION | MB_YESNO))
					{
						Web::OpenUpdates();
					}
				}
				else
				{
					MessageBox(0, _T("You are using latest version of the software."), _T("Information"), MB_OK);
				}
			}
			else
			{
				MessageBox(0, _T("An error occured when trying to get version information. Please try again later."), 
					_T("Information"), MB_ICONERROR | MB_OK);
			}

			return 0;
		}

		void CCustomWindow::UpdateControlsDisabledState(wchar_t *state)
		{
			AudioNarrationCheckBox.set_attribute("disabled", state);
			WebcamVideoCheckBox.set_attribute("disabled", state);

			ScreenRadioButton.set_attribute("disabled", state);
			ActiveWindowRadioButton.set_attribute("disabled", state);
			CustomAreaRadioButton.set_attribute("disabled", state);
			SelectRegionButton.set_attribute("disabled", state);

			HideWindowWhenRecording.set_attribute("disabled", state);

			FrameRate.set_attribute("disabled", state);

			TextWritingCheckBox.set_attribute("disabled", state);
			ArrowPointerCheckBox.set_attribute("disabled", state);
			CompressQualitySlider.set_attribute("disabled", state);
			CompressQualityEdit.set_attribute("disabled", state);

			SoundTestButton.set_attribute("disabled", state);
			SourceButton.set_attribute("disabled", state);
		}

		void CCustomWindow::UpdateRecordControls(bool canRecord, bool canPause, bool canStop)
		{
			RecordButton.set_attribute("disabled", (canRecord) ? 0 : L"true");
			EnableMenuItem(TrayIcon->Menu(), ID_RECORD, ((canRecord) ? MF_ENABLED : MF_GRAYED) | MF_BYCOMMAND);
			PauseButton.set_attribute("disabled", (canPause) ? 0 : L"true");
			EnableMenuItem(TrayIcon->Menu(), ID_PAUSERESUME, ((canPause) ? MF_ENABLED : MF_GRAYED) | MF_BYCOMMAND);
			StopButton.set_attribute("disabled", (canStop) ? 0 : L"true");
			EnableMenuItem(TrayIcon->Menu(), ID_STOP, ((canStop) ? MF_ENABLED : MF_GRAYED) | MF_BYCOMMAND);

			RecordButton.update(true);
			PauseButton.update(true);
			StopButton.update(true);
		}

		void CCustomWindow::Show()
		{
			ShowWindow(HWnd, SW_SHOWNORMAL);
			skipWMActivate = false;
		}

		void CCustomWindow::Hide()
		{
			skipWMActivate = true;
			ShowWindow(HWnd, SW_HIDE);
		}

		bool CCustomWindow::ShouldHideWindowWhenRecording() const
		{
			return (HideWindowWhenRecording.is_valid() && (0 != (HideWindowWhenRecording.get_state() & STATE_CHECKED)));
		}

		BOOL CCustomWindow::on_event(HELEMENT he, HELEMENT target, BEHAVIOR_EVENTS type, UINT reason )
		{
			if (type == 32769)
			{
				WL::CRecorder::SetAdditionalFlag(WL::AF_CHANGE_SIZE, true);
				return FALSE;
			}
			// If minimize button click
			if (target == MinimizeButton)
			{
				// Minimize window
				::ShowWindow(HWnd,SW_HIDE); 
				return TRUE;
			}

			if (type == 32912)
			/*for (int i = 0; i<WebComboBox.children_count();++i)
			if ( WebComboBox.child(i) == target)*/
			{
				LoadDevicesToList();
				return TRUE;
			}

			if (type == BUTTON_PRESS)
			{
				if (target == ScreenRadioButton)
				{
					WL::CRecorder::SetAdditionalFlag(WL::AF_ACTIVE_WINDOW, false);
					WL::CRecorder::SetBorders(0, 0, 0xFFFFFFF, 0xFFFFFFF);

					GetWindowRect(GetDesktopWindow(), &captureRegion);

					SetWindowCaption();
					return TRUE;
				}
				if (target == ActiveWindowRadioButton)
				{
					// Get nearest active window
					HWND hActiveWnd;
					if (!GetLastActiveWindowHandleAndRect(&hActiveWnd, &captureRegion))
						return TRUE;

					WL::CRecorder::SetBorders(captureRegion.left, captureRegion.top, captureRegion.right, captureRegion.bottom);
					WL::CRecorder::SetAdditionalFlag(WL::AF_ACTIVE_WINDOW, true);

					SetWindowCaption();
					return TRUE;
				}
				if (target == CustomAreaRadioButton)
				{
					WL::CRecorder::SetAdditionalFlag(WL::AF_ACTIVE_WINDOW, false);
					WL::CRecorder::SetBorders(captureRegion.left, captureRegion.top, captureRegion.right, captureRegion.bottom);

					SetWindowCaption();
					return TRUE;
				}
			}

			if (type == BUTTON_PRESS)
			{
				
				if (target == AudioNarrationCheckBox)
				{
					WL::CRecorder::SetAdditionalFlag(WL::AF_CAP_AUDIO,
													!(WL::CRecorder::GetAdditionalFlag(WL::AF_CAP_AUDIO)));
					return TRUE;
				}
				else if (target == WebcamVideoCheckBox)
				{
					WL::CRecorder::SetAdditionalFlag(WL::AF_CAP_CAMERA,
													!(WL::CRecorder::GetAdditionalFlag(WL::AF_CAP_CAMERA)));
					/*if (WL::CRecorder::GetAdditionalFlag(WL::AF_CAP_CAMERA))
						TransparentBackground.set_attribute("disabled",NULL);
					else
						TransparentBackground.set_attribute("disabled",L"true");*/
					return TRUE;
				}
				/*else if (target == TransparentBackground)
				{
					WL::CRecorder::SetAdditionalFlag(WL::AF_REMOVE_BACK,
													!(WL::CRecorder::GetAdditionalFlag(WL::AF_REMOVE_BACK)));
					return TRUE;
				}*/
				else if (target == TextWritingCheckBox)
				{
					WL::CRecorder::SetAdditionalFlag(WL::AF_SET_TEXT,
													!(WL::CRecorder::GetAdditionalFlag(WL::AF_SET_TEXT)));
					return TRUE;
				}
				else if (target == ArrowPointerCheckBox)
				{
					WL::CRecorder::SetAdditionalFlag(WL::AF_SET_ARROW,
													!(WL::CRecorder::GetAdditionalFlag(WL::AF_SET_ARROW)));
					return TRUE;
				}
			}
			
			if (type == SELECT_SELECTION_CHANGED)
			{
				htmlayout::dom::element cap = WebComboBox.child(0);
				for (unsigned short i = 0; i<WebElementCount;++i)
					if (wcscmp(cap.text(),WebElement[i].text()) == 0)
					{
						DevEnum->SetVideoDevice(i);
						break;
					}

				cap = AudComboBox.child(0);
				if(SoundSetup::HasMixer((TCHAR *)cap.text()))
				{
					SoundTestButton.set_attribute("disabled", 0);
				}
				else
				{
					SoundTestButton.set_attribute("disabled", L"true");
				}
				for (unsigned short i = 0; i<AudElementCount;++i)
					if (wcscmp(cap.text(),AudElement[i].text()) == 0)
					{
						DevEnum->SetAudioDevice(i);
						break;
					}

				WL::CRecorder::ChangeDevice();


				/*ofstream f;
				f.open("C:\\temp.txt",ios_base::out);
				f<<WebComboBox.get_html(true)<<"\r\n";
				f<<"-------------------------"<<"\r\n";
				f.close();*/
				return TRUE;
			}
			/*if(target == ConfigureButton)
			{
				LPBYTE ht;
				HTMLayoutGetElementHtml(VideoCodecSelectBox,&ht,true);
				fstream f;
				f.open("ht.ht",ios_base::out);
					f<<ht;
				f.close();
			}*/

			if (SelectRegionButton == target && BUTTON_PRESS == type)
			{
				Hide();
				if (RegionSelector::SelectRegion(HWnd, &captureRegion))
				{
					ScreenRadioButton.set_state(0, STATE_CHECKED, true);
					ActiveWindowRadioButton.set_state(0, STATE_CHECKED, true);
					CustomAreaRadioButton.set_state(STATE_CHECKED, 0, true);
				}
				Show();

				return TRUE;
			}

			if (target == RecordButton && type == BUTTON_PRESS)
			{
				if (!paused)
				{
					SetWindowCaption();

					WL::CRecorder::Record();

					UpdateControlsDisabledState(L"true");
				}
				else
				{
					WL::CRecorder::PauseResume();
					paused = false;
				}

				UpdateRecordControls(false, true, true);

				return TRUE;
			}

			if (target == PauseButton && type == BUTTON_PRESS)
			{
				WL::CRecorder::PauseResume();
				paused = true;

				UpdateRecordControls(true, false, true);

				return TRUE;
			}

			if (target == StopButton && type == BUTTON_PRESS)
			{
				WL::CRecorder::Stop();
				paused = false;

				ShowWindow(HWnd,SW_NORMAL);

				UpdateControlsDisabledState(0);
				UpdateRecordControls(true, false, false);

				return TRUE;
			}

			if (target == HelpForumButton && type == BUTTON_PRESS)
			{
				HelpForumExecute();
				return TRUE;
			}

			if (target == UpdateButton && type == BUTTON_PRESS)
			{
				DWORD trID;
				LPVOID empty=NULL;
				CreateThread(NULL, 0, &UpdateExecute, empty, 0, &trID);
				return TRUE;
			}

			if (target == FormatButton && type == BUTTON_PRESS)
			{
				DevEnum->ShowFromatDialog();
				return TRUE;
			}

			if (target == SourceButton && type == BUTTON_PRESS)
			{
				DevEnum->ShowSourceDialog();
				return TRUE;
			}

			if (target == SoundTestButton && type == BUTTON_PRESS)
			{
				htmlayout::dom::element cap = AudComboBox.child(0);
				HRESULT hr = SoundSetup::Show(HWnd, (TCHAR *)cap.text());
				if(FAILED(hr))
				{
					CError::ErrMsg(NULL, L"Can not create the level adjusting dialog");
				}
				return TRUE;
			}

			/*if( target == MaximizeButton)
			{
			  if( is_maximized())
				::ShowWindow(hwnd,SW_RESTORE); 
			  else
				::ShowWindow(hwnd,SW_MAXIMIZE); 
		      
			  return TRUE;
			}*/

			// If close button click
			if( target == CloseButton && type == BUTTON_PRESS)
			{
				// Post close message
				::PostMessage(HWnd, WM_CLOSE, 0,0); 
				return TRUE;
			}
			return FALSE;
		}

		void CCustomWindow::SendRecord()
		{
			htmlayout::dom::element root = htmlayout::dom::element::root_element(HWnd);
			root.send_event(BUTTON_PRESS, 0, root.get_element_by_id("Record"));
		}

		void CCustomWindow::SendPause()
		{
			htmlayout::dom::element root = htmlayout::dom::element::root_element(HWnd);
			root.send_event(BUTTON_PRESS, 0, root.get_element_by_id("Pause"));
		}

		void CCustomWindow::SendStop()
		{
			htmlayout::dom::element root = htmlayout::dom::element::root_element(HWnd);
			root.send_event(BUTTON_PRESS, 0, root.get_element_by_id("Stop"));
		}

		void CCustomWindow::ResetCaptureRegion()
		{
			captureRegion.left = 100;
			captureRegion.right = 400;
			captureRegion.top = 100;
			captureRegion.bottom = 400;
		}

		bool CCustomWindow::GetLastActiveWindowHandleAndRect(HWND *pActiveWindow, RECT *pRect)
		{
			HWND hActiveWnd = GetWindow(HWnd, GW_HWNDNEXT);
			while (hActiveWnd)
			{
				if (IsWindowVisible(hActiveWnd))
					break;

				hActiveWnd = GetWindow(hActiveWnd, GW_HWNDNEXT);
			}
			if (!hActiveWnd)
			{
				*pActiveWindow = 0;
				ResetCaptureRegion();
				return false;
			}

			*pActiveWindow = hActiveWnd;
			GetWindowRect(hActiveWnd, pRect);

			//	Workaround for maximized windows.
			if (captureRegion.left < 0)
			{
				captureRegion.right = RectWidth(captureRegion);
				captureRegion.left = 0;
			}
			if (captureRegion.top < 0)
			{
				captureRegion.bottom = RectHeight(captureRegion);
				captureRegion.top = 0;
			}

			return true;
		}

		// Handle main window events
		LRESULT CALLBACK CCustomWindow::CustomWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			LRESULT lResult;
			BOOL    bHandled;

		// HTMLayout +
			// HTMLayout could be created as separate window using CreateWindow API.
			// But in this case we are attaching HTMLayout functionality
			// to the existing window delegating windows message handling to 
			// HTMLayoutProcND function.
			lResult = HTMLayoutProcND(hWnd,message,wParam,lParam, &bHandled);
			if(bHandled)
			  return lResult;
		// HTMLayout -

			CCustomWindow* me = self(hWnd);

			switch (message) 
			{
			case WM_HOTKEY:
				if (me->keyRecord == wParam)
					me->SendRecord();
				else if (me->keyPause == wParam)
					me->SendPause();
				if (me->keyStop == wParam)
					me->SendStop();

				return 0;

			case WM_COMMAND:
				{
					UINT uID; 
					UINT uMsg; 
	
					uID = (UINT) wParam;				// Extract element ID
					uMsg = (UINT) lParam;				// Extract message

					switch (uID)
					{
					case ID_RECORD:
					case ID_RECORD_KEY:
					case 105555:
						{
							me->SendRecord();
							return 0;
						}
						break;
					case ID_PAUSERESUME:
					case ID_PAUSERESUME_KEY:
					case 105556:
						{
							me->SendPause();
							return 0;
						}
					case ID_STOP:
					case ID_STOP_KEY:
					case 105557:
						{
							me->SendStop();
							return 0;
						}
					case ID_HELP40008:
						me->HelpForumExecute();
						return 0;
					case ID_UPDATE:
						{
							DWORD trID;
							LPVOID empty=NULL;
							CreateThread(NULL, 0, UpdateExecute, empty, 0, &trID);
							return 0;
						}
					case ID_EXIT:
						SendMessage(hWnd,WM_CLOSE,0,0);
						return 0;
					}
				}

				break;

			case WM_SYSCOMMAND:
				{
					switch (wParam)
					{
					case SC_MINIMIZE:
						{
							::ShowWindow(hWnd,SW_HIDE);
							me->TrayIcon->Update();
						}
						return 0;
					}
				}
				break;

			case WM_NCHITTEST:
				{
					// Update HTMLayout window
					htmlayout::dom::element r = htmlayout::dom::element::root_element(hWnd);
					r.update(true);
					// Test hit
					if(me)
						return me->HitTest( GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) );
				}
			
			case WM_ACTIVATE:
				{
					if (me->skipWMActivate || WA_INACTIVE == wParam || (0 == (me->ActiveWindowRadioButton.get_state() & STATE_CHECKED)))
						return 0;

					if (WL::CRecorder::GetCurrentRecorderCommand() != WL::RC_RECORD)
					{
						me->TrayIcon->Update();

						// Get nearest active window
						HWND hActiveWnd;
						if (!me->GetLastActiveWindowHandleAndRect(&hActiveWnd, &me->captureRegion))
							return 0;

						wchar_t text[255], form_text[40];

						// Set text to the options of active window name
						wcscpy(form_text,L"\"\0");
						GetWindowTextW(hActiveWnd,text,255);
						if (wcslen(text)>34)
						{
							wcsncat(form_text,text,34);
							wcscat(form_text,L"...\"");
						}
						else
						{
							wcscat(form_text,text);
							wcscat(form_text,L"\"");
						}
						htmlayout::dom::element r = htmlayout::dom::element::root_element(hWnd);
						htmlayout::dom::element ActiveWindowCaption = r.get_element_by_id("ActiveWindowCaption");
						ActiveWindowCaption.set_text(form_text);
						ActiveWindowCaption.update(true);

						if	(WL::CRecorder::GetAdditionalFlag(WL::AF_ACTIVE_WINDOW) && 
							(WL::CRecorder::GetCurrentRecorderCommand() != WL::RC_RECORD) &&
							(WL::CRecorder::GetCurrentRecorderState() != WL::RS_RECORDING))
						{
							RECT rc;

							WL::CRecorder::SetBorders(me->captureRegion.left, me->captureRegion.top, 
								me->captureRegion.right, me->captureRegion.bottom);

							me->SetWindowCaption();
						}
					}
					return 0;
				}

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

			case WM_TRAY_ICON_NOTIFY_MESSAGE:
				{
					//if (TrayIcon != NULL)
					if (CTrayIcon::NotifyMsgProc(hWnd,wParam,lParam) == UA_RECORD_STOP)
						WL::CRecorder::RecordStop();
					return 0;
				}

			case WM_NCPAINT:     
				{
					// Check the current state of the dialog, and then add the WS_EX_LAYERED attribute 
					SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
					// Sets the window to 0 visibility for green colour for window coners
					SetLayeredWindowAttributes(hWnd,RGB(0,255,0), 0, LWA_COLORKEY); 
				}
				return 0; 

			case WM_FGNOTIFY:
				{
					// Something went wrong while capturing - the filtergraph will send us events 
					// like EC_COMPLETE, EC_USERABORT and the one we care about, EC_ERRORABORT.
					if(pME)
					{
						LONG event, l1, l2;
						HRESULT hrAbort = S_OK;
						bool bAbort = false;
						while(pME->GetEvent(&event, (LONG_PTR*)&l1, (LONG_PTR*)&l2, 0) == S_OK)
						{
							pME->FreeEventParams(event, l1, l2);
							if(event == EC_ERRORABORT)
							{
								WL::CRecorder::Stop();
								bAbort = true;
								hrAbort = l1;
								continue;
							}
							else if(event == EC_DEVICE_LOST)
							{
									// Check if we have lost a capture filter being used.
									// lParam2 of EC_DEVICE_LOST event == 1 indicates device added
									//                                 == 0 indicates device removed
									if(l2 == 0)
									{
										/*IBaseFilter *pf;
										IUnknown *punk = (IUnknown *) (LONG_PTR) l1;
										if(S_OK == punk->QueryInterface(IID_IBaseFilter, (void **)&pf))
										{
											if(::IsEqualObject(gcap.pVCap, pf))
											{
												pf->Release();
												bAbort = false;*/
												WL::CRecorder::Stop();
												/*CError::ErrMsg(hWnd,TEXT("Stopping Capture (Device Lost). Select New Capture Device\0"));
												break;
											}
											pf->Release();
										}*/
									}
							}
						} // end while
						/*if(bAbort)
						{
								if(gcap.fWantPreview)
								{
									BuildPreviewGraph();
									StartPreview();
								}
								TCHAR szError[100];
								wsprintf(szError, TEXT("ERROR during capture, error code=%08x\0"), hrAbort);
								ErrMsg(szError);
						}*/
					}
				}
				return 0;

			case WM_DEVICECHANGE:
				{
					// We are interested in only device arrival & removal events
					if(DBT_DEVICEARRIVAL != wParam && DBT_DEVICEREMOVECOMPLETE != wParam)
						break;

					PDEV_BROADCAST_HDR pdbh = (PDEV_BROADCAST_HDR) lParam;
					if(pdbh->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE)
					{
						break;
					}

					PDEV_BROADCAST_DEVICEINTERFACE pdbi = (PDEV_BROADCAST_DEVICEINTERFACE) lParam;
					// Check for capture devices.
					if(pdbi->dbcc_classguid != AM_KSCATEGORY_CAPTURE)
					{
						break;
					}

					// Check for device arrival/removal.
					/*if(DBT_DEVICEARRIVAL == wParam || DBT_DEVICEREMOVECOMPLETE == wParam)
					{
						fDeviceMenuPopulated = false;
					}*/
				}
				break;

			case WM_CLOSE:
				// Destroy window
				::DestroyWindow(hWnd);
				return 0;

			case WM_DESTROY:
				// Delete window class
				me = NULL;
				self(hWnd,0);
				PostQuitMessage(0);
				return 0;
			}
			return DefWindowProc(hWnd, message, wParam, lParam);
		}

		// Load capturing devices to WebComboBox
		void CCustomWindow::LoadDevicesToList()
		{	
			wchar_t ** Names;
			unsigned short n = 0, cur = 0;
			// Load video devices names to comboboxa
			DevEnum->GetVideoDevices(Names, n, cur);
			if (WebElement!=NULL)
			{
				WebComboBox.destroy();
				for (int i = 0; i<WebElementCount; ++i)
					WebElement[i].destroy();				
				delete[] WebElement;
			}
			if ( n > 0 && Names != NULL)
			{
				WebComboBox = htmlayout::dom::element::create("select");
				WebComboBox.set_attribute("type",L"select-dropdown");
				WebComboBox.set_attribute("id",L"WebCom");
				if (WL::CRecorder::GetCurrentRecorderState() != WL::RS_STOP)
					WebComboBox.set_attribute("disabled",L"true");
				WebElement = new htmlayout::dom::element[n];
				for( unsigned short i = 0;i<n;++i)
				{
					WebElement[i] = htmlayout::dom::element::create("option",Names[i]);
					if (i == cur)
						WebElement[i].set_attribute("selected",L"");
					WebComboBox.append(WebElement[i]);
				}
				WebElementCount = n;
				WebCameraSelectBox.append(WebComboBox);
				WebCameraSelectBox.update(true);
			
				for (unsigned short i = 0; i<WebElementCount;++i)
					delete[] Names[i];
				delete[] Names;
			}
			else
			{
				WebComboBox = htmlayout::dom::element::create("select");
				WebElement = new htmlayout::dom::element[1];
				WebElement[0] = htmlayout::dom::element::create("option",L"(No device)");
				WebComboBox.append(WebElement[0]);
				WebCameraSelectBox.append(WebComboBox);
				WebElementCount = 1;
			}
			n = 0;
			cur = 0;

			// Load audio devices names to combobox
			DevEnum->GetAudioDevices(Names, n, cur);
			if (AudElement!=NULL)
			{
				AudComboBox.destroy();
				for (int i = 0; i<AudElementCount; ++i)
					AudElement[i].destroy();				
				delete[] AudElement;
			}
			if ( n > 0 && Names != NULL)
			{
				AudElementCount = n;

				AudComboBox = htmlayout::dom::element::create("select");
				AudComboBox.set_attribute("type",L"select-dropdown");
				AudComboBox.set_attribute("id",L"AudCom");
				if (WL::CRecorder::GetCurrentRecorderState() != WL::RS_STOP)
					AudComboBox.set_attribute("disabled",L"true");
				AudElement = new htmlayout::dom::element[AudElementCount];
				for( unsigned short i = 0;i<AudElementCount;++i)
				{
					AudElement[i] = htmlayout::dom::element::create("option",Names[i]);
					if (i == cur)
						AudElement[i].set_attribute("selected",L"");
					AudComboBox.append(AudElement[i]);
				}
				SoundDeviceSelectBox.append(AudComboBox);
				SoundDeviceSelectBox.update(true);
				
				for (unsigned short i = 0; i<AudElementCount;++i)
					delete[] Names[i];
				delete[] Names;
			}
			else
			{
				AudComboBox = htmlayout::dom::element::create("select");
				AudElement = new htmlayout::dom::element[1];
				AudElement[0] = htmlayout::dom::element::create("option",L"(No device)");
				AudComboBox.append(AudElement[0]);
				SoundDeviceSelectBox.append(AudComboBox);
				AudElementCount = 1;
			}
		}
	}
}
