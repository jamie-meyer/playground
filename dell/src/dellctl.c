#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    DELL_BRIDGE_VID = 0x0424,
    DELL_BRIDGE_PID = 0x7260,
    DELL_I2C_ADDRESS = 0x37,
    HID_REPORT_SIZE = 64,
    HID_PAYLOAD_SIZE = 60,
    HID_READ_PAYLOAD_SIZE = 59,
};

typedef struct {
    IOHIDManagerRef manager;
    IOHIDDeviceRef device;
    uint8_t input_storage[HID_REPORT_SIZE];
    uint8_t report[HID_REPORT_SIZE];
    CFIndex report_length;
    IOReturn report_result;
    bool report_ready;
} Bridge;

typedef struct {
    const char *name;
    uint16_t value;
} NamedValue;

typedef struct {
    CFRunLoopRef run_loop;
    CFRunLoopSourceRef switch_source;
    CFMachPortRef event_tap;
    uint16_t target_source;
    bool switching;
} HotkeyContext;

static const NamedValue U4323QE_LAYOUTS[] = {
    {"off", 0x00},
    {"pip-small", 0x21},
    {"pip-large", 0x22},
    {"side-by-side", 0x24},
    {"top-bottom", 0x2f},
    {"three-a", 0x31},
    {"three-b", 0x32},
    {"three-c", 0x33},
    {"three-d", 0x34},
    {"three-e", 0x35},
    {"quad", 0x41},
};

static const NamedValue U4323QE_SOURCES[] = {
    {"usb-c", 0x1b},
    {"dp1", 0x0f},
    {"dp2", 0x13},
    {"hdmi1", 0x11},
    {"hdmi2", 0x12},
};

static void print_bytes(const char *label, const uint8_t *bytes, size_t length) {
    printf("%s", label);
    for (size_t i = 0; i < length; ++i) {
        printf("%s%02X", i == 0 ? "" : " ", bytes[i]);
    }
    putchar('\n');
}

static uint8_t tx_checksum(const uint8_t *bytes, size_t length) {
    uint8_t checksum = 0x6e;
    for (size_t i = 0; i < length; ++i) {
        checksum ^= bytes[i];
    }
    return checksum;
}

static bool rx_checksum_valid(const uint8_t *bytes, size_t length) {
    if (length < 2) {
        return false;
    }

    uint8_t checksum = 0x50;
    for (size_t i = 0; i + 1 < length; ++i) {
        checksum ^= bytes[i];
    }
    return checksum == bytes[length - 1];
}

static CFMutableDictionaryRef create_matching_dictionary(void) {
    CFMutableDictionaryRef dictionary = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (dictionary == NULL) {
        return NULL;
    }

    int vendor = DELL_BRIDGE_VID;
    int product = DELL_BRIDGE_PID;
    CFNumberRef vendor_number =
        CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
    CFNumberRef product_number =
        CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &product);
    if (vendor_number == NULL || product_number == NULL) {
        if (vendor_number != NULL) {
            CFRelease(vendor_number);
        }
        if (product_number != NULL) {
            CFRelease(product_number);
        }
        CFRelease(dictionary);
        return NULL;
    }

    CFDictionarySetValue(
        dictionary, CFSTR(kIOHIDVendorIDKey), vendor_number);
    CFDictionarySetValue(
        dictionary, CFSTR(kIOHIDProductIDKey), product_number);
    CFRelease(vendor_number);
    CFRelease(product_number);
    return dictionary;
}

static int64_t integer_property(IOHIDDeviceRef device, CFStringRef key) {
    CFTypeRef value = IOHIDDeviceGetProperty(device, key);
    int64_t result = -1;
    if (value != NULL && CFGetTypeID(value) == CFNumberGetTypeID()) {
        (void)CFNumberGetValue(
            (CFNumberRef)value, kCFNumberSInt64Type, &result);
    }
    return result;
}

static void print_string_property(
    IOHIDDeviceRef device, CFStringRef key, const char *label) {
    CFTypeRef value = IOHIDDeviceGetProperty(device, key);
    if (value == NULL || CFGetTypeID(value) != CFStringGetTypeID()) {
        printf("%s: (not reported)\n", label);
        return;
    }

    char string[512];
    if (!CFStringGetCString(
            (CFStringRef)value,
            string,
            (CFIndex)sizeof(string),
            kCFStringEncodingUTF8)) {
        printf("%s: (unprintable)\n", label);
        return;
    }
    printf("%s: %s\n", label, string);
}

static void print_device(IOHIDDeviceRef device) {
    print_string_property(device, CFSTR(kIOHIDProductKey), "product");
    print_string_property(
        device, CFSTR(kIOHIDManufacturerKey), "manufacturer");
    print_string_property(
        device, CFSTR(kIOHIDSerialNumberKey), "serial");
    printf("VID:PID: %04llX:%04llX\n",
           (unsigned long long)integer_property(
               device, CFSTR(kIOHIDVendorIDKey)),
           (unsigned long long)integer_property(
               device, CFSTR(kIOHIDProductIDKey)));
    printf("location ID: 0x%08llX\n",
           (unsigned long long)integer_property(
               device, CFSTR(kIOHIDLocationIDKey)));
    printf("usage page / usage: 0x%llX / 0x%llX\n",
           (unsigned long long)integer_property(
               device, CFSTR(kIOHIDPrimaryUsagePageKey)),
           (unsigned long long)integer_property(
               device, CFSTR(kIOHIDPrimaryUsageKey)));
    printf("max input / output report: %lld / %lld bytes\n",
           (long long)integer_property(
               device, CFSTR(kIOHIDMaxInputReportSizeKey)),
           (long long)integer_property(
               device, CFSTR(kIOHIDMaxOutputReportSizeKey)));
}

static int create_manager(
    IOHIDManagerRef *manager_out, CFSetRef *devices_out) {
    *manager_out = NULL;
    *devices_out = NULL;

    IOHIDManagerRef manager =
        IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (manager == NULL) {
        fprintf(stderr, "Could not create IOHIDManager.\n");
        return 1;
    }

    CFMutableDictionaryRef matching = create_matching_dictionary();
    if (matching == NULL) {
        fprintf(stderr, "Could not create HID matching dictionary.\n");
        CFRelease(manager);
        return 1;
    }

    IOHIDManagerSetDeviceMatching(manager, matching);
    CFRelease(matching);

    IOReturn result =
        IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    if (result != kIOReturnSuccess) {
        fprintf(stderr,
                "IOHIDManagerOpen failed: 0x%08X\n",
                (unsigned int)result);
        CFRelease(manager);
        return 1;
    }

    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (devices == NULL || CFSetGetCount(devices) == 0) {
        fprintf(stderr,
                "No %04X:%04X HID I2C bridge is connected.\n",
                DELL_BRIDGE_VID,
                DELL_BRIDGE_PID);
        if (devices != NULL) {
            CFRelease(devices);
        }
        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
        CFRelease(manager);
        return 1;
    }

    *manager_out = manager;
    *devices_out = devices;
    return 0;
}

