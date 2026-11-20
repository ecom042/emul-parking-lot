#include "fsm/lane.h"

#include <stddef.h>

void lane_init(struct lane *lane, uint8_t id, const struct lane_def *def, uint8_t initial)
{
	lane->def = def;
	lane->id = id;
	lane->state = initial;
}

uint8_t lane_state(const struct lane *lane)
{
	return lane->state;
}

/* The cell for (state, event), or NULL if either is outside the table. */
static const struct lane_transition *find_cell(const struct lane *lane, uint8_t event)
{
	const struct lane_def *def = lane->def;

	if (lane->state >= def->n_states || event >= def->n_events) {
		return NULL;
	}
	return &def->table[(size_t)lane->state * def->n_events + event];
}

/* Where the cell sends the lane; LANE_STATE_NONE means "ignore the event". */
static uint8_t choose_target(const struct lane *lane, const struct lane_transition *cell)
{
	if (cell->guard == NULL || cell->guard(lane)) {
		return cell->next;
	}
	return cell->otherwise;
}

static void enter(struct lane *lane, uint8_t state)
{
	lane_enter_fn action = lane->def->on_enter[state];

	lane->state = state;
	if (action != NULL) {
		action(lane);
	}
}

bool lane_dispatch(struct lane *lane, uint8_t event)
{
	const struct lane_transition *cell = find_cell(lane, event);

	if (cell == NULL || cell->next == LANE_STATE_NONE) {
		return false;
	}

	uint8_t target = choose_target(lane, cell);

	if (target == LANE_STATE_NONE) {
		return false;
	}
	enter(lane, target);
	return true;
}
