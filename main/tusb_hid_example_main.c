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

#define APP_BUTTON  (GPIO_NUM_0)  // BOOT Taste auf dem ESP32 Board
#define MAX_TOKENS  10            // Maximale Anzahl an Tokens pro Zeile
#define DELTA_TIME  12         // Wartezeit in ms zwischen Tastendrücken

static const char *TAG = "example";  // Tag für ESP_LOG Ausgaben

// ─────────────────────────────────────────────
// USB / HID DESKRIPTOREN
// ─────────────────────────────────────────────

// Gesamtlänge aller USB Deskriptoren zusammen
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

// HID Report Deskriptor - teilt dem Host mit dass wir eine Tastatur sind
const uint8_t hid_report_descriptor[] =
{
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
};

// String Deskriptoren - Informationen die der Host über das Gerät anzeigt
const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},       // 0: Sprache = Englisch
    "TinyUSB",                  // 1: Hersteller
    "TinyUSB Device",           // 2: Produktname
    "123456",                   // 3: Seriennummer
    "Example HID interface",    // 4: HID Interface Name
};

// Konfigurations-Deskriptor - beschreibt die USB Konfiguration
static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

// ─────────────────────────────────────────────
// TINYUSB CALLBACKS
// Diese Funktionen werden von TinyUSB automatisch aufgerufen
// ─────────────────────────────────────────────

// Wird aufgerufen wenn der Host den HID Report Deskriptor anfordert
// Gibt einfach unseren definierten Deskriptor zurück
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return hid_report_descriptor;
}

// Wird aufgerufen wenn der Host einen Report anfordert
// Wir brauchen das nicht → gibt 0 zurück
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

// Wird aufgerufen wenn der Host einen Report sendet (z.B. Num Lock LED)
// Wir brauchen das nicht → leer lassen
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
}

// ─────────────────────────────────────────────
// ASCII → HID LOOKUP TABELLE (QWERTZ Layout)
// ─────────────────────────────────────────────

// Struct die ein ASCII Zeichen auf einen HID Keycode + Modifier mappt
typedef struct {
    char    ascii;      // Das Zeichen das wir tippen wollen
    uint8_t keycode;    // Physische Taste die gedrückt werden soll
    uint8_t modifier;   // Modifier Taste (SHIFT, ALTGR etc.) - 0 = keine
} AsciiToHid;

// Struct die einen Modifier-Namen auf seinen HID Wert mappt
typedef struct {
    const char *name;   // Name des Modifiers z.B. "CTRL"
    uint8_t modifier;   // HID Modifier Wert
} ModifierMap;

// Tabelle: Modifier-Name → HID Wert
static const ModifierMap modifierMap[] = {
    {"CTRL",  KEYBOARD_MODIFIER_LEFTCTRL},
    {"SHIFT", KEYBOARD_MODIFIER_LEFTSHIFT},
    {"ALT",   KEYBOARD_MODIFIER_LEFTALT},
    {"GUI",   KEYBOARD_MODIFIER_LEFTGUI},
    {"ALTGR", KEYBOARD_MODIFIER_RIGHTALT},
};
#define MODIFIER_MAP_SIZE (sizeof(modifierMap) / sizeof(modifierMap[0]))

