#ifndef MENU_H
#define MENU_H

#include "stm32f4xx_hal.h"

/* -------------------------------------------------------------------------
 * Name field length.
 * 12 characters maximum per field, plus null terminator.
 * ------------------------------------------------------------------------- */
#define MENU_NAME_MAX_LEN  12U

/* -------------------------------------------------------------------------
 * Game settings — populated by the menu and consumed by the clock engine.
 * ------------------------------------------------------------------------- */
typedef struct
{
    uint32_t time_per_side_ms;              /* Initial time per player in milliseconds    */
    uint32_t increment_ms;                  /* Fischer increment per move in milliseconds */
    uint8_t  eval_visible;                  /* 1 = VISIBLE, 0 = HIDDEN                    */
    char     white_first[MENU_NAME_MAX_LEN + 1U];
    char     white_last [MENU_NAME_MAX_LEN + 1U];
    char     black_first[MENU_NAME_MAX_LEN + 1U];
    char     black_last [MENU_NAME_MAX_LEN + 1U];
} MENU_Settings;

/* -------------------------------------------------------------------------
 * Menu stage enum — matches the state machine in spec §2.6.
 * MENU_STAGE_DONE signals the menu is complete and RUNNING should begin.
 * ------------------------------------------------------------------------- */
typedef enum
{
    MENU_SELECT_PRESET  = 0,
    MENU_SET_TIME       = 1,
    MENU_SET_INCREMENT  = 2,
    MENU_TOGGLE_EVAL    = 3,
    MENU_ENTER_NAMES    = 4,   /* Four sequential name-entry pages             */
    MENU_READY          = 5,   /* Summary confirmation — click to start        */
    MENU_STAGE_DONE     = 6    /* Sentinel — not a real stage; triggers RUNNING */
} MENU_Stage;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * @brief  Initialise the menu: draw Stage 1 and reset all internal state.
 *         Call once after LCD4 is initialised, before entering the main loop.
 */
void MENU_Init(void);

/**
 * @brief  Poll the joystick and update the menu for the current stage.
 *         Call every main-loop iteration while App_Phase == PHASE_MENU.
 * @return 1 when the menu is complete (settings confirmed through Stage 6),
 *         0 while still navigating.
 */
uint8_t MENU_Update(void);

/**
 * @brief  Return a pointer to the confirmed settings struct.
 *         Valid only after MENU_Update() has returned 1.
 */
const MENU_Settings *MENU_GetSettings(void);

#endif /* MENU_H */
