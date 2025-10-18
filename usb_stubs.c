// usb_stubs.c — implementación mínima de USB CDC ACM para ATSAMD21G18A
//
// Nota: este fichero implementa un stack USB CDC autocontenido usando
// accesos directos a registros. No depende de CMSIS ni de bibliotecas
// externas; se definen únicamente los registros y bits necesarios.

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Direcciones base de periféricos utilizados
#define PM_BASE             (0x40000400u)
#define GCLK_BASE           (0x40000C00u)
#define PORT_BASE           (0x41004400u)
#define USB_BASE            (0x41005000u)
#define NVMCTRL_OTP5        (0x00806024u)

// Reloj principal y periféricos
#define REG_PM_AHBMASK      (*(volatile uint32_t *)(PM_BASE + 0x14u))
#define REG_PM_APBBMASK     (*(volatile uint32_t *)(PM_BASE + 0x20u))
#define PM_AHBMASK_USB      (1u << 6)
#define PM_APBBMASK_USB     (1u << 5)

#define REG_GCLK_GENDIV     (*(volatile uint32_t *)(GCLK_BASE + 0x20u))
#define REG_GCLK_GENCTRL    (*(volatile uint32_t *)(GCLK_BASE + 0x24u))
#define REG_GCLK_CLKCTRL    (*(volatile uint16_t *)(GCLK_BASE + 0x02u))
#define REG_GCLK_STATUS     (*(volatile uint8_t  *)(GCLK_BASE + 0x01u))

#define GCLK_STATUS_SYNCBUSY        (1u << 7)
#define GCLK_GENCTRL_ID(x)          ((uint32_t)(x) << 0)
#define GCLK_GENCTRL_SRC_DFLL48M    (0x07u << 8)
#define GCLK_GENCTRL_GENEN          (1u << 16)
#define GCLK_GENCTRL_IDC            (1u << 17)
#define GCLK_GENCTRL_RUNSTDBY       (1u << 21)
#define GCLK_GENDIV_ID(x)           ((uint32_t)(x) << 0)
#define GCLK_GENDIV_DIV(x)          ((uint32_t)(x) << 8)
#define GCLK_CLKCTRL_ID(x)          ((uint16_t)(x) << 0)
#define GCLK_CLKCTRL_GEN(x)         ((uint16_t)(x) << 8)
#define GCLK_CLKCTRL_CLKEN          (1u << 14)

#define GCLK_GEN_USB                0u
#define GCLK_CLKCTRL_ID_USB         0x03u

// Configuración de pines PA24/PA25 para USB (función G)
#define REG_PORT_DIRCLR0            (*(volatile uint32_t *)(PORT_BASE + 0x04u))
#define REG_PORT_PMUX0              ((volatile uint8_t  *)(PORT_BASE + 0x30u))
#define REG_PORT_PINCFG0            ((volatile uint8_t  *)(PORT_BASE + 0x40u))
#define PORT_PMUX_PMUXE(x)          ((uint8_t)(x) << 0)
#define PORT_PMUX_PMUXO(x)          ((uint8_t)(x) << 4)
#define PORT_PINCFG_PMUXEN          (1u << 0)
#define PORT_PINCFG_INEN            (1u << 1)
#define PORT_FUNCTION_G             (6u)

// Definiciones mínimas de registros USB

typedef struct {
    volatile uint32_t ADDR;
    volatile uint32_t PCKSIZE;
    volatile uint16_t EXTREG;
    volatile uint16_t STATUS_BK;
} usb_desc_bank_t;

typedef struct {
    usb_desc_bank_t bank[2];
} usb_desc_ep_t;

typedef struct {
    volatile uint8_t  EPCFG;
    volatile uint8_t  _reserved1;
    volatile uint8_t  EPSTATUSCLR;
    volatile uint8_t  EPSTATUSSET;
    volatile uint8_t  EPSTATUS;
    volatile uint8_t  _reserved2;
    volatile uint8_t  EPINTFLAG;
    volatile uint8_t  EPINTENCLR;
    volatile uint8_t  EPINTENSET;
    volatile uint8_t  _reserved3[3];
} usb_endpoint_hw_t;

typedef struct {
    volatile uint8_t  CTRLA;
    volatile uint8_t  _padding1;
    volatile uint8_t  SYNCBUSY;
    volatile uint8_t  QOSCTRL;
    volatile uint16_t CTRLB;
    volatile uint8_t  DADD;
    volatile uint8_t  _padding2;
    volatile uint16_t STATUS;
    volatile uint16_t FSMSTATUS;
    volatile uint32_t FNUM;
    volatile uint32_t _reserved1[3];
    volatile uint16_t INTENCLR;
    volatile uint16_t INTENSET;
    volatile uint16_t INTFLAG;
    volatile uint16_t EPINTSMRY;
    volatile uint32_t _reserved2[9];
    volatile uint32_t DESCADD;
    volatile uint16_t PADCAL;
    volatile uint16_t _reserved3;
    usb_endpoint_hw_t DeviceEndpoint[8];
} usb_device_hw_t;

#define USB_DEVICE     ((usb_device_hw_t *)USB_BASE)

// Bits y constantes de registros USB
#define USB_CTRLA_SWRST             (1u << 0)
#define USB_CTRLA_ENABLE            (1u << 1)
#define USB_CTRLA_MODE_DEVICE       (1u << 2)
#define USB_CTRLA_RUNSTDBY          (1u << 6)

#define USB_CTRLB_DETACH            (1u << 0)
#define USB_CTRLB_SPDCONF_FS        (0u << 10)

#define USB_DADD_ADDEN              (1u << 7)

#define USB_INTFLAG_EORST           (1u << 5)