// Tabelle: ASCII Zeichen → HID Keycode + Modifier (QWERTZ)
static const AsciiToHid asciiMap[] = {
    // Kleinbuchstaben
    {'a', HID_KEY_A, 0}, {'b', HID_KEY_B, 0}, {'c', HID_KEY_C, 0},
    {'d', HID_KEY_D, 0}, {'e', HID_KEY_E, 0}, {'f', HID_KEY_F, 0},
    {'g', HID_KEY_G, 0}, {'h', HID_KEY_H, 0}, {'i', HID_KEY_I, 0},
    {'j', HID_KEY_J, 0}, {'k', HID_KEY_K, 0}, {'l', HID_KEY_L, 0},
    {'m', HID_KEY_M, 0}, {'n', HID_KEY_N, 0}, {'o', HID_KEY_O, 0},
    {'p', HID_KEY_P, 0}, {'q', HID_KEY_Q, 0}, {'r', HID_KEY_R, 0},
    {'s', HID_KEY_S, 0}, {'t', HID_KEY_T, 0}, {'u', HID_KEY_U, 0},
    {'v', HID_KEY_V, 0}, {'w', HID_KEY_W, 0}, {'x', HID_KEY_X, 0},
    {'y', HID_KEY_Z, 0},  // Y und Z sind auf QWERTZ vertauscht
    {'z', HID_KEY_Y, 0},

    // Großbuchstaben
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

    // Umlaute
    {'ä', HID_KEY_APOSTROPHE,   0},
    {'ö', HID_KEY_SEMICOLON,    0},
    {'ü', HID_KEY_BRACKET_LEFT, 0},
    {'Ä', HID_KEY_APOSTROPHE,   KEYBOARD_MODIFIER_LEFTSHIFT},
    {'Ö', HID_KEY_SEMICOLON,    KEYBOARD_MODIFIER_LEFTSHIFT},
    {'Ü', HID_KEY_BRACKET_LEFT, KEYBOARD_MODIFIER_LEFTSHIFT},
    {'ß', HID_KEY_MINUS,        0},

    // Zahlen
    {'0', HID_KEY_0, 0}, {'1', HID_KEY_1, 0}, {'2', HID_KEY_2, 0},
    {'3', HID_KEY_3, 0}, {'4', HID_KEY_4, 0}, {'5', HID_KEY_5, 0},
    {'6', HID_KEY_6, 0}, {'7', HID_KEY_7, 0}, {'8', HID_KEY_8, 0},
    {'9', HID_KEY_9, 0},

    // Steuerzeichen
    {' ',  HID_KEY_SPACE,  0},
    {'\n', HID_KEY_RETURN, 0},
    {'\t', HID_KEY_TAB,    0},

    // Sonderzeichen ohne Modifier
    {'.',  HID_KEY_PERIOD,           0},
    {',',  HID_KEY_COMMA,            0},
    {'-',  HID_KEY_SLASH,            0},
    {'#',  HID_KEY_BACKSLASH,        0},
    //{'<',  HID_KEY_NON_US_BACKSLASH, 0},
    {'+',  HID_KEY_BRACKET_RIGHT,    0},

    // Sonderzeichen mit SHIFT
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

    // Sonderzeichen mit ALTGR
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
// HILFSFUNKTIONEN
// ─────────────────────────────────────────────

/**
 * sendKey - Sendet einen einzelnen Tastendruck über USB HID
 *
 * Drückt eine Taste (mit optionalem Modifier wie SHIFT/CTRL),
 * wartet kurz, lässt sie wieder los und wartet erneut.
 * DELTA_TIME sorgt dafür dass der Host jeden Tastendruck registriert.
 *
 * @param keycode  Array mit bis zu 6 gleichzeitigen Tasten
 * @param modifier Modifier Byte (CTRL, SHIFT, ALT etc.)
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
 * findModifier - Sucht einen Modifier-Namen in der modifierMap
 *
 * Vergleicht den übergebenen String mit allen Einträgen in modifierMap
 * und gibt den zugehörigen HID Modifier Wert zurück.
 *
 * @param  name    Modifier Name z.B. "CTRL", "SHIFT"
 * @return         HID Modifier Wert, 0 wenn nicht gefunden
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
 * findKeycode - Sucht ein ASCII Zeichen in der asciiMap
 *
 * Gibt den HID Keycode zurück und schreibt den nötigen
 * Modifier über einen Pointer in die aufrufende Funktion zurück.
 *
 * @param  c           Das gesuchte ASCII Zeichen
 * @param  outModifier Pointer wo der Modifier hineingeschrieben wird
 * @return             HID Keycode, 0 wenn nicht gefunden
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
 * tokenizeInput - Zerlegt eine Zeile in einzelne Tokens
 *
 * Trennt den String an Leerzeichen und speichert Zeiger
 * auf die einzelnen Wörter im tokens Array.
 * Achtung: Verändert den originalen String (strtok)!
 *
 * @param  line      Der zu zerlegende String
 * @param  tokens    Array wo die Token-Zeiger gespeichert werden
 * @param  maxTokens Maximale Anzahl an Tokens (Overflow Schutz)
 * @return           Anzahl der gefundenen Tokens
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
// SEND FUNKTIONEN
// ─────────────────────────────────────────────

/**
 * sendChar - Sendet ein einzelnes ASCII Zeichen als Tastendruck
 *
 * Sucht das Zeichen in der asciiMap und sendet den
 * passenden Keycode mit Modifier über sendKey.
 * Unbekannte Zeichen werden ignoriert.
 *
 * @param c Das zu sendende ASCII Zeichen
 */
static void sendChar(char c)
{
    uint8_t modifier = 0;
    uint8_t keycode  = findKeycode(c, &modifier);

    if (keycode == 0) return;  // Zeichen nicht in Tabelle → ignorieren

    uint8_t keycodeArr[6] = {keycode, 0, 0, 0, 0, 0};
    sendKey(keycodeArr, modifier);
}

/**
 * sendString - Sendet einen kompletten String als Tastendrücke
 *
 * Iteriert durch jeden Char des Strings und ruft
 * sendChar für jedes Zeichen auf.
 *
 * @param str Der zu sendende String
 */
static void sendString(const char *str)
{
    for (int i = 0; str[i] != '\0'; i++)
    {
        sendChar(str[i]);
    }
}

/**
 * sendLine - Verarbeitet eine einzelne Zeile und führt den Befehl aus
 *
 * Erkennt anhand des ersten Tokens welcher Befehl ausgeführt werden soll:
 *
 * MOD <zeichen> <MODIFIER>  → Sendet ein Zeichen mit Modifier
 *                              Beispiel: "MOD a CTRL" → CTRL+A
 *
 * Alles andere              → Wird als normaler Text gesendet
 *                              Beispiel: "Hallo Welt" → tippt "Hallo Welt"
 *
 * Zeilen die mit # beginnen werden als Kommentare ignoriert (in sendFile).
 *
 * @param line Die zu verarbeitende Zeile
 */
static void sendLine(char *line)
{
    // Zeilenende Zeichen entfernen damit strcmp korrekt funktioniert
    line[strcspn(line, "\r\n")] = 0;

    // Leere Zeilen ignorieren
    if (line[0] == '\0') return;

    // Kopie erstellen weil strtok den String verändert
    // und wir den Original-String noch für sendString brauchen
    char copy[256];
    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *tokens[MAX_TOKENS];
    int numTokens = tokenizeInput(copy, tokens, MAX_TOKENS);

    if (numTokens == 0) return;

    // ── MOD Befehl ──────────────────────────────────────────
    // Format: MOD <zeichen> <MODIFIER>
    // Sendet ein Zeichen mit einem Modifier (CTRL, SHIFT, ALT, GUI, ALTGR)
    if (strcmp(tokens[0], "MOD") == 0)
    {
        if (numTokens < 3)
        {
            ESP_LOGE(TAG, "MOD braucht 2 Argumente: MOD <zeichen> <modifier>");
            return;
        }

        char c = tokens[1][0];  // Erstes Zeichen von tokens[1]

        // Eigenen Modifier des Zeichens ignorieren
        // weil wir ihn mit dem MOD Befehl überschreiben wollen
        uint8_t ignoredModifier = 0;
        uint8_t keycode = findKeycode(c, &ignoredModifier);

        if (keycode == 0)
        {
            ESP_LOGE(TAG, "Zeichen '%c' nicht in Tabelle", c);
            return;
        }

        uint8_t modifier = findModifier(tokens[2]);
        if (modifier == 0)
        {
            ESP_LOGE(TAG, "Modifier '%s' nicht erkannt", tokens[2]);
            return;
        }

        uint8_t keycodeArr[6] = {keycode, 0, 0, 0, 0, 0};
        sendKey(keycodeArr, modifier);
    }

    else if (strcmp(tokens[0], "RET") == 0)
    {
        uint8_t keycode[6] = {40, 0, 0, 0, 0, 0};
        vTaskDelay(pdMS_TO_TICKS(DELTA_TIME));  // längere Pause damit du siehst was passiert
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycode);
        vTaskDelay(pdMS_TO_TICKS(DELTA_TIME));
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
    }

    else if (strcmp(tokens[0], "WAIT") == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(atoi(tokens[1])));  
    }



    // ── Normaler String ─────────────────────────────────────
    // Alles was kein bekannter Befehl ist wird als Text gesendet
    else
    {
        // Original line verwenden - nicht die tokenisierte Kopie!
        sendString(line);
    }
}

