#include "resource.h"
#include "sm_inoah.h"
#include "iNoahSession.h"
#include "iNoahParser.h"
#include "TAPIDevice.h"
#include "reclookups.h"
#include "registration.h"

#include "BaseTypes.hpp"
#include "GenericTextElement.hpp"
#include "BulletElement.hpp"
#include "ParagraphElement.hpp"
#include "HorizontalLineElement.hpp"
#include "Definition.hpp"

// those 3 must be in this sequence in order to get IID_DestNetInternet
// http://www.smartphonedn.com/forums/viewtopic.php?t=360
#include <objbase.h>
#include <initguid.h>
#include <connmgr.h>

#include <windows.h>
#ifndef PPC
#include <tpcshell.h>
#endif
#include <wingdi.h>
#include <fonteffects.hpp>
#include <sms.h>
#include <uniqueid.h>

HINSTANCE g_hInst      = NULL;  // Local copy of hInstance
HWND      g_hwndMain   = NULL;    // Handle to Main window returned from CreateWindow
HWND      g_hwndScroll = NULL;
HWND      g_hwndEdit   = NULL;

WNDPROC   g_oldEditWndProc = NULL;

static bool g_forceLayoutRecalculation=false;

TCHAR szAppName[] = TEXT("iNoah");
TCHAR szTitle[]   = TEXT("iNoah");

Definition *g_definition = NULL;

bool rec=false;

ArsLexis::String g_wordList;
ArsLexis::String recentWord;
ArsLexis::String regCode;
ArsLexis::String g_text = TEXT("");
iNoahSession     g_session;

LRESULT CALLBACK EditWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);


void    drawProgressInfo(HWND hwnd, TCHAR* text);
void    setFontSize(int diff,HWND hwnd);
void    paint(HWND hwnd, HDC hdc);
void    setScrollBar(Definition* definition);

HANDLE    g_hConnection = NULL;

// try to establish internet connection.
// If can't (e.g. because tcp/ip stack is not working), display a dialog box
// informing about that and return false
// Return true if established connection.
// Can be called multiple times - will do nothing if connection is already established.
static bool fInitConnection()
{
    if (NULL!=g_hConnection)
        return true;

    CONNMGR_CONNECTIONINFO ccInfo;
    memset(&ccInfo, 0, sizeof(CONNMGR_CONNECTIONINFO));
    ccInfo.cbSize      = sizeof(CONNMGR_CONNECTIONINFO);
    ccInfo.dwParams    = CONNMGR_PARAM_GUIDDESTNET;
    ccInfo.dwFlags     = CONNMGR_FLAG_PROXY_HTTP;
    ccInfo.dwPriority  = CONNMGR_PRIORITY_USERINTERACTIVE;
    ccInfo.guidDestNet = IID_DestNetInternet;
    
    DWORD dwStatus  = 0;
    DWORD dwTimeout = 5000;     // connection timeout: 5 seconds
    HRESULT res = ConnMgrEstablishConnectionSync(&ccInfo, &g_hConnection, dwTimeout, &dwStatus);

    if (FAILED(res))
    {
        //assert(NULL==g_hConnection);
        g_hConnection = NULL;
    }

    if (NULL==g_hConnection)
    {
#ifdef DEBUG
        ArsLexis::String errorMsg = _T("Unable to connect to ");
        errorMsg += server;
#else
        ArsLexis::String errorMsg = _T("Unable to connect");
#endif
        errorMsg.append(_T(". Verify your dialup or proxy settings are correct, and try again."));
        MessageBox(
            g_hwndMain,
            errorMsg.c_str(),
            TEXT("Error"),
            MB_OK|MB_ICONERROR|MB_APPLMODAL|MB_SETFOREGROUND
            );
        return false;
    }
    else
        return true;
}

static void deinitConnection()
{
    if (NULL != g_hConnection)
    {
        ConnMgrReleaseConnection(g_hConnection,1);
        g_hConnection = NULL;
    }
}

RenderingPreferences* g_renderingPrefs = NULL;

static RenderingPreferences* renderingPrefsPtr(void)
{
    if (NULL==g_renderingPrefs)
        g_renderingPrefs = new RenderingPreferences();

    return g_renderingPrefs;
}

