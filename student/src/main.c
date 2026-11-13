/* Student starter: diagnostics and safe initial outputs, not a solution.
 * Implement the controller with Zephyr GPIO, ADC, UART and SPI APIs.
 * The simulator never interprets these messages as control commands.
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define USER DT_PATH(zephyr_user)
static const struct gpio_dt_spec enable = GPIO_DT_SPEC_GET(USER, enable_gpios);
static const struct gpio_dt_spec direction = GPIO_DT_SPEC_GET(USER, direction_gpios);
static const struct gpio_dt_spec red = GPIO_DT_SPEC_GET(USER, red_gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(USER, green_gpios);
static const struct gpio_dt_spec buzzer = GPIO_DT_SPEC_GET(USER, buzzer_gpios);

int main(void)
{
        const struct gpio_dt_spec *outputs[] = {
                &enable, &direction, &red, &green, &buzzer
        };

        for (size_t i = 0; i < ARRAY_SIZE(outputs); ++i) {
                if (!gpio_is_ready_dt(outputs[i]) ||
                    gpio_pin_configure_dt(outputs[i], GPIO_OUTPUT_INACTIVE) < 0) {
                        printk("PARKING_STUDENT_SETUP_FAILED\n");
                        return 0;
                }
        }
        gpio_pin_set_dt(&red, 1);
        printk("PARKING_STUDENT_READY: controller not implemented\n");
        for (;;) {
                /* TODO: service safety deadlines without blocking on devices. */
                k_sleep(K_MSEC(10));
        }
        return 0;
}
