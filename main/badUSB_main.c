#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "esp_littlefs.h"

// ─────────────────────────────────────────────
// DEFINES
// ─────────────────────────────────────────────

#define APP_BUTTON  (GPIO_NUM_0)  // BOOT button on the ESP32 board
#define MAX_TOKENS  10            // Maximum number of tokens per line
#define DELTA_TIME  12            // Delay in ms between key presses

static const char *TAG = "example";  // Tag for ESP_LOG output

// ─────────────────────────────────────────────
// USB / HID DESCRIPTORS
// ─────────────────────────────────────────────

// Total length of all USB descriptors combined
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

// HID report descriptor - tells the host that we are a keyboard
const uint8_t hid_report_descriptor[] =
{
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
};

// String descriptors - information displayed by the host about the device
const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},       // 0: Language = English
    "TinyUSB",                  // 1: Manufacturer
    "TinyUSB Device",           // 2: Product name
    "123456",                   // 3: Serial number
    "Example HID interface",    // 4: HID interface name
};

// Configuration descriptor - describes the USB configuration
static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

// ─────────────────────────────────────────────
// TINYUSB CALLBACKS
// These functions are called automatically by TinyUSB
// ─────────────────────────────────────────────

// Called when the host requests the HID report descriptor
// Simply returns our defined descriptor
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return hid_report_descriptor;
}

// Called when the host requests a report
// Not needed here → returns 0
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

// Called when the host sends a report (e.g. Num Lock LED)
// Not needed here → left empty
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
}

// ─────────────────────────────────────────────
// ASCII → HID LOOKUP TABLE (QWERTZ LAYOUT)
// ─────────────────────────────────────────────

// Struct that maps an ASCII character to a HID keycode + modifier
typedef struct {
    char    ascii;      // The character we want to type
    uint8_t keycode;    // Physical key to press
    uint8_t modifier;   // Modifier key (SHIFT, ALTGR etc.) - 0 = none
} AsciiToHid;

// Struct that maps a modifier name to its HID value
typedef struct {
    const char *name;   // Name of the modifier e.g. "CTRL"
    uint8_t modifier;   // HID modifier value
} ModifierMap;

// Table: modifier name → HID value
static const ModifierMap modifierMap[] = {
    {"CTRL",  KEYBOARD_MODIFIER_LEFTCTRL},
    {"SHIFT", KEYBOARD_MODIFIER_LEFTSHIFT},
    {"ALT",   KEYBOARD_MODIFIER_LEFTALT},
    {"GUI",   KEYBOARD_MODIFIER_LEFTGUI},
    {"ALTGR", KEYBOARD_MODIFIER_RIGHTALT},
};
#define MODIFIER_MAP_SIZE (sizeof(modifierMap) / sizeof(modifierMap[0]))

