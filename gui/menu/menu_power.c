#include "menu_power.h"

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vanilla.h>

#include "lang.h"
#include "menu_common.h"
#include "menu_game.h"
#include "ui/ui_anim.h"

static int power_layer = -1;
static int confirm_layer = -1;
static int opened_from_game;
static int power_wiiu_sleep_button = -1;
static int power_cancel_button = -1;
static int confirm_cancel_button = -1;
enum {
    PENDING_POWER_NONE = 0,
    PENDING_POWER_SLEEP,
    PENDING_POWER_HEKATE,
    PENDING_POWER_OFF
};

static _Atomic int pending_power_action;

static void *run_power_helper(void *arg)
{
    const char *action = (const char *)arg;
    char command[128];

    snprintf(command, sizeof(command),
             "/usr/libexec/vanilla-power %s", action);
    (void)system(command);
    return NULL;
}

static void start_power_helper(const char *action)
{
    pthread_t thread;

    if (access("/usr/libexec/vanilla-power", X_OK) != 0)
        return;

    if (pthread_create(&thread, NULL, run_power_helper,
                       (void *)action) == 0)
        pthread_detach(thread);
}

static void hide_power_layers(vui_context_t *vui)
{
    if (power_layer >= 0)
        vui_layer_set_enabled(vui, power_layer, 0);

    if (confirm_layer >= 0)
        vui_layer_set_enabled(vui, confirm_layer, 0);

    vui_layer_set_modal(vui, -1, -1);
    vpi_game_power_overlay_set(vui, 0);
}

static void power_cancel(vui_context_t *vui, int button, void *data)
{
    (void)button;
    (void)data;
    hide_power_layers(vui);
}

static void power_sleep(vui_context_t *vui, int button, void *data)
{
    (void)button;
    (void)data;

    hide_power_layers(vui);

    if (vpi_game_session_is_active()) {
        atomic_store(&pending_power_action, PENDING_POWER_SLEEP);
        vpi_game_return_to_menu();
    } else {
        start_power_helper("sleep");
    }
}

static void *power_wiiu_sleep_worker(void *unused)
{
    (void)unused;

    if (vpi_game_session_is_active()) {
        vanilla_set_button(VANILLA_BTN_POWER, INT16_MAX);
        sleep(2);
        vanilla_set_button(VANILLA_BTN_POWER, 0);

        atomic_store(&pending_power_action, PENDING_POWER_SLEEP);
        vpi_game_return_to_menu();
    } else {
        start_power_helper("sleep");
    }

    return NULL;
}

void vpi_menu_power_main_ready(void)
{
    switch (atomic_exchange(&pending_power_action, PENDING_POWER_NONE)) {
    case PENDING_POWER_SLEEP:
        start_power_helper("sleep");
        break;
    case PENDING_POWER_HEKATE:
        start_power_helper("hekate");
        break;
    case PENDING_POWER_OFF:
        start_power_helper("poweroff");
        break;
    default:
        break;
    }
}

static void power_wiiu_sleep(vui_context_t *vui, int button, void *data)
{
    pthread_t thread;

    (void)button;
    (void)data;

    hide_power_layers(vui);

    if (pthread_create(&thread, NULL,
                       power_wiiu_sleep_worker, NULL) == 0)
        pthread_detach(thread);
}

static void power_vanilla_menu(vui_context_t *vui, int button, void *data)
{
    (void)button;
    (void)data;

    hide_power_layers(vui);

    if (vpi_game_session_is_active())
        vpi_game_return_to_menu();
}

static void power_hekate(vui_context_t *vui, int button, void *data)
{
    (void)button;
    (void)data;

    hide_power_layers(vui);

    if (vpi_game_session_is_active()) {
        atomic_store(&pending_power_action, PENDING_POWER_HEKATE);
        vpi_game_return_to_menu();
    } else {
        start_power_helper("hekate");
    }
}

static void power_off(vui_context_t *vui, int button, void *data)
{
    (void)button;
    (void)data;

    hide_power_layers(vui);

    if (vpi_game_session_is_active()) {
        atomic_store(&pending_power_action, PENDING_POWER_OFF);
        vpi_game_return_to_menu();
    } else {
        start_power_helper("poweroff");
    }
}

