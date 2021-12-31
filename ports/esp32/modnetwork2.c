#include "modnetwork2.h"
#include "esp_wifi.h"
#include "lwip/dns.h"
#include "py/mperrno.h"

STATIC const mp_rom_map_elem_t mp_module_network2_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_network2) },

    { MP_ROM_QSTR(MP_QSTR_WIFI_MODE_STA), MP_ROM_INT(WIFI_MODE_STA) },
    { MP_ROM_QSTR(MP_QSTR_WIFI_MODE_AP), MP_ROM_INT(WIFI_MODE_AP) },
    { MP_ROM_QSTR(MP_QSTR_WIFI_MODE_APSTA), MP_ROM_INT(WIFI_MODE_APSTA) },

    { MP_ROM_QSTR(MP_QSTR_AUTH_OPEN), MP_ROM_INT(WIFI_AUTH_OPEN) },
    { MP_ROM_QSTR(MP_QSTR_AUTH_WEP), MP_ROM_INT(WIFI_AUTH_WEP) },
    { MP_ROM_QSTR(MP_QSTR_AUTH_WPA_PSK), MP_ROM_INT(WIFI_AUTH_WPA_PSK) },
    { MP_ROM_QSTR(MP_QSTR_AUTH_WPA2_PSK), MP_ROM_INT(WIFI_AUTH_WPA2_PSK) },
    { MP_ROM_QSTR(MP_QSTR_AUTH_WPA_WPA2_PSK), MP_ROM_INT(WIFI_AUTH_WPA_WPA2_PSK) },
    { MP_ROM_QSTR(MP_QSTR_AUTH_WPA2_ENTERPRISE), MP_ROM_INT(WIFI_AUTH_WPA2_ENTERPRISE) },
    { MP_ROM_QSTR(MP_QSTR_AUTH_WPA3_PSK), MP_ROM_INT(WIFI_AUTH_WPA3_PSK) },
    { MP_ROM_QSTR(MP_QSTR_AUTH_WPA2_WPA3_PSK), MP_ROM_INT(WIFI_AUTH_WPA2_WPA3_PSK) },

    { MP_ROM_QSTR(MP_QSTR_PHY_LAN8720), MP_ROM_INT(PHY_LAN8720) },
    { MP_ROM_QSTR(MP_QSTR_PHY_IP101), MP_ROM_INT(PHY_IP101) },
    { MP_ROM_QSTR(MP_QSTR_PHY_RTL8201), MP_ROM_INT(PHY_RTL8201) },
    { MP_ROM_QSTR(MP_QSTR_PHY_DP83848), MP_ROM_INT(PHY_DP83848) },
    { MP_ROM_QSTR(MP_QSTR_PHY_KSZ8041), MP_ROM_INT(PHY_KSZ8041) },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_network2_globals, mp_module_network2_globals_table);

const mp_obj_module_t mp_module_network2 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_network2_globals,
};

MP_REGISTER_MODULE(MP_QSTR_network2, mp_module_network2, MODULE_NETWORK2_ENABLED);