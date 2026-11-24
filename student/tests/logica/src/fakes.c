#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, hw_disp_reset, bool);
DEFINE_FAKE_VALUE_FUNC(int, hw_disp_write, bool, uint8_t *, size_t);
DEFINE_FAKE_VOID_FUNC(hw_sleep_ms, int32_t);
DEFINE_FAKE_VALUE_FUNC(int, hw_adc_start, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, hw_adc_check, uint16_t *);

void fakes_reset(void)
{
	RESET_FAKE(hw_disp_reset);
	RESET_FAKE(hw_disp_write);
	RESET_FAKE(hw_sleep_ms);
	RESET_FAKE(hw_adc_start);
	RESET_FAKE(hw_adc_check);
	FFF_RESET_HISTORY();
}
