/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/usb_device.h"
#include "lufa/AudioClassCommon.h"

#include "btstack/btstack_avdtp_source.h"


#include "pico/flash.h"



// todo forget why this is using core 1 for sound: presumably not necessary
// todo noop when muted

CU_REGISTER_DEBUG_PINS(audio_timing)

// ---- select at most one ---
//CU_SELECT_DEBUG_PINS(audio_timing)

// todo make descriptor strings should probably belong to the configs
static char *descriptor_strings[] =
        {
                "Raspberry Pi",
                "Pico W USB Bluetooth Audio",
                "0123456789AB"
        };

// todo fix these
#define VENDOR_ID   0x2e8au
#define PRODUCT_ID  0xfeddu

#define AUDIO_OUT_ENDPOINT  0x01U
#define AUDIO_IN_ENDPOINT   0x82U

#undef AUDIO_SAMPLE_FREQ
#define AUDIO_SAMPLE_FREQ(frq) (uint8_t)(frq), (uint8_t)((frq >> 8)), (uint8_t)((frq >> 16))

#define AUDIO_MAX_PACKET_SIZE(freq) (uint8_t)(((freq + 999) / 1000) * 4)
#define FEATURE_MUTE_CONTROL 1u
#define FEATURE_VOLUME_CONTROL 2u

#define ENDPOINT_FREQ_CONTROL 1u

struct audio_device_config {
    struct usb_configuration_descriptor descriptor;
    struct usb_interface_descriptor ac_interface;
    struct __packed {
        USB_Audio_StdDescriptor_Interface_AC_t core;
        USB_Audio_StdDescriptor_InputTerminal_t input_terminal;
        USB_Audio_StdDescriptor_FeatureUnit_t feature_unit;
        USB_Audio_StdDescriptor_OutputTerminal_t output_terminal;
    } ac_audio;
    struct usb_interface_descriptor as_zero_interface;
    struct usb_interface_descriptor as_op_interface;
    struct __packed {
        USB_Audio_StdDescriptor_Interface_AS_t streaming;
        struct __packed {
            USB_Audio_StdDescriptor_Format_t core;
            USB_Audio_SampleFreq_t freqs[2];
        } format;
    } as_audio;
    struct __packed {
        struct usb_endpoint_descriptor_long core;
        USB_Audio_StdDescriptor_StreamEndpoint_Spc_t audio;
    } ep1;
    struct usb_endpoint_descriptor_long ep2;
};

