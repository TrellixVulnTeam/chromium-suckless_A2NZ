// Copyright 2016 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "fpdfsdk/cpdfsdk_formfillenvironment.h"

#include <memory>

#include "fpdfsdk/cpdfsdk_annothandlermgr.h"
#include "fpdfsdk/cpdfsdk_document.h"
#include "fpdfsdk/formfiller/cffl_interactiveformfiller.h"
#include "fpdfsdk/fsdk_actionhandler.h"
#include "fpdfsdk/javascript/ijs_runtime.h"
#include "third_party/base/ptr_util.h"

#ifdef PDF_ENABLE_XFA
#include "fpdfsdk/fpdfxfa/cpdfxfa_app.h"
#endif  // PDF_ENABLE_XFA

namespace {

// NOTE: |bsUTF16LE| must outlive the use of the result. Care must be taken
// since modifying the result would impact |bsUTF16LE|.
FPDF_WIDESTRING AsFPDFWideString(CFX_ByteString* bsUTF16LE) {
  return reinterpret_cast<FPDF_WIDESTRING>(
      bsUTF16LE->GetBuffer(bsUTF16LE->GetLength()));
}

}  // namespace

CPDFSDK_FormFillEnvironment::CPDFSDK_FormFillEnvironment(
    UnderlyingDocumentType* pDoc,
    FPDF_FORMFILLINFO* pFFinfo)
    : m_pInfo(pFFinfo),
      m_pSDKDoc(new CPDFSDK_Document(pDoc, this)),
      m_pUnderlyingDoc(pDoc),
      m_pSysHandler(new CFX_SystemHandler(this)) {}

CPDFSDK_FormFillEnvironment::~CPDFSDK_FormFillEnvironment() {
  // |m_pAnnotHandlerMgr| will try to access |m_pFormFiller|
  // when it cleans up. So, we must make sure it is cleaned up before
  // |m_pFormFiller|.
  m_pAnnotHandlerMgr.reset();

  // Must destroy the |m_pFormFiller| before the environment (|this|)
  // because any created form widgets hold a pointer to the environment.
  // Those widgets may call things like KillTimer() as they are shutdown.
  m_pFormFiller.reset();

#ifdef PDF_ENABLE_XFA
  CPDFXFA_App* pProvider = CPDFXFA_App::GetInstance();
  if (pProvider->m_pEnvList.GetSize() == 0)
    pProvider->SetJavaScriptInitialized(FALSE);
#endif  // PDF_ENABLE_XFA
  if (m_pInfo && m_pInfo->Release)
    m_pInfo->Release(m_pInfo);
}

int CPDFSDK_FormFillEnvironment::JS_appAlert(const FX_WCHAR* Msg,
                                             const FX_WCHAR* Title,
                                             uint32_t Type,
                                             uint32_t Icon) {
  if (!m_pInfo || !m_pInfo->m_pJsPlatform ||
      !m_pInfo->m_pJsPlatform->app_alert) {
    return -1;
  }
  CFX_ByteString bsMsg = CFX_WideString(Msg).UTF16LE_Encode();
  CFX_ByteString bsTitle = CFX_WideString(Title).UTF16LE_Encode();
  return m_pInfo->m_pJsPlatform->app_alert(
      m_pInfo->m_pJsPlatform, AsFPDFWideString(&bsMsg),
      AsFPDFWideString(&bsTitle), Type, Icon);
}

