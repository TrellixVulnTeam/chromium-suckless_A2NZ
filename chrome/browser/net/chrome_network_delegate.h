// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NET_CHROME_NETWORK_DELEGATE_H_
#define CHROME_BROWSER_NET_CHROME_NETWORK_DELEGATE_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/net/safe_search_util.h"
#include "components/data_use_measurement/content/data_use_measurement.h"
#include "components/metrics/data_use_tracker.h"
#include "components/prefs/pref_member.h"
#include "net/base/network_delegate_impl.h"

class ChromeExtensionsNetworkDelegate;
class PrefService;

template<class T> class PrefMember;

typedef PrefMember<bool> BooleanPrefMember;

namespace base {
class Value;
}

namespace content_settings {
class CookieSettings;
}

namespace data_usage {
class DataUseAggregator;
}

namespace domain_reliability {
class DomainReliabilityMonitor;
}

namespace extensions {
class EventRouterForwarder;
class InfoMap;
}

namespace net {
class URLRequest;
}

namespace policy {
class URLBlacklistManager;
}

// ChromeNetworkDelegate is the central point from within the chrome code to
// add hooks into the network stack.
class ChromeNetworkDelegate : public net::NetworkDelegateImpl {
 public:
  // |enable_referrers| (and all of the other optional PrefMembers) should be
  // initialized on the UI thread (see below) beforehand. This object's owner is
  // responsible for cleaning them up at shutdown.
  ChromeNetworkDelegate(
      extensions::EventRouterForwarder* event_router,
      BooleanPrefMember* enable_referrers,
      const metrics::UpdateUsagePrefCallbackType& metrics_data_use_forwarder);
  ~ChromeNetworkDelegate() override;

  // Pass through to ChromeExtensionsNetworkDelegate::set_extension_info_map().
  void set_extension_info_map(extensions::InfoMap* extension_info_map);

  void set_url_blacklist_manager(
      const policy::URLBlacklistManager* url_blacklist_manager) {
    url_blacklist_manager_ = url_blacklist_manager;
  }

  // If |profile| is nullptr or not set, events will be broadcast to all
  // profiles, otherwise they will only be sent to the specified profile.
  // Also pass through to ChromeExtensionsNetworkDelegate::set_profile().
  void set_profile(void* profile);

  // |profile_path| is used to locate the "Downloads" folder on Chrome OS. If it
  // is set, the location of the Downloads folder for the profile is added to
  // the whitelist for accesses via file: scheme.
  void set_profile_path(const base::FilePath& profile_path) {
    profile_path_ = profile_path;
  }

  // If |cookie_settings| is nullptr or not set, all cookies are enabled,
  // otherwise the settings are enforced on all observed network requests.
  // Not inlined because we assign a scoped_refptr, which requires us to include
  // the header file. Here we just forward-declare it.
  void set_cookie_settings(content_settings::CookieSettings* cookie_settings);

  void set_enable_do_not_track(BooleanPrefMember* enable_do_not_track) {
    enable_do_not_track_ = enable_do_not_track;
  }

  void set_force_google_safe_search(
      BooleanPrefMember* force_google_safe_search) {
    force_google_safe_search_ = force_google_safe_search;
  }

  void set_force_youtube_restrict(
      IntegerPrefMember* force_youtube_restrict) {
    force_youtube_restrict_ = force_youtube_restrict;
  }

  void set_allowed_domains_for_apps(
      StringPrefMember* allowed_domains_for_apps) {
    allowed_domains_for_apps_ = allowed_domains_for_apps;
  }

  void set_domain_reliability_monitor(
      domain_reliability::DomainReliabilityMonitor* monitor) {
    domain_reliability_monitor_ = monitor;
  }

  void set_data_use_aggregator(
      data_usage::DataUseAggregator* data_use_aggregator,
      bool is_data_usage_off_the_record);

  // Binds the pref members to |pref_service| and moves them to the IO thread.
  // |enable_referrers| cannot be nullptr, the others can.
  // This method should be called on the UI thread.
  static void InitializePrefsOnUIThread(
      BooleanPrefMember* enable_referrers,
      BooleanPrefMember* enable_do_not_track,
      BooleanPrefMember* force_google_safe_search,
      IntegerPrefMember* force_youtube_restrict,
      StringPrefMember* allowed_domains_for_apps,
      PrefService* pref_service);

