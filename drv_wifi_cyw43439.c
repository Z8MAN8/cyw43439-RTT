/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date             Author           Notes
 * 2023-11-14       ChuShicheng      first version
 */
#include <rtdevice.h>
#include <rtthread.h>
#include "board.h"
#include "cyw43_arch.h"

#ifdef PKG_USING_WLAN_CYW43439

#define DBG_LEVEL   DBG_LOG
#include <rtdbg.h>
#define LOG_TAG "DRV.CYW43439"

struct ifx_wifi
{
    /* inherit from ethernet device */
    struct rt_wlan_device *wlan;
};

static struct ifx_wifi wifi_sta, wifi_ap;

rt_inline struct ifx_wifi *_GET_DEV(struct rt_wlan_device *wlan)
{
    if (wlan == wifi_sta.wlan)
    {
        return &wifi_sta;
    }
    if (wlan == wifi_ap.wlan)
    {
        return &wifi_ap;
    }
    return RT_NULL;
}

static uint32_t get_security(rt_wlan_security_t security)
{
    /* security type */
    switch (security)
    {
    case SECURITY_OPEN:
        return CYW43_AUTH_OPEN;
    case SECURITY_WPA_TKIP_PSK:
        return CYW43_AUTH_WPA_TKIP_PSK;
    case SECURITY_WPA2_AES_PSK:
        return CYW43_AUTH_WPA2_AES_PSK;
    case SECURITY_WPA2_MIXED_PSK:
        return CYW43_AUTH_WPA2_MIXED_PSK;
    default:
        return CYW43_AUTH_OPEN;
    }
}

static int _ifx_scan_info2rtt(const cyw43_ev_scan_result_t *result_ptr, struct rt_wlan_info *wlan_info)
{
    /* security type */
    switch (result_ptr->auth_mode)
    {
    case CYW43_AUTH_OPEN:
        wlan_info->security = SECURITY_OPEN;
        break;
    case CYW43_AUTH_WPA_TKIP_PSK:
        wlan_info->security = SECURITY_WPA_TKIP_PSK;
        break;
    case CYW43_AUTH_WPA2_AES_PSK:
        wlan_info->security = SECURITY_WPA2_AES_PSK;
        break;
    case CYW43_AUTH_WPA2_MIXED_PSK:
        wlan_info->security = SECURITY_WPA2_MIXED_PSK;
        break;
    default:
        wlan_info->security = SECURITY_UNKNOWN;
        break;
    }
    /* radio channel */
    wlan_info->channel = result_ptr->channel;
    /* signal strength */
    wlan_info->rssi = -result_ptr->rssi;
    /* ssid */
    rt_strncpy(wlan_info->ssid.val, result_ptr->ssid, RT_WLAN_SSID_MAX_LENGTH);
    wlan_info->ssid.len = result_ptr->ssid_len > RT_WLAN_SSID_MAX_LENGTH ? RT_WLAN_SSID_MAX_LENGTH : result_ptr->ssid_len;
    /* hwaddr */
    rt_strncpy(wlan_info->bssid, result_ptr->bssid, RT_WLAN_BSSID_MAX_LENGTH);
    wlan_info->hidden = RT_TRUE;
    return 0;
}

#define SCAN_BSSI_ARR_MAX 30

#define CMP_MAC( a, b )  (((((unsigned char*)a)[0])==(((unsigned char*)b)[0]))&& \
                          ((((unsigned char*)a)[1])==(((unsigned char*)b)[1]))&& \
                          ((((unsigned char*)a)[2])==(((unsigned char*)b)[2]))&& \
                          ((((unsigned char*)a)[3])==(((unsigned char*)b)[3]))&& \
                          ((((unsigned char*)a)[4])==(((unsigned char*)b)[4]))&& \
                          ((((unsigned char*)a)[5])==(((unsigned char*)b)[5])))