int CPDFSDK_FormFillEnvironment::JS_appResponse(const FX_WCHAR* Question,
                                                const FX_WCHAR* Title,
                                                const FX_WCHAR* Default,
                                                const FX_WCHAR* cLabel,
                                                FPDF_BOOL bPassword,
                                                void* response,
                                                int length) {
  if (!m_pInfo || !m_pInfo->m_pJsPlatform ||
      !m_pInfo->m_pJsPlatform->app_response) {
    return -1;
  }
  CFX_ByteString bsQuestion = CFX_WideString(Question).UTF16LE_Encode();
  CFX_ByteString bsTitle = CFX_WideString(Title).UTF16LE_Encode();
  CFX_ByteString bsDefault = CFX_WideString(Default).UTF16LE_Encode();
  CFX_ByteString bsLabel = CFX_WideString(cLabel).UTF16LE_Encode();
  return m_pInfo->m_pJsPlatform->app_response(
      m_pInfo->m_pJsPlatform, AsFPDFWideString(&bsQuestion),
      AsFPDFWideString(&bsTitle), AsFPDFWideString(&bsDefault),
      AsFPDFWideString(&bsLabel), bPassword, response, length);
}

void CPDFSDK_FormFillEnvironment::JS_appBeep(int nType) {
  if (!m_pInfo || !m_pInfo->m_pJsPlatform ||
      !m_pInfo->m_pJsPlatform->app_beep) {
    return;
  }
  m_pInfo->m_pJsPlatform->app_beep(m_pInfo->m_pJsPlatform, nType);
}

CFX_WideString CPDFSDK_FormFillEnvironment::JS_fieldBrowse() {
  if (!m_pInfo || !m_pInfo->m_pJsPlatform ||
      !m_pInfo->m_pJsPlatform->Field_browse) {
    return CFX_WideString();
  }
  const int nRequiredLen =
      m_pInfo->m_pJsPlatform->Field_browse(m_pInfo->m_pJsPlatform, nullptr, 0);
  if (nRequiredLen <= 0)
    return CFX_WideString();

  std::unique_ptr<char[]> pBuff(new char[nRequiredLen]);
  memset(pBuff.get(), 0, nRequiredLen);
  const int nActualLen = m_pInfo->m_pJsPlatform->Field_browse(
      m_pInfo->m_pJsPlatform, pBuff.get(), nRequiredLen);
  if (nActualLen <= 0 || nActualLen > nRequiredLen)
    return CFX_WideString();

  return CFX_WideString::FromLocal(CFX_ByteStringC(pBuff.get(), nActualLen));
}

CFX_WideString CPDFSDK_FormFillEnvironment::JS_docGetFilePath() {
  if (!m_pInfo || !m_pInfo->m_pJsPlatform ||
      !m_pInfo->m_pJsPlatform->Doc_getFilePath) {
    return CFX_WideString();
  }
  const int nRequiredLen = m_pInfo->m_pJsPlatform->Doc_getFilePath(
      m_pInfo->m_pJsPlatform, nullptr, 0);
  if (nRequiredLen <= 0)
    return CFX_WideString();

  std::unique_ptr<char[]> pBuff(new char[nRequiredLen]);
  memset(pBuff.get(), 0, nRequiredLen);
  const int nActualLen = m_pInfo->m_pJsPlatform->Doc_getFilePath(
      m_pInfo->m_pJsPlatform, pBuff.get(), nRequiredLen);
  if (nActualLen <= 0 || nActualLen > nRequiredLen)
    return CFX_WideString();

  return CFX_WideString::FromLocal(CFX_ByteStringC(pBuff.get(), nActualLen));
}

void CPDFSDK_FormFillEnvironment::JS_docSubmitForm(void* formData,
                                                   int length,
                                                   const FX_WCHAR* URL) {
  if (!m_pInfo || !m_pInfo->m_pJsPlatform ||
      !m_pInfo->m_pJsPlatform->Doc_submitForm) {
    return;
  }
  CFX_ByteString bsDestination = CFX_WideString(URL).UTF16LE_Encode();
  m_pInfo->m_pJsPlatform->Doc_submitForm(m_pInfo->m_pJsPlatform, formData,
                                         length,
                                         AsFPDFWideString(&bsDestination));
}