  // When called, all file:// URLs will now be accessible.  If this is not
  // called, then some platforms restrict access to file:// paths.
  static void AllowAccessToAllFiles();

 private:
  // NetworkDelegate implementation.
  int OnBeforeURLRequest(net::URLRequest* request,
                         const net::CompletionCallback& callback,
                         GURL* new_url) override;
  int OnBeforeStartTransaction(net::URLRequest* request,
                               const net::CompletionCallback& callback,
                               net::HttpRequestHeaders* headers) override;
  void OnStartTransaction(net::URLRequest* request,
                          const net::HttpRequestHeaders& headers) override;
  int OnHeadersReceived(
      net::URLRequest* request,
      const net::CompletionCallback& callback,
      const net::HttpResponseHeaders* original_response_headers,
      scoped_refptr<net::HttpResponseHeaders>* override_response_headers,
      GURL* allowed_unsafe_redirect_url) override;
  void OnBeforeRedirect(net::URLRequest* request,
                        const GURL& new_location) override;
  void OnResponseStarted(net::URLRequest* request) override;
  void OnNetworkBytesReceived(net::URLRequest* request,
                              int64_t bytes_received) override;
  void OnNetworkBytesSent(net::URLRequest* request,
                          int64_t bytes_sent) override;
  void OnCompleted(net::URLRequest* request, bool started) override;
  void OnURLRequestDestroyed(net::URLRequest* request) override;
  void OnPACScriptError(int line_number, const base::string16& error) override;
  net::NetworkDelegate::AuthRequiredResponse OnAuthRequired(
      net::URLRequest* request,
      const net::AuthChallengeInfo& auth_info,
      const AuthCallback& callback,
      net::AuthCredentials* credentials) override;
  bool OnCanGetCookies(const net::URLRequest& request,
                       const net::CookieList& cookie_list) override;
  bool OnCanSetCookie(const net::URLRequest& request,
                      const std::string& cookie_line,
                      net::CookieOptions* options) override;
  bool OnCanAccessFile(const net::URLRequest& request,
                       const base::FilePath& path) const override;
  bool OnCanEnablePrivacyMode(
      const GURL& url,
      const GURL& first_party_for_cookies) const override;
  bool OnAreExperimentalCookieFeaturesEnabled() const override;
  bool OnAreStrictSecureCookiesEnabled() const override;
  bool OnCancelURLRequestWithPolicyViolatingReferrerHeader(
      const net::URLRequest& request,
      const GURL& target_url,
      const GURL& referrer_url) const override;

  // Convenience function for reporting network usage to the
  // |data_use_aggregator_|.
  void ReportDataUsageStats(net::URLRequest* request,
                            int64_t tx_bytes,
                            int64_t rx_bytes);

  std::unique_ptr<ChromeExtensionsNetworkDelegate> extensions_delegate_;

  void* profile_;
  base::FilePath profile_path_;
  scoped_refptr<content_settings::CookieSettings> cookie_settings_;

  // Weak, owned by our owner.
  BooleanPrefMember* enable_referrers_;
  BooleanPrefMember* enable_do_not_track_;
  BooleanPrefMember* force_google_safe_search_;
  IntegerPrefMember* force_youtube_restrict_;
  StringPrefMember* allowed_domains_for_apps_;

  // Weak, owned by our owner.
  const policy::URLBlacklistManager* url_blacklist_manager_;
  domain_reliability::DomainReliabilityMonitor* domain_reliability_monitor_;

  // When true, allow access to all file:// URLs.
  static bool g_allow_file_access_;

  // Component to measure data use.
  data_use_measurement::DataUseMeasurement data_use_measurement_;

  bool experimental_web_platform_features_enabled_;

  // Aggregates and reports network usage.
  data_usage::DataUseAggregator* data_use_aggregator_;
  // Controls whether network usage is reported as being off the record.
  bool is_data_usage_off_the_record_;

  DISALLOW_COPY_AND_ASSIGN(ChromeNetworkDelegate);
};

#endif  // CHROME_BROWSER_NET_CHROME_NETWORK_DELEGATE_H_