typedef struct
{
    uint8_t octet[6]; /**< Unique 6-byte MAC address */
}_mac_t;
static _mac_t mac_addr_arr[SCAN_BSSI_ARR_MAX];
rt_uint8_t current_bssid_arr_length = 0;

bool scan_bssi_has(const uint8_t *BSSID)
{
    _mac_t *mac_iter = NULL;
    /* Check for duplicate SSID */
    for (mac_iter = mac_addr_arr; (mac_iter < mac_addr_arr + current_bssid_arr_length); ++mac_iter)
    {
        if (CMP_MAC(mac_iter->octet, BSSID))
        {
            /* The scanned result is a duplicate; just return */
            return true;
        }
    }
    /* If scanned Wi-Fi is not a duplicate then populate the array */
    if (current_bssid_arr_length < SCAN_BSSI_ARR_MAX)
    {
        memcpy(mac_iter->octet, BSSID, sizeof(_mac_t));
        current_bssid_arr_length++;
    }
    return false;
}

int scan_callback(void *env, const cyw43_ev_scan_result_t *result)
{
    if (result->ssid_len != 0)
    {
        /* parse scan report event data */
        struct rt_wlan_buff buff;
        struct rt_wlan_info wlan_info;
        if (scan_bssi_has(result->bssid) == false)
        {
            _ifx_scan_info2rtt(result, &wlan_info);

            buff.data = &wlan_info;
            buff.len = sizeof(struct rt_wlan_info);

            /* indicate scan report event */
            rt_wlan_dev_indicate_event_handle(wifi_sta.wlan, RT_WLAN_DEV_EVT_SCAN_REPORT, &buff);
        }
    }
    return RT_EOK;
}

static rt_err_t wlan_init(struct rt_wlan_device *wlan)
{
    rt_int8_t res = cyw43_arch_init();;

    if (res == 0)
    {
        return RT_EOK;
    }
    LOG_E("cyw43_arch_init failed...! error code: %d\n", res);
    return -RT_ERROR;
}

static rt_err_t wlan_scan(struct rt_wlan_device *wlan, struct rt_scan_info *scan_info)
{
    memset(mac_addr_arr, 0, sizeof(_mac_t) * SCAN_BSSI_ARR_MAX);
    cyw43_wifi_scan_options_t scan_options = {0};
    int err = cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, scan_callback);

    if (err == 0)
    {
        rt_wlan_dev_indicate_event_handle(wifi_sta.wlan, RT_WLAN_DEV_EVT_SCAN_DONE, 0);
        return RT_EOK;
    }
    return -RT_ERROR;
}

static rt_err_t wlan_join(struct rt_wlan_device *wlan, struct rt_sta_info *sta_info)
{
    uint32_t res;
    /** Join to Wi-Fi AP **/
    res = cyw43_wifi_join(&cyw43_state, sta_info->ssid.len, sta_info->ssid.val, sta_info->key.len, sta_info->key.val, CYW43_AUTH_WPA2_AES_PSK, RT_NULL, RT_NULL);

    if (res == 0)
    {
        return RT_EOK;
    }
    return -RT_ERROR;
}

rt_err_t wlan_mode(struct rt_wlan_device *wlan, rt_wlan_mode_t mode)
{
    switch (mode)
    {
    case RT_WLAN_STATION:
        LOG_D("wlan_mode RT_WLAN_STATION\n");
        cyw43_arch_enable_sta_mode();
        break;
    case RT_WLAN_AP:
        LOG_D("wlan_mode RT_WLAN_AP\n");
        break;
    }

    return 0;
}

rt_err_t wlan_softap(struct rt_wlan_device *wlan, struct rt_ap_info *ap_info)
{
    LOG_D("wlan_softap");
    cyw43_arch_enable_ap_mode(ap_info->ssid.val, ap_info->key.val, get_security(ap_info->security));
    LOG_D("ap start ok");
    rt_wlan_dev_indicate_event_handle(wifi_ap.wlan, RT_WLAN_DEV_EVT_AP_START, 0);

    return RT_EOK;
}

