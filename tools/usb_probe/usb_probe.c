/* Read-only USB probe for the DropBridge target adapter.
 *
 * Opens the adapter through libusb, prints its descriptors and endpoint map,
 * then claims the vendor Wi-Fi interface (class ff, bulk IN + bulk OUT) and
 * releases it. It does NOT initialize the chip, write registers, download
 * firmware or transmit anything.
 *
 * Build: powershell -File tools/Build-UsbProbe.ps1
 * Usage: usb_probe.exe [vid_hex] [pid_hex] [--read8|--read32 <reg_hex>]
 *
 * --read8/--read32 issue one Realtek vendor control READ (0xC0, bRequest 5),
 * the same transfer devourer uses before Init. devourer reads the chip id at
 * SYS_CFG2 0x00FC as ONE byte; use --read8 00FC. No register is written, no
 * firmware is downloaded and no frame is transmitted.
 *
 * Exit codes: 0 open+claim OK, 1 open failed, 2 device not found,
 *             3 claim failed, 4 other libusb error, 5 register read failed.
 */
#include <libusb.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *err_name(int code) {
    return libusb_error_name(code);
}

int main(int argc, char **argv) {
    uint16_t vid = 0x0bda, pid = 0xc820;
    int do_read = 0;
    int read_width = 0;
    uint16_t read_reg = 0;
    if (argc > 1) vid = (uint16_t)strtoul(argv[1], NULL, 16);
    if (argc > 2) pid = (uint16_t)strtoul(argv[2], NULL, 16);
    if (argc > 4 && strcmp(argv[3], "--read8") == 0) {
        do_read = 1;
        read_width = 1;
        read_reg = (uint16_t)strtoul(argv[4], NULL, 16);
    } else if (argc > 4 && strcmp(argv[3], "--read32") == 0) {
        do_read = 1;
        read_width = 4;
        read_reg = (uint16_t)strtoul(argv[4], NULL, 16);
    }

    printf("libusb %s\n", libusb_get_version()->describe);
    printf("target %04x:%04x\n", vid, pid);

    libusb_context *ctx = NULL;
    int rc = libusb_init(&ctx);
    if (rc != 0) {
        printf("libusb_init failed: %s\n", err_name(rc));
        return 4;
    }

    libusb_device **list = NULL;
    ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        printf("libusb_get_device_list failed: %s\n", err_name((int)count));
        libusb_exit(ctx);
        return 4;
    }

    libusb_device *target = NULL;
    for (ssize_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (desc.idVendor == vid && desc.idProduct == pid) {
            target = list[i];
            break;
        }
    }

    if (!target) {
        printf("device not found on the bus\n");
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return 2;
    }

    printf("bus %u address %u speed %d\n",
           (unsigned)libusb_get_bus_number(target),
           (unsigned)libusb_get_device_address(target),
           (int)libusb_get_device_speed(target));

    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(target, &desc);
    printf("descriptor: bcdUSB=%04x class=%02x subclass=%02x protocol=%02x "
           "maxPacket0=%u\n",
           desc.bcdUSB, desc.bDeviceClass, desc.bDeviceSubClass,
           desc.bDeviceProtocol, desc.bMaxPacketSize0);

    libusb_device_handle *handle = NULL;
    rc = libusb_open(target, &handle);
    if (rc != 0) {
        printf("open failed: %s\n", err_name(rc));
        printf("hint: MI_02 is still bound to a vendor driver; follow "
               "docs/BINDING_RUNBOOK.md first.\n");
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return 1;
    }
    printf("open OK\n");

    int candidate = -1;
    struct libusb_config_descriptor *config = NULL;
    rc = libusb_get_active_config_descriptor(target, &config);
    if (rc != 0) {
        printf("get_active_config_descriptor failed: %s\n", err_name(rc));
        libusb_close(handle);
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return 4;
    }

    for (uint8_t i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface *iface = &config->interface[i];
        for (int a = 0; a < iface->num_altsetting; a++) {
            const struct libusb_interface_descriptor *alt = &iface->altsetting[a];
            printf("interface %u alt %d: class=%02x subclass=%02x protocol=%02x "
                   "endpoints=%u\n",
                   alt->bInterfaceNumber, alt->bAlternateSetting,
                   alt->bInterfaceClass, alt->bInterfaceSubClass,
                   alt->bInterfaceProtocol, alt->bNumEndpoints);
            int has_in = 0, has_out = 0;
            for (uint8_t e = 0; e < alt->bNumEndpoints; e++) {
                const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];
                const char *dir = (ep->bEndpointAddress & 0x80) ? "IN" : "OUT";
                printf("  endpoint 0x%02x %s attr=%02x maxPacket=%u\n",
                       ep->bEndpointAddress, dir, ep->bmAttributes,
                       ep->wMaxPacketSize);
                if ((ep->bmAttributes & 0x03) == 0x02) {
                    if (ep->bEndpointAddress & 0x80) has_in = 1;
                    else has_out = 1;
                }
            }
            if (alt->bInterfaceClass == 0xff && has_in && has_out && candidate < 0)
                candidate = alt->bInterfaceNumber;
        }
    }

    if (candidate < 0) {
        printf("no vendor interface with bulk IN and OUT found\n");
        libusb_free_config_descriptor(config);
        libusb_close(handle);
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return 3;
    }

    printf("claiming candidate interface %d ...\n", candidate);
    rc = libusb_claim_interface(handle, candidate);
    if (rc != 0) {
        printf("claim failed: %s\n", err_name(rc));
        libusb_free_config_descriptor(config);
        libusb_close(handle);
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return 3;
    }
    printf("claim OK\n");

    int result = 0;
    if (do_read) {
        uint8_t buf[4] = {0, 0, 0, 0};
        int n = libusb_control_transfer(handle, 0xC0, 5, read_reg, 0, buf,
                                        (uint16_t)read_width, 500);
        if (n == read_width) {
            if (read_width == 1) {
                printf("read8  0x%04x = 0x%02x\n", read_reg, buf[0]);
            } else {
                uint32_t v = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                             ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
                printf("read32 0x%04x = 0x%08x\n", read_reg, v);
            }
        } else {
            printf("read%d 0x%04x failed: %s (%d)\n", read_width * 8, read_reg,
                   err_name(n), n);
            result = 5;
        }
    }

    printf("releasing interface; no Init, no firmware, no TX\n");
    libusb_release_interface(handle, candidate);

    libusb_free_config_descriptor(config);
    libusb_close(handle);
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return result;
}
