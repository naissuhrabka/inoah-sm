// WinFont.h: interface for the WinFont class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_WINFONT_H__4DB36623_0043_4054_B7FF_C3474967BB8E__INCLUDED_)
#define AFX_WINFONT_H__4DB36623_0043_4054_B7FF_C3474967BB8E__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <wingdi.h>
class FontWrapper
{
    friend class WinFont;
    FontWrapper(HFONT fnt);
    virtual ~FontWrapper();

private:
    void attach() {refsCount++; }
    void detach() {refsCount--; }
    unsigned getRefsCount() {return refsCount;}
    FontWrapper();
    unsigned refsCount;
    HFONT font;
};

class WinFont  
{
public:
    WinFont();
	WinFont(HFONT font);
    WinFont& operator=(const WinFont& r);
    WinFont(const WinFont& copy);
	virtual ~WinFont();
private:
    FontWrapper* fntWrapper;
};

#endif // !defined(AFX_WINFONT_H__4DB36623_0043_4054_B7FF_C3474967BB8E__INCLUDED_)