#define USB_DEVICE_EPCFG_EPTYPE_DISABLED   0u
#define USB_DEVICE_EPCFG_EPTYPE_CONTROL    1u
#define USB_DEVICE_EPCFG_EPTYPE_ISOCHRONOUS 2u
#define USB_DEVICE_EPCFG_EPTYPE_BULK       3u
#define USB_DEVICE_EPCFG_EPTYPE_INTERRUPT  4u
#define USB_DEVICE_EPCFG_EPTYPE0_Pos       0u
#define USB_DEVICE_EPCFG_EPTYPE1_Pos       4u

#define USB_DEVICE_EPINTFLAG_TRCPT0        (1u << 0)
#define USB_DEVICE_EPINTFLAG_TRCPT1        (1u << 1)
#define USB_DEVICE_EPINTFLAG_RXSTP         (1u << 4)

#define USB_DEVICE_EPSTATUS_DTGLIN         (1u << 1)
#define USB_DEVICE_EPSTATUS_DTGLINCLR      (1u << 1)
#define USB_DEVICE_EPSTATUS_BK0RDY         (1u << 6)
#define USB_DEVICE_EPSTATUS_BK1RDY         (1u << 7)
#define USB_DEVICE_EPSTATUS_STALLRQ0       (1u << 4)
#define USB_DEVICE_EPSTATUS_STALLRQ1       (1u << 5)
#define USB_DEVICE_EPSTATUSCLR_BK0RDY      USB_DEVICE_EPSTATUS_BK0RDY
#define USB_DEVICE_EPSTATUSCLR_BK1RDY      USB_DEVICE_EPSTATUS_BK1RDY

#define USB_DEVICE_STATUS_BK_BK_RDY        (1u << 6)

#define USB_PCKSIZE_BYTE_COUNT_Pos         0u
#define USB_PCKSIZE_BYTE_COUNT_Msk         (0x3FFFu << USB_PCKSIZE_BYTE_COUNT_Pos)
#define USB_PCKSIZE_MULTI_PACKET_SIZE_Pos  14u
#define USB_PCKSIZE_SIZE_Pos               28u
#define USB_PCKSIZE_SIZE_8                 (0u << USB_PCKSIZE_SIZE_Pos)
#define USB_PCKSIZE_SIZE_16                (1u << USB_PCKSIZE_SIZE_Pos)
#define USB_PCKSIZE_SIZE_32                (2u << USB_PCKSIZE_SIZE_Pos)
#define USB_PCKSIZE_SIZE_64                (3u << USB_PCKSIZE_SIZE_Pos)

// Parámetros de CDC
#define EP0_SIZE                   64u
#define CDC_NOTIFICATION_EP        3u
#define CDC_NOTIFICATION_SIZE      8u
#define CDC_OUT_EP                 1u
#define CDC_IN_EP                  2u
#define CDC_DATA_EP_SIZE           64u

#define CDC_RX_BUFFER_SIZE         512u
#define CDC_TX_BUFFER_SIZE         256u

#define CDC_RX_BUFFER_MASK         (CDC_RX_BUFFER_SIZE - 1u)
#define CDC_TX_BUFFER_MASK         (CDC_TX_BUFFER_SIZE - 1u)

// Descriptores USB estándar (packed)
#define USB_DESC_TYPE_DEVICE             0x01u
#define USB_DESC_TYPE_CONFIGURATION      0x02u
#define USB_DESC_TYPE_STRING             0x03u
#define USB_DESC_TYPE_INTERFACE          0x04u
#define USB_DESC_TYPE_ENDPOINT           0x05u
#define USB_DESC_TYPE_CS_INTERFACE       0x24u
#define USB_DESC_TYPE_INTERFACE_ASSOC    0x0Bu

#define CDC_FUNC_DESC_SUBTYPE_HEADER     0x00u
#define CDC_FUNC_DESC_SUBTYPE_CALL_MGMT  0x01u
#define CDC_FUNC_DESC_SUBTYPE_ACM        0x02u
#define CDC_FUNC_DESC_SUBTYPE_UNION      0x06u

#define CDC_CLASS_COMMUNICATION          0x02u
#define CDC_CLASS_DATA                   0x0Au
#define CDC_SUBCLASS_ACM                 0x02u
#define CDC_PROTOCOL_AT                  0x01u

#define USB_REQ_GET_STATUS               0x00u
#define USB_REQ_CLEAR_FEATURE            0x01u
#define USB_REQ_SET_FEATURE              0x03u
#define USB_REQ_SET_ADDRESS              0x05u
#define USB_REQ_GET_DESCRIPTOR           0x06u
#define USB_REQ_SET_DESCRIPTOR           0x07u
#define USB_REQ_GET_CONFIGURATION        0x08u
#define USB_REQ_SET_CONFIGURATION        0x09u
#define USB_REQ_GET_INTERFACE            0x0Au
#define USB_REQ_SET_INTERFACE            0x0Bu

#define CDC_REQ_SET_LINE_CODING          0x20u
#define CDC_REQ_GET_LINE_CODING          0x21u
#define CDC_REQ_SET_CONTROL_LINE_STATE   0x22u

#define REQTYPE_DIRECTION_DEVICE_TO_HOST 0x80u
#define REQTYPE_TYPE_MASK                0x60u
#define REQTYPE_TYPE_STANDARD            0x00u
#define REQTYPE_TYPE_CLASS               0x20u
#define REQTYPE_RECIPIENT_MASK           0x1Fu
#define REQTYPE_RECIPIENT_DEVICE         0x00u
#define REQTYPE_RECIPIENT_INTERFACE      0x01u

typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

