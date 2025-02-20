// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/common/system/tray/tray_item_more.h"

#include "ash/common/material_design/material_design_controller.h"
#include "ash/common/system/tray/fixed_sized_image_view.h"
#include "ash/common/system/tray/system_tray_item.h"
#include "ash/common/system/tray/tray_constants.h"
#include "ash/common/system/tray/tray_popup_item_style.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "base/memory/ptr_util.h"
#include "grit/ash_resources.h"
#include "ui/accessibility/ax_view_state.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace ash {

TrayItemMore::TrayItemMore(SystemTrayItem* owner, bool show_more)
    : ActionableView(owner),
      show_more_(show_more),
      icon_(nullptr),
      label_(nullptr),
      more_(nullptr) {
  SetLayoutManager(new views::BoxLayout(views::BoxLayout::kHorizontal,
                                        kTrayPopupPaddingHorizontal, 0,
                                        kTrayPopupPaddingBetweenItems));

  icon_ = new FixedSizedImageView(0, GetTrayConstant(TRAY_POPUP_ITEM_HEIGHT));
  AddChildView(icon_);

  label_ = new views::Label;
  label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  AddChildView(label_);

  if (show_more) {
    more_ = new views::ImageView;
    more_->EnableCanvasFlippingForRTLUI(true);
    if (!MaterialDesignController::IsSystemTrayMenuMaterial()) {
      // The icon doesn't change in non-md.
      more_->SetImage(ui::ResourceBundle::GetSharedInstance()
                          .GetImageNamed(IDR_AURA_UBER_TRAY_MORE)
                          .ToImageSkia());
    }
    AddChildView(more_);
  }
}

TrayItemMore::~TrayItemMore() {}

void TrayItemMore::SetLabel(const base::string16& label) {
  label_->SetText(label);
  Layout();
  SchedulePaint();
}

void TrayItemMore::SetImage(const gfx::ImageSkia& image_skia) {
  icon_->SetImage(image_skia);
  SchedulePaint();
}

void TrayItemMore::SetAccessibleName(const base::string16& name) {
  accessible_name_ = name;
}

std::unique_ptr<TrayPopupItemStyle> TrayItemMore::CreateStyle() const {
  return base::MakeUnique<TrayPopupItemStyle>(
      GetNativeTheme(), TrayPopupItemStyle::FontStyle::DEFAULT_VIEW_LABEL);
}

void TrayItemMore::UpdateStyle() {
  if (!MaterialDesignController::IsSystemTrayMenuMaterial())
    return;
  std::unique_ptr<TrayPopupItemStyle> style = CreateStyle();
  style->SetupLabel(label_);

  if (more_) {
    more_->SetImage(gfx::CreateVectorIcon(kSystemMenuArrowRightIcon,
                                          style->GetForegroundColor()));
  }
}

bool TrayItemMore::PerformAction(const ui::Event& event) {
  if (!show_more_)
    return false;

  owner()->TransitionDetailedView();
  return true;
}

void TrayItemMore::Layout() {
  // Let the box-layout do the layout first. Then move the '>' arrow to right
  // align.
  views::View::Layout();

  if (!show_more_)
    return;

  // Make sure the chevron always has the full size.
  gfx::Size size = more_->GetPreferredSize();
  gfx::Rect bounds(size);
  bounds.set_x(width() - size.width() - kTrayPopupPaddingBetweenItems);
  bounds.set_y((height() - size.height()) / 2);
  more_->SetBoundsRect(bounds);

  // Adjust the label's bounds in case it got cut off by |more_|.
  if (label_->bounds().Intersects(more_->bounds())) {
    gfx::Rect bounds = label_->bounds();
    bounds.set_width(more_->x() - kTrayPopupPaddingBetweenItems - label_->x());
    label_->SetBoundsRect(bounds);
  }
}

void TrayItemMore::GetAccessibleState(ui::AXViewState* state) {
  ActionableView::GetAccessibleState(state);
  if (!accessible_name_.empty())
    state->name = accessible_name_;
}

void TrayItemMore::OnNativeThemeChanged(const ui::NativeTheme* theme) {
  ActionableView::OnNativeThemeChanged(theme);
  UpdateStyle();
}

}  // namespace ash