static void input_report_callback(
    void *context,
    IOReturn result,
    void *sender,
    IOHIDReportType type,
    uint32_t report_id,
    uint8_t *report,
    CFIndex report_length) {
    (void)sender;
    (void)type;
    (void)report_id;

    Bridge *bridge = context;
    CFIndex length = report_length;
    if (length > HID_REPORT_SIZE) {
        length = HID_REPORT_SIZE;
    }
    memcpy(bridge->report, report, (size_t)length);
    bridge->report_length = length;
    bridge->report_result = result;
    bridge->report_ready = true;
}

static int bridge_open(Bridge *bridge) {
    memset(bridge, 0, sizeof(*bridge));

    CFSetRef devices = NULL;
    if (create_manager(&bridge->manager, &devices) != 0) {
        return 1;
    }

    CFIndex count = CFSetGetCount(devices);
    const void **values = calloc((size_t)count, sizeof(*values));
    if (values == NULL) {
        fprintf(stderr, "Could not allocate device list.\n");
        CFRelease(devices);
        IOHIDManagerClose(bridge->manager, kIOHIDOptionsTypeNone);
        CFRelease(bridge->manager);
        bridge->manager = NULL;
        return 1;
    }
    CFSetGetValues(devices, values);
    bridge->device = (IOHIDDeviceRef)CFRetain(values[0]);
    free(values);
    CFRelease(devices);

    IOReturn result =
        IOHIDDeviceOpen(bridge->device, kIOHIDOptionsTypeNone);
    if (result != kIOReturnSuccess) {
        fprintf(stderr,
                "IOHIDDeviceOpen failed: 0x%08X\n",
                (unsigned int)result);
        CFRelease(bridge->device);
        bridge->device = NULL;
        IOHIDManagerClose(bridge->manager, kIOHIDOptionsTypeNone);
        CFRelease(bridge->manager);
        bridge->manager = NULL;
        return 1;
    }

    IOHIDDeviceRegisterInputReportCallback(
        bridge->device,
        bridge->input_storage,
        HID_REPORT_SIZE,
        input_report_callback,
        bridge);
    IOHIDDeviceScheduleWithRunLoop(
        bridge->device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    return 0;
}

static void bridge_close(Bridge *bridge) {
    if (bridge->device != NULL) {
        IOHIDDeviceUnscheduleFromRunLoop(
            bridge->device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        IOHIDDeviceClose(bridge->device, kIOHIDOptionsTypeNone);
        CFRelease(bridge->device);
    }
    if (bridge->manager != NULL) {
        IOHIDManagerClose(bridge->manager, kIOHIDOptionsTypeNone);
        CFRelease(bridge->manager);
    }
    memset(bridge, 0, sizeof(*bridge));
}

static int send_hid_report(Bridge *bridge, const uint8_t *report) {
    /*
     * This mirrors the report convention used by Dell's bundled HID layer:
     * the opcode is both the IOHID report ID and byte zero of the 64-byte
     * report. The device descriptor itself uses a vendor page and 64-byte
     * reports.
     */
    IOReturn result = IOHIDDeviceSetReport(
        bridge->device,
        kIOHIDReportTypeOutput,
        report[0],
        report,
        HID_REPORT_SIZE);
    if (result != kIOReturnSuccess) {
        fprintf(stderr,
                "IOHIDDeviceSetReport(0x%02X) failed: 0x%08X\n",
                report[0],
                (unsigned int)result);
        return 1;
    }
    return 0;
}

static int bridge_set_bus_speed_100khz(Bridge *bridge) {
    /*
     * UMCP7260 does not reliably inherit a usable I2C clock after USB open.
     * Dell initializes this backend with a 0x95 report before DDC traffic.
     * The two zero bytes select 100 kHz.
     */
    uint8_t report[HID_REPORT_SIZE];
    memset(report, 0xff, sizeof(report));
    report[0] = 0x95;
    report[1] = 0x00;
    report[2] = 0x00;
    return send_hid_report(bridge, report);
}

static int bridge_i2c_write(
    Bridge *bridge, uint8_t address, const uint8_t *data, size_t length) {
    if (length == 0 || length > HID_PAYLOAD_SIZE) {
        fprintf(stderr,
                "I2C write length must be between 1 and %d bytes.\n",
                HID_PAYLOAD_SIZE);
        return 1;
    }

    uint8_t report[HID_REPORT_SIZE];
    memset(report, 0xff, sizeof(report));
    report[0] = 0x92;
    report[1] = address;
    report[2] = (uint8_t)length;
    report[3] = 0x00;
    memcpy(report + 4, data, length);
    return send_hid_report(bridge, report);
}

static int bridge_i2c_read(
    Bridge *bridge, uint8_t address, uint8_t *data, size_t length) {
    if (length == 0 || length > HID_READ_PAYLOAD_SIZE) {
        fprintf(stderr,
                "I2C read length must be between 1 and %d bytes.\n",
                HID_READ_PAYLOAD_SIZE);
        return 1;
    }

    uint8_t request[HID_REPORT_SIZE];
    memset(request, 0xff, sizeof(request));
    request[0] = 0x93;
    request[1] = address;
    request[2] = (uint8_t)(length & 0xff);
    request[3] = (uint8_t)((length >> 8) & 0xff);
    request[4] = (uint8_t)((length >> 16) & 0xff);
    request[5] = (uint8_t)((length >> 24) & 0xff);

    bridge->report_ready = false;
    if (send_hid_report(bridge, request) != 0) {
        return 1;
    }

    CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 0.6;
    while (!bridge->report_ready &&
           CFAbsoluteTimeGetCurrent() < deadline) {
        (void)CFRunLoopRunInMode(
            kCFRunLoopDefaultMode, 0.05, true);
    }
    if (!bridge->report_ready) {
        fprintf(stderr, "Timed out waiting for HID I2C response.\n");
        return 1;
    }
    if (bridge->report_result != kIOReturnSuccess) {
        fprintf(stderr,
                "HID input report failed: 0x%08X\n",
                (unsigned int)bridge->report_result);
        return 1;
    }
    if (bridge->report_length < 4 || bridge->report[0] != 0x94) {
        print_bytes(
            "unexpected HID report: ",
            bridge->report,
            (size_t)bridge->report_length);
        return 1;
    }

    size_t received = bridge->report[2];
    size_t available = (size_t)bridge->report_length - 4;
    if (received > available) {
        received = available;
    }
    if (received < length) {
        fprintf(stderr,
                "Short HID I2C response: wanted %zu, received %zu.\n",
                length,
                received);
        return 1;
    }
    memcpy(data, bridge->report + 4, length);
    return 0;
}

static int dell_private_read(
    Bridge *bridge,
    uint8_t command,
    const uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *actual_output_length,
    bool verbose) {
    if (input_length > 54 || output_capacity > 53) {
        fprintf(stderr, "Private command payload is too large.\n");
        return 1;
    }
    if (actual_output_length != NULL) {
        *actual_output_length = 0;
    }

    uint8_t request[60];
    size_t request_length = input_length + 5;
    request[0] = 0x51;
    request[1] = (uint8_t)(0x80 | (input_length + 2));
    request[2] = 0xeb;
    request[3] = command;
    if (input_length > 0) {
        memcpy(request + 4, input, input_length);
    }
    request[request_length - 1] =
        tx_checksum(request, request_length - 1);

    if (verbose) {
        print_bytes("DDC request:  ", request, request_length);
    }
    if (bridge_i2c_write(
            bridge, DELL_I2C_ADDRESS, request, request_length) != 0) {
        return 1;
    }

    usleep(command == 0xfe ? 100000 : 50000);

    size_t expected_length = output_capacity + 6;
    size_t read_length = expected_length < 8 ? 8 : expected_length;
    uint8_t response[59];
    memset(response, 0, sizeof(response));
    if (bridge_i2c_read(
            bridge, DELL_I2C_ADDRESS, response, read_length) != 0) {
        return 1;
    }

    if (verbose) {
        print_bytes("DDC response: ", response, read_length);
    }
    if (response[0] != 0x6e) {
        fprintf(stderr,
                "Invalid DDC response source: 0x%02X.\n",
                response[0]);
        return 1;
    }

    size_t message_length = (size_t)(response[1] & 0x7f) + 3;
    if (message_length > read_length || message_length < 6) {
        fprintf(stderr,
                "Invalid DDC response length: %zu.\n",
                message_length);
        return 1;
    }
    if (!rx_checksum_valid(response, message_length)) {
        fprintf(stderr, "Invalid DDC response checksum.\n");
        return 1;
    }
    if (response[2] != 0x02 || response[4] != command) {
        fprintf(stderr,
                "Private response mismatch (type=0x%02X, command=0x%02X).\n",
                response[2],
                response[4]);
        return 1;
    }
    if (response[3] != 0x00) {
        fprintf(stderr,
                "Monitor returned private status 0x%02X for command 0x%02X.\n",
                response[3],
                command);
        return 1;
    }
    if (message_length > expected_length) {
        fprintf(stderr,
                "DDC response exceeds capacity: capacity %zu, got %zu.\n",
                expected_length,
                message_length);
        return 1;
    }

    size_t output_length = message_length - 6;
    if (output_length > 0) {
        memcpy(output, response + 5, output_length);
    }
    if (actual_output_length != NULL) {
        *actual_output_length = output_length;
    }
    return 0;
}

static int dell_private_write(
    Bridge *bridge,
    uint8_t command,
    const uint8_t *input,
    size_t input_length,
    bool verbose) {
    if (input_length > 54) {
        fprintf(stderr, "Private command payload is too large.\n");
        return 1;
    }

    uint8_t request[60];
    size_t request_length = input_length + 5;
    request[0] = 0x51;
    request[1] = (uint8_t)(0x80 | (input_length + 2));
    request[2] = 0xea;
    request[3] = command;
    if (input_length > 0) {
        memcpy(request + 4, input, input_length);
    }
    request[request_length - 1] =
        tx_checksum(request, request_length - 1);

    if (verbose) {
        print_bytes("DDC write:    ", request, request_length);
    }
    if (bridge_i2c_write(
            bridge, DELL_I2C_ADDRESS, request, request_length) != 0) {
        return 1;
    }

    /*
     * Dell's SDK gives tunneled DDC/CI writes 100 ms before requesting the
     * acknowledgement. Other private writes use a shorter 50 ms delay.
     */
    usleep(command == 0xfe ? 100000 : 50000);

    uint8_t response[8];
    memset(response, 0, sizeof(response));
    if (bridge_i2c_read(
            bridge, DELL_I2C_ADDRESS, response, sizeof(response)) != 0) {
        return 1;
    }
    if (verbose) {
        print_bytes("DDC write ack:", response, sizeof(response));
    }

    if (response[0] != 0x6e) {
        fprintf(stderr,
                "Invalid DDC write response source: 0x%02X.\n",
                response[0]);
        return 1;
    }
    size_t message_length = (size_t)(response[1] & 0x7f) + 3;
    if (message_length < 6 || message_length > sizeof(response) ||
        !rx_checksum_valid(response, message_length)) {
        fprintf(stderr, "Invalid DDC write acknowledgement.\n");
        return 1;
    }
    if (response[2] != 0x02 || response[4] != command) {
        fprintf(stderr,
                "Private write acknowledgement mismatch "
                "(type=0x%02X, command=0x%02X).\n",
                response[2],
                response[4]);
        return 1;
    }
    if (response[3] != 0x00) {
        fprintf(stderr,
                "Monitor rejected private write command 0x%02X "
                "with status 0x%02X.\n",
                command,
                response[3]);
        return 1;
    }
    return 0;
}

static int dell_raw_ddcci(
    Bridge *bridge,
    const uint8_t *request,
    size_t request_length,
    uint8_t *response,
    size_t response_length,
    size_t *actual_response_length,
    bool verbose) {
    if (request_length > 52) {
        fprintf(stderr, "Raw DDC/CI request is too large.\n");
        return 1;
    }

    uint8_t private_input[54];
    private_input[0] = 0x00;
    private_input[1] = 0x00;
    memcpy(private_input + 2, request, request_length);
    return dell_private_read(
        bridge,
        0xfe,
        private_input,
        request_length + 2,
        response,
        response_length,
        actual_response_length,
        verbose);
}

static int dell_raw_ddcci_write(
    Bridge *bridge,
    const uint8_t *request,
    size_t request_length,
    bool verbose) {
    if (request_length > 52) {
        fprintf(stderr, "Raw DDC/CI request is too large.\n");
        return 1;
    }

    uint8_t private_input[54];
    private_input[0] = 0x00;
    private_input[1] = 0x00;
    memcpy(private_input + 2, request, request_length);
    return dell_private_write(
        bridge, 0xfe, private_input, request_length + 2, verbose);
}

static int command_list(void) {
    IOHIDManagerRef manager = NULL;
    CFSetRef devices = NULL;
    if (create_manager(&manager, &devices) != 0) {
        return 1;
    }

    CFIndex count = CFSetGetCount(devices);
    const void **values = calloc((size_t)count, sizeof(*values));
    if (values == NULL) {
        fprintf(stderr, "Could not allocate device list.\n");
        CFRelease(devices);
        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
        CFRelease(manager);
        return 1;
    }

    CFSetGetValues(devices, values);
    printf("Found %lld Dell/Microchip HID I2C bridge%s.\n",
           (long long)count,
           count == 1 ? "" : "s");
    for (CFIndex i = 0; i < count; ++i) {
        if (count > 1) {
            printf("\nbridge %lld\n", (long long)(i + 1));
        }
        print_device((IOHIDDeviceRef)values[i]);
    }

    free(values);
    CFRelease(devices);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}

static int command_brightness(void) {
    Bridge bridge;
    if (bridge_open(&bridge) != 0) {
        return 1;
    }

    uint8_t value = 0;
    size_t value_length = 0;
    int result = bridge_set_bus_speed_100khz(&bridge);
    if (result == 0) {
        result = dell_private_read(
            &bridge,
            0x30,
            NULL,
            0,
            &value,
            1,
            &value_length,
            true);
    }
    if (result == 0 && value_length == 1) {
        printf("brightness: %u\n", value);
    } else if (result == 0) {
        fprintf(stderr,
                "Private brightness response has %zu data bytes.\n",
                value_length);
        result = 1;
    }
    bridge_close(&bridge);
    return result;
}

static int read_vcp(
    Bridge *bridge, uint8_t vcp, uint8_t *response, bool verbose) {
    uint8_t inner_request[] = {0x51, 0x82, 0x01, vcp, 0x00};
    inner_request[4] = tx_checksum(inner_request, 4);

    size_t inner_response_length = 0;
    int result = dell_raw_ddcci(
        bridge,
        inner_request,
        sizeof(inner_request),
        response,
        11,
        &inner_response_length,
        verbose);
    if (result == 0) {
        if (verbose) {
            print_bytes("VCP response: ", response, inner_response_length);
        }
        if (inner_response_length != 11 ||
            response[0] != 0x6e ||
            response[2] != 0x02 ||
            response[4] != vcp ||
            !rx_checksum_valid(response, 11)) {
            return 1;
        }
    }
    return result;
}

static uint16_t vcp_current_value(const uint8_t *response) {
    return (uint16_t)(((uint16_t)response[8] << 8) | response[9]);
}

static int read_vcp_value(
    Bridge *bridge, uint8_t vcp, uint16_t *value, bool verbose) {
    uint8_t response[11];
    int result = read_vcp(bridge, vcp, response, verbose);
    if (result != 0) {
        return result;
    }
    if (response[3] != 0x00) {
        fprintf(stderr,
                "Monitor reports VCP 0x%02X is unsupported "
                "(result 0x%02X).\n",
                vcp,
                response[3]);
        return 1;
    }
    *value = vcp_current_value(response);
    return 0;
}

static int write_vcp(
    Bridge *bridge, uint8_t vcp, uint16_t value, bool verbose) {
    uint8_t request[] = {
        0x51,
        0x84,
        0x03,
        vcp,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xff),
        0x00,
    };
    request[6] = tx_checksum(request, 6);
    return dell_raw_ddcci_write(
        bridge, request, sizeof(request), verbose);
}

static int open_prepared_bridge(Bridge *bridge) {
    if (bridge_open(bridge) != 0) {
        return 1;
    }
    if (bridge_set_bus_speed_100khz(bridge) != 0) {
        bridge_close(bridge);
        return 1;
    }
    return 0;
}

static const char *name_for_value(
    const NamedValue *values, size_t count, uint16_t value) {
    for (size_t i = 0; i < count; ++i) {
        if (values[i].value == value) {
            return values[i].name;
        }
    }
    return NULL;
}

static int value_for_name(
    const NamedValue *values,
    size_t count,
    const char *name,
    uint16_t *value) {
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(values[i].name, name) == 0) {
            *value = values[i].value;
            return 0;
        }
    }
    return 1;
}