// Descriptores (device + configuration + strings)
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} __attribute__((packed)) usb_configuration_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceCount;
    uint8_t bFunctionClass;
    uint8_t bFunctionSubClass;
    uint8_t bFunctionProtocol;
    uint8_t iFunction;
} __attribute__((packed)) usb_interface_assoc_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint16_t bcdCDC;
} __attribute__((packed)) usb_cdc_header_desc_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint8_t bmCapabilities;
    uint8_t bDataInterface;
} __attribute__((packed)) usb_cdc_call_mgmt_desc_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint8_t bmCapabilities;
} __attribute__((packed)) usb_cdc_acm_desc_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint8_t bMasterInterface;
    uint8_t bSlaveInterface0;
} __attribute__((packed)) usb_cdc_union_desc_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

typedef struct {
    usb_configuration_descriptor_t config;
    usb_interface_assoc_descriptor_t iad;
    usb_interface_descriptor_t comm_if;
    usb_cdc_header_desc_t cdc_header;
    usb_cdc_call_mgmt_desc_t call_mgmt;
    usb_cdc_acm_desc_t acm;
    usb_cdc_union_desc_t cdc_union;
    usb_endpoint_descriptor_t notification_ep;
    usb_interface_descriptor_t data_if;
    usb_endpoint_descriptor_t data_out_ep;
    usb_endpoint_descriptor_t data_in_ep;
} __attribute__((packed)) usb_cdc_config_descriptor_t;

static const usb_device_descriptor_t device_descriptor = {
    .bLength = sizeof(usb_device_descriptor_t),
    .bDescriptorType = USB_DESC_TYPE_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x02,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = EP0_SIZE,
    .idVendor = 0x2341u,
    .idProduct = 0x004Du,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1
};

static const usb_cdc_config_descriptor_t configuration_descriptor = {
    .config = {
        .bLength = sizeof(usb_configuration_descriptor_t),
        .bDescriptorType = USB_DESC_TYPE_CONFIGURATION,
        .wTotalLength = sizeof(usb_cdc_config_descriptor_t),
        .bNumInterfaces = 2,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = 0x80,
        .bMaxPower = 50,
    },
    .iad = {
        .bLength = sizeof(usb_interface_assoc_descriptor_t),
        .bDescriptorType = USB_DESC_TYPE_INTERFACE_ASSOC,
        .bInterfaceCount = 2,
        .bFunctionClass = CDC_CLASS_COMMUNICATION,
        .bFunctionSubClass = CDC_SUBCLASS_ACM,
        .bFunctionProtocol = CDC_PROTOCOL_AT,
        .iFunction = 0,
    },
    .comm_if = {
        .bLength = sizeof(usb_interface_descriptor_t),
        .bDescriptorType = USB_DESC_TYPE_INTERFACE,
        .bInterfaceNumber = 0,
        .bAlternateSetting = 0,
        .bNumEndpoints = 1,
        .bInterfaceClass = CDC_CLASS_COMMUNICATION,
        .bInterfaceSubClass = CDC_SUBCLASS_ACM,
        .bInterfaceProtocol = CDC_PROTOCOL_AT,
        .iInterface = 0,
    },
    .cdc_header = {
        .bLength = sizeof(usb_cdc_header_desc_t),
        .bDescriptorType = USB_DESC_TYPE_CS_INTERFACE,
        .bDescriptorSubtype = CDC_FUNC_DESC_SUBTYPE_HEADER,
        .bcdCDC = 0x0110,
    },
    .call_mgmt = {
        .bLength = sizeof(usb_cdc_call_mgmt_desc_t),
        .bDescriptorType = USB_DESC_TYPE_CS_INTERFACE,
        .bDescriptorSubtype = CDC_FUNC_DESC_SUBTYPE_CALL_MGMT,
        .bmCapabilities = 0x00,
        .bDataInterface = 1,
    },
    .acm = {
        .bLength = sizeof(usb_cdc_acm_desc_t),
        .bDescriptorType = USB_DESC_TYPE_CS_INTERFACE,
        .bDescriptorSubtype = CDC_FUNC_DESC_SUBTYPE_ACM,
        .bmCapabilities = 0x02,
    },
    .cdc_union = {
        .bLength = sizeof(usb_cdc_union_desc_t),
        .bDescriptorType = USB_DESC_TYPE_CS_INTERFACE,
        .bDescriptorSubtype = CDC_FUNC_DESC_SUBTYPE_UNION,
        .bMasterInterface = 0,
        .bSlaveInterface0 = 1,
    },
    .notification_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DESC_TYPE_ENDPOINT,
        .bEndpointAddress = 0x80 | CDC_NOTIFICATION_EP,
        .bmAttributes = 0x03,
        .wMaxPacketSize = CDC_NOTIFICATION_SIZE,
        .bInterval = 16,
    },
    .data_if = {
        .bLength = sizeof(usb_interface_descriptor_t),
        .bDescriptorType = USB_DESC_TYPE_INTERFACE,
        .bInterfaceNumber = 1,
        .bAlternateSetting = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = CDC_CLASS_DATA,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface = 0,
    },
    .data_out_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DESC_TYPE_ENDPOINT,
        .bEndpointAddress = CDC_OUT_EP,
        .bmAttributes = 0x02,
        .wMaxPacketSize = CDC_DATA_EP_SIZE,
        .bInterval = 0,
    },
    .data_in_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DESC_TYPE_ENDPOINT,
        .bEndpointAddress = 0x80 | CDC_IN_EP,
        .bmAttributes = 0x02,
        .wMaxPacketSize = CDC_DATA_EP_SIZE,
        .bInterval = 0,
    },
};

static const struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wString[1];
} __attribute__((packed)) string_lang = {
    .bLength = 4,
    .bDescriptorType = USB_DESC_TYPE_STRING,
    .wString = {0x0409}
};