void CPDFSDK_FormFillEnvironment::JS_docmailForm(void* mailData,
                                                 int length,
                                                 FPDF_BOOL bUI,
                                                 const FX_WCHAR* To,
                                                 const FX_WCHAR* Subject,
                                                 const FX_WCHAR* CC,
                                                 const FX_WCHAR* BCC,
                                                 const FX_WCHAR* Msg) {
  if (!m_pInfo || !m_pInfo->m_pJsPlatform ||
      !m_pInfo->m_pJsPlatform->Doc_mail) {
    return;
  }
  CFX_ByteString bsTo = CFX_WideString(To).UTF16LE_Encode();
  CFX_ByteString bsSubject = CFX_WideString(Subject).UTF16LE_Encode();
  CFX_ByteString bsCC = CFX_WideString(CC).UTF16LE_Encode();
  CFX_ByteString bsBcc = CFX_WideString(BCC).UTF16LE_Encode();
  CFX_ByteString bsMsg = CFX_WideString(Msg).UTF16LE_Encode();
  m_pInfo->m_pJsPlatform->Doc_mail(
      m_pInfo->m_pJsPlatform, mailData, length, bUI, AsFPDFWideString(&bsTo),
      AsFPDFWideString(&bsSubject), AsFPDFWideString(&bsCC),
      AsFPDFWideString(&bsBcc), AsFPDFWideString(&bsMsg));
}

void CPDFSDK_FormFillEnvironment::JS_docprint(FPDF_BOOL bUI,
                                              int nStart,
                                              int nEnd,
                                              FPDF_BOOL bSilent,
                                              FPDF_BOOL bShrinkToFit,
                                              FPDF_BOOL bPrintAsImage,
                                              FPDF_BOOL bReverse,
                                              FPDF_BOOL bAnnotations) {
  if (!m_pInfo || !m_pInfo->m_pJsPlatform ||
      !m_pInfo->m_pJsPlatform->Doc_print) {
    return;
  }
  m_pInfo->m_pJsPlatform->Doc_print(m_pInfo->m_pJsPlatform, bUI, nStart, nEnd,
                                    bSilent, bShrinkToFit, bPrintAsImage,
                                    bReverse, bAnnotations);
}

void CPDFSDK_FormFillEnvironment::JS_docgotoPage(int nPageNum) {
  if (!m_pInfo || !m_pInfo->m_pJsPlatform ||
      !m_pInfo->m_pJsPlatform->Doc_gotoPage) {
    return;
  }
  m_pInfo->m_pJsPlatform->Doc_gotoPage(m_pInfo->m_pJsPlatform, nPageNum);
}

IJS_Runtime* CPDFSDK_FormFillEnvironment::GetJSRuntime() {
  if (!IsJSInitiated())
    return nullptr;
  if (!m_pJSRuntime)
    m_pJSRuntime.reset(IJS_Runtime::Create(this));
  return m_pJSRuntime.get();
}

CPDFSDK_AnnotHandlerMgr* CPDFSDK_FormFillEnvironment::GetAnnotHandlerMgr() {
  if (!m_pAnnotHandlerMgr)
    m_pAnnotHandlerMgr = pdfium::MakeUnique<CPDFSDK_AnnotHandlerMgr>(this);
  return m_pAnnotHandlerMgr.get();
}

CPDFSDK_ActionHandler* CPDFSDK_FormFillEnvironment::GetActionHander() {
  if (!m_pActionHandler)
    m_pActionHandler = pdfium::MakeUnique<CPDFSDK_ActionHandler>();
  return m_pActionHandler.get();
}

CFFL_InteractiveFormFiller*
CPDFSDK_FormFillEnvironment::GetInteractiveFormFiller() {
  if (!m_pFormFiller)
    m_pFormFiller = pdfium::MakeUnique<CFFL_InteractiveFormFiller>(this);
  return m_pFormFiller.get();
}

void CPDFSDK_FormFillEnvironment::Invalidate(FPDF_PAGE page,
                                             double left,
                                             double top,
                                             double right,
                                             double bottom) {
  if (m_pInfo && m_pInfo->FFI_Invalidate)
    m_pInfo->FFI_Invalidate(m_pInfo, page, left, top, right, bottom);
}

