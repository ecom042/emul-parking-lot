/* Student starter: diagnostics and safe initial outputs, not a solution.
 * Implement the controller with Zephyr GPIO, ADC, UART and SPI APIs.
 * Both lanes start with the motor disabled and the red signal on.
 * The simulator never interprets these messages as control commands.
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/** Outputs of one lane, held inactive except for the red signal. */
struct lane_outputs {
        const char *name;
        struct gpio_dt_spec enable;
        struct gpio_dt_spec direction;
        struct gpio_dt_spec red;
        struct gpio_dt_spec green;
        struct gpio_dt_spec buzzer;
};

/* Every device of the parking_lot_u5 board has an alias
 * (hardware/boards/ufal/parking_lot_u5/parking_lot_u5.dts).
 */
static const struct lane_outputs lanes[] = {
        {
                .name = "entry",
                .enable = GPIO_DT_SPEC_GET(DT_ALIAS(entry_motor_enable), gpios),
                .direction = GPIO_DT_SPEC_GET(DT_ALIAS(entry_motor_direction), gpios),
                .red = GPIO_DT_SPEC_GET(DT_ALIAS(entry_light_red), gpios),
                .green = GPIO_DT_SPEC_GET(DT_ALIAS(entry_light_green), gpios),
                .buzzer = GPIO_DT_SPEC_GET(DT_ALIAS(entry_buzzer), gpios),
        },
        {
                .name = "exit",
                .enable = GPIO_DT_SPEC_GET(DT_ALIAS(exit_motor_enable), gpios),
                .direction = GPIO_DT_SPEC_GET(DT_ALIAS(exit_motor_direction), gpios),
                .red = GPIO_DT_SPEC_GET(DT_ALIAS(exit_light_red), gpios),
                .green = GPIO_DT_SPEC_GET(DT_ALIAS(exit_light_green), gpios),
                .buzzer = GPIO_DT_SPEC_GET(DT_ALIAS(exit_buzzer), gpios),
        },
};

/** Configure one lane's outputs inactive, then assert red. */
static int lane_setup(const struct lane_outputs *lane)
{
        const struct gpio_dt_spec *outputs[] = {
                &lane->enable, &lane->direction, &lane->red, &lane->green, &lane->buzzer
        };

        for (size_t i = 0; i < ARRAY_SIZE(outputs); ++i) {
                if (!gpio_is_ready_dt(outputs[i]) ||
                    gpio_pin_configure_dt(outputs[i], GPIO_OUTPUT_INACTIVE) < 0) {
                        printk("PARKING_STUDENT_SETUP_FAILED: %s\n", lane->name);
                        return -EIO;
                }
        }
        return gpio_pin_set_dt(&lane->red, 1);
}

int main(void)
{
        for (size_t i = 0; i < ARRAY_SIZE(lanes); ++i) {
                if (lane_setup(&lanes[i]) < 0) {
                        return 0;
                }
        }
        printk("PARKING_STUDENT_READY: controller not implemented (capacity %d)\n",
               CONFIG_PARKING_CAPACITY);
        for (;;) {
                /* TODO: service both lanes' safety deadlines without blocking. */
                k_sleep(K_MSEC(10));
        }
        return 0;
}