// Table: ASCII character → HID keycode + modifier (QWERTZ)
static const AsciiToHid asciiMap[] = {
    // Lowercase letters
    {'a', HID_KEY_A, 0}, {'b', HID_KEY_B, 0}, {'c', HID_KEY_C, 0},
    {'d', HID_KEY_D, 0}, {'e', HID_KEY_E, 0}, {'f', HID_KEY_F, 0},
    {'g', HID_KEY_G, 0}, {'h', HID_KEY_H, 0}, {'i', HID_KEY_I, 0},
    {'j', HID_KEY_J, 0}, {'k', HID_KEY_K, 0}, {'l', HID_KEY_L, 0},
    {'m', HID_KEY_M, 0}, {'n', HID_KEY_N, 0}, {'o', HID_KEY_O, 0},
    {'p', HID_KEY_P, 0}, {'q', HID_KEY_Q, 0}, {'r', HID_KEY_R, 0},
    {'s', HID_KEY_S, 0}, {'t', HID_KEY_T, 0}, {'u', HID_KEY_U, 0},
    {'v', HID_KEY_V, 0}, {'w', HID_KEY_W, 0}, {'x', HID_KEY_X, 0},
    {'y', HID_KEY_Z, 0},  // Y and Z are swapped on QWERTZ
    {'z', HID_KEY_Y, 0},

    // Uppercase letters
    {'A', HID_KEY_A, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'B', HID_KEY_B, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'C', HID_KEY_C, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'D', HID_KEY_D, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'E', HID_KEY_E, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'F', HID_KEY_F, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'G', HID_KEY_G, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'H', HID_KEY_H, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'I', HID_KEY_I, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'J', HID_KEY_J, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'K', HID_KEY_K, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'L', HID_KEY_L, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'M', HID_KEY_M, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'N', HID_KEY_N, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'O', HID_KEY_O, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'P', HID_KEY_P, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'Q', HID_KEY_Q, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'R', HID_KEY_R, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'S', HID_KEY_S, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'T', HID_KEY_T, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'U', HID_KEY_U, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'V', HID_KEY_V, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'W', HID_KEY_W, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'X', HID_KEY_X, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'Y', HID_KEY_Z, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'Z', HID_KEY_Y, KEYBOARD_MODIFIER_LEFTSHIFT},

    // German umlauts
    {'ä', HID_KEY_APOSTROPHE,   0},
    {'ö', HID_KEY_SEMICOLON,    0},
    {'ü', HID_KEY_BRACKET_LEFT, 0},
    {'Ä', HID_KEY_APOSTROPHE,   KEYBOARD_MODIFIER_LEFTSHIFT},
    {'Ö', HID_KEY_SEMICOLON,    KEYBOARD_MODIFIER_LEFTSHIFT},
    {'Ü', HID_KEY_BRACKET_LEFT, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'ß', HID_KEY_MINUS,        0},

    // Digits
    {'0', HID_KEY_0, 0}, {'1', HID_KEY_1, 0}, {'2', HID_KEY_2, 0},
    {'3', HID_KEY_3, 0}, {'4', HID_KEY_4, 0}, {'5', HID_KEY_5, 0},
    {'6', HID_KEY_6, 0}, {'7', HID_KEY_7, 0}, {'8', HID_KEY_8, 0},
    {'9', HID_KEY_9, 0},

    // Control characters
    {' ',  HID_KEY_SPACE,  0},
    {'\n', HID_KEY_RETURN, 0},
    {'\t', HID_KEY_TAB,    0},

    // Special characters without modifier
    {'.',  HID_KEY_PERIOD,           0},
    {',',  HID_KEY_COMMA,            0},
    {'-',  HID_KEY_SLASH,            0},
    {'#',  HID_KEY_BACKSLASH,        0},
    //{'<',  HID_KEY_NON_US_BACKSLASH, 0},
    {'+',  HID_KEY_BRACKET_RIGHT,    0},

    // Special characters with SHIFT
    {'!',  HID_KEY_1,                KEYBOARD_MODIFIER_LEFTSHIFT},
    {'"',  HID_KEY_2,                KEYBOARD_MODIFIER_LEFTSHIFT},
    {'§',  HID_KEY_3,                KEYBOARD_MODIFIER_LEFTSHIFT},
    {'$',  HID_KEY_4,                KEYBOARD_MODIFIER_LEFTSHIFT},
    {'%',  HID_KEY_5,                KEYBOARD_MODIFIER_LEFTSHIFT},
    {'&',  HID_KEY_6,                KEYBOARD_MODIFIER_LEFTSHIFT},
    {'/',  HID_KEY_7,                KEYBOARD_MODIFIER_LEFTSHIFT},
    {'(',  HID_KEY_8,                KEYBOARD_MODIFIER_LEFTSHIFT},
    {')',  HID_KEY_9,                KEYBOARD_MODIFIER_LEFTSHIFT},
    {'=',  HID_KEY_0,                KEYBOARD_MODIFIER_LEFTSHIFT},
    {'?',  HID_KEY_MINUS,            KEYBOARD_MODIFIER_LEFTSHIFT},
    //{'>',  HID_KEY_NON_US_BACKSLASH, KEYBOARD_MODIFIER_LEFTSHIFT},
    {';',  HID_KEY_COMMA,            KEYBOARD_MODIFIER_LEFTSHIFT},
    {':',  HID_KEY_PERIOD,           KEYBOARD_MODIFIER_LEFTSHIFT},
    {'_',  HID_KEY_SLASH,            KEYBOARD_MODIFIER_LEFTSHIFT},
    {'*',  HID_KEY_BRACKET_RIGHT,    KEYBOARD_MODIFIER_LEFTSHIFT},

    // Special characters with ALTGR
    {'@',  HID_KEY_Q,                KEYBOARD_MODIFIER_RIGHTALT},
    {'€',  HID_KEY_E,                KEYBOARD_MODIFIER_RIGHTALT},
    {'{',  HID_KEY_7,                KEYBOARD_MODIFIER_RIGHTALT},
    {'[',  HID_KEY_8,                KEYBOARD_MODIFIER_RIGHTALT},
    {']',  HID_KEY_9,                KEYBOARD_MODIFIER_RIGHTALT},
    {'}',  HID_KEY_0,                KEYBOARD_MODIFIER_RIGHTALT},
    {'\\', HID_KEY_MINUS,            KEYBOARD_MODIFIER_RIGHTALT},
    {'~',  HID_KEY_BRACKET_RIGHT,    KEYBOARD_MODIFIER_RIGHTALT},
  //  {'|',  HID_KEY_NON_US_BACKSLASH, KEYBOARD_MODIFIER_RIGHTALT},
};
#define ASCII_MAP_SIZE (sizeof(asciiMap) / sizeof(asciiMap[0]))