void CPDFSDK_FormFillEnvironment::OutputSelectedRect(FPDF_PAGE page,
                                                     double left,
                                                     double top,
                                                     double right,
                                                     double bottom) {
  if (m_pInfo && m_pInfo->FFI_OutputSelectedRect)
    m_pInfo->FFI_OutputSelectedRect(m_pInfo, page, left, top, right, bottom);
}

void CPDFSDK_FormFillEnvironment::SetCursor(int nCursorType) {
  if (m_pInfo && m_pInfo->FFI_SetCursor)
    m_pInfo->FFI_SetCursor(m_pInfo, nCursorType);
}

int CPDFSDK_FormFillEnvironment::SetTimer(int uElapse,
                                          TimerCallback lpTimerFunc) {
  if (m_pInfo && m_pInfo->FFI_SetTimer)
    return m_pInfo->FFI_SetTimer(m_pInfo, uElapse, lpTimerFunc);
  return -1;
}

void CPDFSDK_FormFillEnvironment::KillTimer(int nTimerID) {
  if (m_pInfo && m_pInfo->FFI_KillTimer)
    m_pInfo->FFI_KillTimer(m_pInfo, nTimerID);
}

FX_SYSTEMTIME CPDFSDK_FormFillEnvironment::GetLocalTime() const {
  FX_SYSTEMTIME fxtime;
  if (!m_pInfo || !m_pInfo->FFI_GetLocalTime)
    return fxtime;

  FPDF_SYSTEMTIME systime = m_pInfo->FFI_GetLocalTime(m_pInfo);
  fxtime.wDay = systime.wDay;
  fxtime.wDayOfWeek = systime.wDayOfWeek;
  fxtime.wHour = systime.wHour;
  fxtime.wMilliseconds = systime.wMilliseconds;
  fxtime.wMinute = systime.wMinute;
  fxtime.wMonth = systime.wMonth;
  fxtime.wSecond = systime.wSecond;
  fxtime.wYear = systime.wYear;
  return fxtime;
}

void CPDFSDK_FormFillEnvironment::OnChange() {
  if (m_pInfo && m_pInfo->FFI_OnChange)
    m_pInfo->FFI_OnChange(m_pInfo);
}

FX_BOOL CPDFSDK_FormFillEnvironment::IsSHIFTKeyDown(uint32_t nFlag) const {
  return (nFlag & FWL_EVENTFLAG_ShiftKey) != 0;
}

FX_BOOL CPDFSDK_FormFillEnvironment::IsCTRLKeyDown(uint32_t nFlag) const {
  return (nFlag & FWL_EVENTFLAG_ControlKey) != 0;
}

FX_BOOL CPDFSDK_FormFillEnvironment::IsALTKeyDown(uint32_t nFlag) const {
  return (nFlag & FWL_EVENTFLAG_AltKey) != 0;
}

FPDF_PAGE CPDFSDK_FormFillEnvironment::GetPage(FPDF_DOCUMENT document,
                                               int nPageIndex) {
  if (m_pInfo && m_pInfo->FFI_GetPage)
    return m_pInfo->FFI_GetPage(m_pInfo, document, nPageIndex);
  return nullptr;
}

FPDF_PAGE CPDFSDK_FormFillEnvironment::GetCurrentPage(FPDF_DOCUMENT document) {
  if (m_pInfo && m_pInfo->FFI_GetCurrentPage)
    return m_pInfo->FFI_GetCurrentPage(m_pInfo, document);
  return nullptr;
}

void CPDFSDK_FormFillEnvironment::ExecuteNamedAction(
    const FX_CHAR* namedAction) {
  if (m_pInfo && m_pInfo->FFI_ExecuteNamedAction)
    m_pInfo->FFI_ExecuteNamedAction(m_pInfo, namedAction);
}

void CPDFSDK_FormFillEnvironment::OnSetFieldInputFocus(
    FPDF_WIDESTRING focusText,
    FPDF_DWORD nTextLen,
    FX_BOOL bFocus) {
  if (m_pInfo && m_pInfo->FFI_SetTextFieldFocus)
    m_pInfo->FFI_SetTextFieldFocus(m_pInfo, focusText, nTextLen, bFocus);
}

