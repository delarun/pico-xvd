// USB-дескрипторы: составное устройство CDC-NCM (сеть, XVC) + CDC-ACM (UART-мост)
#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

// Тестовый VID TinyUSB — только для личного использования
#define USB_VID 0xCAFE
#define USB_PID 0x4A59   // NCM + CDC-ACM

enum { STRID_LANGID = 0, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL, STRID_INTERFACE, STRID_MAC, STRID_UART };
enum { ITF_NUM_NET = 0, ITF_NUM_NET_DATA, ITF_NUM_CDC, ITF_NUM_CDC_DATA, ITF_NUM_TOTAL };

#define EPNUM_NET_NOTIF 0x81
#define EPNUM_NET_OUT   0x02
#define EPNUM_NET_IN    0x82
#define EPNUM_CDC_NOTIF 0x83
#define EPNUM_CDC_OUT   0x04
#define EPNUM_CDC_IN    0x84

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_NCM_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_NCM_DESCRIPTOR(ITF_NUM_NET, STRID_INTERFACE, STRID_MAC, EPNUM_NET_NOTIF, 64,
                           EPNUM_NET_OUT, EPNUM_NET_IN, CFG_TUD_NET_ENDPOINT_SIZE, CFG_TUD_NET_MTU),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_UART, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

static const char *const string_desc[] = {
    [STRID_MANUFACTURER] = "Antminer-mister",
    [STRID_PRODUCT]      = "Pico XVC JTAG",
    [STRID_INTERFACE]    = "Pico XVC Network",
    [STRID_UART]         = "Pico XVC UART",
};

static uint16_t desc_str[32 + 1];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    char buf[32];
    const char *str = NULL;
    unsigned len;

    switch (index) {
    case STRID_LANGID:
        desc_str[1] = 0x0409;
        len = 1;
        goto done;
    case STRID_SERIAL:
        pico_get_unique_board_id_string(buf, sizeof(buf));
        str = buf;
        break;
    case STRID_MAC:
        for (unsigned i = 0; i < 6; i++) {
            static const char hex[] = "0123456789ABCDEF";
            buf[2 * i]     = hex[tud_network_mac_address[i] >> 4];
            buf[2 * i + 1] = hex[tud_network_mac_address[i] & 0xF];
        }
        buf[12] = 0;
        str = buf;
        break;
    default:
        if (index >= TU_ARRAY_SIZE(string_desc) || !string_desc[index]) return NULL;
        str = string_desc[index];
        break;
    }

    len = strlen(str);
    if (len > 32) len = 32;
    for (unsigned i = 0; i < len; i++) desc_str[1 + i] = (uint8_t)str[i];

done:
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc_str;
}