static const struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wString[8];
} __attribute__((packed)) string_manufacturer = {
    .bLength = 2 + 2 * 8,
    .bDescriptorType = USB_DESC_TYPE_STRING,
    .wString = {'Z','e','r','o','B','o','o','t'}
};

static const struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wString[10];
} __attribute__((packed)) string_product = {
    .bLength = 2 + 2 * 10,
    .bDescriptorType = USB_DESC_TYPE_STRING,
    .wString = {'S','A','M','D','2','1',' ','C','D','C'}
};

static const struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wString[8];
} __attribute__((packed)) string_serial = {
    .bLength = 2 + 2 * 8,
    .bDescriptorType = USB_DESC_TYPE_STRING,
    .wString = {'0','0','0','0','0','0','0','1'}
};

static const uint8_t *const string_descriptors[] = {
    (const uint8_t *)&string_lang,
    (const uint8_t *)&string_manufacturer,
    (const uint8_t *)&string_product,
    (const uint8_t *)&string_serial,
};

// Descriptor table de endpoints
static usb_desc_ep_t usb_descriptor_table[8] __attribute__((aligned(4)));

// Buffers para endpoints
static uint8_t ep0_out_buffer[EP0_SIZE] __attribute__((aligned(4)));
static uint8_t ep0_in_buffer[EP0_SIZE] __attribute__((aligned(4)));
static uint8_t cdc_out_buffer[CDC_DATA_EP_SIZE] __attribute__((aligned(4)));
static uint8_t cdc_in_buffer[CDC_DATA_EP_SIZE] __attribute__((aligned(4)));
static uint8_t cdc_notification_buffer[CDC_NOTIFICATION_SIZE] __attribute__((aligned(4)));

// Buffers circulares para CDC
static uint8_t cdc_rx_buffer[CDC_RX_BUFFER_SIZE];
static uint16_t cdc_rx_head = 0;
static uint16_t cdc_rx_tail = 0;

static uint8_t cdc_tx_buffer[CDC_TX_BUFFER_SIZE];
static uint16_t cdc_tx_head = 0;
static uint16_t cdc_tx_tail = 0;
static bool cdc_tx_busy = false;

// Estado del control endpoint

typedef enum {
    CTRL_IDLE,
    CTRL_DATA_IN,
    CTRL_DATA_OUT,
    CTRL_STATUS_IN,
    CTRL_STATUS_OUT
} ctrl_phase_t;

static struct {
    usb_setup_packet_t setup;
    const uint8_t *in_ptr;
    uint16_t in_remaining;
    uint16_t in_total;
    uint8_t pending_address;
    ctrl_phase_t phase;
    bool configured;
    uint8_t configuration;
    uint8_t line_coding[7];
    uint16_t control_line_state;
} usb_control_state = {
    .pending_address = 0,
    .phase = CTRL_IDLE,
    .configured = false,
    .configuration = 0,
    .line_coding = {0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08}, // 115200 8N1
    .control_line_state = 0,
};

uint32_t
usb_cdc_get_baud(void)
{
    uint32_t baud = (uint32_t)usb_control_state.line_coding[0];
    baud |= (uint32_t)usb_control_state.line_coding[1] << 8;
    baud |= (uint32_t)usb_control_state.line_coding[2] << 16;
    baud |= (uint32_t)usb_control_state.line_coding[3] << 24;

    return baud;
}

uint16_t
usb_cdc_get_line_state(void)
{
    return usb_control_state.control_line_state;
}

static inline void usb_wait_syncbusy(void) {
    while (USB_DEVICE->SYNCBUSY) { }
}

static inline void gclk_wait_sync(void) {
    while (REG_GCLK_STATUS & GCLK_STATUS_SYNCBUSY) { }
}

static void usb_ep0_prime_out(uint16_t size) {
    (void)size;
    usb_descriptor_table[0].bank[0].ADDR = (uint32_t)ep0_out_buffer;
    usb_descriptor_table[0].bank[0].PCKSIZE = USB_PCKSIZE_SIZE_64;
    usb_descriptor_table[0].bank[0].STATUS_BK = USB_DEVICE_STATUS_BK_BK_RDY;
    USB_DEVICE->DeviceEndpoint[0].EPSTATUSCLR = USB_DEVICE_EPSTATUS_BK0RDY;
}

static void usb_ep0_send_packet(const uint8_t *data, uint16_t len) {
    if (len > EP0_SIZE) {
        len = EP0_SIZE;
    }
    if (data && len) {
        memcpy(ep0_in_buffer, data, len);
    }
    usb_descriptor_table[0].bank[1].ADDR = (uint32_t)ep0_in_buffer;
    usb_descriptor_table[0].bank[1].PCKSIZE = USB_PCKSIZE_SIZE_64 |
        ((uint32_t)len << USB_PCKSIZE_BYTE_COUNT_Pos);
    usb_descriptor_table[0].bank[1].STATUS_BK = USB_DEVICE_STATUS_BK_BK_RDY;
    USB_DEVICE->DeviceEndpoint[0].EPSTATUSCLR = USB_DEVICE_EPSTATUS_BK1RDY;
}

static void usb_ep0_send_zlp(void) {
    usb_ep0_send_packet(NULL, 0);
}

static uint16_t usb_fill_tx_packet(void) {
    if (usb_control_state.in_remaining == 0) {
        return 0;
    }
    uint16_t packet = usb_control_state.in_remaining;
    if (packet > EP0_SIZE) {
        packet = EP0_SIZE;
    }
    usb_ep0_send_packet(usb_control_state.in_ptr, packet);
    usb_control_state.in_ptr += packet;
    usb_control_state.in_remaining -= packet;
    usb_control_state.in_total += packet;
    usb_control_state.phase = CTRL_DATA_IN;
    return packet;
}