static const char *source_name(uint16_t value) {
    const char *name = name_for_value(
        U4323QE_SOURCES,
        sizeof(U4323QE_SOURCES) / sizeof(U4323QE_SOURCES[0]),
        (uint16_t)(value & 0x1f));
    return name == NULL ? "unknown" : name;
}

static int verify_vcp_value(
    Bridge *bridge,
    uint8_t vcp,
    uint16_t expected,
    bool low_byte_only) {
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        uint16_t actual = 0;
        if (read_vcp_value(bridge, vcp, &actual, false) == 0) {
            bool matches = low_byte_only
                ? (actual & 0xff) == (expected & 0xff)
                : actual == expected;
            if (matches) {
                printf("verified VCP 0x%02X = 0x%04X\n", vcp, actual);
                return 0;
            }
            if (attempt == 2) {
                fprintf(stderr,
                        "VCP 0x%02X read-back is 0x%04X, "
                        "expected 0x%04X.\n",
                        vcp,
                        actual,
                        expected);
            }
        }
        usleep(100000);
    }
    return 1;
}

static void print_vcp_value(uint8_t vcp, const uint8_t *response) {
    unsigned maximum =
        ((unsigned)response[6] << 8) | response[7];
    unsigned current =
        ((unsigned)response[8] << 8) | response[9];
    printf("VCP 0x%02X: result=0x%02X, type=%s, "
           "max=0x%04X (%u), current=0x%04X (%u)\n",
           vcp,
           response[3],
           response[5] == 0 ? "continuous" : "non-continuous",
           maximum,
           maximum,
           current,
           current);
}