// ─────────────────────────────────────────────
// HELPER FUNCTIONS
// ─────────────────────────────────────────────

/**
 * sendKey - Sends a single key press over USB HID
 *
 * Presses a key (with optional modifier like SHIFT/CTRL),
 * waits briefly, releases it and waits again.
 * DELTA_TIME ensures the host registers every key press.
 *
 * @param keycode  Array of up to 6 simultaneous keys
 * @param modifier Modifier byte (CTRL, SHIFT, ALT etc.)
 */
static void sendKey(uint8_t keycode[], uint8_t modifier)
{
    vTaskDelay(pdMS_TO_TICKS(DELTA_TIME));
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, keycode);
    vTaskDelay(pdMS_TO_TICKS(DELTA_TIME));
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
    vTaskDelay(pdMS_TO_TICKS(DELTA_TIME));
}

/**
 * findModifier - Looks up a modifier name in the modifierMap
 *
 * Compares the given string against all entries in modifierMap
 * and returns the corresponding HID modifier value.
 *
 * @param  name    Modifier name e.g. "CTRL", "SHIFT"
 * @return         HID modifier value, 0 if not found
 */
static uint8_t findModifier(const char *name)
{
    for (int i = 0; i < MODIFIER_MAP_SIZE; i++)
    {
        if (strcmp(modifierMap[i].name, name) == 0)
        {
            return modifierMap[i].modifier;
        }
    }
    return 0;
}

/**
 * findKeycode - Looks up an ASCII character in the asciiMap
 *
 * Returns the HID keycode and writes the required modifier
 * back to the caller via a pointer.
 *
 * @param  c           The ASCII character to look up
 * @param  outModifier Pointer where the modifier will be written
 * @return             HID keycode, 0 if not found
 */
static uint8_t findKeycode(char c, uint8_t *outModifier)
{
    for (int i = 0; i < ASCII_MAP_SIZE; i++)
    {
        if (asciiMap[i].ascii == c)
        {
            *outModifier = asciiMap[i].modifier;
            return asciiMap[i].keycode;
        }
    }
    return 0;
}

/**
 * tokenizeInput - Splits a line into individual tokens
 *
 * Splits the string at spaces and stores pointers
 * to the individual words in the tokens array.
 * Warning: modifies the original string (strtok)!
 *
 * @param  line      The string to split
 * @param  tokens    Array where token pointers will be stored
 * @param  maxTokens Maximum number of tokens (overflow protection)
 * @return           Number of tokens found
 */
static int tokenizeInput(char *line, char *tokens[], int maxTokens)
{
    int count = 0;
    char *tok = strtok(line, " ");
    while (tok != NULL && count < maxTokens)
    {
        tokens[count++] = tok;
        tok = strtok(NULL, " ");
    }
    return count;
}

// ─────────────────────────────────────────────
// SEND FUNCTIONS
// ─────────────────────────────────────────────

/**
 * sendChar - Sends a single ASCII character as a key press
 *
 * Looks up the character in the asciiMap and sends the
 * matching keycode with modifier via sendKey.
 * Unknown characters are ignored.
 *
 * @param c The ASCII character to send
 */
static void sendChar(char c)
{
    uint8_t modifier = 0;
    uint8_t keycode  = findKeycode(c, &modifier);

    if (keycode == 0) return;  // Character not in table → ignore

    uint8_t keycodeArr[6] = {keycode, 0, 0, 0, 0, 0};
    sendKey(keycodeArr, modifier);
}

/**
 * sendString - Sends a complete string as key presses
 *
 * Iterates through each character of the string and calls
 * sendChar for every character.
 *
 * @param str The string to send
 */
static void sendString(const char *str)
{
    for (int i = 0; str[i] != '\0'; i++)
    {
        sendChar(str[i]);
    }
}

