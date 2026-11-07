// SPDX-License-Identifier: GPL-2.0-only

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libusb-1.0/libusb.h>

enum {
    ALC4082_VENDOR_ID = 0x26ce,
    ALC4082_PRODUCT_ID = 0x0a08,
    ALC4082_WRITE_REGISTER = 0x24,
    ALC4082_READ_REGISTER = 0x25,
    ALC4082_WIDGET_COMMAND = 0x6c,
    ALC4082_TIMEOUT_MS = 1000,
};

struct register_update {
    uint16_t address;
    uint8_t preserve;
    uint8_t set;
};

static int read_register(libusb_device_handle *handle, uint16_t address,
                         uint8_t *value)
{
    int status = libusb_control_transfer(
        handle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR |
            LIBUSB_RECIPIENT_DEVICE,
        ALC4082_READ_REGISTER,
        0,
        address,
        value,
        sizeof(*value),
        ALC4082_TIMEOUT_MS);

    if (status == sizeof(*value))
        return 0;

    fprintf(stderr, "read 0x%04x: %s (%d)\n", address,
            status < 0 ? libusb_error_name(status) : "short transfer",
            status);
    return -1;
}

static int write_register(libusb_device_handle *handle, uint16_t address,
                          uint8_t value)
{
    int status = libusb_control_transfer(
        handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR |
            LIBUSB_RECIPIENT_DEVICE,
        ALC4082_WRITE_REGISTER,
        0,
        address,
        &value,
        sizeof(value),
        ALC4082_TIMEOUT_MS);

    if (status == sizeof(value))
        return 0;

    fprintf(stderr, "write 0x%04x: %s (%d)\n", address,
            status < 0 ? libusb_error_name(status) : "short transfer",
            status);
    return -1;
}

static int update_register(libusb_device_handle *handle,
                           const struct register_update *update)
{
    uint8_t previous = 0;
    if (read_register(handle, update->address, &previous) < 0)
        return -1;

    uint8_t value = (previous & update->preserve) | update->set;
    if (write_register(handle, update->address, value) < 0)
        return -1;

    printf("0x%04x: 0x%02x -> 0x%02x\n",
           update->address, previous, value);
    return 0;
}

static int send_widget_command(libusb_device_handle *handle, uint8_t command)
{
    int status = libusb_control_transfer(
        handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR |
            LIBUSB_RECIPIENT_DEVICE,
        ALC4082_WIDGET_COMMAND,
        0,
        1,
        &command,
        sizeof(command),
        ALC4082_TIMEOUT_MS);

    if (status == sizeof(command)) {
        printf("widget command: 0x%02x\n", command);
        return 0;
    }

    fprintf(stderr, "widget command 0x%02x: %s (%d)\n", command,
            status < 0 ? libusb_error_name(status) : "short transfer",
            status);
    return -1;
}

static int activate_rear_mic(libusb_device_handle *handle)
{
    return send_widget_command(handle, 0x0f);
}

static const struct register_update rear_capture_enable[] = {
    {0x0319, 0x3f, 0x40},
    {0x0303, 0xf0, 0x05},
    {0x050a, 0xfd, 0x02},
    {0x031a, 0x00, 0x20},
    {0x0510, 0xfd, 0x02},
};

/* This is the exact false branch of the Windows boolean register routine. */
static const struct register_update rear_capture_disable[] = {
    {0x0319, 0x3f, 0x00},
    {0x0303, 0xf0, 0x00},
    {0x050a, 0xfd, 0x02},
    {0x0510, 0xfd, 0x00},
    {0x031a, 0x00, 0x00},
};

static int apply_updates(libusb_device_handle *handle,
                         const struct register_update *updates,
                         size_t update_count, int omitted_address)
{
    if (omitted_address >= 0) {
        size_t index;

        for (index = 0; index < update_count; index++) {
            if (updates[index].address == omitted_address)
                break;
        }
        if (index == update_count) {
            fprintf(stderr, "0x%04x is not part of this transaction\n",
                    omitted_address);
            return -1;
        }
    }

    for (size_t index = 0; index < update_count; index++) {
        if (updates[index].address == omitted_address) {
            printf("0x%04x: omitted\n", updates[index].address);
            continue;
        }
        if (update_register(handle, &updates[index]) < 0)
            return -1;
    }

    return 0;
}

static int enable_rear_registers(libusb_device_handle *handle,
                                 int omitted_address)
{
    return apply_updates(handle, rear_capture_enable,
                         sizeof(rear_capture_enable) /
                             sizeof(rear_capture_enable[0]),
                         omitted_address);
}

static int enable_rear_capture(libusb_device_handle *handle,
                               int omitted_address)
{
    if (activate_rear_mic(handle) < 0)
        return -1;

    return enable_rear_registers(handle, omitted_address);
}

static int disable_rear_registers(libusb_device_handle *handle)
{
    return apply_updates(handle, rear_capture_disable,
                         sizeof(rear_capture_disable) /
                             sizeof(rear_capture_disable[0]),
                         -1);
}

