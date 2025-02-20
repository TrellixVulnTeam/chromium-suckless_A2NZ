// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/common/system/chromeos/brightness/tray_brightness.h"

#include <algorithm>

#include "ash/common/ash_constants.h"
#include "ash/common/material_design/material_design_controller.h"
#include "ash/common/shell_observer.h"
#include "ash/common/system/brightness_control_delegate.h"
#include "ash/common/system/tray/fixed_sized_image_view.h"
#include "ash/common/system/tray/system_tray_delegate.h"
#include "ash/common/system/tray/tray_constants.h"
#include "ash/common/wm/maximize_mode/maximize_mode_controller.h"
#include "ash/common/wm_shell.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "base/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/power_manager_client.h"
#include "grit/ash_resources.h"
#include "grit/ash_strings.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/display/display.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/slider.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"

namespace ash {
namespace tray {
namespace {

// We don't let the screen brightness go lower than this when it's being
// adjusted via the slider.  Otherwise, if the user doesn't know about the
// brightness keys, they may turn the backlight off and not know how to turn it
// back on.
const double kMinBrightnessPercent = 5.0;

}  // namespace

// TODO(yiyix|tdanderson): Once Chrome OS material design is enabled by default,
// BrightnessView does not need to be a ShellObserver to observe touch view mode
// changes. See crbug.com/614453.
class BrightnessView : public ShellObserver,
                       public views::View,
                       public views::SliderListener {
 public:
  BrightnessView(bool default_view, double initial_percent);
  ~BrightnessView() override;

  bool is_default_view() const { return is_default_view_; }

  // |percent| is in the range [0.0, 100.0].
  void SetBrightnessPercent(double percent);

  // ShellObserver:
  void OnMaximizeModeStarted() override;
  void OnMaximizeModeEnded() override;

 private:
  // views::View:
  void OnBoundsChanged(const gfx::Rect& old_bounds) override;

  // views:SliderListener:
  void SliderValueChanged(views::Slider* sender,
                          float value,
                          float old_value,
                          views::SliderChangeReason reason) override;

  // views:SliderListener:
  void SliderDragStarted(views::Slider* slider) override;
  void SliderDragEnded(views::Slider* slider) override;

  views::Slider* slider_;

  // Is |slider_| currently being dragged?
  bool dragging_;

  // True if this view is for the default tray view. Used to control hide/show
  // behaviour of the default view when entering or leaving Maximize Mode.
  bool is_default_view_;

  // Last brightness level that we observed, in the range [0.0, 100.0].
  double last_percent_;

  DISALLOW_COPY_AND_ASSIGN(BrightnessView);
};

BrightnessView::BrightnessView(bool default_view, double initial_percent)
    : dragging_(false),
      is_default_view_(default_view),
      last_percent_(initial_percent) {
  SetLayoutManager(new views::BoxLayout(views::BoxLayout::kHorizontal,
                                        kTrayPopupPaddingHorizontal, 0,
                                        kTrayPopupPaddingBetweenItems));

  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  views::ImageView* icon =
      new FixedSizedImageView(0, GetTrayConstant(TRAY_POPUP_ITEM_HEIGHT));
  if (MaterialDesignController::IsSystemTrayMenuMaterial()) {
    icon->SetImage(
        gfx::CreateVectorIcon(kSystemMenuBrightnessIcon, kMenuIconColor));
  } else {
    icon->SetImage(
        rb.GetImageNamed(IDR_AURA_UBER_TRAY_BRIGHTNESS).ToImageSkia());
  }
  AddChildView(icon);
  slider_ = views::Slider::CreateSlider(
      ash::MaterialDesignController::IsSystemTrayMenuMaterial(), this);
  if (ash::MaterialDesignController::IsSystemTrayMenuMaterial()) {
    slider_->SetBorder(views::Border::CreateEmptyBorder(
        gfx::Insets(0, kTrayPopupSliderPaddingMD) + slider_->GetInsets()));
  }
  slider_->set_focus_border_color(kFocusBorderColor);
  slider_->SetValue(static_cast<float>(initial_percent / 100.0));
  slider_->SetAccessibleName(
      rb.GetLocalizedString(IDS_ASH_STATUS_TRAY_BRIGHTNESS));
  AddChildView(slider_);

  if (MaterialDesignController::IsSystemTrayMenuMaterial()) {
    SetVisible(true);
  } else {
    if (is_default_view_) {
      WmShell::Get()->AddShellObserver(this);
      SetVisible(WmShell::Get()
                     ->maximize_mode_controller()
                     ->IsMaximizeModeWindowManagerEnabled());
    }
  }
}

BrightnessView::~BrightnessView() {
  if (is_default_view_)
    WmShell::Get()->RemoveShellObserver(this);
}

void BrightnessView::SetBrightnessPercent(double percent) {
  last_percent_ = percent;
  if (!dragging_)
    slider_->SetValue(static_cast<float>(percent / 100.0));
}

void BrightnessView::OnMaximizeModeStarted() {
  if (!MaterialDesignController::IsSystemTrayMenuMaterial())
    SetVisible(true);
}

void BrightnessView::OnMaximizeModeEnded() {
  if (!MaterialDesignController::IsSystemTrayMenuMaterial())
    SetVisible(false);
}

void BrightnessView::OnBoundsChanged(const gfx::Rect& old_bounds) {
  int w = width() - slider_->x();
  slider_->SetSize(gfx::Size(w, slider_->height()));
}

void BrightnessView::SliderValueChanged(views::Slider* sender,
                                        float value,
                                        float old_value,
                                        views::SliderChangeReason reason) {
  DCHECK_EQ(sender, slider_);
  if (reason != views::VALUE_CHANGED_BY_USER)
    return;
  BrightnessControlDelegate* brightness_control_delegate =
      WmShell::Get()->brightness_control_delegate();
  if (brightness_control_delegate) {
    double percent = std::max(value * 100.0, kMinBrightnessPercent);
    brightness_control_delegate->SetBrightnessPercent(percent, true);
  }
}

void BrightnessView::SliderDragStarted(views::Slider* slider) {
  DCHECK_EQ(slider, slider_);
  dragging_ = true;
}

void BrightnessView::SliderDragEnded(views::Slider* slider) {
  DCHECK_EQ(slider, slider_);
  dragging_ = false;
  slider_->SetValue(static_cast<float>(last_percent_ / 100.0));
}

}  // namespace tray

TrayBrightness::TrayBrightness(SystemTray* system_tray)
    : SystemTrayItem(system_tray, UMA_DISPLAY_BRIGHTNESS),
      brightness_view_(NULL),
      current_percent_(100.0),
      got_current_percent_(false),
      weak_ptr_factory_(this) {
  // Post a task to get the initial brightness; the BrightnessControlDelegate
  // isn't created yet.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&TrayBrightness::GetInitialBrightness,
                            weak_ptr_factory_.GetWeakPtr()));
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->AddObserver(
      this);
}

