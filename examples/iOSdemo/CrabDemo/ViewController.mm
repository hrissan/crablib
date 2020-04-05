// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#import "ViewController.h"

@interface ViewController ()

@end

@implementation ViewController

- (void)onConnection {
	[[self connected_label] setText:[NSString stringWithFormat:@"Requests: %d", int(self->counter)]];
}

- (void)viewDidLoad {
	[super viewDidLoad];
	// Do any additional setup after loading the view.
	
	if( !self->server ) {
		self->counter = 0;
		self->server.reset(new crab::http::Server(7000));
		self->server->r_handler = [self](crab::http::Client *who, crab::http::Request &&request) {
			crab::http::Response response;
			response.header.status = 200;
			response.header.set_content_type("text/plain", "charset=utf-8");
			response.set_body("Hello, Crab!");
			who->write(std::move(response));
			counter += 1;
			[self onConnection];
		};
	}
	[self onConnection];
}


@end