static int parse_u16(const char *text, uint16_t *value)
{
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT16_MAX)
        return -1;

    *value = (uint16_t)parsed;
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s read REGISTER [REGISTER ...]\n"
            "       %s dump START END\n"
            "       %s widget COMMAND\n"
            "       %s update REGISTER PRESERVE SET\n"
            "       %s activate-rear-mic\n"
            "       %s enable-rear-registers [--omit REGISTER]\n"
            "       %s enable-rear-capture [--omit REGISTER]\n"
            "       %s disable-rear-registers\n",
            program, program, program, program, program, program, program,
            program);
}

int main(int argc, char **argv)
{
    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;
    int result = EXIT_FAILURE;

    if (argc < 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    int status = libusb_init(&context);
    if (status != LIBUSB_SUCCESS) {
        fprintf(stderr, "libusb_init: %s\n", libusb_error_name(status));
        return EXIT_FAILURE;
    }

    handle = libusb_open_device_with_vid_pid(
        context, ALC4082_VENDOR_ID, ALC4082_PRODUCT_ID);
    if (handle == NULL) {
        fprintf(stderr, "unable to open %04x:%04x\n",
                ALC4082_VENDOR_ID, ALC4082_PRODUCT_ID);
        goto out;
    }

    if (strcmp(argv[1], "activate-rear-mic") == 0 && argc == 2) {
        result = activate_rear_mic(handle) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
        goto out;
    }

    if (strcmp(argv[1], "disable-rear-registers") == 0 && argc == 2) {
        result = disable_rear_registers(handle) < 0 ?
            EXIT_FAILURE : EXIT_SUCCESS;
        goto out;
    }

    if (strcmp(argv[1], "widget") == 0 && argc == 3) {
        uint16_t command = 0;

        if (parse_u16(argv[2], &command) < 0 || command > UINT8_MAX) {
            fprintf(stderr, "invalid widget command: %s\n", argv[2]);
            goto out;
        }
        result = send_widget_command(handle, (uint8_t)command) < 0 ?
            EXIT_FAILURE : EXIT_SUCCESS;
        goto out;
    }

    if (strcmp(argv[1], "update") == 0 && argc == 5) {
        uint16_t address = 0;
        uint16_t preserve = 0;
        uint16_t set = 0;
        struct register_update update;

        if (parse_u16(argv[2], &address) < 0 ||
            parse_u16(argv[3], &preserve) < 0 || preserve > UINT8_MAX ||
            parse_u16(argv[4], &set) < 0 || set > UINT8_MAX) {
            fprintf(stderr, "invalid register update\n");
            goto out;
        }
        update.address = address;
        update.preserve = (uint8_t)preserve;
        update.set = (uint8_t)set;
        result = update_register(handle, &update) < 0 ?
            EXIT_FAILURE : EXIT_SUCCESS;
        goto out;
    }

    if ((strcmp(argv[1], "enable-rear-registers") == 0 ||
         strcmp(argv[1], "enable-rear-capture") == 0) &&
        (argc == 2 || argc == 4)) {
        int omitted_address = -1;

        if (argc == 4) {
            uint16_t parsed_address = 0;

            if (strcmp(argv[2], "--omit") != 0 ||
                parse_u16(argv[3], &parsed_address) < 0) {
                fprintf(stderr, "invalid omitted register\n");
                goto out;
            }
            omitted_address = parsed_address;
        }

        if (strcmp(argv[1], "enable-rear-capture") == 0)
            status = enable_rear_capture(handle, omitted_address);
        else
            status = enable_rear_registers(handle, omitted_address);
        result = status < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
        goto out;
    }

    if (strcmp(argv[1], "read") == 0 && argc > 2) {
        for (int index = 2; index < argc; index++) {
            uint16_t address = 0;
            uint8_t value = 0;

            if (parse_u16(argv[index], &address) < 0) {
                fprintf(stderr, "invalid register: %s\n", argv[index]);
                goto out;
            }
            if (read_register(handle, address, &value) < 0)
                goto out;

            printf("0x%04x = 0x%02x\n", address, value);
        }
        result = EXIT_SUCCESS;
        goto out;
    }

    if (strcmp(argv[1], "dump") == 0 && argc == 4) {
        uint16_t start = 0;
        uint16_t end = 0;

        if (parse_u16(argv[2], &start) < 0 ||
            parse_u16(argv[3], &end) < 0 || start > end) {
            fprintf(stderr, "invalid register range: %s..%s\n",
                    argv[2], argv[3]);
            goto out;
        }

        for (uint32_t address = start; address <= end; address++) {
            uint8_t value = 0;

            if (read_register(handle, (uint16_t)address, &value) < 0)
                goto out;

            printf("0x%04x = 0x%02x\n", (unsigned int)address, value);
        }
        result = EXIT_SUCCESS;
        goto out;
    }

    usage(argv[0]);

out:
    if (handle != NULL)
        libusb_close(handle);
    libusb_exit(context);
    return result;
}