static int command_get_vcp(uint8_t vcp) {
    Bridge bridge;
    if (open_prepared_bridge(&bridge) != 0) {
        return 1;
    }

    int result;
    uint8_t response[11];
    result = read_vcp(&bridge, vcp, response, true);
    if (result == 0) {
        print_vcp_value(vcp, response);
        if (response[3] != 0x00) {
            result = 1;
        }
    } else {
        fprintf(stderr, "Invalid nested VCP response for 0x%02X.\n", vcp);
    }
    bridge_close(&bridge);
    return result;
}

static int command_probe(void) {
    static const uint8_t advertised_vcp[] = {
        0x02, 0x04, 0x05, 0x08, 0x10, 0x12, 0x14, 0x16, 0x18, 0x1a,
        0x52, 0x60, 0x62, 0x8d, 0xac, 0xae, 0xb2, 0xb6, 0xc6, 0xc8,
        0xc9, 0xcc, 0xd6, 0xdc, 0xdf, 0xe0, 0xe1, 0xe2, 0xe5, 0xe7,
        0xe8, 0xe9, 0xee, 0xef, 0xf1, 0xf2, 0xfd, 0xfe,
    };

    Bridge bridge;
    if (bridge_open(&bridge) != 0) {
        return 1;
    }
    int result = bridge_set_bus_speed_100khz(&bridge);
    if (result != 0) {
        bridge_close(&bridge);
        return result;
    }

    unsigned successful = 0;
    unsigned failed = 0;
    for (size_t i = 0;
         i < sizeof(advertised_vcp) / sizeof(advertised_vcp[0]);
         ++i) {
        uint8_t response[11];
        uint8_t vcp = advertised_vcp[i];
        if (read_vcp(&bridge, vcp, response, false) == 0) {
            print_vcp_value(vcp, response);
            ++successful;
        } else {
            printf("VCP 0x%02X: transport/protocol read failed\n", vcp);
            ++failed;
        }
    }

    printf("probe summary: %u replies, %u transport/protocol failures\n",
           successful,
           failed);
    bridge_close(&bridge);
    return failed == 0 ? 0 : 1;
}