static RenderingPreferences& renderingPrefs(void)
{
    if (NULL==g_renderingPrefs)
        g_renderingPrefs = new RenderingPreferences();

    return *g_renderingPrefs;
}

static void setDefinition(ArsLexis::String& defs, HWND hwnd)
{
    iNoahSession::ResponseCode code=g_session.getLastResponseCode();
    switch(code)
    {
        case iNoahSession::srvmessage:
        {
            MessageBox(hwnd,defs.c_str(),TEXT("Information"), 
            MB_OK|MB_ICONINFORMATION|MB_APPLMODAL|MB_SETFOREGROUND);
            return;
        }
        case iNoahSession::srverror:
        case iNoahSession::error:
        {
            MessageBox(hwnd,defs.c_str(),TEXT("Error"), 
            MB_OK|MB_ICONERROR|MB_APPLMODAL|MB_SETFOREGROUND);
            return;
        }
        default:
        {
            delete g_definition;
            g_definition = NULL;
            ParagraphElement* parent=0;
            int start=0;
            iNoahParser parser;
            g_definition=parser.parse(defs);
            ArsLexis::Graphics gr(GetDC(g_hwndMain), g_hwndMain);
            rec=true;
            InvalidateRect(hwnd,NULL,TRUE);
        }
    }
}

#define MAX_WORD_LEN 64
static void doLookup(HWND hwnd)
{
    if (!fInitConnection())
        return;

    TCHAR buf[MAX_WORD_LEN+1];
    int len = SendMessage(g_hwndEdit, EM_LINELENGTH, 0,0);
    if (0==len)
        return;

    memset(buf,0,sizeof(buf));
    len = SendMessage(g_hwndEdit, WM_GETTEXT, len+1, (LPARAM)buf);
    SendMessage(g_hwndEdit, EM_SETSEL, 0,-1);

    ArsLexis::String word(buf); 
    drawProgressInfo(hwnd, TEXT("definition..."));
    g_session.getWord(word,g_text);
    setDefinition(g_text,hwnd);
}

static void doRandom(HWND hwnd)
{
    if (!fInitConnection())
        return;

    HDC hdc = GetDC(hwnd);
    paint(hwnd, hdc);
    ReleaseDC(hwnd, hdc);
    drawProgressInfo(hwnd, TEXT("random definition..."));

    ArsLexis::String word;
    g_session.getRandomWord(word);
    setDefinition(word,hwnd);
}

static void doCompact(HWND hwnd)
{
    static bool fCompactView = false;

    HWND hwndMB = SHFindMenuBar (hwnd);
    if (!hwndMB)
    {
        // can it happen?
        return;
    }

    HMENU hMenu = (HMENU)SendMessage (hwndMB, SHCMBM_GETSUBMENU, 0, ID_MENU_BTN);

    if (fCompactView)
    {
        CheckMenuItem(hMenu, IDM_MENU_COMPACT, MF_UNCHECKED | MF_BYCOMMAND);
        renderingPrefsPtr()->setClassicView();
        fCompactView = false;
    }
    else
    {
        CheckMenuItem(hMenu, IDM_MENU_COMPACT, MF_CHECKED | MF_BYCOMMAND);
        renderingPrefsPtr()->setCompactView();
        fCompactView = true;
    }

    g_forceLayoutRecalculation = true;
    rec = true;
    InvalidateRect(hwnd,NULL,TRUE);
}