static const struct audio_device_config audio_device_config = {
        .descriptor = {
                .bLength             = sizeof(audio_device_config.descriptor),
                .bDescriptorType     = DTYPE_Configuration,
                .wTotalLength        = sizeof(audio_device_config),
                .bNumInterfaces      = 2,
                .bConfigurationValue = 0x01,
                .iConfiguration      = 0x00,
                .bmAttributes        = 0x80,
                .bMaxPower           = 0x32,
        },
        .ac_interface = {
                .bLength            = sizeof(audio_device_config.ac_interface),
                .bDescriptorType    = DTYPE_Interface,
                .bInterfaceNumber   = 0x00,
                .bAlternateSetting  = 0x00,
                .bNumEndpoints      = 0x00,
                .bInterfaceClass    = AUDIO_CSCP_AudioClass,
                .bInterfaceSubClass = AUDIO_CSCP_ControlSubclass,
                .bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
                .iInterface         = 0x00,
        },
        .ac_audio = {
                .core = {
                        .bLength = sizeof(audio_device_config.ac_audio.core),
                        .bDescriptorType = AUDIO_DTYPE_CSInterface,
                        .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_Header,
                        .bcdADC = VERSION_BCD(1, 0, 0),
                        .wTotalLength = sizeof(audio_device_config.ac_audio),
                        .bInCollection = 1,
                        .bInterfaceNumbers = 1,
                },
                .input_terminal = {
                        .bLength = sizeof(audio_device_config.ac_audio.input_terminal),
                        .bDescriptorType = AUDIO_DTYPE_CSInterface,
                        .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_InputTerminal,
                        .bTerminalID = 1,
                        .wTerminalType = AUDIO_TERMINAL_STREAMING,
                        .bAssocTerminal = 0,
                        .bNrChannels = 2,
                        .wChannelConfig = AUDIO_CHANNEL_LEFT_FRONT | AUDIO_CHANNEL_RIGHT_FRONT,
                        .iChannelNames = 0,
                        .iTerminal = 0,
                },
                .feature_unit = {
                        .bLength = sizeof(audio_device_config.ac_audio.feature_unit),
                        .bDescriptorType = AUDIO_DTYPE_CSInterface,
                        .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_Feature,
                        .bUnitID = 2,
                        .bSourceID = 1,
                        .bControlSize = 1,
                        .bmaControls = {AUDIO_FEATURE_MUTE | AUDIO_FEATURE_VOLUME, 0, 0},
                        .iFeature = 0,
                },
                .output_terminal = {
                        .bLength = sizeof(audio_device_config.ac_audio.output_terminal),
                        .bDescriptorType = AUDIO_DTYPE_CSInterface,
                        .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_OutputTerminal,
                        .bTerminalID = 3,
                        .wTerminalType = AUDIO_TERMINAL_OUT_SPEAKER,
                        .bAssocTerminal = 0,
                        .bSourceID = 2,
                        .iTerminal = 0,
                },
        },
        .as_zero_interface = {
                .bLength            = sizeof(audio_device_config.as_zero_interface),
                .bDescriptorType    = DTYPE_Interface,
                .bInterfaceNumber   = 0x01,
                .bAlternateSetting  = 0x00,
                .bNumEndpoints      = 0x00,
                .bInterfaceClass    = AUDIO_CSCP_AudioClass,
                .bInterfaceSubClass = AUDIO_CSCP_AudioStreamingSubclass,
                .bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
                .iInterface         = 0x00,
        },
        .as_op_interface = {
                .bLength            = sizeof(audio_device_config.as_op_interface),
                .bDescriptorType    = DTYPE_Interface,
                .bInterfaceNumber   = 0x01,
                .bAlternateSetting  = 0x01,
                .bNumEndpoints      = 0x02,
                .bInterfaceClass    = AUDIO_CSCP_AudioClass,
                .bInterfaceSubClass = AUDIO_CSCP_AudioStreamingSubclass,
                .bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
                .iInterface         = 0x00,
        },
        .as_audio = {
                .streaming = {
                        .bLength = sizeof(audio_device_config.as_audio.streaming),
                        .bDescriptorType = AUDIO_DTYPE_CSInterface,
                        .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_General,
                        .bTerminalLink = 1,
                        .bDelay = 1,
                        .wFormatTag = 1, // PCM
                },
                .format = {
                        .core = {
                                .bLength = sizeof(audio_device_config.as_audio.format),
                                .bDescriptorType = AUDIO_DTYPE_CSInterface,
                                .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_FormatType,
                                .bFormatType = 1,
                                .bNrChannels = 2,
                                .bSubFrameSize = 2,
                                .bBitResolution = 16,
                                .bSampleFrequencyType = count_of(audio_device_config.as_audio.format.freqs),
                        },
                        .freqs = {
                                AUDIO_SAMPLE_FREQ(44100),
                               // AUDIO_SAMPLE_FREQ(48000)
                        },
                },
        },
        .ep1 = {
                .core = {
                        .bLength          = sizeof(audio_device_config.ep1.core),
                        .bDescriptorType  = DTYPE_Endpoint,
                        .bEndpointAddress = AUDIO_OUT_ENDPOINT,
                        .bmAttributes     = 5,
                        .wMaxPacketSize   = AUDIO_MAX_PACKET_SIZE(AUDIO_FREQ_MAX),
                        .bInterval        = 1,
                        .bRefresh         = 0,
                        .bSyncAddr        = AUDIO_IN_ENDPOINT,
                },
                .audio = {
                        .bLength = sizeof(audio_device_config.ep1.audio),
                        .bDescriptorType = AUDIO_DTYPE_CSEndpoint,
                        .bDescriptorSubtype = AUDIO_DSUBTYPE_CSEndpoint_General,
                        .bmAttributes = 1,
                        .bLockDelayUnits = 0,
                        .wLockDelay = 0,
                }
        },
        .ep2 = {
                .bLength          = sizeof(audio_device_config.ep2),
                .bDescriptorType  = 0x05,
                .bEndpointAddress = AUDIO_IN_ENDPOINT,
                .bmAttributes     = 0x11,
                .wMaxPacketSize   = 3,
                .bInterval        = 0x01,
                .bRefresh         = 2,
                .bSyncAddr        = 0,
        },
};