static int command_capabilities(void) {
    Bridge bridge;
    if (bridge_open(&bridge) != 0) {
        return 1;
    }

    int result = bridge_set_bus_speed_100khz(&bridge);
    if (result != 0) {
        bridge_close(&bridge);
        return result;
    }

    char capabilities[16384];
    size_t used = 0;
    uint16_t offset = 0;
    bool complete = false;

    for (unsigned chunk = 0; chunk < 373 && !complete; ++chunk) {
        uint8_t request[] = {
            0x51,
            0x83,
            0xf3,
            (uint8_t)(offset >> 8),
            (uint8_t)(offset & 0xff),
            0x00,
        };
        request[5] = tx_checksum(request, 5);

        uint8_t response[50];
        size_t response_length = 0;
        result = dell_raw_ddcci(
            &bridge,
            request,
            sizeof(request),
            response,
            sizeof(response),
            &response_length,
            false);
        if (result != 0) {
            fprintf(stderr,
                    "Capability read failed at offset %u.\n",
                    offset);
            break;
        }

        if (response_length < 6 || response[0] != 0x6e ||
            response[2] != 0xe3 ||
            response[3] != (uint8_t)(offset >> 8) ||
            response[4] != (uint8_t)(offset & 0xff)) {
            print_bytes(
                "invalid capability response: ",
                response,
                response_length);
            result = 1;
            break;
        }

        size_t body_length = response[1] & 0x7f;
        size_t message_length = body_length + 3;
        if (body_length < 3 ||
            message_length > response_length ||
            !rx_checksum_valid(response, message_length)) {
            fprintf(stderr,
                    "Invalid capability packet length/checksum: "
                    "body=%zu, received=%zu.\n",
                    body_length,
                    response_length);
            result = 1;
            break;
        }
        size_t data_length = body_length - 3;
        if (data_length == 0) {
            complete = true;
            break;
        }

        for (size_t i = 0; i < data_length; ++i) {
            uint8_t byte = response[5 + i];
            if (byte == 0x00) {
                complete = true;
                break;
            }
            if (used + 1 >= sizeof(capabilities)) {
                fprintf(stderr, "Capability string exceeds local limit.\n");
                result = 1;
                complete = true;
                break;
            }
            capabilities[used++] = (char)byte;
        }

        offset = (uint16_t)(offset + data_length);
    }

    if (result == 0) {
        capabilities[used] = '\0';
        printf("capabilities (%zu bytes):\n%s\n", used, capabilities);
    }

    bridge_close(&bridge);
    return result;
}

static int command_status(void) {
    Bridge bridge;
    if (open_prepared_bridge(&bridge) != 0) {
        return 1;
    }

    uint16_t input = 0;
    uint16_t usb_map = 0;
    uint16_t sub_inputs = 0;
    uint16_t layout = 0;
    int result =
        read_vcp_value(&bridge, 0x60, &input, false) |
        read_vcp_value(&bridge, 0xe7, &usb_map, false) |
        read_vcp_value(&bridge, 0xe8, &sub_inputs, false) |
        read_vcp_value(&bridge, 0xe9, &layout, false);
    bridge_close(&bridge);
    if (result != 0) {
        return 1;
    }

    const char *layout_name = name_for_value(
        U4323QE_LAYOUTS,
        sizeof(U4323QE_LAYOUTS) / sizeof(U4323QE_LAYOUTS[0]),
        layout);
    printf("main input: %s (VCP 0x60 raw 0x%04X)\n",
           source_name((uint16_t)(input & 0xff)),
           input);
    printf("PIP/PBP layout: %s (VCP 0xE9 0x%04X)\n",
           layout_name == NULL ? "unknown" : layout_name,
           layout);
    printf("cached window sources: 1=%s, 2=%s, 3=%s, 4=%s "
           "(VCP 0xE8 0x%04X)\n",
           source_name((uint16_t)(input & 0xff)),
           source_name((uint16_t)(sub_inputs & 0x1f)),
           source_name((uint16_t)((sub_inputs >> 5) & 0x1f)),
           source_name((uint16_t)((sub_inputs >> 10) & 0x1f)),
           sub_inputs);
    printf("USB associations: DP1=USB-C%u, DP2=USB-C%u, "
           "HDMI1=USB-C%u, HDMI2=USB-C%u (VCP 0xE7 0x%04X)\n",
           (unsigned)(((usb_map >> 12) & 0x03) + 1),
           (unsigned)(((usb_map >> 10) & 0x03) + 1),
           (unsigned)(((usb_map >> 8) & 0x03) + 1),
           (unsigned)(((usb_map >> 6) & 0x03) + 1),
           usb_map);
    return 0;
}

static int command_layout_list(void) {
    puts("U4323QE layouts advertised by the connected monitor:");
    for (size_t i = 0;
         i < sizeof(U4323QE_LAYOUTS) / sizeof(U4323QE_LAYOUTS[0]);
         ++i) {
        printf("  %-14s 0x%02X\n",
               U4323QE_LAYOUTS[i].name,
               U4323QE_LAYOUTS[i].value);
    }
    puts("\nThe monitor also advertises 0x01 and 0x02, but current DDPM "
         "does not assign those values a user-facing name.");
    return 0;
}