rt_err_t wlan_disconnect(struct rt_wlan_device *wlan)
{
    LOG_D("wlan_disconnect");
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    return RT_EOK;
}

rt_err_t wlan_ap_stop(struct rt_wlan_device *wlan)
{
    LOG_D("wlan_ap_stop");
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_AP);
    return RT_EOK;
}

int wlan_get_rssi(struct rt_wlan_device *wlan)
{
    int32_t rssi;
    cyw43_wifi_get_rssi(&cyw43_state, &rssi);
    return rssi;
}
rt_err_t wlan_set_channel(struct rt_wlan_device *wlan, int channel)
{
    LOG_D("wlan_set_channel");
    cyw43_wifi_ap_set_channel(&cyw43_state, channel);
    return 0;
}
int wlan_get_channel(struct rt_wlan_device *wlan)
{
    LOG_D("wlan_get_channel");
    return cyw43_state.ap_channel;
}
rt_err_t wlan_get_mac(struct rt_wlan_device *wlan, rt_uint8_t mac[])
{
    int res = cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);
    if (res == 0)
    {
        LOG_D("WLAN MAC Address : %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5]);
        return RT_EOK;
    }
    return -RT_ERROR;
}
rt_err_t wlan_set_country(struct rt_wlan_device *wlan, rt_country_code_t country_code){
    rt_int8_t res = cyw43_arch_init_with_country(country_code);
    if (res == 0)
    {
        return RT_EOK;
    }
    return -RT_ERROR;
}
rt_country_code_t wlan_get_country(struct rt_wlan_device *wlan){
    return cyw43_arch_get_country_code();
}

static int wlan_send(struct rt_wlan_device *wlan, void *buff, int len)
{
    if(wlan == RT_NULL)
    {
        LOG_E("wlan is null!!!");
        return -RT_ERROR;
    }

    if (wlan == wifi_sta.wlan)
    {
        cyw43_send_ethernet(&cyw43_state, CYW43_ITF_STA, len, buff, false);
    }
    else
    {
        cyw43_send_ethernet(&cyw43_state, CYW43_ITF_AP, len, buff, false);
    }
    return len;
}

const static struct rt_wlan_dev_ops ops =
{
    .wlan_init          = wlan_init,
    .wlan_mode          = wlan_mode,
    .wlan_scan          = wlan_scan,
    .wlan_join          = wlan_join,
    .wlan_softap        = wlan_softap,
    .wlan_disconnect    = wlan_disconnect,
    .wlan_ap_stop       = wlan_ap_stop,
    .wlan_get_rssi      = wlan_get_rssi,
    .wlan_set_channel   = wlan_set_channel,
    .wlan_get_channel   = wlan_get_channel,
    .wlan_set_country   = wlan_set_country,
    .wlan_get_country   = wlan_get_country,
    .wlan_get_mac       = wlan_get_mac,
    .wlan_send          = wlan_send,
};

int rt_hw_wifi_init(void)
{
    static struct rt_wlan_device wlan_sta, wlan_ap;
    rt_err_t ret;
    wifi_sta.wlan = &wlan_sta;
    wifi_ap.wlan = &wlan_ap;

    /* register wlan device for ap */
    ret = rt_wlan_dev_register(&wlan_ap, RT_WLAN_DEVICE_AP_NAME, &ops, 0, &wifi_ap);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* register wlan device for sta */
    ret = rt_wlan_dev_register(&wlan_sta, RT_WLAN_DEVICE_STA_NAME, &ops, 0, &wifi_sta);
    if (ret != RT_EOK)
    {
        return ret;
    }

}
INIT_DEVICE_EXPORT(rt_hw_wifi_init);

#endif /* PKG_USING_WLAN_CYW43439 */