static void create_power_layers(vui_context_t *vui)
{
    int scrw;
    int scrh;

    vui_get_screen_size(vui, &scrw, &scrh);

    power_layer = vui_layer_create(vui);
    vui_layer_set_bgcolor(
        vui, power_layer,
        vui_color_create(0.035f, 0.055f, 0.075f, 0.98f));

    const int width = 520;
    const int height = 50;
    const int gap = 8;
    const int button_count = 6;
    const int x = (scrw - width) / 2;
    const int total_height = button_count * height + (button_count - 1) * gap;
    const int y = (scrh - total_height) / 2;

    power_wiiu_sleep_button = vui_button_create(
        vui, x, y + (height + gap) * 0, width, height,
        lang(VPI_LANG_POWER_WIIU_SLEEP), 0,
        VUI_BUTTON_STYLE_BUTTON, power_layer,
        power_wiiu_sleep, NULL);

    vui_button_create(
        vui, x, y + (height + gap) * 1, width, height,
        lang(VPI_LANG_POWER_SLEEP_SWITCH), 0,
        VUI_BUTTON_STYLE_BUTTON, power_layer,
        power_sleep, NULL);

    vui_button_create(
        vui, x, y + (height + gap) * 2, width, height,
        lang(VPI_LANG_POWER_VANILLA_MENU), 0,
        VUI_BUTTON_STYLE_BUTTON, power_layer,
        power_vanilla_menu, NULL);

    vui_button_create(
        vui, x, y + (height + gap) * 3, width, height,
        lang(VPI_LANG_POWER_HEKATE), 0,
        VUI_BUTTON_STYLE_BUTTON, power_layer,
        power_hekate, NULL);

    vui_button_create(
        vui, x, y + (height + gap) * 4, width, height,
        lang(VPI_LANG_POWER_OFF_SWITCH), 0,
        VUI_BUTTON_STYLE_BUTTON, power_layer,
        power_off, NULL);

    power_cancel_button = vui_button_create(
        vui, x, y + (height + gap) * 5, width, height,
        lang(VPI_LANG_CANCEL_BTN), 0,
        VUI_BUTTON_STYLE_BUTTON, power_layer,
        power_cancel, NULL);

    confirm_layer = vui_layer_create(vui);
    vui_layer_set_bgcolor(
        vui, confirm_layer,
        vui_color_create(0.035f, 0.055f, 0.075f, 0.98f));

    vui_label_create(
        vui, scrw / 8, 145, scrw * 3 / 4, 100,
        lang(VPI_LANG_POWER_CONFIRM),
        vui_color_create(1, 1, 1, 1),
        VUI_FONT_SIZE_NORMAL, confirm_layer);

    vui_button_create(
        vui, 92, 320, 410, BTN_SZ,
        lang(VPI_LANG_POWER_WIIU_SLEEP), 0,
        VUI_BUTTON_STYLE_BUTTON, confirm_layer,
        power_wiiu_sleep, NULL);

    confirm_cancel_button = vui_button_create(
        vui, 522, 320, 240, BTN_SZ,
        lang(VPI_LANG_CANCEL_BTN), 0,
        VUI_BUTTON_STYLE_BUTTON, confirm_layer,
        power_cancel, NULL);

    vui_layer_set_enabled(vui, power_layer, 0);
    vui_layer_set_enabled(vui, confirm_layer, 0);
}

static void ensure_power_layers(vui_context_t *vui)
{
    if (power_layer < 0 || confirm_layer < 0)
        create_power_layers(vui);
}

void vpi_menu_power_reset(void)
{
    power_layer = -1;
    confirm_layer = -1;
    power_wiiu_sleep_button = -1;
    power_cancel_button = -1;
    confirm_cancel_button = -1;
}

void vpi_menu_power(vui_context_t *vui)
{
    opened_from_game = vui_game_mode_get(vui);
    vpi_game_power_overlay_set(vui, 1);
    ensure_power_layers(vui);

    vui_layer_set_enabled(vui, confirm_layer, 0);
    vui_layer_set_enabled(vui, power_layer, 1);
    vui_layer_set_modal(vui, power_layer, power_cancel_button);
    vui_button_select(vui, power_wiiu_sleep_button);
    vui_transition_fade_layer_in(vui, power_layer, 0, 0);
}

void vpi_menu_power_confirm(vui_context_t *vui)
{
    opened_from_game = vui_game_mode_get(vui);
    vpi_game_power_overlay_set(vui, 1);
    ensure_power_layers(vui);

    vui_layer_set_enabled(vui, power_layer, 0);
    vui_layer_set_enabled(vui, confirm_layer, 1);
    vui_layer_set_modal(vui, confirm_layer, confirm_cancel_button);
    vui_transition_fade_layer_in(vui, confirm_layer, 0, 0);
}