static int command_set_vcp(uint8_t vcp, uint16_t value) {
    Bridge bridge;
    if (open_prepared_bridge(&bridge) != 0) {
        return 1;
    }

    uint16_t before = 0;
    if (read_vcp_value(&bridge, vcp, &before, false) == 0) {
        printf("before: VCP 0x%02X = 0x%04X\n", vcp, before);
    }

    int result = write_vcp(&bridge, vcp, value, true);
    if (result == 0) {
        printf("monitor acknowledged VCP 0x%02X <- 0x%04X\n",
               vcp,
               value);
    }

    /*
     * E7 may deliberately move this USB bridge to another computer. Power
     * and input changes can similarly make read-back unavailable even after
     * a valid acknowledgement, so the generic command does not make a
     * second mutation or treat a missing read-back as rejection.
     */
    if (result == 0 && vcp != 0xe7 && vcp != 0x60 && vcp != 0xd6) {
        uint16_t after = 0;
        if (read_vcp_value(&bridge, vcp, &after, false) == 0) {
            printf("after:  VCP 0x%02X = 0x%04X\n", vcp, after);
        } else {
            fprintf(stderr,
                    "Write was acknowledged, but read-back is unavailable.\n");
        }
    }
    bridge_close(&bridge);
    return result;
}

static int command_switch_input(uint16_t source, bool verbose) {
    Bridge bridge;
    if (open_prepared_bridge(&bridge) != 0) {
        return 1;
    }

    int result = write_vcp(&bridge, 0x60, source, verbose);
    bridge_close(&bridge);
    if (result == 0) {
        printf("input switch accepted: %s (0x%02X)\n",
               source_name(source),
               source);
    } else {
        fprintf(stderr,
                "The input packet was sent, but no valid acknowledgement "
                "arrived; the USB hub may already have moved.\n");
    }
    return result;
}

static int command_kvm_next(void) {
    Bridge bridge;
    if (open_prepared_bridge(&bridge) != 0) {
        return 1;
    }
    int result = write_vcp(&bridge, 0xe7, 0xff00, true);
    bridge_close(&bridge);
    if (result == 0) {
        puts("monitor accepted the request to advance the USB upstream");
    } else {
        fputs("The request was sent, but acknowledgement was lost; "
              "the USB bridge may already have switched computers.\n",
              stderr);
    }
    return result;
}

static int command_kvm_map(
    uint8_t dp1, uint8_t dp2, uint8_t hdmi1, uint8_t hdmi2) {
    uint16_t value =
        (uint16_t)(((uint16_t)(dp1 - 1) << 12) |
                   ((uint16_t)(dp2 - 1) << 10) |
                   ((uint16_t)(hdmi1 - 1) << 8) |
                   ((uint16_t)(hdmi2 - 1) << 6));

    Bridge bridge;
    if (open_prepared_bridge(&bridge) != 0) {
        return 1;
    }
    int result = write_vcp(&bridge, 0xe7, value, true);
    bridge_close(&bridge);
    if (result == 0) {
        printf("USB map accepted: DP1=USB-C%u, DP2=USB-C%u, "
               "HDMI1=USB-C%u, HDMI2=USB-C%u (0x%04X)\n",
               dp1,
               dp2,
               hdmi1,
               hdmi2,
               value);
    }
    return result;
}

static int command_pbp_layout(uint16_t layout) {
    Bridge bridge;
    if (open_prepared_bridge(&bridge) != 0) {
        return 1;
    }
    int result = write_vcp(&bridge, 0xe9, layout, true);
    if (result == 0) {
        result = verify_vcp_value(&bridge, 0xe9, layout, false);
    }
    bridge_close(&bridge);
    return result;
}

static int command_pbp_source(unsigned position, uint16_t source) {
    Bridge bridge;
    if (open_prepared_bridge(&bridge) != 0) {
        return 1;
    }

    uint8_t vcp = position == 1 ? 0x60 : 0xe8;
    uint16_t value = source;
    if (position > 1) {
        if (read_vcp_value(&bridge, 0xe8, &value, false) != 0) {
            bridge_close(&bridge);
            return 1;
        }
        unsigned shift = (position - 2) * 5;
        value = (uint16_t)(
            (value & (uint16_t)~((uint16_t)0x1f << shift)) |
            ((source & 0x1f) << shift));
    }

    int result = write_vcp(&bridge, vcp, value, true);
    if (result == 0) {
        result = verify_vcp_value(
            &bridge, vcp, value, position == 1);
    }
    bridge_close(&bridge);
    return result;
}

static int command_video_swap(unsigned first, unsigned second) {
    if (first == second) {
        fprintf(stderr, "Video swap positions must be different.\n");
        return 2;
    }
    uint16_t value = (uint16_t)(
        0xf000 | ((first - 1) << 4) | (second - 1));

    Bridge bridge;
    if (open_prepared_bridge(&bridge) != 0) {
        return 1;
    }
    int result = write_vcp(&bridge, 0xe5, value, true);
    bridge_close(&bridge);
    if (result == 0) {
        printf("video swap accepted: window %u <-> window %u\n",
               first,
               second);
    }
    return result;
}

static int parse_byte(const char *text, uint8_t *value) {
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 0);
    if (text[0] == '\0' || end == NULL || *end != '\0' || parsed > 0xff) {
        fprintf(stderr,
                "Expected a byte value such as 0x10; got '%s'.\n",
                text);
        return 1;
    }
    *value = (uint8_t)parsed;
    return 0;
}

static int parse_u16(const char *text, uint16_t *value) {
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 0);
    if (text[0] == '\0' || end == NULL || *end != '\0' ||
        parsed > 0xffff) {
        fprintf(stderr,
                "Expected a 16-bit value such as 0xFF00; got '%s'.\n",
                text);
        return 1;
    }
    *value = (uint16_t)parsed;
    return 0;
}

static int parse_position(const char *text, unsigned *position) {
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 10);
    if (text[0] == '\0' || end == NULL || *end != '\0' ||
        parsed < 1 || parsed > 4) {
        fprintf(stderr, "Expected a window position from 1 to 4.\n");
        return 1;
    }
    *position = (unsigned)parsed;
    return 0;
}

