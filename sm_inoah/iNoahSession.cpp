#include <aygshell.h>

#ifdef WIN32_PLATFORM_WFSP
#include <tpcshell.h>
#include <winuserm.h>
#include <sms.h>
#endif
#include <shellapi.h>
#include <winbase.h>

#include <BaseTypes.hpp>
#include <Debug.hpp>
#include <Text.hpp>

#include "sm_inoah.h"
#include "Transmission.h"
#include "iNoahSession.h"

using namespace ArsLexis;

const char_t * errorStr        = _T("ERROR");
const char_t * messageStr      = _T("MESSAGE");

const char_t * cookieStr       = _T("COOKIE");

const char_t * wordListStr     = _T("WORDLIST");

const char_t * definitionStr   = _T("DEF");

const char_t * regFailedStr    = _T("REGISTRATION_FAILED");
const char_t * regOkStr        = _T("REGISTRATION_OK");

#define urlCommon    _T("/dict-2.php?pv=2&cv=1.0&")
#define urlCommonLen sizeof(urlCommon)/sizeof(urlCommon[0])

const String sep             = _T("&");
const String cookieRequest   = _T("get_cookie=");
const String deviceInfoParam = _T("di=");

const String cookieParam     = _T("c=");
const String registerParam   = _T("register=");
const String regCodeParam    = _T("rc=");
const String getWordParam    = _T("get_word=");

const String randomRequest   = _T("get_random_word=");
const String recentRequest   = _T("recent_lookups=");

enum eFieldType {
    fieldNoValue,     // field has no value e.g. "REGISTRATION_OK\n"
    fieldValueInline,  // field has arguments before new-line e.g. "REQUESTS_LEFT 5\n"
    fieldValueFollow,  // field has arguments after new-line e.g. "WORD\nhero\n"
};

typedef struct _fieldDef {
    const char_t *  fieldName;
    eFieldType      fieldType;
} FieldDef;

// those must be in the same order as fieldId. They describe the format
// of each field
static FieldDef fieldsDef[fieldsCount] = {
    { errorStr, fieldValueFollow },
    { cookieStr, fieldValueFollow},
    { messageStr, fieldValueFollow },
    { definitionStr, fieldValueFollow },
    { wordListStr, fieldValueFollow },
//    { requestsLeftStr, fieldValueInline },
//    { pronunciationStr,fieldValueInline },
    { regFailedStr, fieldNoValue },
    { regOkStr, fieldNoValue }
};

static String::size_type FieldStrLen(eFieldId fieldId)
{
    const char_t *    fieldName = fieldsDef[(int)fieldId].fieldName;
    String::size_type fieldLen = tstrlen(fieldName);
    return fieldLen;
}

static bool fFieldHasValue(eFieldId fieldId)
{
    switch (fieldId)
    {
        case registrationFailedField:
        case registrationOkField:
            return false;
        default:
            return true;
    }
}

ServerResponseParser::ServerResponseParser(String &content)
{
    _content    = content;
    _fParsed    = false;
    _fMalformed = false;

    for (int id=(int)fieldIdFirst; id<(int)fieldsCount; id++)
    {
        SetFieldStart((eFieldId)id,String::npos);
        SetFieldValueStart((eFieldId)id,String::npos);
        SetFieldValueLen((eFieldId)id,String::npos);
    }
}

// return true if this response contains a given field
bool ServerResponseParser::fHasField(eFieldId fieldId)
{
    assert(_fParsed);
    if (String::npos==GetFieldStart(fieldId))
        return false;
        
    return true;
}

// return a value for a given field
void ServerResponseParser::GetFieldValue(eFieldId fieldId, String& fieldValueOut)
{
    assert(_fParsed);
    if (String::npos==GetFieldStart(fieldId))
    {
        fieldValueOut.assign(_T(""));
        return;
    }

    if (!fFieldHasValue(fieldId))
    {
        assert(String::npos==GetFieldValueStart(fieldId));
        assert(String::npos==GetFieldValueLen(fieldId));
        fieldValueOut.assign(_T(""));
        return;
    }        

    assert(String::npos!=GetFieldValueStart(fieldId));
    assert(String::npos!=GetFieldValueLen(fieldId));

    fieldValueOut.assign(_content, GetFieldValueStart(fieldId), GetFieldValueLen(fieldId));
}

