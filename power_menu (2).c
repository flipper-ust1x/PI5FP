#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/view.h>
#include <gui/canvas.h>
#include <input/input.h>
#include <stdlib.h>
#include <stdio.h>

#define ANIMATION_TIMER_PERIOD_MS 50
#define ANIMATION_FRAMES_MAX 20

typedef enum {
    PowerMenuViewMenu,
    PowerMenuViewConfirm,
} PowerMenuView;

typedef enum {
    PowerMenuActionNone,
    PowerMenuActionRestart,
    PowerMenuActionShutdown,
} PowerMenuAction;

typedef struct {
    ViewDispatcher* view_dispatcher;
    Gui* gui;
    View* menu_view;
    View* confirm_view;
    PowerMenuAction pending_action;
    uint32_t animation_frame;
    FuriTimer* animation_timer;
} PowerMenuApp;

// Forward declarations
static void power_menu_draw_menu(Canvas* canvas, void* context);
static void power_menu_draw_confirm(Canvas* canvas, void* context);
static bool power_menu_input_menu(InputEvent* event, void* context);
static bool power_menu_input_confirm(InputEvent* event, void* context);
static void power_menu_animation_tick(void* context);
static void power_menu_led_pulse(void);
static void power_menu_led_reset(void);

// LED pulse animation
static void power_menu_led_pulse(void) {
    furi_hal_light_set(LightRed, 255);
}

static void power_menu_led_reset(void) {
    furi_hal_light_set(LightRed, 0);
}

// Get comprehensive power info
static void power_menu_get_power_info_full(char* buf1, char* buf2, char* buf3, size_t size) {
    uint8_t battery_pct = furi_hal_power_get_pct();
    uint32_t voltage_mv = furi_hal_power_get_battery_voltage_mv();
    uint32_t current_ma = furi_hal_power_get_battery_current_ma();
    float temp = furi_hal_power_get_temperature();
    bool charging = furi_hal_power_is_charging();
    
    snprintf(buf1, size, "Batt: %d%% (%umV)", battery_pct, voltage_mv);
    snprintf(buf2, size, "Current: %umA %s", current_ma, charging ? "CHG" : "");
    snprintf(buf3, size, "Temp: %.1fC", temp);
}

// Menu view draw callback
static void power_menu_draw_menu(Canvas* canvas, void* context) {
    PowerMenuApp* app = (PowerMenuApp*)context;
    furi_assert(app);

    // Draw background
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 64);
    
    // Draw title
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Power Menu");
    
    // Draw separator line
    canvas_draw_line(canvas, 0, 12, 128, 12);
    
    // Draw menu options
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 10, 18, AlignLeft, AlignTop, "\x1b UP: Restart");
    canvas_draw_str_aligned(canvas, 10, 28, AlignLeft, AlignTop, "\x1b DOWN: Shutdown");
    
    // Draw separator line
    canvas_draw_line(canvas, 0, 38, 128, 38);
    
    // Get power info
    char line1[64], line2[64], line3[64];
    power_menu_get_power_info_full(line1, line2, line3, sizeof(line1));
    
    // Draw power info at bottom
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 2, 42, AlignLeft, AlignTop, line1);
    canvas_draw_str_aligned(canvas, 2, 50, AlignLeft, AlignTop, line2);
    canvas_draw_str_aligned(canvas, 2, 58, AlignLeft, AlignTop, line3);
    
    // Draw exit hint
    canvas_draw_str_aligned(canvas, 126, 62, AlignRight, AlignBottom, "BACK");
}

// Confirmation view draw callback
static void power_menu_draw_confirm(Canvas* canvas, void* context) {
    PowerMenuApp* app = (PowerMenuApp*)context;
    furi_assert(app);

    // Draw background
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 64);
    
    // Draw title based on action
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    const char* title = (app->pending_action == PowerMenuActionRestart) ? 
                        "Restarting..." : "Shutting Down...";
    canvas_draw_str_aligned(canvas, 64, 4, AlignCenter, AlignTop, title);
    
    // Draw separator line
    canvas_draw_line(canvas, 0, 14, 128, 14);
    
    // Draw progress bar background
    int bar_y = 26;
    int bar_height = 10;
    int bar_width = 110;
    int bar_x = (128 - bar_width) / 2;
    
    canvas_draw_frame(canvas, bar_x, bar_y, bar_width, bar_height);
    
    // Draw animated progress bar
    uint32_t animation_progress = (app->animation_frame * 5);
    if (animation_progress > 100) animation_progress = 100;
    int filled_width = (bar_width - 2) * (int)animation_progress / 100;
    canvas_draw_box(canvas, bar_x + 1, bar_y + 1, filled_width, bar_height - 2);
    
    // Draw percentage
    canvas_set_font(canvas, FontSecondary);
    char percent_str[16];
    snprintf(percent_str, sizeof(percent_str), "%lu%%", animation_progress);
    canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignTop, percent_str);
    
    // Draw separator line
    canvas_draw_line(canvas, 0, 50, 128, 50);
    
    // Draw cancel hint
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignTop, "BACK to cancel");
}