static int parse_usb_upstream(const char *text, uint8_t *upstream) {
    const char *number = text;
    if (strncmp(text, "usb-c", 5) == 0) {
        number = text + 5;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(number, &end, 10);
    if (number[0] == '\0' || end == NULL || *end != '\0' ||
        parsed < 1 || parsed > 4) {
        fprintf(stderr,
                "Expected USB upstream 1..4 or usb-c1..usb-c4; "
                "got '%s'.\n",
                text);
        return 1;
    }
    *upstream = (uint8_t)parsed;
    return 0;
}

static void perform_hotkey_switch(void *info) {
    HotkeyContext *context = info;
    printf("Ctrl-A: switching to %s...\n",
           source_name(context->target_source));
    fflush(stdout);
    (void)command_switch_input(context->target_source, false);
    context->switching = false;
}

static CGEventRef hotkey_callback(
    CGEventTapProxy proxy,
    CGEventType type,
    CGEventRef event,
    void *refcon) {
    (void)proxy;
    HotkeyContext *context = refcon;

    if (type == kCGEventTapDisabledByTimeout ||
        type == kCGEventTapDisabledByUserInput) {
        CGEventTapEnable(context->event_tap, true);
        return event;
    }

    CGEventFlags relevant_flags =
        kCGEventFlagMaskControl |
        kCGEventFlagMaskShift |
        kCGEventFlagMaskAlternate |
        kCGEventFlagMaskCommand;
    CGEventFlags flags = CGEventGetFlags(event) & relevant_flags;
    int64_t keycode =
        CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    bool ctrl_a =
        keycode == 0 && flags == kCGEventFlagMaskControl;
    if (!ctrl_a) {
        return event;
    }

    if (type == kCGEventKeyDown) {
        bool autorepeat = CGEventGetIntegerValueField(
            event, kCGKeyboardEventAutorepeat) != 0;
        if (!autorepeat && !context->switching) {
            context->switching = true;
            CFRunLoopSourceSignal(context->switch_source);
            CFRunLoopWakeUp(context->run_loop);
        }
    }

    /* Ctrl-A is the requested global hotkey, so do not pass it to the app. */
    return NULL;
}

static int command_hotkey(uint16_t target_source) {
    const void *keys[] = {kAXTrustedCheckOptionPrompt};
    const void *values[] = {kCFBooleanTrue};
    CFDictionaryRef options = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    bool trusted = options != NULL &&
        AXIsProcessTrustedWithOptions(options);
    if (options != NULL) {
        CFRelease(options);
    }
    if (!trusted) {
        fputs("macOS has not yet authorized this locally built binary for "
              "Accessibility input monitoring. Approve dellctl in "
              "System Settings > Privacy & Security > Accessibility, "
              "then run the command again.\n",
              stderr);
        return 1;
    }

    HotkeyContext context;
    memset(&context, 0, sizeof(context));
    context.run_loop = CFRunLoopGetCurrent();
    context.target_source = target_source;

    CFRunLoopSourceContext source_context;
    memset(&source_context, 0, sizeof(source_context));
    source_context.info = &context;
    source_context.perform = perform_hotkey_switch;
    context.switch_source = CFRunLoopSourceCreate(
        kCFAllocatorDefault, 0, &source_context);
    if (context.switch_source == NULL) {
        fputs("Could not create the hotkey switch source.\n", stderr);
        return 1;
    }

    CGEventMask mask =
        CGEventMaskBit(kCGEventKeyDown) |
        CGEventMaskBit(kCGEventKeyUp);
    context.event_tap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionDefault,
        mask,
        hotkey_callback,
        &context);
    if (context.event_tap == NULL) {
        fputs("Could not create a keyboard event tap. Check Accessibility "
              "permission and run the command again.\n",
              stderr);
        CFRelease(context.switch_source);
        return 1;
    }

    CFRunLoopSourceRef tap_source = CFMachPortCreateRunLoopSource(
        kCFAllocatorDefault, context.event_tap, 0);
    if (tap_source == NULL) {
        fputs("Could not attach the keyboard event tap.\n", stderr);
        CFRelease(context.event_tap);
        CFRelease(context.switch_source);
        return 1;
    }

    CFRunLoopAddSource(
        context.run_loop, context.switch_source, kCFRunLoopDefaultMode);
    CFRunLoopAddSource(
        context.run_loop, tap_source, kCFRunLoopCommonModes);
    CGEventTapEnable(context.event_tap, true);

    printf("Ctrl-A hotkey active; target input is %s (0x%02X).\n",
           source_name(target_source),
           target_source);
    puts("This process must run on both computers, targeting the other "
         "computer's input. Press Ctrl-C to stop it.");
    fflush(stdout);
    CFRunLoopRun();

    CFRunLoopRemoveSource(
        context.run_loop, tap_source, kCFRunLoopCommonModes);
    CFRunLoopRemoveSource(
        context.run_loop,
        context.switch_source,
        kCFRunLoopDefaultMode);
    CFRelease(tap_source);
    CFRelease(context.event_tap);
    CFRelease(context.switch_source);
    return 0;
}