// given a string and a start position in that string, returned
// fieldId represented by that string at this position. Return
// field id or fieldIdInvalid if the string doesn't represent any
// known field.
// Just compare field strings with a given string starting with startPos
static eFieldId GetFieldId(String &str, String::size_type startPos)
{
    const char_t *      fieldStr;
    String::size_type   fieldStrLen;

    for (int id=(int)fieldIdFirst; id<(int)fieldsCount; id++)
    {
        fieldStr = fieldsDef[id].fieldName;
        fieldStrLen = tstrlen(fieldStr);
        if (0==str.compare(startPos, fieldStrLen, fieldStr))
        {
            return (eFieldId)id;
        }
    }
    return fieldIdInvalid;
}

// This is kind of broken.
// It looks for the end of the value that follows a field. Value can be multi-line so basically
// we look until the first line that looks like one of fields or the end of text.
String::size_type ServerResponseParser::GetFollowValueEnd(String::size_type fieldValueStart)
{
    String::size_type fieldValueEnd;
    String::size_type newLineStart;
    eFieldId          fieldId;

    String::size_type startLookingFrom = fieldValueStart;
    while (true)
    {
        fieldValueEnd = _content.find(_T("\n"), startLookingFrom);
        if (String::npos==fieldValueEnd)
        {
            fieldValueEnd = _content.length();
            break;        
        }

        newLineStart = fieldValueEnd + 1;
        fieldId = GetFieldId(_content, newLineStart);
        if (fieldId!=fieldIdInvalid)
            break;
        startLookingFrom = newLineStart;
    }
    return fieldValueEnd;
}

bool ServerResponseParser::FParseResponseIfNot()
{
    if (_fParsed)
        return !_fMalformed;

    bool fOk = FParseResponse();
    _fParsed = true;
    _fMalformed = !fOk;       
    return fOk;
}

// parse response. return true if everythings good, false if the response
// is malformed
bool ServerResponseParser::FParseResponse()
{
    String             line;
    bool               fEnd;
    String::size_type  curPos = 0;
    String::size_type  lineStartPos;

    assert( !_fParsed );

    eFieldId          fieldId;

    lineStartPos = curPos;
    line = GetNextLine(_content, curPos, fEnd);
    if (fEnd)
        return false;

    fieldId = GetFieldId(line,0);
    if (fieldIdInvalid==fieldId)
        return false;

    if ( (registrationFailedField==fieldId) || (registrationOkField==fieldId) )
    {
        SetFieldStart(fieldId, lineStartPos);
        // this should be the only thing in the line
        line = GetNextLine(_content, curPos, fEnd);
        if (!fEnd)
            return false;
        else
            return true;
    }

    String::size_type fieldValueStart = curPos;
    String::size_type fieldValueEnd;

    if ( (errorField==fieldId)  || (messageField==fieldId) ||
         (cookieField==fieldId) || (wordListField==fieldId) )
    {
        fieldValueEnd = GetFollowValueEnd(fieldValueStart);
    }
    else if (definitionField==fieldId)
    {
        // now we must have definition field, followed by word,
        // optional pronField, optional requestsLeftField and definition itself
        // we treat is as one thing
        fieldValueEnd = _content.length();
    }
    else
        return false;

    String::size_type fieldValueLen = fieldValueEnd - fieldValueStart;
    SetFieldStart(fieldId, lineStartPos);
    SetFieldValueStart(fieldId, fieldValueStart);
    SetFieldValueLen(fieldId, fieldValueLen);
    return true;
}

bool ServerResponseParser::fMalformed()
{    
    bool fOk = FParseResponseIfNot();
    assert(fOk==!_fMalformed);
    return _fMalformed;
}

#define SIZE_FOR_REST_OF_URL 64
static String BuildCommonWithCookie(const String& cookie, const String& optionalOne=_T(""), const String& optionalTwo=_T(""))
{
    String url;
    url.reserve(urlCommonLen +
                cookieParam.length() + cookie.length() +
                sep.length() +
                optionalOne.length() +
                optionalTwo.length() +
                SIZE_FOR_REST_OF_URL);

    url.assign(urlCommon);
    url.append(cookieParam);
    url.append(cookie);
    url.append(sep);
    url.append(optionalOne);
    url.append(optionalTwo);
    return url;
}

static String BuildGetCookieUrl()
{
    String deviceInfo = getDeviceInfo();
    String url;
    url.reserve(urlCommonLen +
                cookieRequest.length() +
                sep.length() + 
                deviceInfoParam.length() +
                deviceInfo.length());

    url.assign(urlCommon);
    url.append(cookieRequest);
    url.append(sep); 
    url.append(deviceInfoParam);
    url.append(deviceInfo); 
    return url;
}

static String BuildGetRandomUrl(const String& cookie)
{    
    String url = BuildCommonWithCookie(cookie,randomRequest);
    return url;
}