static void doRecent(HWND hwnd)
{
    if (!fInitConnection())
        return;

    HDC hdc = GetDC(hwnd);
    paint(hwnd, hdc);
    ReleaseDC(hwnd, hdc);
    drawProgressInfo(hwnd, TEXT("recent lookups list..."));

    g_wordList.assign(TEXT(""));
    g_session.getWordList(g_wordList);
    iNoahSession::ResponseCode code=g_session.getLastResponseCode();

    switch (code)
    {   
        case iNoahSession::srvmessage:
        {
            MessageBox(hwnd, g_wordList.c_str(), TEXT("Information"), 
                MB_OK|MB_ICONINFORMATION|MB_APPLMODAL|MB_SETFOREGROUND);
            break;
        }

        case iNoahSession::srverror:
        case iNoahSession::error:
        {
            MessageBox(hwnd, g_wordList.c_str(), TEXT("Error"), 
                MB_OK|MB_ICONERROR|MB_APPLMODAL|MB_SETFOREGROUND);
            break;
        }

        default:
        {
            if (DialogBox(g_hInst, MAKEINTRESOURCE(IDD_RECENT), hwnd,RecentLookupsDlgProc))
            {                        
                drawProgressInfo(hwnd, TEXT("definition..."));
                g_session.getWord(recentWord,g_text);
                setDefinition(g_text,hwnd);
            }
            break;
        }
    }
}

static void onCreate(HWND hwnd)
{
    SHMENUBARINFO mbi;
    ZeroMemory(&mbi, sizeof(SHMENUBARINFO));
    mbi.cbSize     = sizeof(SHMENUBARINFO);
    mbi.hwndParent = hwnd;
    mbi.nToolBarId = IDR_HELLO_MENUBAR;
    mbi.hInstRes   = g_hInst;

    if (!SHCreateMenuBar(&mbi)) 
    {
        PostQuitMessage(0);
        return;
    }
    
    g_hwndEdit = CreateWindow(
        TEXT("edit"),
        NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | 
        WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        0,0,0,0,hwnd,
        (HMENU) ID_EDIT,
        g_hInst,
        NULL);

    g_oldEditWndProc = (WNDPROC)SetWindowLong(g_hwndEdit, GWL_WNDPROC, (LONG)EditWndProc);

    g_hwndScroll = CreateWindow(
        TEXT("scrollbar"),
        NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP| SBS_VERT,
        0,0,0,0,hwnd,
        (HMENU) ID_SCROLL,
        g_hInst,
        NULL);

    setScrollBar(g_definition);

    // In order to make Back work properly, it's necessary to 
    // override it and then call the appropriate SH API
    (void)SendMessage(
        mbi.hwndMB, SHCMBM_OVERRIDEKEY, VK_TBACK,
        MAKELPARAM(SHMBOF_NODEFAULT | SHMBOF_NOTIFY,
        SHMBOF_NODEFAULT | SHMBOF_NOTIFY)
        );
    
    setFontSize(IDM_FNT_STANDARD, hwnd);
    SetFocus(g_hwndEdit);
}

void static onHotKey(WPARAM wp, LPARAM lp)
{
    ArsLexis::Graphics gr(GetDC(g_hwndMain), g_hwndMain);

    int page=0;
    if (NULL!=g_definition)
        page=g_definition->shownLinesCount();

    switch(HIWORD(lp))
    {
        case VK_TBACK:
#ifndef PPC
            if (0 != (MOD_KEYUP & LOWORD(lp)))
                SHSendBackToFocusWindow(WM_HOTKEY, wp, lp);
#endif
            break;
        case VK_TDOWN:
            if(NULL!=g_definition)
                g_definition->scroll(gr, renderingPrefs(), page);

            setScrollBar(g_definition);
            break;
    }
}

static LRESULT onCommand(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT result = TRUE;

    switch (wp)
    {
        case IDOK:
            SendMessage(hwnd,WM_CLOSE,0,0);
            break;

        case IDM_MENU_COMPACT:
            doCompact(hwnd);
            break;

        case IDM_FNT_LARGE:
            setFontSize(IDM_FNT_LARGE, hwnd);
            break;

        case IDM_FNT_STANDARD:
            setFontSize(IDM_FNT_STANDARD, hwnd);
            break;

        case IDM_FNT_SMALL:
            setFontSize(IDM_FNT_SMALL, hwnd);
            break;

        case ID_LOOKUP:
            doLookup(hwnd);
            InvalidateRect(hwnd,NULL,TRUE);
            break;

        case IDM_MENU_RANDOM:
            doRandom(hwnd);
            break;

        case IDM_MENU_RECENT:
            doRecent(hwnd);
            break;

        case IDM_MENU_REGISTER:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_REGISTER), hwnd, RegistrationDlgProc);
            break;

        case IDM_CACHE:
            g_session.clearCache();
            break;

        default:
            // can it happen?
            return DefWindowProc(hwnd, msg, wp, lp);
    }

    SetFocus(g_hwndEdit);
    return result;
}


LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT      lResult = TRUE;
    HDC          hdc;
    PAINTSTRUCT  ps;

    switch (msg)
    {
        case WM_CREATE:
            onCreate(hwnd);
            break;

        case WM_SIZE:
            MoveWindow(g_hwndEdit, 2, 2, LOWORD(lp)-4, 20, true);
            MoveWindow(g_hwndScroll, LOWORD(lp)-5, 28 , 5, HIWORD(lp)-28, false);
            break;

        case WM_SETFOCUS:
            SetFocus(g_hwndEdit);
            break;

        case WM_COMMAND:
            onCommand(hwnd, msg, wp, lp);
            break;

        case WM_HOTKEY:
            onHotKey(wp,lp);
            break;

        case WM_PAINT:
            hdc = BeginPaint (hwnd, &ps);
            paint(hwnd, hdc);
            EndPaint (hwnd, &ps);
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            lResult = DefWindowProc(hwnd, msg, wp, lp);
            break;
    }
    return lResult;
}

BOOL InitInstance (HINSTANCE hInstance, int CmdShow )
{    
    g_hInst = hInstance;

    g_hwndMain = CreateWindow(szAppName,
        szTitle,
        WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL );

    if (!g_hwndMain)
        return FALSE;

    ShowWindow(g_hwndMain, CmdShow );
    UpdateWindow(g_hwndMain);
    TAPIDevice::initInstance();    
    return TRUE;
}

BOOL InitApplication ( HINSTANCE hInstance )
{
    WNDCLASS wc;
    
    wc.style = CS_HREDRAW | CS_VREDRAW ;
    wc.lpfnWndProc = (WNDPROC)WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hIcon = NULL;
    wc.hInstance = hInstance;
    wc.hCursor = NULL;
    wc.hbrBackground = (HBRUSH) GetStockObject( WHITE_BRUSH );
    wc.lpszMenuName = NULL;
    wc.lpszClassName = szAppName;

    BOOL f = RegisterClass(&wc);
    return f;
}

int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   LPWSTR    lpCmdLine,
                   int        CmdShow)
                   
{
    // if we're already running, then just bring our window to front
    HWND hwndPrev = FindWindow(szAppName, szTitle);
    if (hwndPrev) 
    {
        SetForegroundWindow(hwndPrev);    
        return 0;
    }
    
    if (!hPrevInstance)
    {
        if (!InitApplication(hInstance))
            return FALSE; 
    }

    if (!InitInstance(hInstance, CmdShow))
        return FALSE;
    
    MSG     msg;
    while (TRUE == GetMessage( &msg, NULL, 0,0 ))
    {
        TranslateMessage (&msg);
        DispatchMessage(&msg);
    }

    deinitConnection();
    Transmission::closeInternet();

    return msg.wParam;
}