static struct usb_interface ac_interface;
static struct usb_interface as_op_interface;
static struct usb_endpoint ep_op_out, ep_op_sync;

static const struct usb_device_descriptor boot_device_descriptor = {
        .bLength            = 18,
        .bDescriptorType    = 0x01,
        .bcdUSB             = 0x0110,
        .bDeviceClass       = 0x00,
        .bDeviceSubClass    = 0x00,
        .bDeviceProtocol    = 0x00,
        .bMaxPacketSize0    = 0x40,
        .idVendor           = VENDOR_ID,
        .idProduct          = PRODUCT_ID,
        .bcdDevice          = 0x0200,
        .iManufacturer      = 0x01,
        .iProduct           = 0x02,
        .iSerialNumber      = 0x03,
        .bNumConfigurations = 0x01,
};

const char *_get_descriptor_string(uint index) {
    if (index <= count_of(descriptor_strings)) {
        return descriptor_strings[index - 1];
    } else {
        return "";
    }
}

static struct {
    uint32_t freq;
    int16_t volume;
    int16_t vol_mul;
    bool mute;
} audio_state = {
        .freq = 44100,
};


uint16_t buffer_counter = 0;
uint16_t audio_buffer_pool[AUDIO_BUF_POOL_LEN] = {0};


void _as_audio_packet(struct usb_endpoint *ep) {
    assert(ep->current_transfer);
    struct usb_buffer *usb_buffer = usb_current_out_packet_buffer(ep);

    assert(!(usb_buffer->data_len & 3u));

    uint16_t vol_mul = audio_state.vol_mul;
    int16_t *out = malloc(usb_buffer->data_len);
    int16_t *in = (int16_t *) usb_buffer->data;

    uint8_t sample_count = usb_buffer->data_len / 4;

    for (int i = 0; i < sample_count * 2; i++) {
        out[i] = (int16_t) ((in[i] * vol_mul) >> 15u);
    }
    //printf("usb 1ms ~~~~~~~~~~\n");

    // New resync func
    // Check if usb_audio_buf_counter is in the range of shared_audio_counter and shared_audio_counter + num_audio_samples_per_sbc_buffer * 2
    // 128 is a not good value; need get from btstack_sbc_encoder_sbc_buffer_length()
    if (get_bt_buf_counter() < buffer_counter && buffer_counter < (get_bt_buf_counter() + 128 * 2)){
            // If so, wait until more data is written
        buffer_counter += sample_count * 2;
    }

    for (int i = 0; i < sample_count * 2; i++) {

        if (buffer_counter < AUDIO_BUF_POOL_LEN){
        }
        else{
            buffer_counter = 0;
        }

        audio_buffer_pool[buffer_counter] = out[i];
        buffer_counter++; 
        
    }
    set_usb_buf_counter(buffer_counter);
    free(out);
    usb_grow_transfer(ep->current_transfer, 1);
    usb_packet_done(ep);
}

