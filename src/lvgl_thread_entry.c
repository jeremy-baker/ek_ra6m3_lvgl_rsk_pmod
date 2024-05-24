#include "lvgl_thread.h"
#include "lvgl.h"
#include "lvgl/src/drivers/display/st7735/lv_st7735.h"
#include "stdio.h"

#define DISPLAY_HSIZE_INPUT0  128
#define DISPLAY_VSIZE_INPUT0  128

#define LCD_BUF_LINES         20

#define LCD_RESET           (BSP_IO_PORT_08_PIN_00)
#define LCD_CMD             (BSP_IO_PORT_08_PIN_01)
#define LCD_CS              (BSP_IO_PORT_02_PIN_05)

lv_display_t * g_disp;

void my_lcd_send_cmd(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size, const uint8_t *param, size_t param_size);
void my_lcd_send_color(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size, uint8_t *param, size_t param_size);
void ui_init(lv_display_t *disp);

void lv_example_chart_5_jb(void);

void timer_tick_callback(timer_callback_args_t *p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    lv_tick_inc(1);
}

/* Send short command to the LCD. This function shall wait until the transaction finishes. */
void my_lcd_send_cmd(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size, const uint8_t *param, size_t param_size)
{
    FSP_PARAMETER_NOT_USED(disp);

    fsp_err_t err;

    R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_CMD, BSP_IO_LEVEL_LOW);
    R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_CS,  BSP_IO_LEVEL_LOW);

    err = R_SPI_Write(&g_spi0_ctrl, cmd, cmd_size, SPI_BIT_WIDTH_8_BITS);
    LV_ASSERT(FSP_SUCCESS == err);

    xSemaphoreTake( g_main_semaphore_lcdc, portMAX_DELAY);

    if (param_size)
    {
        R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_CMD, BSP_IO_LEVEL_HIGH);

        err = R_SPI_Write(&g_spi0_ctrl, (void const *)param, param_size, SPI_BIT_WIDTH_8_BITS);
        LV_ASSERT(FSP_SUCCESS == err);

        xSemaphoreTake( g_main_semaphore_lcdc, portMAX_DELAY);
    }
    R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_CS, BSP_IO_LEVEL_HIGH);
}

/* Send large array of pixel data to the LCD. If necessary, this function has to do the byte-swapping. This function can do the transfer in the background. */
void my_lcd_send_color(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size, uint8_t *param, size_t param_size)
{
    FSP_PARAMETER_NOT_USED(disp);

    fsp_err_t err;

    R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_CMD, BSP_IO_LEVEL_LOW);
    R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_CS,  BSP_IO_LEVEL_LOW);

    err = R_SPI_Write(&g_spi0_ctrl, cmd, cmd_size, SPI_BIT_WIDTH_8_BITS);
    LV_ASSERT(FSP_SUCCESS == err);

    xSemaphoreTake( g_main_semaphore_lcdc, portMAX_DELAY);

    if (param_size)
    {
        R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_CMD, BSP_IO_LEVEL_HIGH);

        err = R_SPI_Write(&g_spi0_ctrl, (void const *)param, param_size/2, SPI_BIT_WIDTH_16_BITS);
        LV_ASSERT(FSP_SUCCESS == err);

        xSemaphoreTake( g_main_semaphore_lcdc, portMAX_DELAY);
    }
    R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_CS, BSP_IO_LEVEL_HIGH);

    lv_display_flush_ready(disp);
}

void lcd_spi_callback(spi_callback_args_t * p_args)
{
    BaseType_t xHigherPriorityTaskWoken;
    FSP_PARAMETER_NOT_USED(p_args);

    xSemaphoreGiveFromISR( g_main_semaphore_lcdc, &xHigherPriorityTaskWoken );

    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

/* LVGL Thread entry function */
/* pvParameters contains TaskHandle_t */
void lvgl_thread_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED (pvParameters);
    fsp_err_t err;

        printf("Hello\r\n");

        lv_init();

        err = R_GPT_Open(&g_timer0_ctrl, &g_timer0_cfg);
        LV_ASSERT(FSP_SUCCESS == err);

        err = R_GPT_Start(&g_timer0_ctrl);
        LV_ASSERT(FSP_SUCCESS == err);

        err = R_SPI_Open(&g_spi0_ctrl, &g_spi0_cfg);
        LV_ASSERT(FSP_SUCCESS == err);

        R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_CS,    BSP_IO_LEVEL_HIGH);
        R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_RESET, BSP_IO_LEVEL_HIGH);
        R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_RESET, BSP_IO_LEVEL_LOW);
        vTaskDelay (1);
        R_IOPORT_PinWrite(&g_ioport_ctrl, LCD_RESET, BSP_IO_LEVEL_HIGH);

        //120ms delay based on longest possible delay needed before sending
        //commands when the device is in sleep out mode.
        vTaskDelay (12);

        g_disp = lv_st7735_create(DISPLAY_HSIZE_INPUT0, DISPLAY_VSIZE_INPUT0, LV_LCD_FLAG_BGR,
                                                my_lcd_send_cmd, my_lcd_send_color);

        /* Set display orientation to landscape */
        lv_display_set_rotation(g_disp, LV_DISPLAY_ROTATION_0);

        /* Configure draw buffers, etc. */
        lv_color_t * buf1 = NULL;
        lv_color_t * buf2 = NULL;

        uint32_t buf_size = DISPLAY_HSIZE_INPUT0 * LCD_BUF_LINES * lv_color_format_get_size(lv_display_get_color_format(g_disp));

        buf1 = lv_malloc(buf_size);
        if(buf1 == NULL) {
            LV_ASSERT(0);
        }

        buf2 = lv_malloc(buf_size);
        if(buf2 == NULL) {
            lv_free(buf1);
            LV_ASSERT(0);
        }

        lv_display_set_buffers(g_disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

        //ui_init(g_disp);

        lv_example_chart_5_jb();

        /* TODO: add your own code here */
        while (1)
        {
            lv_timer_handler();
            vTaskDelay (1);
        }
}