static String BuildGetWordListUrl(const String& cookie)
{    
    String url = BuildCommonWithCookie(cookie,recentRequest);
    return url;
}

static String BuildGetWordUrl(const String& cookie, const String& word)
{
    String url = BuildCommonWithCookie(cookie,getWordParam,word);
    String regCode = GetRegCode();
    if (!regCode.empty())
    {
        url.append(sep);
        url.append(regCodeParam);
        url.append(regCode);
    }
    return url;
}

static String BuildRegisterUrl(const String& cookie, const String& regCode)
{
    String url = BuildCommonWithCookie(cookie,registerParam,regCode);
    return url;
}

static void HandleConnectionError(DWORD errorCode)
{
    if (errConnectionFailed==errorCode)
    {
#ifdef DEBUG
        ArsLexis::String errorMsg = _T("Unable to connect to ");
        errorMsg += server;
#else
        ArsLexis::String errorMsg = _T("Unable to connect");
#endif
        errorMsg.append(_T(". Verify your dialup or proxy settings are correct, and try again."));
        MessageBox(g_hwndMain,errorMsg.c_str(),
            TEXT("Error"), MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND );
    }
    else
    {
        // TODO: show errorCode in the message as well?
        MessageBox(g_hwndMain,
            _T("Connection error. Please contact support@arslexis.com if the problem persists."),
            _T("Error"), MB_OK | MB_ICONINFORMATION | MB_APPLMODAL | MB_SETFOREGROUND );
    }
}    

static void HandleMalformedResponse()
{
    MessageBox(g_hwndMain,
        _T("Server returned malformed response. Please contact support@arslexis.com if the problem persists."),
        _T("Error"), 
        MB_OK | MB_ICONINFORMATION | MB_APPLMODAL | MB_SETFOREGROUND );
}

static void HandleServerError(const String& errorStr)
{
    MessageBox(g_hwndMain,
        errorStr.c_str(),
        _T("Error"), 
        MB_OK | MB_ICONINFORMATION | MB_APPLMODAL | MB_SETFOREGROUND );
}

static void HandleServerMessage(const String& msg)
{
    MessageBox(g_hwndMain,
        msg.c_str(),
       _T("Information"), 
        MB_OK | MB_ICONINFORMATION | MB_APPLMODAL | MB_SETFOREGROUND );
}

// handle common error cases for parsed message:
// * response returned from the server is malformed
// * server indicated error during processing
// * server sent a message instead of response
// Returns true if response is none of the above and we can proceed
// handling the message. Returns false if further processing should be aborted
bool FHandleParsedResponse(ServerResponseParser& responseParser)
{
    if (responseParser.fMalformed())
    {
        HandleMalformedResponse();
        return false;
    }

    if (responseParser.fHasField(errorField))
    {
        String errorStr;
        responseParser.GetFieldValue(errorField,errorStr);
        HandleServerError(errorStr);
        return false;
    }

    if (responseParser.fHasField(messageField))
    {
        String msg;
        responseParser.GetFieldValue(messageField,msg);
        HandleServerMessage(msg);
        return false;
    }

    return true;
}

// Returns a cookie in cookieOut. If cookie is not present, it'll get it from
// the server. If there's a problem retrieving the cookie, it'll display
// appropriate error messages to the client and return false.
// Return true if cookie was succesfully obtained.
bool FGetCookie(String& cookieOut)
{

    cookieOut = GetCookie();
    if (!cookieOut.empty())
    {
        return true;
    }

    String url = BuildGetCookieUrl();
    String response;
    DWORD  err = GetHttpBody(server,serverPort,url,response);
    if (errNone != err)
    {
        HandleConnectionError(err);
        return false;
    }

    ServerResponseParser responseParser(response);

    bool fOk = FHandleParsedResponse(responseParser);
    if (!fOk)
        return false;

    if (!responseParser.fHasField(cookieField))
    {
        HandleMalformedResponse();
        return false;
    }

    responseParser.GetFieldValue(cookieField,cookieOut);
    SetCookie(cookieOut);
    return true;
}

bool FGetRandomDef(String& defOut)
{
    String cookie;
    bool fOk = FGetCookie(cookie);
    if (!fOk)
        return false;

    String  url = BuildGetRandomUrl(cookie);
    String  response;
    DWORD err = GetHttpBody(server,serverPort,url,response);
    if (errNone != err)
    {
        HandleConnectionError(err);
        return false;
    }

    ServerResponseParser responseParser(response);

    fOk = FHandleParsedResponse(responseParser);
    if (!fOk)
        return false;

    if (!responseParser.fHasField(definitionField))
    {
        HandleMalformedResponse();
        return false;
    }

    responseParser.GetFieldValue(definitionField,defOut);
    return true;
}

