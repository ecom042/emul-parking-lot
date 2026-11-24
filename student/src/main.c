/*
 * Parking lot controller: two lanes, each run by its own thread
 * (see fsm/runner.h). main() only starts them and returns; the lane threads
 * keep running. A setup error is reported by runner_init(), which also puts
 * the failed lane in assistance.
 */
#include "fsm/runner.h"

int main(void)
{
	(void)runner_init();
	runner_start();
	return 0;
}
