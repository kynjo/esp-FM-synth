#ifndef UI_MENU_H
#define UI_MENU_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PAGE_DASHBOARD = 0, PAGE_ALGORITHM, PAGE_OP1, PAGE_OP1_ENV,
    PAGE_OP2, PAGE_OP2_ENV, PAGE_OP3, PAGE_OP3_ENV,
    PAGE_FILTER, PAGE_LFO, PAGE_SYSTEM, PAGE_COUNT
} menu_page_t;

typedef enum {
    EVT_BTN_PREV = 0,
    EVT_BTN_NEXT,
    EVT_ENC_ROTATE_CW,
    EVT_ENC_ROTATE_CCW,
    EVT_ENC_CLICK,
    EVT_ENC_LONG
} ui_event_type_t;

typedef struct {
    ui_event_type_t type;
    int32_t value;
} ui_event_t;

void ui_menu_init(void);
void ui_menu_handle_event(ui_event_t evt);
void ui_menu_render(void);
void ui_task(void *pvParameters);

#ifdef __cplusplus
}
#endif
#endif
