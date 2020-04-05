//
//  AppDelegate.h
//  CrabDemo
//
//  Created by Grigory Buteyko on 04.04.2020.
//  Copyright © 2020 Hrissan. All rights reserved.
//

#import <UIKit/UIKit.h>
#include <crab/crab.hpp>

@interface AppDelegate : UIResponder <UIApplicationDelegate> {
	crab::RunLoop runloop;
}

@property (strong, nonatomic) UIWindow *window;

@end