static void usb_configure_endpoints(void) {
    // Endpoint de notificación (IN interrupt)
    usb_descriptor_table[CDC_NOTIFICATION_EP].bank[1].ADDR = (uint32_t)cdc_notification_buffer;
    usb_descriptor_table[CDC_NOTIFICATION_EP].bank[1].PCKSIZE = USB_PCKSIZE_SIZE_8;
    usb_descriptor_table[CDC_NOTIFICATION_EP].bank[1].STATUS_BK = 0;
    USB_DEVICE->DeviceEndpoint[CDC_NOTIFICATION_EP].EPCFG =
        (USB_DEVICE_EPCFG_EPTYPE_DISABLED << USB_DEVICE_EPCFG_EPTYPE0_Pos) |
        (USB_DEVICE_EPCFG_EPTYPE_INTERRUPT << USB_DEVICE_EPCFG_EPTYPE1_Pos);
    USB_DEVICE->DeviceEndpoint[CDC_NOTIFICATION_EP].EPSTATUSCLR =
        USB_DEVICE_EPSTATUS_BK0RDY | USB_DEVICE_EPSTATUS_BK1RDY;
    USB_DEVICE->DeviceEndpoint[CDC_NOTIFICATION_EP].EPINTENSET =
        USB_DEVICE_EPINTFLAG_TRCPT1;

    // Endpoint OUT (bulk)
    usb_descriptor_table[CDC_OUT_EP].bank[0].ADDR = (uint32_t)cdc_out_buffer;
    usb_descriptor_table[CDC_OUT_EP].bank[0].PCKSIZE = USB_PCKSIZE_SIZE_64;
    usb_descriptor_table[CDC_OUT_EP].bank[0].STATUS_BK = USB_DEVICE_STATUS_BK_BK_RDY;
    USB_DEVICE->DeviceEndpoint[CDC_OUT_EP].EPCFG =
        (USB_DEVICE_EPCFG_EPTYPE_BULK << USB_DEVICE_EPCFG_EPTYPE0_Pos);
    USB_DEVICE->DeviceEndpoint[CDC_OUT_EP].EPSTATUSCLR =
        USB_DEVICE_EPSTATUS_BK0RDY | USB_DEVICE_EPSTATUS_BK1RDY;
    USB_DEVICE->DeviceEndpoint[CDC_OUT_EP].EPINTFLAG = 0xFF;
    USB_DEVICE->DeviceEndpoint[CDC_OUT_EP].EPINTENSET = USB_DEVICE_EPINTFLAG_TRCPT0;

    // Endpoint IN (bulk)
    usb_descriptor_table[CDC_IN_EP].bank[1].ADDR = (uint32_t)cdc_in_buffer;
    usb_descriptor_table[CDC_IN_EP].bank[1].PCKSIZE = USB_PCKSIZE_SIZE_64;
    usb_descriptor_table[CDC_IN_EP].bank[1].STATUS_BK = 0;
    USB_DEVICE->DeviceEndpoint[CDC_IN_EP].EPCFG =
        (USB_DEVICE_EPCFG_EPTYPE_BULK << USB_DEVICE_EPCFG_EPTYPE1_Pos);
    USB_DEVICE->DeviceEndpoint[CDC_IN_EP].EPSTATUSCLR =
        USB_DEVICE_EPSTATUS_BK0RDY | USB_DEVICE_EPSTATUS_BK1RDY;
    USB_DEVICE->DeviceEndpoint[CDC_IN_EP].EPINTFLAG = 0xFF;
    USB_DEVICE->DeviceEndpoint[CDC_IN_EP].EPINTENSET = USB_DEVICE_EPINTFLAG_TRCPT1;

    cdc_tx_busy = false;
}

static void usb_reset_device(void) {
    usb_control_state.configured = false;
    usb_control_state.configuration = 0;
    usb_control_state.pending_address = 0;
    usb_control_state.phase = CTRL_IDLE;
    cdc_rx_head = cdc_rx_tail = 0;
    cdc_tx_head = cdc_tx_tail = 0;
    cdc_tx_busy = false;

    memset(usb_descriptor_table, 0, sizeof(usb_descriptor_table));

    usb_ep0_prime_out(EP0_SIZE);
    usb_descriptor_table[0].bank[1].ADDR = (uint32_t)ep0_in_buffer;
    usb_descriptor_table[0].bank[1].PCKSIZE = USB_PCKSIZE_SIZE_64;
    usb_descriptor_table[0].bank[1].STATUS_BK = 0;

    USB_DEVICE->DeviceEndpoint[0].EPCFG =
        (USB_DEVICE_EPCFG_EPTYPE_CONTROL << USB_DEVICE_EPCFG_EPTYPE0_Pos) |
        (USB_DEVICE_EPCFG_EPTYPE_CONTROL << USB_DEVICE_EPCFG_EPTYPE1_Pos);
    USB_DEVICE->DeviceEndpoint[0].EPSTATUSCLR =
        USB_DEVICE_EPSTATUS_BK0RDY | USB_DEVICE_EPSTATUS_BK1RDY | USB_DEVICE_EPSTATUS_DTGLIN;
    USB_DEVICE->DeviceEndpoint[0].EPINTFLAG = 0xFF;
    USB_DEVICE->DeviceEndpoint[0].EPINTENSET =
        USB_DEVICE_EPINTFLAG_RXSTP | USB_DEVICE_EPINTFLAG_TRCPT0 | USB_DEVICE_EPINTFLAG_TRCPT1;
}

static uint16_t usb_ring_rx_count(void) {
    return (uint16_t)(cdc_rx_head - cdc_rx_tail);
}