static int command_self_test(void) {
    static const uint8_t get_brightness[] = {0x51, 0x82, 0x01, 0x10};
    if (tx_checksum(get_brightness, sizeof(get_brightness)) != 0xac) {
        fprintf(stderr, "TX checksum self-test failed.\n");
        return 1;
    }

    static const uint8_t brightness_reply[] = {
        0x6e, 0x88, 0x02, 0x00, 0x10, 0x00,
        0x00, 0x64, 0x00, 0x32, 0xf2};
    if (!rx_checksum_valid(
            brightness_reply, sizeof(brightness_reply))) {
        fprintf(stderr, "RX checksum self-test failed.\n");
        return 1;
    }

    static const uint8_t set_brightness[] = {
        0x51, 0x84, 0x03, 0x10, 0x00, 0x32};
    if (tx_checksum(set_brightness, sizeof(set_brightness)) != 0x9a) {
        fprintf(stderr, "Set VCP checksum self-test failed.\n");
        return 1;
    }

    uint16_t usb_map = (uint16_t)(
        ((2 - 1) << 12) |
        ((2 - 1) << 10) |
        ((3 - 1) << 8) |
        ((2 - 1) << 6));
    if (usb_map != 0x1640) {
        fprintf(stderr, "USB association encoding self-test failed.\n");
        return 1;
    }

    uint16_t layout = 0;
    if (value_for_name(
            U4323QE_LAYOUTS,
            sizeof(U4323QE_LAYOUTS) / sizeof(U4323QE_LAYOUTS[0]),
            "side-by-side",
            &layout) != 0 ||
        layout != 0x24) {
        fprintf(stderr, "PIP/PBP layout map self-test failed.\n");
        return 1;
    }

    puts("self-test: passed");
    return 0;
}

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "Usage:\n"
            "  %s list\n"
            "  %s brightness\n"
            "  %s get-vcp <opcode>\n"
            "  %s capabilities\n"
            "  %s probe\n"
            "  %s status\n"
            "  %s layout-list\n"
            "  %s self-test\n"
            "\n"
            "Explicitly enabled writes:\n"
            "  %s set-vcp --enable-writes <opcode> <value>\n"
            "  %s switch-input --enable-writes "
            "<usb-c|dp1|dp2|hdmi1|hdmi2>\n"
            "  %s hotkey --enable-writes <target-source>\n"
            "  %s kvm-next --enable-writes\n"
            "  %s kvm-map --enable-writes "
            "<dp1-usb> <dp2-usb> <hdmi1-usb> <hdmi2-usb>\n"
            "  %s pbp-layout --enable-writes <layout-name>\n"
            "  %s pbp-source --enable-writes "
            "<window-1..4> <source>\n"
            "  %s video-swap --enable-writes <window-1..4> <window-1..4>\n"
            "\n"
            "Every setting command requires the literal --enable-writes. "
            "Run layout-list for the U4323QE layout names.\n",
            program,
            program,
            program,
            program,
            program,
            program,
            program,
            program,
            program,
            program,
            program,
            program,
            program,
            program,
            program,
            program);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        return command_list();
    }
    if (argc == 2 && strcmp(argv[1], "brightness") == 0) {
        return command_brightness();
    }
    if (argc == 3 && strcmp(argv[1], "get-vcp") == 0) {
        uint8_t vcp = 0;
        if (parse_byte(argv[2], &vcp) != 0) {
            return 2;
        }
        return command_get_vcp(vcp);
    }
    if (argc == 2 && strcmp(argv[1], "capabilities") == 0) {
        return command_capabilities();
    }
    if (argc == 2 && strcmp(argv[1], "probe") == 0) {
        return command_probe();
    }
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        return command_status();
    }
    if (argc == 2 && strcmp(argv[1], "layout-list") == 0) {
        return command_layout_list();
    }
    if (argc == 2 && strcmp(argv[1], "self-test") == 0) {
        return command_self_test();
    }
    if (argc == 5 && strcmp(argv[1], "set-vcp") == 0 &&
        strcmp(argv[2], "--enable-writes") == 0) {
        uint8_t vcp = 0;
        uint16_t value = 0;
        if (parse_byte(argv[3], &vcp) != 0 ||
            parse_u16(argv[4], &value) != 0) {
            return 2;
        }
        return command_set_vcp(vcp, value);
    }
    if (argc == 4 && strcmp(argv[1], "switch-input") == 0 &&
        strcmp(argv[2], "--enable-writes") == 0) {
        uint16_t source = 0;
        if (value_for_name(
                U4323QE_SOURCES,
                sizeof(U4323QE_SOURCES) / sizeof(U4323QE_SOURCES[0]),
                argv[3],
                &source) != 0) {
            fprintf(stderr, "Unknown U4323QE source '%s'.\n", argv[3]);
            return 2;
        }
        return command_switch_input(source, true);
    }
    if (argc == 4 && strcmp(argv[1], "hotkey") == 0 &&
        strcmp(argv[2], "--enable-writes") == 0) {
        uint16_t source = 0;
        if (value_for_name(
                U4323QE_SOURCES,
                sizeof(U4323QE_SOURCES) / sizeof(U4323QE_SOURCES[0]),
                argv[3],
                &source) != 0) {
            fprintf(stderr, "Unknown U4323QE source '%s'.\n", argv[3]);
            return 2;
        }
        return command_hotkey(source);
    }
    if (argc == 3 && strcmp(argv[1], "kvm-next") == 0 &&
        strcmp(argv[2], "--enable-writes") == 0) {
        return command_kvm_next();
    }
    if (argc == 7 && strcmp(argv[1], "kvm-map") == 0 &&
        strcmp(argv[2], "--enable-writes") == 0) {
        uint8_t upstreams[4];
        for (size_t i = 0; i < 4; ++i) {
            if (parse_usb_upstream(argv[i + 3], &upstreams[i]) != 0) {
                return 2;
            }
        }
        return command_kvm_map(
            upstreams[0], upstreams[1], upstreams[2], upstreams[3]);
    }
    if (argc == 4 && strcmp(argv[1], "pbp-layout") == 0 &&
        strcmp(argv[2], "--enable-writes") == 0) {
        uint16_t layout = 0;
        if (value_for_name(
                U4323QE_LAYOUTS,
                sizeof(U4323QE_LAYOUTS) / sizeof(U4323QE_LAYOUTS[0]),
                argv[3],
                &layout) != 0) {
            fprintf(stderr,
                    "Unknown or unadvertised U4323QE layout '%s'.\n",
                    argv[3]);
            return 2;
        }
        return command_pbp_layout(layout);
    }
    if (argc == 5 && strcmp(argv[1], "pbp-source") == 0 &&
        strcmp(argv[2], "--enable-writes") == 0) {
        unsigned position = 0;
        uint16_t source = 0;
        if (parse_position(argv[3], &position) != 0) {
            return 2;
        }
        if (value_for_name(
                U4323QE_SOURCES,
                sizeof(U4323QE_SOURCES) / sizeof(U4323QE_SOURCES[0]),
                argv[4],
                &source) != 0) {
            fprintf(stderr, "Unknown U4323QE source '%s'.\n", argv[4]);
            return 2;
        }
        return command_pbp_source(position, source);
    }
    if (argc == 5 && strcmp(argv[1], "video-swap") == 0 &&
        strcmp(argv[2], "--enable-writes") == 0) {
        unsigned first = 0;
        unsigned second = 0;
        if (parse_position(argv[3], &first) != 0 ||
            parse_position(argv[4], &second) != 0) {
            return 2;
        }
        return command_video_swap(first, second);
    }

    if (argc >= 2 &&
        (strcmp(argv[1], "set-vcp") == 0 ||
         strcmp(argv[1], "switch-input") == 0 ||
         strcmp(argv[1], "hotkey") == 0 ||
         strcmp(argv[1], "kvm-next") == 0 ||
         strcmp(argv[1], "kvm-map") == 0 ||
         strcmp(argv[1], "pbp-layout") == 0 ||
         strcmp(argv[1], "pbp-source") == 0 ||
         strcmp(argv[1], "video-swap") == 0)) {
        fputs("Setting commands require the literal --enable-writes in "
              "the position shown below.\n\n",
              stderr);
    }

    usage(stderr, argv[0]);
    return 2;
}
