// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_NOTIFICATIONS_NOTIFICATION_DELIVERY_H_
#define CHROME_BROWSER_UI_COCOA_NOTIFICATIONS_NOTIFICATION_DELIVERY_H_

#import <Foundation/Foundation.h>

// Collection of protocols used for XPC communication between chrome
// and the alert notification xpc service.

// Protocol for the XPC notification service.
@protocol NotificationDelivery

// |notificationData| is generated using a NofiticationBuilder object.
- (void)deliverNotification:(NSDictionary*)notificationData;

@end

// Response protocol for the XPC notification service to notify Chrome of
// notification interactions.
@protocol NotificationReply

// |notificationResponseData| is generated through a
// NotificationResponseBuilder.
- (void)notificationClick:(NSDictionary*)notificationResponseData;

@end

#endif  // CHROME_BROWSER_UI_COCOA_NOTIFICATIONS_NOTIFICATION_DELIVERY_H_