// Input callback for menu view
static bool power_menu_input_menu(InputEvent* event, void* context) {
    PowerMenuApp* app = (PowerMenuApp*)context;
    furi_assert(app);
    furi_assert(event);

    if (event->type == InputTypePress) {
        if (event->key == InputKeyUp) {
            // Switch to Restart action
            app->pending_action = PowerMenuActionRestart;
            app->animation_frame = 0;
            power_menu_led_pulse();
            view_dispatcher_switch_to_view(app->view_dispatcher, PowerMenuViewConfirm);
            furi_timer_start(app->animation_timer, ANIMATION_TIMER_PERIOD_MS);
            return true;
        } else if (event->key == InputKeyDown) {
            // Switch to Shutdown action
            app->pending_action = PowerMenuActionShutdown;
            app->animation_frame = 0;
            power_menu_led_pulse();
            view_dispatcher_switch_to_view(app->view_dispatcher, PowerMenuViewConfirm);
            furi_timer_start(app->animation_timer, ANIMATION_TIMER_PERIOD_MS);
            return true;
        } else if (event->key == InputKeyBack) {
            // Exit application
            power_menu_led_reset();
            view_dispatcher_stop(app->view_dispatcher);
            return true;
        }
    }
    return false;
}

// Input callback for confirmation view
static bool power_menu_input_confirm(InputEvent* event, void* context) {
    PowerMenuApp* app = (PowerMenuApp*)context;
    furi_assert(app);
    furi_assert(event);

    if (event->type == InputTypePress) {
        if (event->key == InputKeyBack) {
            // Cancel action - switch back to menu
            furi_timer_stop(app->animation_timer);
            app->animation_frame = 0;
            app->pending_action = PowerMenuActionNone;
            power_menu_led_reset();
            view_dispatcher_switch_to_view(app->view_dispatcher, PowerMenuViewMenu);
            return true;
        }
    }
    return false;
}

// Animation timer callback
static void power_menu_animation_tick(void* context) {
    PowerMenuApp* app = (PowerMenuApp*)context;
    furi_assert(app);

    app->animation_frame++;
    
    // When animation reaches 100%, execute the action
    if (app->animation_frame >= ANIMATION_FRAMES_MAX) {
        furi_timer_stop(app->animation_timer);
        power_menu_led_reset();
        
        if (app->pending_action == PowerMenuActionRestart) {
            furi_hal_power_reset();
        } else if (app->pending_action == PowerMenuActionShutdown) {
            furi_hal_power_off();
        }
    }
}

// Create and allocate app resources
static PowerMenuApp* power_menu_app_alloc(void) {
    PowerMenuApp* app = malloc(sizeof(PowerMenuApp));
    furi_assert(app);
    
    // Open GUI
    app->gui = furi_record_open(RECORD_GUI);
    furi_assert(app->gui);
    
    // Create view dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    furi_assert(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    
    // Create menu view
    app->menu_view = view_alloc();
    furi_assert(app->menu_view);
    view_set_draw_callback(app->menu_view, power_menu_draw_menu);
    view_set_input_callback(app->menu_view, power_menu_input_menu);
    view_set_context(app->menu_view, app);
    view_dispatcher_add_view(app->view_dispatcher, PowerMenuViewMenu, app->menu_view);
    
    // Create confirmation view
    app->confirm_view = view_alloc();
    furi_assert(app->confirm_view);
    view_set_draw_callback(app->confirm_view, power_menu_draw_confirm);
    view_set_input_callback(app->confirm_view, power_menu_input_confirm);
    view_set_context(app->confirm_view, app);
    view_dispatcher_add_view(app->view_dispatcher, PowerMenuViewConfirm, app->confirm_view);
    
    // Create animation timer
    app->animation_timer = furi_timer_alloc(power_menu_animation_tick, FuriTimerTypePeriodic, app);
    furi_assert(app->animation_timer);
    
    // Initialize state
    app->pending_action = PowerMenuActionNone;
    app->animation_frame = 0;
    
    return app;
}

// Free app resources
static void power_menu_app_free(PowerMenuApp* app) {
    furi_assert(app);
    
    // Stop and free timer
    if (furi_timer_is_running(app->animation_timer)) {
        furi_timer_stop(app->animation_timer);
    }
    furi_timer_free(app->animation_timer);
    
    // Remove and free views
    view_dispatcher_remove_view(app->view_dispatcher, PowerMenuViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, PowerMenuViewConfirm);
    view_free(app->menu_view);
    view_free(app->confirm_view);
    
    // Free dispatcher and close GUI
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    
    // Reset LED
    power_menu_led_reset();
    
    // Free app struct
    free(app);
}

// Main application entry point
int32_t power_menu_app(void* p) {
    UNUSED(p);
    
    PowerMenuApp* app = power_menu_app_alloc();
    
    // Start with menu view
    view_dispatcher_switch_to_view(app->view_dispatcher, PowerMenuViewMenu);
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    power_menu_app_free(app);
    
    return 0;
}