void CPDFSDK_FormFillEnvironment::DoURIAction(const FX_CHAR* bsURI) {
  if (m_pInfo && m_pInfo->FFI_DoURIAction)
    m_pInfo->FFI_DoURIAction(m_pInfo, bsURI);
}

void CPDFSDK_FormFillEnvironment::DoGoToAction(int nPageIndex,
                                               int zoomMode,
                                               float* fPosArray,
                                               int sizeOfArray) {
  if (m_pInfo && m_pInfo->FFI_DoGoToAction) {
    m_pInfo->FFI_DoGoToAction(m_pInfo, nPageIndex, zoomMode, fPosArray,
                              sizeOfArray);
  }
}

#ifdef PDF_ENABLE_XFA
void CPDFSDK_FormFillEnvironment::DisplayCaret(FPDF_PAGE page,
                                               FPDF_BOOL bVisible,
                                               double left,
                                               double top,
                                               double right,
                                               double bottom) {
  if (m_pInfo && m_pInfo->FFI_DisplayCaret) {
    m_pInfo->FFI_DisplayCaret(m_pInfo, page, bVisible, left, top, right,
                              bottom);
  }
}

int CPDFSDK_FormFillEnvironment::GetCurrentPageIndex(FPDF_DOCUMENT document) {
  if (!m_pInfo || !m_pInfo->FFI_GetCurrentPageIndex)
    return -1;
  return m_pInfo->FFI_GetCurrentPageIndex(m_pInfo, document);
}

void CPDFSDK_FormFillEnvironment::SetCurrentPage(FPDF_DOCUMENT document,
                                                 int iCurPage) {
  if (m_pInfo && m_pInfo->FFI_SetCurrentPage)
    m_pInfo->FFI_SetCurrentPage(m_pInfo, document, iCurPage);
}

CFX_WideString CPDFSDK_FormFillEnvironment::GetPlatform() {
  if (!m_pInfo || !m_pInfo->FFI_GetPlatform)
    return L"";

  int nRequiredLen = m_pInfo->FFI_GetPlatform(m_pInfo, nullptr, 0);
  if (nRequiredLen <= 0)
    return L"";

  char* pbuff = new char[nRequiredLen];
  memset(pbuff, 0, nRequiredLen);
  int nActualLen = m_pInfo->FFI_GetPlatform(m_pInfo, pbuff, nRequiredLen);
  if (nActualLen <= 0 || nActualLen > nRequiredLen) {
    delete[] pbuff;
    return L"";
  }
  CFX_ByteString bsRet = CFX_ByteString(pbuff, nActualLen);
  CFX_WideString wsRet = CFX_WideString::FromUTF16LE(
      (unsigned short*)bsRet.GetBuffer(bsRet.GetLength()),
      bsRet.GetLength() / sizeof(unsigned short));
  delete[] pbuff;
  return wsRet;
}

void CPDFSDK_FormFillEnvironment::GotoURL(FPDF_DOCUMENT document,
                                          const CFX_WideStringC& wsURL) {
  if (!m_pInfo || !m_pInfo->FFI_GotoURL)
    return;

  CFX_ByteString bsTo = CFX_WideString(wsURL).UTF16LE_Encode();
  FPDF_WIDESTRING pTo = (FPDF_WIDESTRING)bsTo.GetBuffer(wsURL.GetLength());
  m_pInfo->FFI_GotoURL(m_pInfo, document, pTo);
  bsTo.ReleaseBuffer();
}