static void _as_sync_packet(struct usb_endpoint *ep) {
    assert(ep->current_transfer);
    DEBUG_PINS_SET(audio_timing, 2);
    DEBUG_PINS_CLR(audio_timing, 2);
    struct usb_buffer *buffer = usb_current_in_packet_buffer(ep);
    assert(buffer->data_max >= 3);
    buffer->data_len = 3;

    // todo lie thru our teeth for now
    uint feedback = (audio_state.freq << 14u) / 1000u;

    buffer->data[0] = feedback;
    buffer->data[1] = feedback >> 8u;
    buffer->data[2] = feedback >> 16u;

    // keep on truckin'
    usb_grow_transfer(ep->current_transfer, 1);
    usb_packet_done(ep);
}

static const struct usb_transfer_type as_transfer_type = {
        .on_packet = _as_audio_packet,
        .initial_packet_count = 1,
};

static const struct usb_transfer_type as_sync_transfer_type = {
        .on_packet = _as_sync_packet,
        .initial_packet_count = 1,
};

static struct usb_transfer as_transfer;
static struct usb_transfer as_sync_transfer;

static bool do_get_current(struct usb_setup_packet *setup) {
    usb_debug("AUDIO_REQ_GET_CUR\n");

    if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_INTERFACE) {
        switch (setup->wValue >> 8u) {
            case FEATURE_MUTE_CONTROL: {
                usb_start_tiny_control_in_transfer(audio_state.mute, 1);
                return true;
            }
            case FEATURE_VOLUME_CONTROL: {
                /* Current volume. See UAC Spec 1.0 p.77 */
                usb_start_tiny_control_in_transfer(audio_state.volume, 2);
                return true;
            }
        }
    } else if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_ENDPOINT) {
        if ((setup->wValue >> 8u) == ENDPOINT_FREQ_CONTROL) {
            /* Current frequency */
            usb_start_tiny_control_in_transfer(audio_state.freq, 3);
            return true;
        }
    }
    return false;
}

// todo this seemed like aood guess, but is not correct
uint16_t db_to_vol[91] = {
        0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0002, 0x0002,
        0x0002, 0x0002, 0x0003, 0x0003, 0x0004, 0x0004, 0x0005, 0x0005,
        0x0006, 0x0007, 0x0008, 0x0009, 0x000a, 0x000b, 0x000d, 0x000e,
        0x0010, 0x0012, 0x0014, 0x0017, 0x001a, 0x001d, 0x0020, 0x0024,
        0x0029, 0x002e, 0x0033, 0x003a, 0x0041, 0x0049, 0x0052, 0x005c,
        0x0067, 0x0074, 0x0082, 0x0092, 0x00a4, 0x00b8, 0x00ce, 0x00e7,
        0x0104, 0x0124, 0x0147, 0x016f, 0x019c, 0x01ce, 0x0207, 0x0246,
        0x028d, 0x02dd, 0x0337, 0x039b, 0x040c, 0x048a, 0x0518, 0x05b7,
        0x066a, 0x0732, 0x0813, 0x090f, 0x0a2a, 0x0b68, 0x0ccc, 0x0e5c,
        0x101d, 0x1214, 0x1449, 0x16c3, 0x198a, 0x1ca7, 0x2026, 0x2413,
        0x287a, 0x2d6a, 0x32f5, 0x392c, 0x4026, 0x47fa, 0x50c3, 0x5a9d,
        0x65ac, 0x7214, 0x7fff
};

// actually windows doesn't seem to like this in the middle, so set top range to 0db
#define CENTER_VOLUME_INDEX 91

#define ENCODE_DB(x) ((uint16_t)(int16_t)((x)*256))

#define MIN_VOLUME           ENCODE_DB(-CENTER_VOLUME_INDEX)
#define DEFAULT_VOLUME       ENCODE_DB(0)
#define MAX_VOLUME           ENCODE_DB(count_of(db_to_vol)-CENTER_VOLUME_INDEX)
#define VOLUME_RESOLUTION    ENCODE_DB(1)

static bool do_get_minimum(struct usb_setup_packet *setup) {
    usb_debug("AUDIO_REQ_GET_MIN\n");
    if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_INTERFACE) {
        switch (setup->wValue >> 8u) {
            case FEATURE_VOLUME_CONTROL: {
                usb_start_tiny_control_in_transfer(MIN_VOLUME, 2);
                return true;
            }
        }
    }
    return false;
}