static uint16_t usb_ring_tx_count(void) {
    return (uint16_t)(cdc_tx_head - cdc_tx_tail);
}

static uint16_t usb_ring_tx_space(void) {
    return (uint16_t)(CDC_TX_BUFFER_SIZE - usb_ring_tx_count());
}

static void usb_cdc_try_send(void) {
    if (!usb_control_state.configured || cdc_tx_busy) {
        return;
    }
    uint16_t available = usb_ring_tx_count();
    if (available == 0) {
        return;
    }
    uint16_t packet = available;
    if (packet > CDC_DATA_EP_SIZE) {
        packet = CDC_DATA_EP_SIZE;
    }
    for (uint16_t i = 0; i < packet; ++i) {
        cdc_in_buffer[i] = cdc_tx_buffer[(cdc_tx_tail + i) & CDC_TX_BUFFER_MASK];
    }
    cdc_tx_tail = (uint16_t)(cdc_tx_tail + packet) & CDC_TX_BUFFER_MASK;
    usb_descriptor_table[CDC_IN_EP].bank[1].PCKSIZE = USB_PCKSIZE_SIZE_64 |
        ((uint32_t)packet << USB_PCKSIZE_BYTE_COUNT_Pos);
    usb_descriptor_table[CDC_IN_EP].bank[1].STATUS_BK = USB_DEVICE_STATUS_BK_BK_RDY;
    USB_DEVICE->DeviceEndpoint[CDC_IN_EP].EPSTATUSCLR = USB_DEVICE_EPSTATUS_BK1RDY;
    cdc_tx_busy = true;
}

static void usb_handle_standard_request(const usb_setup_packet_t *setup);
static void usb_handle_class_request(const usb_setup_packet_t *setup);

static void usb_handle_setup(void) {
    memcpy(&usb_control_state.setup, ep0_out_buffer, sizeof(usb_setup_packet_t));
    const usb_setup_packet_t *setup = &usb_control_state.setup;

    USB_DEVICE->DeviceEndpoint[0].EPINTFLAG = USB_DEVICE_EPINTFLAG_RXSTP;
    usb_control_state.in_total = 0;
    usb_control_state.in_remaining = 0;
    usb_control_state.in_ptr = NULL;

    switch (setup->bmRequestType & REQTYPE_TYPE_MASK) {
        case REQTYPE_TYPE_STANDARD:
            usb_handle_standard_request(setup);
            break;
        case REQTYPE_TYPE_CLASS:
            usb_handle_class_request(setup);
            break;
        default:
            // Stall en caso no soportado
            USB_DEVICE->DeviceEndpoint[0].EPSTATUSSET = USB_DEVICE_EPSTATUS_STALLRQ0 | USB_DEVICE_EPSTATUS_STALLRQ1;
            usb_control_state.phase = CTRL_IDLE;
            break;
    }
}

static void usb_handle_standard_request(const usb_setup_packet_t *setup) {
    switch (setup->bRequest) {
        case USB_REQ_GET_DESCRIPTOR: {
            const uint8_t descriptor_type = (uint8_t)(setup->wValue >> 8);
            const uint8_t descriptor_index = (uint8_t)(setup->wValue & 0xFFu);
            const uint8_t *desc = NULL;
            uint16_t len = 0;
            if (descriptor_type == USB_DESC_TYPE_DEVICE) {
                desc = (const uint8_t *)&device_descriptor;
                len = sizeof(device_descriptor);
            } else if (descriptor_type == USB_DESC_TYPE_CONFIGURATION) {
                desc = (const uint8_t *)&configuration_descriptor;
                len = sizeof(configuration_descriptor);
            } else if (descriptor_type == USB_DESC_TYPE_STRING) {
                if (descriptor_index < (sizeof(string_descriptors) / sizeof(string_descriptors[0]))) {
                    const uint8_t *str = string_descriptors[descriptor_index];
                    len = str[0];
                    desc = str;
                }
            }
            if (!desc) {
                USB_DEVICE->DeviceEndpoint[0].EPSTATUSSET = USB_DEVICE_EPSTATUS_STALLRQ0 | USB_DEVICE_EPSTATUS_STALLRQ1;
                usb_control_state.phase = CTRL_IDLE;
                return;
            }
            if (setup->wLength < len) {
                len = setup->wLength;
            }
            usb_control_state.in_ptr = desc;
            usb_control_state.in_remaining = len;
            usb_control_state.in_total = 0;
            usb_fill_tx_packet();
            break;
        }
        case USB_REQ_SET_ADDRESS: {
            usb_control_state.pending_address = (uint8_t)(setup->wValue & 0x7Fu);
            usb_control_state.phase = CTRL_STATUS_IN;
            usb_ep0_send_zlp();
            break;
        }
        case USB_REQ_SET_CONFIGURATION: {
            usb_control_state.configuration = (uint8_t)(setup->wValue & 0xFFu);
            if (usb_control_state.configuration) {
                usb_control_state.configured = true;
                usb_configure_endpoints();
            } else {
                usb_control_state.configured = false;
            }
            usb_control_state.phase = CTRL_STATUS_IN;
            usb_ep0_send_zlp();
            break;
        }
        case USB_REQ_GET_CONFIGURATION: {
            usb_control_state.in_ptr = &usb_control_state.configuration;
            usb_control_state.in_remaining = 1;
            usb_control_state.in_total = 0;
            usb_fill_tx_packet();
            break;
        }
        case USB_REQ_GET_STATUS: {
            static uint16_t status_zero = 0;
            usb_control_state.in_ptr = (const uint8_t *)&status_zero;
            usb_control_state.in_remaining = 2;
            usb_control_state.in_total = 0;
            usb_fill_tx_packet();
            break;
        }
        case USB_REQ_GET_INTERFACE: {
            static uint8_t alt = 0;
            usb_control_state.in_ptr = &alt;
            usb_control_state.in_remaining = 1;
            usb_control_state.in_total = 0;
            usb_fill_tx_packet();
            break;
        }
        case USB_REQ_SET_INTERFACE: {
            usb_control_state.phase = CTRL_STATUS_IN;
            usb_ep0_send_zlp();
            break;
        }
        default:
            USB_DEVICE->DeviceEndpoint[0].EPSTATUSSET = USB_DEVICE_EPSTATUS_STALLRQ0 | USB_DEVICE_EPSTATUS_STALLRQ1;
            usb_control_state.phase = CTRL_IDLE;
            break;
    }
}

