// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/run_loop.h"
#include "chrome/browser/safe_browsing/mock_permission_report_sender.h"
#include "content/public/browser/browser_thread.h"

namespace safe_browsing {

MockPermissionReportSender::MockPermissionReportSender()
    : net::ReportSender(nullptr, DO_NOT_SEND_COOKIES),
      number_of_reports_(0) {
  DCHECK(quit_closure_.is_null());
}

MockPermissionReportSender::~MockPermissionReportSender() {
}

void MockPermissionReportSender::Send(const GURL& report_uri,
                                      base::StringPiece content_type,
                                      base::StringPiece report) {
  latest_report_uri_ = report_uri;
  report.CopyToString(&latest_report_);
  content_type.CopyToString(&latest_content_type_);
  number_of_reports_++;

  // BrowserThreads aren't initialized in the unittest, so don't post tasks
  // to them.
  if (!content::BrowserThread::IsThreadInitialized(content::BrowserThread::UI))
    return;

  content::BrowserThread::PostTask(
      content::BrowserThread::UI, FROM_HERE,
      base::Bind(
          &MockPermissionReportSender::NotifyReportSentOnUIThread,
          base::Unretained(this)));
}

void MockPermissionReportSender::WaitForReportSent() {
  base::RunLoop run_loop;
  quit_closure_ = run_loop.QuitClosure();
  run_loop.Run();
}

void MockPermissionReportSender::NotifyReportSentOnUIThread() {
  if (!quit_closure_.is_null()) {
    quit_closure_.Run();
    quit_closure_.Reset();
  }
}

const GURL& MockPermissionReportSender::latest_report_uri() {
  return latest_report_uri_;
}

const std::string& MockPermissionReportSender::latest_report() {
  return latest_report_;
}

const std::string& MockPermissionReportSender::latest_content_type() {
  return latest_content_type_;
}

int MockPermissionReportSender::GetAndResetNumberOfReportsSent() {
  int new_reports = number_of_reports_;
  number_of_reports_ = 0;
  return new_reports;
}

}  // namespace safe_browsing
