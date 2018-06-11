/*
 * ESPRSSIF MIT License
 *
 * Copyright (c) 2017 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP8266 only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "esp_common.h"
#include "user_config.h"
#include <lwip/api.h>
#include <gpio.h>

static os_timer_t timer;

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32 user_rf_cal_sector_set(void)
{
    flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;
        case FLASH_SIZE_64M_MAP_1024_1024:
            rf_cal_sec = 2048 - 5;
            break;
        case FLASH_SIZE_128M_MAP_1024_1024:
            rf_cal_sec = 4096 - 5;
            break;
        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}

LOCAL void ICACHE_FLASH_ATTR wait_for_connection_ready(uint8 flag)
{
    os_timer_disarm(&timer);
    if(wifi_station_connected()){
        os_printf("connected\n");
    } else {
        os_printf("reconnect after 2s\n");
        os_timer_setfn(&timer, (os_timer_func_t *)wait_for_connection_ready, NULL);
        os_timer_arm(&timer, 2000, 0);
    }
}

LOCAL void ICACHE_FLASH_ATTR on_wifi_connect(){
    os_timer_disarm(&timer);
    os_timer_setfn(&timer, (os_timer_func_t *)wait_for_connection_ready, NULL);
    os_timer_arm(&timer, 100, 0);
}

LOCAL void ICACHE_FLASH_ATTR on_wifi_disconnect(uint8_t reason){
    os_printf("disconnect %d\n", reason);
}

void httpd_task(void *pvParameters)
{
    struct netconn *client = NULL;
    struct netconn *nc = netconn_new(NETCONN_TCP);
    if (nc == NULL) {
        printf("Failed to allocate socket.\n");
        vTaskDelete(NULL);
    }
    netconn_bind(nc, IP_ADDR_ANY, 80);
    netconn_listen(nc);
    char buf[512];
    const char *webpage = {
        "HTTP/1.1 200 OK\r\n"
        "Content-type: text/html\r\n\r\n"
        "<html><head><title>HTTP Server</title>"
        "<style> div.main {"
        "font-family: Arial;"
        "padding: 0.01em 16px;"
        "box-shadow: 2px 2px 1px 1px #d2d2d2;"
        "background-color: #f1f1f1;}"
        "</style></head>"
        "<body><div class='main'>"
        "<h3>HTTP Server</h3>"
        "<p>URL: %s</p>"
        "<p>Uptime: %d seconds</p>"
        "<p>Free heap: %d bytes</p>"
        "<button onclick=\"location.href='/on'\" type='button'>"
        "LED On</button></p>"
        "<button onclick=\"location.href='/off'\" type='button'>"
        "LED Off</button></p>"
        "</div></body></html>"
    };
    /* disable LED */
    GPIO_AS_OUTPUT(15);
    GPIO_OUTPUT(15, true);
    while (1) {
        err_t err = netconn_accept(nc, &client);
        if (err == ERR_OK) {
            struct netbuf *nb;
            if ((err = netconn_recv(client, &nb)) == ERR_OK) {
                void *data;
                u16_t len;
                netbuf_data(nb, &data, &len);
                /* check for a GET request */
                if (!strncmp(data, "GET ", 4)) {
                    char uri[16];
                    const int max_uri_len = 16;
                    char *sp1, *sp2;
                    /* extract URI */
                    sp1 = data;
                    sp1 += 4;
                    sp2 = memchr(sp1, ' ', max_uri_len);
                    int len = sp2 - sp1;
                    memcpy(uri, sp1, len);
                    uri[len] = '\0';
                    printf("uri: %s\n", uri);
                    if (!strncmp(uri, "/on", max_uri_len))
                        GPIO_OUTPUT(15, false);
                    else if (!strncmp(uri, "/off", max_uri_len))
                        GPIO_OUTPUT(15, true);
                    snprintf(buf, sizeof(buf), webpage,
                            uri,
                            xTaskGetTickCount() * portTICK_RATE_MS / 1000,
                            (int) xPortGetFreeHeapSize());
                    netconn_write(client, buf, strlen(buf), NETCONN_COPY);
                }
            }
            netbuf_delete(nb);
        }
        printf("Closing connection\n");
        netconn_close(client);
        netconn_delete(client);
    }
}
/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void user_init(void)
{
    printf("SDK version:%s\n", system_get_sdk_version());

    set_on_station_connect(on_wifi_connect);
    set_on_station_disconnect(on_wifi_disconnect);
    init_esp_wifi();
    stop_wifi_ap();
    start_wifi_station(SSID, PASSWORD);
    xTaskCreate(&httpd_task, "http_server", 1024, NULL, 2, NULL);
}