static void usb_handle_class_request(const usb_setup_packet_t *setup) {
    if ((setup->bmRequestType & REQTYPE_RECIPIENT_MASK) != REQTYPE_RECIPIENT_INTERFACE) {
        USB_DEVICE->DeviceEndpoint[0].EPSTATUSSET = USB_DEVICE_EPSTATUS_STALLRQ0 | USB_DEVICE_EPSTATUS_STALLRQ1;
        usb_control_state.phase = CTRL_IDLE;
        return;
    }
    switch (setup->bRequest) {
        case CDC_REQ_SET_LINE_CODING:
            usb_control_state.phase = CTRL_DATA_OUT;
            usb_ep0_prime_out(setup->wLength);
            break;
        case CDC_REQ_GET_LINE_CODING:
            usb_control_state.in_ptr = usb_control_state.line_coding;
            usb_control_state.in_remaining = sizeof(usb_control_state.line_coding);
            usb_control_state.in_total = 0;
            usb_fill_tx_packet();
            break;
        case CDC_REQ_SET_CONTROL_LINE_STATE:
            usb_control_state.control_line_state = setup->wValue;
            usb_control_state.phase = CTRL_STATUS_IN;
            usb_ep0_send_zlp();
            break;
        default:
            USB_DEVICE->DeviceEndpoint[0].EPSTATUSSET = USB_DEVICE_EPSTATUS_STALLRQ0 | USB_DEVICE_EPSTATUS_STALLRQ1;
            usb_control_state.phase = CTRL_IDLE;
            break;
    }
}

static void usb_handle_ep0_out_complete(void) {
    if (usb_control_state.phase == CTRL_DATA_OUT) {
        uint16_t count = (uint16_t)(usb_descriptor_table[0].bank[0].PCKSIZE & USB_PCKSIZE_BYTE_COUNT_Msk);
        if (count > sizeof(usb_control_state.line_coding)) {
            count = sizeof(usb_control_state.line_coding);
        }
        memcpy(usb_control_state.line_coding, ep0_out_buffer, count);
        usb_control_state.phase = CTRL_STATUS_IN;
        usb_ep0_send_zlp();
    } else {
        usb_control_state.phase = CTRL_IDLE;
        usb_ep0_prime_out(EP0_SIZE);
    }
}

static void usb_handle_ep0_in_complete(void) {
    if (usb_control_state.phase == CTRL_DATA_IN && usb_control_state.in_remaining > 0) {
        usb_fill_tx_packet();
    } else {
        if (usb_control_state.phase == CTRL_STATUS_IN && usb_control_state.pending_address) {
            USB_DEVICE->DADD = usb_control_state.pending_address | USB_DADD_ADDEN;
            usb_control_state.pending_address = 0;
        }
        usb_control_state.phase = CTRL_IDLE;
        usb_ep0_prime_out(EP0_SIZE);
    }
}

static void usb_handle_out_endpoint(void) {
    if (!usb_control_state.configured) {
        return;
    }
    uint8_t flags = USB_DEVICE->DeviceEndpoint[CDC_OUT_EP].EPINTFLAG;
    if (flags & USB_DEVICE_EPINTFLAG_TRCPT0) {
        USB_DEVICE->DeviceEndpoint[CDC_OUT_EP].EPINTFLAG = USB_DEVICE_EPINTFLAG_TRCPT0;
        uint16_t count = (uint16_t)(usb_descriptor_table[CDC_OUT_EP].bank[0].PCKSIZE & USB_PCKSIZE_BYTE_COUNT_Msk);
        for (uint16_t i = 0; i < count; ++i) {
            uint16_t next_head = (uint16_t)((cdc_rx_head + 1u) & CDC_RX_BUFFER_MASK);
            if (next_head != cdc_rx_tail) {
                cdc_rx_buffer[cdc_rx_head] = cdc_out_buffer[i];
                cdc_rx_head = next_head;
            } else {
                break; // overflow, descartamos
            }
        }
        usb_descriptor_table[CDC_OUT_EP].bank[0].PCKSIZE = USB_PCKSIZE_SIZE_64;
        usb_descriptor_table[CDC_OUT_EP].bank[0].STATUS_BK = USB_DEVICE_STATUS_BK_BK_RDY;
        USB_DEVICE->DeviceEndpoint[CDC_OUT_EP].EPSTATUSCLR = USB_DEVICE_EPSTATUS_BK0RDY;
    }
}

static void usb_handle_in_endpoint(void) {
    if (!usb_control_state.configured) {
        return;
    }
    uint8_t flags = USB_DEVICE->DeviceEndpoint[CDC_IN_EP].EPINTFLAG;
    if (flags & USB_DEVICE_EPINTFLAG_TRCPT1) {
        USB_DEVICE->DeviceEndpoint[CDC_IN_EP].EPINTFLAG = USB_DEVICE_EPINTFLAG_TRCPT1;
        cdc_tx_busy = false;
    }
    usb_cdc_try_send();
}

