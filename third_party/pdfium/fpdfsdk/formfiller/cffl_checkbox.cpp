// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "fpdfsdk/formfiller/cffl_checkbox.h"

#include "fpdfsdk/cpdfsdk_formfillenvironment.h"
#include "fpdfsdk/cpdfsdk_widget.h"
#include "fpdfsdk/formfiller/cffl_formfiller.h"
#include "fpdfsdk/pdfwindow/PWL_SpecialButton.h"
#include "public/fpdf_fwlevent.h"

CFFL_CheckBox::CFFL_CheckBox(CPDFSDK_FormFillEnvironment* pApp,
                             CPDFSDK_Widget* pWidget)
    : CFFL_Button(pApp, pWidget) {}

CFFL_CheckBox::~CFFL_CheckBox() {}

CPWL_Wnd* CFFL_CheckBox::NewPDFWindow(const PWL_CREATEPARAM& cp,
                                      CPDFSDK_PageView* pPageView) {
  CPWL_CheckBox* pWnd = new CPWL_CheckBox();
  pWnd->Create(cp);
  pWnd->SetCheck(m_pWidget->IsChecked());
  return pWnd;
}

FX_BOOL CFFL_CheckBox::OnKeyDown(CPDFSDK_Annot* pAnnot,
                                 uint32_t nKeyCode,
                                 uint32_t nFlags) {
  switch (nKeyCode) {
    case FWL_VKEY_Return:
    case FWL_VKEY_Space:
      return TRUE;
    default:
      return CFFL_FormFiller::OnKeyDown(pAnnot, nKeyCode, nFlags);
  }
}
FX_BOOL CFFL_CheckBox::OnChar(CPDFSDK_Annot* pAnnot,
                              uint32_t nChar,
                              uint32_t nFlags) {
  switch (nChar) {
    case FWL_VKEY_Return:
    case FWL_VKEY_Space: {
      CPDFSDK_PageView* pPageView = pAnnot->GetPageView();
      ASSERT(pPageView);

      FX_BOOL bReset = FALSE;
      FX_BOOL bExit = FALSE;
      CPDFSDK_Annot::ObservedPtr pObserved(m_pWidget);
      m_pEnv->GetInteractiveFormFiller()->OnButtonUp(&pObserved, pPageView,
                                                     bReset, bExit, nFlags);
      if (!pObserved) {
        m_pWidget = nullptr;
        return TRUE;
      }
      if (bReset || bExit)
        return TRUE;

      CFFL_FormFiller::OnChar(pAnnot, nChar, nFlags);
      if (CPWL_CheckBox* pWnd = (CPWL_CheckBox*)GetPDFWindow(pPageView, TRUE))
        pWnd->SetCheck(!pWnd->IsChecked());

      CommitData(pPageView, nFlags);
      return TRUE;
    }
    default:
      return CFFL_FormFiller::OnChar(pAnnot, nChar, nFlags);
  }
}

FX_BOOL CFFL_CheckBox::OnLButtonUp(CPDFSDK_PageView* pPageView,
                                   CPDFSDK_Annot* pAnnot,
                                   uint32_t nFlags,
                                   const CFX_FloatPoint& point) {
  CFFL_Button::OnLButtonUp(pPageView, pAnnot, nFlags, point);

  if (IsValid()) {
    if (CPWL_CheckBox* pWnd = (CPWL_CheckBox*)GetPDFWindow(pPageView, TRUE)) {
      CPDFSDK_Widget* pWidget = (CPDFSDK_Widget*)pAnnot;
      pWnd->SetCheck(!pWidget->IsChecked());
    }

    if (!CommitData(pPageView, nFlags))
      return FALSE;
  }

  return TRUE;
}

FX_BOOL CFFL_CheckBox::IsDataChanged(CPDFSDK_PageView* pPageView) {
  CPWL_CheckBox* pWnd = (CPWL_CheckBox*)GetPDFWindow(pPageView, FALSE);
  return pWnd && pWnd->IsChecked() != m_pWidget->IsChecked();
}

void CFFL_CheckBox::SaveData(CPDFSDK_PageView* pPageView) {
  if (CPWL_CheckBox* pWnd = (CPWL_CheckBox*)GetPDFWindow(pPageView, FALSE)) {
    bool bNewChecked = pWnd->IsChecked();

    if (bNewChecked) {
      CPDF_FormField* pField = m_pWidget->GetFormField();
      for (int32_t i = 0, sz = pField->CountControls(); i < sz; i++) {
        if (CPDF_FormControl* pCtrl = pField->GetControl(i)) {
          if (pCtrl->IsChecked()) {
            break;
          }
        }
      }
    }

    m_pWidget->SetCheck(bNewChecked, false);
    m_pWidget->UpdateField();
    SetChangeMark();
  }
}