bool FGetWord(const String& word, String& defOut)
{
    String cookie;
    bool fOk = FGetCookie(cookie);
    if (!fOk)
        return false;

    String  url = BuildGetWordUrl(cookie,word);
    String  response;
    DWORD err = GetHttpBody(server,serverPort,url,response);
    if (errNone != err)
    {
        HandleConnectionError(err);
        return false;
    }

    ServerResponseParser responseParser(response);

    fOk = FHandleParsedResponse(responseParser);
    if (!fOk)
        return false;

    if (!responseParser.fHasField(definitionField))
    {
        HandleMalformedResponse();
        return false;
    }

    responseParser.GetFieldValue(definitionField,defOut);
    return true;
}

bool FGetWordList(String& wordListOut)
{
    String cookie;
    bool fOk = FGetCookie(cookie);
    if (!fOk)
        return false;

    String  url = BuildGetWordListUrl(cookie);
    String  response;
    DWORD err = GetHttpBody(server,serverPort,url,response);
    if (errNone != err)
    {
        HandleConnectionError(err);
        return false;
    }

    ServerResponseParser responseParser(response);

    fOk = FHandleParsedResponse(responseParser);
    if (!fOk)
        return false;

    if (!responseParser.fHasField(wordListField))
    {
        HandleMalformedResponse();
        return false;
    }

    responseParser.GetFieldValue(wordListField,wordListOut);
    return true;
}

// send the register request to the server to find out, if a registration
// code regCode is valid. Return false if there was a connaction error.
// If return true, fRegCodeValid indicates if regCode was valid or not
bool FCheckRegCode(const String& regCode, bool& fRegCodeValid)
{
    String cookie;
    bool fOk = FGetCookie(cookie);
    if (!fOk)
        return false;

    String  url = BuildRegisterUrl(cookie, regCode);
    String  response;
    DWORD err = GetHttpBody(server,serverPort,url,response);
    if (errNone != err)
    {
        HandleConnectionError(err);
        return false;
    }

    ServerResponseParser responseParser(response);

    fOk = FHandleParsedResponse(responseParser);
    if (!fOk)
        return false;

    if (responseParser.fHasField(registrationOkField))
    {
        fRegCodeValid = true;
    }
    else if (responseParser.fHasField(registrationFailedField))
    {
        fRegCodeValid = false;
    }
    else
    {
        HandleMalformedResponse();
        return false;
    }

    return true;
}

TCHAR numToHex(TCHAR num)
{    
    if (num>=0 && num<=9)
        return TCHAR('0')+num;
    // assert (num<16);
    return TCHAR('A')+num-10;
}

// Append hexbinary-encoded string toHexify to the input/output string str
void stringAppendHexified(String& str, const String& toHexify)
{
    size_t toHexifyLen = toHexify.length();
    for(size_t i=0;i<toHexifyLen;i++)
    {
        str += numToHex((toHexify[i] & 0xF0) >> 4);
        str += numToHex(toHexify[i] & 0x0F);
    }
}

// According to this msdn info:
// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/dnppc2k/html/ppc_hpocket.asp
// 128 is enough for buffer size
#define INFO_BUF_SIZE 128
String getDeviceInfo()
{
    SMS_ADDRESS      address;
    ArsLexis::String regsInfo;
    ArsLexis::String text;
    TCHAR            buffer[INFO_BUF_SIZE];
    BOOL             fOk;

    ZeroMemory(&address, sizeof(address));
#ifdef WIN32_PLATFORM_WFSP
    HRESULT res = SmsGetPhoneNumber(&address); 
    if (SUCCEEDED(res))
    {
        text.assign(TEXT("PN"));
        stringAppendHexified(text, address.ptsAddress);
    }
#endif

    ZeroMemory(buffer,sizeof(buffer));
    fOk = SystemParametersInfo(SPI_GETOEMINFO, sizeof(buffer), buffer, 0);

    if (fOk)
    {
        if (text.length()>0)
            text += TEXT(":");
        text += TEXT("OC");
        stringAppendHexified(text,buffer);
    }

    ZeroMemory(buffer,sizeof(buffer));
    fOk = SystemParametersInfo(SPI_GETPLATFORMTYPE, sizeof(buffer), buffer, 0);

    if (fOk)
    {
        if (text.length()>0)
            text += TEXT(":");
        text += TEXT("OD");
        stringAppendHexified(text,buffer);
    }
    return text;
}

