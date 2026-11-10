#include "board.h"
#include "input.h"
#include "ui.h"
#include "store.h"
#include "esp_event.h"
#include "esp_netif.h"

void app_main(void)
{
    board_early_init();          // power latch first — board stays on
    esp_netif_init();
    esp_event_loop_create_default();
    store_init();
    input_init();
    ui_run();                    // never returns
}