static bool do_get_maximum(struct usb_setup_packet *setup) {
    usb_debug("AUDIO_REQ_GET_MAX\n");
    if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_INTERFACE) {
        switch (setup->wValue >> 8u) {
            case FEATURE_VOLUME_CONTROL: {
                usb_start_tiny_control_in_transfer(MAX_VOLUME, 2);
                return true;
            }
        }
    }
    return false;
}

static bool do_get_resolution(struct usb_setup_packet *setup) {
    usb_debug("AUDIO_REQ_GET_RES\n");
    if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_INTERFACE) {
        switch (setup->wValue >> 8u) {
            case FEATURE_VOLUME_CONTROL: {
                usb_start_tiny_control_in_transfer(VOLUME_RESOLUTION, 2);
                return true;
            }
        }
    }
    return false;
}

static struct audio_control_cmd {
    uint8_t cmd;
    uint8_t type;
    uint8_t cs;
    uint8_t cn;
    uint8_t unit;
    uint8_t len;
} audio_control_cmd_t;

static void _audio_reconfigure() {
    switch (audio_state.freq) {
        case 44100:
        case 48000:
            break;
        default:
            audio_state.freq = 44100;
    }
    // todo hack overwriting const
}

static void audio_set_volume(int16_t volume) {
    audio_state.volume = volume;
    // todo interpolate
    volume += CENTER_VOLUME_INDEX * 256;
    if (volume < 0) volume = 0;
    if (volume >= count_of(db_to_vol) * 256) volume = count_of(db_to_vol) * 256 - 1;
    audio_state.vol_mul = db_to_vol[((uint16_t)volume) >> 8u];
//    printf("VOL MUL %04x\n", audio_state.vol_mul);
}

static void audio_cmd_packet(struct usb_endpoint *ep) {
    assert(audio_control_cmd_t.cmd == AUDIO_REQ_SetCurrent);
    struct usb_buffer *buffer = usb_current_out_packet_buffer(ep);
    audio_control_cmd_t.cmd = 0;
    if (buffer->data_len >= audio_control_cmd_t.len) {
        if (audio_control_cmd_t.type == USB_REQ_TYPE_RECIPIENT_INTERFACE) {
            switch (audio_control_cmd_t.cs) {
                case FEATURE_MUTE_CONTROL: {
                    audio_state.mute = buffer->data[0];
                    usb_warn("Set Mute %d\n", buffer->data[0]);
                    break;
                }
                case FEATURE_VOLUME_CONTROL: {
                    audio_set_volume(*(int16_t *) buffer->data);
                    break;
                }
            }

        } else if (audio_control_cmd_t.type == USB_REQ_TYPE_RECIPIENT_ENDPOINT) {
            if (audio_control_cmd_t.cs == ENDPOINT_FREQ_CONTROL) {
                uint32_t new_freq = (*(uint32_t *) buffer->data) & 0x00ffffffu;
                usb_warn("Set freq %d\n", new_freq == 0xffffffu ? -1 : (int) new_freq);

                if (audio_state.freq != new_freq) {
                    audio_state.freq = new_freq;
                    _audio_reconfigure();
                }
            }
        }
    }
    usb_start_empty_control_in_transfer_null_completion();
    // todo is there error handling?
}


static const struct usb_transfer_type _audio_cmd_transfer_type = {
        .on_packet = audio_cmd_packet,
        .initial_packet_count = 1,
};

static bool as_set_alternate(struct usb_interface *interface, uint alt) {
    assert(interface == &as_op_interface);
    usb_warn("SET ALTERNATE %d\n", alt);
    return alt < 2;
}

