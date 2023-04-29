// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#import <UIKit/UIKit.h>
#include <crab/crab.hpp>

@interface ViewController : UIViewController {

std::unique_ptr<crab::http::Server> server;
size_t counter;

}

@property (strong, nonatomic) IBOutlet UILabel * connected_label;

@end