/**
 * sendLine - Processes a single line and executes the command
 *
 * Detects which command to execute based on the first token:
 *
 * MOD <char> <MODIFIER>  → Sends a character with a modifier
 *                          Example: "MOD a CTRL" → CTRL+A
 *
 * RET                    → Sends the Enter/Return key
 *
 * WAIT <ms>              → Waits for the given number of milliseconds
 *
 * Anything else          → Sent as plain text
 *                          Example: "Hello World" → types "Hello World"
 *
 * Lines starting with # are treated as comments (in sendFile).
 *
 * @param line The line to process
 */
static void sendLine(char *line)
{
    // Remove line ending characters so strcmp works correctly
    line[strcspn(line, "\r\n")] = 0;

    // Ignore empty lines
    if (line[0] == '\0') return;

    // Create a copy because strtok modifies the string
    // and we still need the original for sendString
    char copy[256];
    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *tokens[MAX_TOKENS];
    int numTokens = tokenizeInput(copy, tokens, MAX_TOKENS);

    if (numTokens == 0) return;

    // ── MOD command ─────────────────────────────────────────
    // Format: MOD <char> <MODIFIER>
    // Sends a character with a modifier (CTRL, SHIFT, ALT, GUI, ALTGR)
    if (strcmp(tokens[0], "MOD") == 0)
    {
        if (numTokens < 3)
        {
            ESP_LOGE(TAG, "MOD requires 2 arguments: MOD <char> <modifier>");
            return;
        }

        char c = tokens[1][0];  // First character of tokens[1]

        // Ignore the character's own modifier
        // because we want to override it with the MOD command
        uint8_t ignoredModifier = 0;
        uint8_t keycode = findKeycode(c, &ignoredModifier);

        if (keycode == 0)
        {
            ESP_LOGE(TAG, "Character '%c' not found in table", c);
            return;
        }

        uint8_t modifier = findModifier(tokens[2]);
        if (modifier == 0)
        {
            ESP_LOGE(TAG, "Modifier '%s' not recognized", tokens[2]);
            return;
        }

        uint8_t keycodeArr[6] = {keycode, 0, 0, 0, 0, 0};
        sendKey(keycodeArr, modifier);
    }

    // ── RET command ─────────────────────────────────────────
    // Sends the Enter/Return key
    else if (strcmp(tokens[0], "RET") == 0)
    {
        uint8_t keycode[6] = {40, 0, 0, 0, 0, 0};
        vTaskDelay(pdMS_TO_TICKS(DELTA_TIME));
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycode);
        vTaskDelay(pdMS_TO_TICKS(DELTA_TIME));
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
    }

    // ── WAIT command ─────────────────────────────────────────
    // Pauses execution for the given number of milliseconds
    else if (strcmp(tokens[0], "WAIT") == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(atoi(tokens[1])));
    }

    // ── Plain string ─────────────────────────────────────────
    // Everything that is not a known command is sent as text
    else
    {
        // Use the original line - not the tokenized copy!
        sendString(line);
    }
}

/**
 * app_send_hid_demo - Reads the script file and sends all commands
 *
 * Mounts LittleFS, opens example.txt and processes
 * each line with sendLine. Lines starting with # are
 * skipped as comments.
 * LittleFS is unmounted again at the end.
 */
static void app_send_hid_demo(void)
{
    ESP_LOGI(TAG, "Mounting LittleFS");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
        return;
    }

    FILE *f = fopen("/littlefs/example.txt", "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file");
        esp_vfs_littlefs_unregister(conf.partition_label);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f) != NULL)
    {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        sendLine(line);
    }

    fclose(f);

    esp_vfs_littlefs_unregister(conf.partition_label);
    ESP_LOGI(TAG, "LittleFS unmounted");
}

/**
 * app_main - Application entry point
 *
 * Initializes the BOOT button (GPIO0) and USB.
 * Waits until USB is connected, then runs
 * app_send_hid_demo once automatically.
 * After that the BOOT button can be pressed
 * to run the script again.
 */
void app_main(void)
{
    // Configure BOOT button
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));

    ESP_LOGI(TAG, "Initializing USB");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = hid_configuration_descriptor,
        .hs_configuration_descriptor = hid_configuration_descriptor,
        .qualifier_descriptor = NULL,
#else
        .configuration_descriptor = hid_configuration_descriptor,
#endif
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB initialized");

    while (1)
    {
        if (tud_mounted())
        {
            static bool send_hid_data = true;
            if (send_hid_data)
            {
                app_send_hid_demo();
            }
            // BOOT button pressed → run again
            send_hid_data = !gpio_get_level(APP_BUTTON);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}