static bool do_set_current(struct usb_setup_packet *setup) {
#ifndef NDEBUG
    usb_warn("AUDIO_REQ_SET_CUR\n");
#endif

    if (setup->wLength && setup->wLength < 64) {
        audio_control_cmd_t.cmd = AUDIO_REQ_SetCurrent;
        audio_control_cmd_t.type = setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK;
        audio_control_cmd_t.len = (uint8_t) setup->wLength;
        audio_control_cmd_t.unit = setup->wIndex >> 8u;
        audio_control_cmd_t.cs = setup->wValue >> 8u;
        audio_control_cmd_t.cn = (uint8_t) setup->wValue;
        usb_start_control_out_transfer(&_audio_cmd_transfer_type);
        return true;
    }
    return false;
}

static bool ac_setup_request_handler(__unused struct usb_interface *interface, struct usb_setup_packet *setup) {
    setup = __builtin_assume_aligned(setup, 4);
    if (USB_REQ_TYPE_TYPE_CLASS == (setup->bmRequestType & USB_REQ_TYPE_TYPE_MASK)) {
        switch (setup->bRequest) {
            case AUDIO_REQ_SetCurrent:
                return do_set_current(setup);

            case AUDIO_REQ_GetCurrent:
                return do_get_current(setup);

            case AUDIO_REQ_GetMinimum:
                return do_get_minimum(setup);

            case AUDIO_REQ_GetMaximum:
                return do_get_maximum(setup);

            case AUDIO_REQ_GetResolution:
                return do_get_resolution(setup);

            default:
                break;
        }
    }
    return false;
}

bool _as_setup_request_handler(__unused struct usb_endpoint *ep, struct usb_setup_packet *setup) {
    setup = __builtin_assume_aligned(setup, 4);
    if (USB_REQ_TYPE_TYPE_CLASS == (setup->bmRequestType & USB_REQ_TYPE_TYPE_MASK)) {
        switch (setup->bRequest) {
            case AUDIO_REQ_SetCurrent:
                return do_set_current(setup);

            case AUDIO_REQ_GetCurrent:
                return do_get_current(setup);

            case AUDIO_REQ_GetMinimum:
                return do_get_minimum(setup);

            case AUDIO_REQ_GetMaximum:
                return do_get_maximum(setup);

            case AUDIO_REQ_GetResolution:
                return do_get_resolution(setup);

            default:
                break;
        }
    }
    return false;
}

void usb_sound_card_init() {
    //msd_interface.setup_request_handler = msd_setup_request_handler;
    usb_interface_init(&ac_interface, &audio_device_config.ac_interface, NULL, 0, true);
    ac_interface.setup_request_handler = ac_setup_request_handler;

    static struct usb_endpoint *const op_endpoints[] = {
            &ep_op_out, &ep_op_sync
    };
    usb_interface_init(&as_op_interface, &audio_device_config.as_op_interface, op_endpoints, count_of(op_endpoints),
                       true);
    as_op_interface.set_alternate_handler = as_set_alternate;
    ep_op_out.setup_request_handler = _as_setup_request_handler;
    as_transfer.type = &as_transfer_type;
    usb_set_default_transfer(&ep_op_out, &as_transfer);
    as_sync_transfer.type = &as_sync_transfer_type;
    usb_set_default_transfer(&ep_op_sync, &as_sync_transfer);

    static struct usb_interface *const boot_device_interfaces[] = {
            &ac_interface,
            &as_op_interface,
    };
    __unused struct usb_device *device = usb_device_init(&boot_device_descriptor, &audio_device_config.descriptor,
                                                         boot_device_interfaces, count_of(boot_device_interfaces),
                                                         _get_descriptor_string);
    assert(device);
    audio_set_volume(DEFAULT_VOLUME);
    _audio_reconfigure();
//    device->on_configure = _on_configure;
    usb_device_start();
}

void * usb_audio_main(void) {

    flash_safe_execute_core_init();

    usb_sound_card_init();

    // share the audio pool buf to BT 
    set_shared_audio_buffer(audio_buffer_pool);

    printf("HAHA %04x %04x %04x %04x\n", MIN_VOLUME, DEFAULT_VOLUME, MAX_VOLUME, VOLUME_RESOLUTION);

}