/**
 * app_send_hid_demo - Liest die Skript-Datei und sendet alle Befehle
 *
 * Mountet LittleFS, öffnet example.txt und verarbeitet
 * jede Zeile mit sendLine. Zeilen die mit # beginnen
 * werden als Kommentare übersprungen.
 * Am Ende wird LittleFS wieder ausgehängt.
 */
static void app_send_hid_demo(void)
{
    ESP_LOGI(TAG, "LittleFS wird gemountet");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LittleFS mount fehlgeschlagen: %s", esp_err_to_name(ret));
        return;
    }

    FILE *f = fopen("/littlefs/example.txt", "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Datei konnte nicht geöffnet werden");
        esp_vfs_littlefs_unregister(conf.partition_label);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f) != NULL)
    {
        // Kommentare und Leerzeilen überspringen
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        sendLine(line);
    }

    fclose(f);

    esp_vfs_littlefs_unregister(conf.partition_label);
    ESP_LOGI(TAG, "LittleFS ausgehängt");
}

/**
 * app_main - Einstiegspunkt der Anwendung
 *
 * Initialisiert den BOOT Button (GPIO0) und USB.
 * Wartet bis USB verbunden ist und führt dann
 * einmalig app_send_hid_demo aus.
 * Danach kann der BOOT Button gedrückt werden
 * um das Skript erneut auszuführen.
 */
void app_main(void)
{
    // BOOT Taste konfigurieren
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));

    ESP_LOGI(TAG, "USB wird initialisiert");
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
    ESP_LOGI(TAG, "USB initialisiert");

    while (1)
    {
        if (tud_mounted())
        {
            static bool send_hid_data = true;
            if (send_hid_data)
            {
                app_send_hid_demo();
            }
            // BOOT Taste gedrückt → erneut ausführen
            send_hid_data = !gpio_get_level(APP_BUTTON);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