void CPDFSDK_FormFillEnvironment::GetPageViewRect(FPDF_PAGE page,
                                                  FS_RECTF& dstRect) {
  if (!m_pInfo || !m_pInfo->FFI_GetPageViewRect)
    return;

  double left;
  double top;
  double right;
  double bottom;
  m_pInfo->FFI_GetPageViewRect(m_pInfo, page, &left, &top, &right, &bottom);

  dstRect.left = static_cast<float>(left);
  dstRect.top = static_cast<float>(top < bottom ? bottom : top);
  dstRect.bottom = static_cast<float>(top < bottom ? top : bottom);
  dstRect.right = static_cast<float>(right);
}

FX_BOOL CPDFSDK_FormFillEnvironment::PopupMenu(FPDF_PAGE page,
                                               FPDF_WIDGET hWidget,
                                               int menuFlag,
                                               CFX_PointF pt) {
  if (!m_pInfo || !m_pInfo->FFI_PopupMenu)
    return FALSE;
  return m_pInfo->FFI_PopupMenu(m_pInfo, page, hWidget, menuFlag, pt.x, pt.y);
}

void CPDFSDK_FormFillEnvironment::Alert(FPDF_WIDESTRING Msg,
                                        FPDF_WIDESTRING Title,
                                        int Type,
                                        int Icon) {
  if (m_pInfo && m_pInfo->m_pJsPlatform && m_pInfo->m_pJsPlatform->app_alert) {
    m_pInfo->m_pJsPlatform->app_alert(m_pInfo->m_pJsPlatform, Msg, Title, Type,
                                      Icon);
  }
}

void CPDFSDK_FormFillEnvironment::EmailTo(FPDF_FILEHANDLER* fileHandler,
                                          FPDF_WIDESTRING pTo,
                                          FPDF_WIDESTRING pSubject,
                                          FPDF_WIDESTRING pCC,
                                          FPDF_WIDESTRING pBcc,
                                          FPDF_WIDESTRING pMsg) {
  if (m_pInfo && m_pInfo->FFI_EmailTo)
    m_pInfo->FFI_EmailTo(m_pInfo, fileHandler, pTo, pSubject, pCC, pBcc, pMsg);
}

void CPDFSDK_FormFillEnvironment::UploadTo(FPDF_FILEHANDLER* fileHandler,
                                           int fileFlag,
                                           FPDF_WIDESTRING uploadTo) {
  if (m_pInfo && m_pInfo->FFI_UploadTo)
    m_pInfo->FFI_UploadTo(m_pInfo, fileHandler, fileFlag, uploadTo);
}

FPDF_FILEHANDLER* CPDFSDK_FormFillEnvironment::OpenFile(int fileType,
                                                        FPDF_WIDESTRING wsURL,
                                                        const char* mode) {
  if (m_pInfo && m_pInfo->FFI_OpenFile)
    return m_pInfo->FFI_OpenFile(m_pInfo, fileType, wsURL, mode);
  return nullptr;
}

IFX_FileRead* CPDFSDK_FormFillEnvironment::DownloadFromURL(
    const FX_WCHAR* url) {
  if (!m_pInfo || !m_pInfo->FFI_DownloadFromURL)
    return nullptr;

  CFX_ByteString bstrURL = CFX_WideString(url).UTF16LE_Encode();
  FPDF_WIDESTRING wsURL =
      (FPDF_WIDESTRING)bstrURL.GetBuffer(bstrURL.GetLength());

  FPDF_LPFILEHANDLER fileHandler = m_pInfo->FFI_DownloadFromURL(m_pInfo, wsURL);

  return new CFPDF_FileStream(fileHandler);
}