static void usb_handle_interrupts(void) {
    uint16_t intflag = USB_DEVICE->INTFLAG;
    if (intflag & USB_INTFLAG_EORST) {
        USB_DEVICE->INTFLAG = USB_INTFLAG_EORST;
        usb_reset_device();
        USB_DEVICE->DADD = 0;
    }

    uint8_t ep0_flags = USB_DEVICE->DeviceEndpoint[0].EPINTFLAG;
    if (ep0_flags & USB_DEVICE_EPINTFLAG_RXSTP) {
        usb_handle_setup();
    }
    if (ep0_flags & USB_DEVICE_EPINTFLAG_TRCPT0) {
        USB_DEVICE->DeviceEndpoint[0].EPINTFLAG = USB_DEVICE_EPINTFLAG_TRCPT0;
        usb_handle_ep0_out_complete();
    }
    if (ep0_flags & USB_DEVICE_EPINTFLAG_TRCPT1) {
        USB_DEVICE->DeviceEndpoint[0].EPINTFLAG = USB_DEVICE_EPINTFLAG_TRCPT1;
        usb_handle_ep0_in_complete();
    }

    usb_handle_out_endpoint();
    usb_handle_in_endpoint();
}

static void usb_configure_pins(void) {
    REG_PORT_DIRCLR0 = (1u << 24) | (1u << 25);
    uint8_t pmux = REG_PORT_PMUX0[12];
    pmux &= 0xF0u;
    pmux |= PORT_PMUX_PMUXE(PORT_FUNCTION_G);
    REG_PORT_PMUX0[12] = pmux;
    pmux = REG_PORT_PMUX0[12];
    pmux &= 0x0Fu;
    pmux |= PORT_PMUX_PMUXO(PORT_FUNCTION_G);
    REG_PORT_PMUX0[12] = pmux;
    REG_PORT_PINCFG0[24] = PORT_PINCFG_PMUXEN | PORT_PINCFG_INEN;
    REG_PORT_PINCFG0[25] = PORT_PINCFG_PMUXEN | PORT_PINCFG_INEN;
}

static void usb_load_padcal(void) {
    uint32_t padcal = *((volatile uint32_t *)NVMCTRL_OTP5);
    uint8_t transn = (uint8_t)((padcal >> 0) & 0x1Fu);
    uint8_t transp = (uint8_t)((padcal >> 5) & 0x1Fu);
    uint8_t trim = (uint8_t)((padcal >> 10) & 0x7u);
    if (transn == 0x1F) {
        transn = 5;
    }
    if (transp == 0x1F) {
        transp = 29;
    }
    if (trim == 0x7) {
        trim = 3;
    }
    USB_DEVICE->PADCAL = (uint16_t)(transn | ((uint16_t)transp << 5) | ((uint16_t)trim << 10));
}

void usb_init(void) {
    /* Ensure the USB peripheral clocks are enabled on the AHB and APBB buses. */
    REG_PM_AHBMASK |= PM_AHBMASK_USB;
    REG_PM_APBBMASK |= PM_APBBMASK_USB;

    /* Reconfigure the generic clock for the USB peripheral (GCLK ID 3). */
    gclk_wait_sync();
    REG_GCLK_CLKCTRL = GCLK_CLKCTRL_ID(GCLK_CLKCTRL_ID_USB);
    gclk_wait_sync();
    REG_GCLK_CLKCTRL = GCLK_CLKCTRL_ID(GCLK_CLKCTRL_ID_USB) |
                       GCLK_CLKCTRL_GEN(GCLK_GEN_USB);
    gclk_wait_sync();
    REG_GCLK_CLKCTRL = GCLK_CLKCTRL_ID(GCLK_CLKCTRL_ID_USB) |
                       GCLK_CLKCTRL_GEN(GCLK_GEN_USB) |
                       GCLK_CLKCTRL_CLKEN;
    gclk_wait_sync();

    usb_configure_pins();

    /* Perform a soft reset of the USB peripheral to guarantee a clean state. */
    USB_DEVICE->CTRLA = USB_CTRLA_SWRST;
    usb_wait_syncbusy();
    USB_DEVICE->CTRLA = 0;
    usb_wait_syncbusy();

    usb_load_padcal();

    USB_DEVICE->CTRLB = USB_CTRLB_DETACH | USB_CTRLB_SPDCONF_FS;
    USB_DEVICE->DESCADD = (uint32_t)usb_descriptor_table;
    USB_DEVICE->INTENSET = USB_INTFLAG_EORST;

    USB_DEVICE->CTRLA = USB_CTRLA_MODE_DEVICE | USB_CTRLA_RUNSTDBY | USB_CTRLA_ENABLE;
    usb_wait_syncbusy();

    usb_reset_device();
    USB_DEVICE->CTRLB &= ~USB_CTRLB_DETACH;
}

void usb_task(void) {
    usb_handle_interrupts();
}

int usb_cdc_getchar(void) {
    if (usb_ring_rx_count() == 0) {
        return -1;
    }
    uint8_t value = cdc_rx_buffer[cdc_rx_tail];
    cdc_rx_tail = (uint16_t)((cdc_rx_tail + 1u) & CDC_RX_BUFFER_MASK);
    return value;
}

void usb_cdc_write(const uint8_t *data, size_t len) {
    if (!data || len == 0) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        while (usb_ring_tx_space() == 0) {
            usb_task();
        }
        cdc_tx_buffer[cdc_tx_head] = data[i];
        cdc_tx_head = (uint16_t)((cdc_tx_head + 1u) & CDC_TX_BUFFER_MASK);
    }
    usb_cdc_try_send();
}
