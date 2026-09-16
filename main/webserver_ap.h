#ifndef WEBSERVER_AP_H
#define WEBSERVER_AP_H

#include <esp_http_server.h>

void register_urihandlers_ap(httpd_handle_t server);

#endif