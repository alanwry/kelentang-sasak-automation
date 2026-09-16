#ifndef WEBSERVER_ADMIN_H
#define WEBSERVER_ADMIN_H

#include <esp_http_server.h>

void register_urihandlers_admin(httpd_handle_t server);

#endif