CFX_WideString CPDFSDK_FormFillEnvironment::PostRequestURL(
    const FX_WCHAR* wsURL,
    const FX_WCHAR* wsData,
    const FX_WCHAR* wsContentType,
    const FX_WCHAR* wsEncode,
    const FX_WCHAR* wsHeader) {
  if (!m_pInfo || !m_pInfo->FFI_PostRequestURL)
    return L"";

  CFX_ByteString bsURL = CFX_WideString(wsURL).UTF16LE_Encode();
  FPDF_WIDESTRING URL = (FPDF_WIDESTRING)bsURL.GetBuffer(bsURL.GetLength());

  CFX_ByteString bsData = CFX_WideString(wsData).UTF16LE_Encode();
  FPDF_WIDESTRING data = (FPDF_WIDESTRING)bsData.GetBuffer(bsData.GetLength());

  CFX_ByteString bsContentType = CFX_WideString(wsContentType).UTF16LE_Encode();
  FPDF_WIDESTRING contentType =
      (FPDF_WIDESTRING)bsContentType.GetBuffer(bsContentType.GetLength());

  CFX_ByteString bsEncode = CFX_WideString(wsEncode).UTF16LE_Encode();
  FPDF_WIDESTRING encode =
      (FPDF_WIDESTRING)bsEncode.GetBuffer(bsEncode.GetLength());

  CFX_ByteString bsHeader = CFX_WideString(wsHeader).UTF16LE_Encode();
  FPDF_WIDESTRING header =
      (FPDF_WIDESTRING)bsHeader.GetBuffer(bsHeader.GetLength());

  FPDF_BSTR response;
  FPDF_BStr_Init(&response);
  m_pInfo->FFI_PostRequestURL(m_pInfo, URL, data, contentType, encode, header,
                              &response);

  CFX_WideString wsRet = CFX_WideString::FromUTF16LE(
      (FPDF_WIDESTRING)response.str, response.len / sizeof(FPDF_WIDESTRING));
  FPDF_BStr_Clear(&response);

  return wsRet;
}

FPDF_BOOL CPDFSDK_FormFillEnvironment::PutRequestURL(const FX_WCHAR* wsURL,
                                                     const FX_WCHAR* wsData,
                                                     const FX_WCHAR* wsEncode) {
  if (!m_pInfo || !m_pInfo->FFI_PutRequestURL)
    return FALSE;

  CFX_ByteString bsURL = CFX_WideString(wsURL).UTF16LE_Encode();
  FPDF_WIDESTRING URL = (FPDF_WIDESTRING)bsURL.GetBuffer(bsURL.GetLength());

  CFX_ByteString bsData = CFX_WideString(wsData).UTF16LE_Encode();
  FPDF_WIDESTRING data = (FPDF_WIDESTRING)bsData.GetBuffer(bsData.GetLength());

  CFX_ByteString bsEncode = CFX_WideString(wsEncode).UTF16LE_Encode();
  FPDF_WIDESTRING encode =
      (FPDF_WIDESTRING)bsEncode.GetBuffer(bsEncode.GetLength());

  return m_pInfo->FFI_PutRequestURL(m_pInfo, URL, data, encode);
}

CFX_WideString CPDFSDK_FormFillEnvironment::GetLanguage() {
  if (!m_pInfo || !m_pInfo->FFI_GetLanguage)
    return L"";

  int nRequiredLen = m_pInfo->FFI_GetLanguage(m_pInfo, nullptr, 0);
  if (nRequiredLen <= 0)
    return L"";

  char* pbuff = new char[nRequiredLen];
  memset(pbuff, 0, nRequiredLen);
  int nActualLen = m_pInfo->FFI_GetLanguage(m_pInfo, pbuff, nRequiredLen);
  if (nActualLen <= 0 || nActualLen > nRequiredLen) {
    delete[] pbuff;
    return L"";
  }
  CFX_ByteString bsRet = CFX_ByteString(pbuff, nActualLen);
  CFX_WideString wsRet = CFX_WideString::FromUTF16LE(
      (FPDF_WIDESTRING)bsRet.GetBuffer(bsRet.GetLength()),
      bsRet.GetLength() / sizeof(FPDF_WIDESTRING));
  delete[] pbuff;
  return wsRet;
}

void CPDFSDK_FormFillEnvironment::PageEvent(int iPageCount,
                                            uint32_t dwEventType) const {
  if (m_pInfo && m_pInfo->FFI_PageEvent)
    m_pInfo->FFI_PageEvent(m_pInfo, iPageCount, dwEventType);
}
#endif  // PDF_ENABLE_XFA