void paint(HWND hwnd, HDC hdc)
{
    RECT  rect;
    GetClientRect(hwnd, &rect);
    FillRect(hdc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));

    rect.top    += 22;
    rect.left   += 2;
    rect.right  -= 7;
    rect.bottom -= 2;

    if (NULL==g_definition)
    {
        LOGFONT logfnt;
        HFONT   fnt=(HFONT)GetStockObject(SYSTEM_FONT);
        GetObject(fnt, sizeof(logfnt), &logfnt);
        logfnt.lfHeight+=1;
        int fontDy = logfnt.lfHeight;
        HFONT fnt2=(HFONT)CreateFontIndirect(&logfnt);
        SelectObject(hdc, fnt2);

        RECT tmpRect=rect;
        DrawText(hdc, TEXT("(enter word and press \"Lookup\")"), -1, &tmpRect, DT_SINGLELINE|DT_CENTER);
        tmpRect.top += 46;
        DrawText(hdc, TEXT("ArsLexis iNoah 1.0"), -1, &tmpRect, DT_SINGLELINE|DT_CENTER);
        tmpRect.top += 18;
        DrawText(hdc, TEXT("http://www.arslexis.com"), -1, &tmpRect, DT_SINGLELINE|DT_CENTER);
        tmpRect.top += 18;
        DrawText(hdc, TEXT("Unregistered"), -1, &tmpRect, DT_SINGLELINE|DT_CENTER);
        SelectObject(hdc,fnt);
        DeleteObject(fnt2);
    }
    else
    {
        ArsLexis::Graphics gr(hdc, hwnd);
        RECT b;
        GetClientRect(hwnd, &b);
        ArsLexis::Rectangle bounds=b;
        ArsLexis::Rectangle defRect=rect;
        bool doubleBuffer=true;
        HDC offscreenDc=::CreateCompatibleDC(hdc);
        if (offscreenDc) {
            HBITMAP bitmap=::CreateCompatibleBitmap(hdc, bounds.width(), bounds.height());
            if (bitmap) {
                HBITMAP oldBitmap=(HBITMAP)::SelectObject(offscreenDc, bitmap);
                {
                    ArsLexis::Graphics offscreen(offscreenDc, NULL);
                    g_definition->render(offscreen, defRect, renderingPrefs(), g_forceLayoutRecalculation);
                    offscreen.copyArea(defRect, gr, defRect.topLeft);
                }
                ::SelectObject(offscreenDc, oldBitmap);
                ::DeleteObject(bitmap);
            }
            else
                doubleBuffer=false;
            ::DeleteDC(offscreenDc);
        }
        else
            doubleBuffer=false;

        if (!doubleBuffer)
            g_definition->render(gr, defRect, renderingPrefs(), g_forceLayoutRecalculation);
        g_forceLayoutRecalculation=false;
    }

    if (rec)
    {
        setScrollBar(g_definition);
        rec=false;
    }
}

LRESULT CALLBACK EditWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch(msg)
    {
        case WM_KEYDOWN:
        {

            if (VK_TACTION==wp)
            {
                doLookup(GetParent(hwnd));
                return 0;
            }

            if (NULL!=g_definition)
            {
                int page=0;
                switch (wp) 
                {
                    case VK_DOWN:
                        page=g_definition->shownLinesCount();
                        break;
                    case VK_UP:
                        page=-static_cast<int>(g_definition->shownLinesCount());
                        break;
                }
                if (0!=page)
                {
                    RECT b;
                    GetClientRect (g_hwndMain, &b);
                    ArsLexis::Rectangle bounds=b;
                    ArsLexis::Rectangle defRect=bounds;
                    defRect.explode(2, 22, -9, -24);
                    ArsLexis::Graphics gr(GetDC(g_hwndMain), g_hwndMain);
                    bool doubleBuffer=true;
                    
                    HDC offscreenDc=::CreateCompatibleDC(gr.handle());
                    if (offscreenDc)
                    {
                        HBITMAP bitmap=::CreateCompatibleBitmap(gr.handle(), bounds.width(), bounds.height());
                        if (bitmap)
                        {
                            HBITMAP oldBitmap=(HBITMAP)::SelectObject(offscreenDc, bitmap);
                            {
                                ArsLexis::Graphics offscreen(offscreenDc, NULL);
                                g_definition->scroll(offscreen, renderingPrefs(), page);
                                offscreen.copyArea(defRect, gr, defRect.topLeft);
                            }
                            ::SelectObject(offscreenDc, oldBitmap);
                            ::DeleteObject(bitmap);
                        }
                        else
                            doubleBuffer=false;
                        ::DeleteDC(offscreenDc);
                    }
                    else
                        doubleBuffer=false;
                    if (!doubleBuffer)
                        g_definition->scroll(gr, renderingPrefs(), page);
                    
                    setScrollBar(g_definition);
                    return 0;
                }
            }
            break;
       }
    }

    return CallWindowProc(g_oldEditWndProc, hwnd, msg, wp, lp);
}