TrayBrightness::~TrayBrightness() {
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->RemoveObserver(
      this);
}

void TrayBrightness::GetInitialBrightness() {
  BrightnessControlDelegate* brightness_control_delegate =
      WmShell::Get()->brightness_control_delegate();
  // Worrisome, but happens in unit tests, so don't log anything.
  if (!brightness_control_delegate)
    return;
  brightness_control_delegate->GetBrightnessPercent(
      base::Bind(&TrayBrightness::HandleInitialBrightness,
                 weak_ptr_factory_.GetWeakPtr()));
}

void TrayBrightness::HandleInitialBrightness(double percent) {
  if (!got_current_percent_)
    HandleBrightnessChanged(percent, false);
}

views::View* TrayBrightness::CreateTrayView(LoginStatus status) {
  return NULL;
}

views::View* TrayBrightness::CreateDefaultView(LoginStatus status) {
  CHECK(brightness_view_ == NULL);
  brightness_view_ = new tray::BrightnessView(true, current_percent_);
  return brightness_view_;
}

views::View* TrayBrightness::CreateDetailedView(LoginStatus status) {
  CHECK(brightness_view_ == NULL);
  WmShell::Get()->RecordUserMetricsAction(
      UMA_STATUS_AREA_DETAILED_BRIGHTNESS_VIEW);
  brightness_view_ = new tray::BrightnessView(false, current_percent_);
  return brightness_view_;
}

void TrayBrightness::DestroyTrayView() {}

void TrayBrightness::DestroyDefaultView() {
  if (brightness_view_ && brightness_view_->is_default_view())
    brightness_view_ = NULL;
}

void TrayBrightness::DestroyDetailedView() {
  if (brightness_view_ && !brightness_view_->is_default_view())
    brightness_view_ = NULL;
}

void TrayBrightness::UpdateAfterLoginStatusChange(LoginStatus status) {}

bool TrayBrightness::ShouldHideArrow() const {
  return true;
}

bool TrayBrightness::ShouldShowShelf() const {
  return false;
}

void TrayBrightness::BrightnessChanged(int level, bool user_initiated) {
  WmShell::Get()->RecordUserMetricsAction(UMA_STATUS_AREA_BRIGHTNESS_CHANGED);
  double percent = static_cast<double>(level);
  HandleBrightnessChanged(percent, user_initiated);
}

void TrayBrightness::HandleBrightnessChanged(double percent,
                                             bool user_initiated) {
  current_percent_ = percent;
  got_current_percent_ = true;

  if (brightness_view_)
    brightness_view_->SetBrightnessPercent(percent);

  if (!user_initiated)
    return;

  // Never show the bubble on systems that lack internal displays: if an
  // external display's brightness is changed, it may already display the new
  // level via an on-screen display.
  if (!display::Display::HasInternalDisplay())
    return;

  if (brightness_view_)
    SetDetailedViewCloseDelay(kTrayPopupAutoCloseDelayInSeconds);
  else
    PopupDetailedView(kTrayPopupAutoCloseDelayInSeconds, false);
}

}  // namespace ash