void drawProgressInfo(HWND hwnd, TCHAR* text)
{
    RECT rect;
    HDC hdc=GetDC(hwnd);
    GetClientRect (hwnd, &rect);
    rect.top+=22;
    rect.left+=2;
    rect.right-=7;
    rect.bottom-=2;
    LOGFONT logfnt;
    
    Rectangle(hdc, 18, 83, 152, 123);
    
    POINT p[2];
    p[0].y=85;
    p[1].y=121;
    LOGPEN pen;
    HGDIOBJ hgdiobj = GetCurrentObject(hdc,OBJ_PEN);
    GetObject(hgdiobj, sizeof(pen), &pen);
    for(p[0].x=20;p[0].x<150;p[0].x++)
    {                           
        HPEN newPen=CreatePenIndirect(&pen);
        pen.lopnColor = RGB(0,0,p[0].x+100);
        SelectObject(hdc,newPen);
        DeleteObject(hgdiobj);
        hgdiobj=newPen;
        p[1].x=p[0].x;
        Polyline(hdc, p, 2);
    }
    DeleteObject(hgdiobj);
    
    SelectObject(hdc,GetStockObject(HOLLOW_BRUSH));
    HFONT fnt=(HFONT)GetStockObject(SYSTEM_FONT);
    GetObject(fnt, sizeof(logfnt), &logfnt);
    logfnt.lfHeight+=1;
    logfnt.lfWeight=800;
    SetTextColor(hdc,RGB(255,255,255));
    SetBkMode(hdc, TRANSPARENT);
    HFONT fnt2=(HFONT)CreateFontIndirect(&logfnt);
    SelectObject(hdc, fnt2);
    rect.top-=10;
    DrawText (hdc, TEXT("Downloading"), -1, &rect, DT_VCENTER|DT_CENTER);
    rect.top+=32;
    DrawText (hdc, text, -1, &rect, DT_VCENTER|DT_CENTER);
    SelectObject(hdc,fnt);
    DeleteObject(fnt2);
    ReleaseDC(hwnd,hdc);
}

void setFontSize(int diff, HWND hwnd)
{
    int delta=0;
    HWND hwndMB = SHFindMenuBar (hwnd);
    if (hwndMB) 
    {
        HMENU hMenu;
        hMenu = (HMENU)SendMessage (hwndMB, SHCMBM_GETSUBMENU, 0, ID_MENU_BTN);
        CheckMenuItem(hMenu, IDM_FNT_LARGE, MF_UNCHECKED | MF_BYCOMMAND);
        CheckMenuItem(hMenu, IDM_FNT_SMALL, MF_UNCHECKED | MF_BYCOMMAND);
        CheckMenuItem(hMenu, IDM_FNT_STANDARD, MF_UNCHECKED | MF_BYCOMMAND);
        switch(diff)
        {
            case IDM_FNT_LARGE:
                CheckMenuItem(hMenu, IDM_FNT_LARGE, MF_CHECKED | MF_BYCOMMAND);
                delta=-2;
                break;
            case IDM_FNT_STANDARD:
                CheckMenuItem(hMenu, IDM_FNT_STANDARD, MF_CHECKED | MF_BYCOMMAND);
                break;
            case IDM_FNT_SMALL:
                CheckMenuItem(hMenu, IDM_FNT_SMALL, MF_CHECKED | MF_BYCOMMAND);
                delta=2;
                break;
        }
    }
    g_forceLayoutRecalculation=true;
    renderingPrefsPtr()->setFontSize(delta);
    InvalidateRect(hwnd,NULL,TRUE);
}

void setScrollBar(Definition* definition)
{
    int frst=0;
    int total=0;
    int shown=0;
    if (definition)
    {
        frst=definition->firstShownLine();
        total=definition->totalLinesCount();
        shown=definition->shownLinesCount();
    }
    
    SetScrollPos(
        g_hwndScroll, 
        SB_CTL,
        frst,
        TRUE);
    
    SetScrollRange(
        g_hwndScroll,
        SB_CTL,
        0,
        total-shown,
        TRUE);
}

void ArsLexis::handleBadAlloc()
{
    RaiseException(1,0,0,NULL);    
}

void* ArsLexis::allocate(size_t size)
{
    void* ptr=0;
    if (size) 
        ptr=malloc(size);
    else
        ptr=malloc(1);
    if (!ptr)
        handleBadAlloc();
    return ptr;
}

