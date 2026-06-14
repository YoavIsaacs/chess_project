/* menu.c — Pre-game menu: SELECT PRESET, SET TIME, SET INCREMENT, TOGGLE EVAL
 *
 * Navigation:
 *
 * Stage 1 (SELECT PRESET):
 *   UP/DOWN  — move > cursor
 *   CLICK    — confirm, advance to SET TIME
 *
 * Stage 2 (SET TIME):
 *   LEFT/RIGHT — move digit cursor (MM_TENS -> MM_UNITS -> SS_TENS -> SS_UNITS ->
 *                                   BACK -> NEXT; clamped at each end)
 *   UP/DOWN    — increment/decrement active digit (no effect on BACK/NEXT)
 *   CLICK      — on digit: increment active digit (same as UP)
 *                on BACK:  return to SELECT PRESET
 *                on NEXT:  save time and advance to SET INCREMENT
 *
 * Stage 3 (SET INCREMENT):
 *   UP/DOWN  — adjust increment value 0–99 s (always, regardless of nav cursor)
 *   LEFT     — move nav cursor to BACK
 *   RIGHT    — move nav cursor to NEXT
 *   CLICK    — on BACK: return to SET TIME
 *              on NEXT (or value row): save increment, advance to TOGGLE EVAL
 *
 * Stage 4 (TOGGLE EVAL):
 *   UP/DOWN  — move cursor among HIDDEN and VISIBLE rows (nav row not reachable
 *              by UP/DOWN — use LEFT/RIGHT to reach BACK/NEXT)
 *   LEFT     — move to BACK (only from NEXT; ignored elsewhere)
 *   RIGHT    — move to NEXT (only from BACK; ignored elsewhere)
 *   UP from BACK or NEXT — jump back to VISIBLE
 *   DOWN on BACK — no effect
 *   CLICK on HIDDEN  — select HIDDEN (tick moves here), stay
 *   CLICK on VISIBLE — select VISIBLE (tick moves here), stay
 *   CLICK on BACK    — return to SET INCREMENT
 *   CLICK on NEXT    — save eval setting, advance (DONE)
 *   Tick (*) marks the active selection; > marks the cursor position.
 *   Default selection: HIDDEN.
 */

#include "../Inc/menu.h"
#include "lcd_4x20.h"
#include "joystick.h"
#include "uart_log.h"
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */
/* Blink period for the active digit in Stage 2 (ms per half-cycle) */
#define BLINK_HALF_MS    500U

/* -------------------------------------------------------------------------
 * Preset table
 * ------------------------------------------------------------------------- */
typedef struct
{
    const char *label;        /* 9 chars including padding */
    uint32_t    time_ms;
    uint32_t    increment_ms;
    const char *detail;       /* 8 chars including padding */
} Preset;

static const Preset k_presets[3] =
{
    { "Blitz    ", 3UL  * 60UL * 1000UL,  2UL * 1000UL, "( 3+ 2) " },
    { "Rapid    ", 15UL * 60UL * 1000UL, 10UL * 1000UL, "(15+10) " },
    { "Classical", 50UL * 60UL * 1000UL, 30UL * 1000UL, "(50+30) " },
};

/* -------------------------------------------------------------------------
 * Stage 2 digit cursor positions (left to right across the display)
 * ------------------------------------------------------------------------- */
typedef enum
{
    DIGIT_MM_TENS  = 0,
    DIGIT_MM_UNITS = 1,
    DIGIT_SS_TENS  = 2,
    DIGIT_SS_UNITS = 3,
    DIGIT_BACK     = 4,
    DIGIT_NEXT     = 5,
    DIGIT_COUNT    = 6
} TimeDigitPos;

/* -------------------------------------------------------------------------
 * Stage 3 nav cursor
 * ------------------------------------------------------------------------- */
typedef enum
{
    INC_VALUE = 0,   /* cursor not on nav row — UP/DOWN edits value */
    INC_BACK  = 1,
    INC_NEXT  = 2
} IncPos;

/* -------------------------------------------------------------------------
 * Stage 4 cursor + selection
 *
 * s_eval_cursor  — where the > cursor is (HIDDEN, VISIBLE, BACK, NEXT)
 * s_eval_sel     — which option has the tick (* marker): 0=HIDDEN, 1=VISIBLE
 * ------------------------------------------------------------------------- */
typedef enum
{
    ECUR_HIDDEN  = 0,
    ECUR_VISIBLE = 1,
    ECUR_BACK    = 2,
    ECUR_NEXT    = 3
} EvalCursor;

/* -------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */
static MENU_Stage    s_stage;
static MENU_Settings s_settings;

/* Stage 1 */
static uint8_t s_preset_cursor;

/* Stage 2 */
static uint8_t      s_mm;
static uint8_t      s_ss;
static TimeDigitPos s_digit_pos;
static uint32_t     s_blink_ms;
static uint8_t      s_blink_visible;

/* Stage 3 */
static uint8_t s_inc_s;
static IncPos  s_inc_pos;

/* Stage 4 */
static EvalCursor s_eval_cursor;
static uint8_t    s_eval_sel;   /* 0=HIDDEN selected, 1=VISIBLE selected */

/* Joystick input — auto-repeat model
 *   On initial press: fire immediately.
 *   If held: fire again after JOY_REPEAT_DELAY_MS, then every JOY_REPEAT_MS.
 *   Click uses a simple debounce (no repeat — one action per physical press).
 *   Direction and click are fully independent.
 */
#define JOY_REPEAT_DELAY_MS  400U   /* ms before auto-repeat begins          */
#define JOY_REPEAT_MS        150U   /* ms between repeat firings             */
#define JOY_CLICK_DEBOUNCE_MS 80U   /* min ms between accepted click events  */

static JOY_Direction s_prev_dir;
static uint32_t      s_dir_press_ms;   /* when current direction was first pressed */
static uint32_t      s_dir_repeat_ms;  /* when last repeat fired                    */
static uint8_t       s_dir_repeated;   /* 0 = in initial delay, 1 = repeating       */

static uint8_t       s_prev_click;
static uint32_t      s_click_last_ms;

/* -------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */
static void stage1_draw(void);
static void stage1_handle(JOY_Direction dir, uint8_t click);

static void stage2_draw(uint8_t full);
static void stage2_handle(JOY_Direction dir, uint8_t click);
static void stage2_blink_tick(void);
static void stage2_increment_active_digit(void);

static void stage3_draw(uint8_t full);
static void stage3_handle(JOY_Direction dir, uint8_t click);

static void stage4_draw(uint8_t full);
static void stage4_handle(JOY_Direction dir, uint8_t click);

static void     enter_stage(MENU_Stage stage);
static void     ms_to_mmss(uint32_t ms, uint8_t *mm, uint8_t *ss);
static uint32_t mmss_to_ms(uint8_t mm, uint8_t ss);

/* =========================================================================
 * Public API
 * ========================================================================= */

void MENU_Init(void)
{
    s_prev_dir      = JOY_CENTRE;
    s_dir_press_ms  = 0;
    s_dir_repeat_ms = 0;
    s_dir_repeated  = 0;
    s_prev_click    = 0;
    s_click_last_ms = 0;
    enter_stage(MENU_SELECT_PRESET);
}

uint8_t MENU_Update(void)
{
    if (s_stage == MENU_STAGE_DONE)
        return 1;

    JOY_Data joy = JOY_Read();
    uint32_t now = HAL_GetTick();

    /* --- Direction: initial press + auto-repeat --------------------------- */
    uint8_t dir_fire = 0;

    if (joy.direction == JOY_CENTRE)
    {
        /* Joystick released — reset repeat state */
        s_prev_dir     = JOY_CENTRE;
        s_dir_repeated = 0;
    }
    else if (joy.direction != s_prev_dir)
    {
        /* New direction pressed — fire immediately */
        s_prev_dir      = joy.direction;
        s_dir_press_ms  = now;
        s_dir_repeat_ms = now;
        s_dir_repeated  = 0;
        dir_fire        = 1;
    }
    else
    {
        /* Same direction held — check for repeat */
        if (!s_dir_repeated)
        {
            if ((now - s_dir_press_ms) >= JOY_REPEAT_DELAY_MS)
            {
                s_dir_repeated  = 1;
                s_dir_repeat_ms = now;
                dir_fire        = 1;
            }
        }
        else
        {
            if ((now - s_dir_repeat_ms) >= JOY_REPEAT_MS)
            {
                s_dir_repeat_ms = now;
                dir_fire        = 1;
            }
        }
    }

    /* --- Click: simple debounce, no repeat -------------------------------- */
    uint8_t click_fire = 0;
    if (joy.click == 1 && s_prev_click == 0 &&
        (now - s_click_last_ms) >= JOY_CLICK_DEBOUNCE_MS)
    {
        s_click_last_ms = now;
        click_fire      = 1;
    }
    s_prev_click = joy.click;

    /* --- Dispatch --------------------------------------------------------- */
    JOY_Direction dir   = dir_fire   ? joy.direction : JOY_CENTRE;
    uint8_t       click = click_fire ? 1             : 0;

    switch (s_stage)
    {
        case MENU_SELECT_PRESET: stage1_handle(dir, click);                      break;
        case MENU_SET_TIME:      stage2_handle(dir, click); stage2_blink_tick(); break;
        case MENU_SET_INCREMENT: stage3_handle(dir, click);                      break;
        case MENU_TOGGLE_EVAL:   stage4_handle(dir, click);                      break;
        default: break;
    }

    return (s_stage == MENU_STAGE_DONE) ? 1 : 0;
}

const MENU_Settings *MENU_GetSettings(void)
{
    return &s_settings;
}

/* =========================================================================
 * Stage transitions
 * ========================================================================= */

static void enter_stage(MENU_Stage stage)
{
    s_stage = stage;

    switch (stage)
    {
        case MENU_SELECT_PRESET:
            s_preset_cursor = 0;
            stage1_draw();
            ULOG_Info("MENU", "Stage", "SELECT_PRESET");
            break;

        case MENU_SET_TIME:
            ms_to_mmss(s_settings.time_per_side_ms, &s_mm, &s_ss);
            s_digit_pos     = DIGIT_MM_TENS;
            s_blink_ms      = HAL_GetTick();
            s_blink_visible = 1;
            stage2_draw(1);
            ULOG_Info("MENU", "Stage", "SET_TIME");
            break;

        case MENU_SET_INCREMENT:
            s_inc_s   = (uint8_t)(s_settings.increment_ms / 1000UL);
            s_inc_pos = INC_VALUE;
            stage3_draw(1);
            ULOG_Info("MENU", "Stage", "SET_INCREMENT");
            break;

        case MENU_TOGGLE_EVAL:
            s_eval_cursor       = ECUR_HIDDEN;
            s_eval_sel          = 0;           /* HIDDEN selected by default */
            s_settings.eval_visible = 0;
            stage4_draw(1);
            ULOG_Info("MENU", "Stage", "TOGGLE_EVAL");
            break;

        case MENU_STAGE_DONE:
        {
            char buf[48];
            snprintf(buf, sizeof(buf), "time=%lums inc=%lums eval=%d",
                     s_settings.time_per_side_ms,
                     s_settings.increment_ms,
                     (int)s_settings.eval_visible);
            ULOG_Info("MENU", "Done", buf);
            break;
        }

        default:
            break;
    }
}

/* =========================================================================
 * Stage 1 — SELECT PRESET
 *
 * Row 0: "SELECT PRESET       "
 * Row 1: "> Blitz    ( 3+ 2) "
 * Row 2: "  Rapid    (15+10) "
 * Row 3: "  Classical(50+30) "
 * ========================================================================= */

static void stage1_draw(void)
{
    LCD4_Clear();
    LCD4_SetCursor(0, 0);
    LCD4_PrintString("SELECT PRESET       ");

    uint8_t i;
    for (i = 0; i < 3; i++)
    {
        char row[21];
        snprintf(row, sizeof(row), "%c %s%s",
                 (i == s_preset_cursor) ? '>' : ' ',
                 k_presets[i].label,
                 k_presets[i].detail);
        LCD4_SetCursor((uint8_t)(i + 1), 0);
        LCD4_PrintString(row);
    }
}

static void stage1_handle(JOY_Direction dir, uint8_t click)
{
    if (dir == JOY_UP && s_preset_cursor > 0)
    {
        s_preset_cursor--;
        stage1_draw();
    }
    else if (dir == JOY_DOWN && s_preset_cursor < 2)
    {
        s_preset_cursor++;
        stage1_draw();
    }
    else if (click)
    {
        s_settings.time_per_side_ms = k_presets[s_preset_cursor].time_ms;
        s_settings.increment_ms     = k_presets[s_preset_cursor].increment_ms;

        char buf[32];
        snprintf(buf, sizeof(buf), "preset=%d", (int)s_preset_cursor);
        ULOG_Info("MENU", "Preset", buf);

        enter_stage(MENU_SET_TIME);
    }
}

/* =========================================================================
 * Stage 2 — SET TIME
 *
 * Row 0: "SET TIME            "
 * Row 1: "  MM : SS           "   (static label)
 * Row 2: "  03 : 00           "   (active digit blinks)
 * Row 3: "   BACK      NEXT   "   ([BACK] and [NEXT] when cursor is there)
 *
 * Digit column map on row 2:
 *   MM_TENS  → col 2
 *   MM_UNITS → col 3
 *   (colon   → col 5, written by the full row string)
 *   SS_TENS  → col 7
 *   SS_UNITS → col 8
 *
 * LEFT/RIGHT move the digit cursor left and right.
 * RIGHT from SS_UNITS → BACK; RIGHT from BACK → NEXT; RIGHT from NEXT → no-op.
 * LEFT  from BACK    → SS_UNITS; LEFT from MM_TENS → no-op.
 * UP/DOWN change the active digit value (no effect when cursor is on BACK/NEXT).
 * CLICK on digit → increment that digit (same as UP).
 * CLICK on BACK  → go back to SELECT PRESET.
 * CLICK on NEXT  → save and advance to SET INCREMENT.
 * ========================================================================= */

static void stage2_draw(uint8_t full)
{
    if (full)
    {
        LCD4_Clear();
        LCD4_SetCursor(0, 0);
        LCD4_PrintString("SET TIME            ");
        LCD4_SetCursor(1, 0);
        LCD4_PrintString("  MM : SS           ");
    }

    /* Row 2 — full time string; blink tick will overwrite the active digit */
    char time_row[21];
    snprintf(time_row, sizeof(time_row), "  %02d : %02d           ", s_mm, s_ss);
    LCD4_SetCursor(2, 0);
    LCD4_PrintString(time_row);

    /* Row 3 — nav; brackets appear only when cursor is on that item */
    uint8_t on_back = (s_digit_pos == DIGIT_BACK);
    uint8_t on_next = (s_digit_pos == DIGIT_NEXT);
    char nav_row[21];
    snprintf(nav_row, sizeof(nav_row), "  %sBACK%s   %sNEXT%s   ",
             on_back ? "[" : " ", on_back ? "]" : " ",
             on_next ? "[" : " ", on_next ? "]" : " ");
    LCD4_SetCursor(3, 0);
    LCD4_PrintString(nav_row);
}

static void stage2_blink_tick(void)
{
    if (s_digit_pos >= DIGIT_BACK)
        return;

    uint32_t now = HAL_GetTick();
    if ((now - s_blink_ms) < BLINK_HALF_MS)
        return;

    s_blink_ms      = now;
    s_blink_visible ^= 1;

    uint8_t col;
    uint8_t digit_val;

    switch (s_digit_pos)
    {
        case DIGIT_MM_TENS:  col = 2; digit_val = s_mm / 10; break;
        case DIGIT_MM_UNITS: col = 3; digit_val = s_mm % 10; break;
        case DIGIT_SS_TENS:  col = 7; digit_val = s_ss / 10; break;
        case DIGIT_SS_UNITS: col = 8; digit_val = s_ss % 10; break;
        default: return;
    }

    LCD4_SetCursor(2, col);
    if (s_blink_visible)
    {
        char ch[2] = { (char)('0' + digit_val), '\0' };
        LCD4_PrintString(ch);
    }
    else
    {
        LCD4_PrintString(" ");
    }
}

/* Increment the currently active digit by 1 (wrapping), same logic as UP. */
static void stage2_increment_active_digit(void)
{
    switch (s_digit_pos)
    {
        case DIGIT_MM_TENS:
        {
            uint8_t tens = s_mm / 10;
            tens = (tens >= 9) ? 0 : tens + 1;
            s_mm = (uint8_t)(tens * 10 + (s_mm % 10));
            if (s_mm > 99) s_mm = 99;
            break;
        }
        case DIGIT_MM_UNITS:
        {
            uint8_t units = s_mm % 10;
            units = (units >= 9) ? 0 : units + 1;
            s_mm = (uint8_t)((s_mm / 10) * 10 + units);
            if (s_mm > 99) s_mm = 99;
            break;
        }
        case DIGIT_SS_TENS:
        {
            uint8_t tens = s_ss / 10;
            tens = (tens >= 5) ? 0 : tens + 1;
            s_ss = (uint8_t)(tens * 10 + (s_ss % 10));
            if (s_ss > 59) s_ss = 59;
            break;
        }
        case DIGIT_SS_UNITS:
        {
            uint8_t units = s_ss % 10;
            units = (units >= 9) ? 0 : units + 1;
            s_ss = (uint8_t)((s_ss / 10) * 10 + units);
            if (s_ss > 59) s_ss = 59;
            break;
        }
        default:
            break;
    }
}

static void stage2_handle(JOY_Direction dir, uint8_t click)
{
    /* --- LEFT: move cursor one step left ---------------------------------- */
    if (dir == JOY_LEFT)
    {
        if      (s_digit_pos == DIGIT_NEXT)   s_digit_pos = DIGIT_BACK;
        else if (s_digit_pos == DIGIT_BACK)   s_digit_pos = DIGIT_SS_UNITS;
        else if (s_digit_pos == DIGIT_SS_UNITS) s_digit_pos = DIGIT_SS_TENS;
        else if (s_digit_pos == DIGIT_SS_TENS)  s_digit_pos = DIGIT_MM_UNITS;
        else if (s_digit_pos == DIGIT_MM_UNITS) s_digit_pos = DIGIT_MM_TENS;
        /* MM_TENS: already at leftmost, no-op */
        stage2_draw(0);
        return;
    }

    /* --- RIGHT: move cursor one step right -------------------------------- */
    if (dir == JOY_RIGHT)
    {
        if      (s_digit_pos == DIGIT_MM_TENS)   s_digit_pos = DIGIT_MM_UNITS;
        else if (s_digit_pos == DIGIT_MM_UNITS)  s_digit_pos = DIGIT_SS_TENS;
        else if (s_digit_pos == DIGIT_SS_TENS)   s_digit_pos = DIGIT_SS_UNITS;
        else if (s_digit_pos == DIGIT_SS_UNITS)  s_digit_pos = DIGIT_BACK;
        else if (s_digit_pos == DIGIT_BACK)      s_digit_pos = DIGIT_NEXT;
        /* NEXT: already at rightmost, no-op */
        stage2_draw(0);
        return;
    }

    /* --- UP/DOWN: change active digit value (no-op on BACK/NEXT) ---------- */
    if (dir == JOY_UP || dir == JOY_DOWN)
    {
        if (s_digit_pos >= DIGIT_BACK)
            return;  /* UP/DOWN does nothing on nav row */

        int8_t delta = (dir == JOY_UP) ? 1 : -1;

        switch (s_digit_pos)
        {
            case DIGIT_MM_TENS:
            {
                int8_t tens = (int8_t)(s_mm / 10) + delta;
                if (tens < 0) tens = 9;
                if (tens > 9) tens = 0;
                s_mm = (uint8_t)((uint8_t)tens * 10u + (s_mm % 10u));
                if (s_mm > 99) s_mm = 99;
                break;
            }
            case DIGIT_MM_UNITS:
            {
                int8_t units = (int8_t)(s_mm % 10) + delta;
                if (units < 0) units = 9;
                if (units > 9) units = 0;
                s_mm = (uint8_t)((s_mm / 10u) * 10u + (uint8_t)units);
                if (s_mm > 99) s_mm = 99;
                break;
            }
            case DIGIT_SS_TENS:
            {
                int8_t tens = (int8_t)(s_ss / 10) + delta;
                if (tens < 0) tens = 5;
                if (tens > 5) tens = 0;
                s_ss = (uint8_t)((uint8_t)tens * 10u + (s_ss % 10u));
                if (s_ss > 59) s_ss = 59;
                break;
            }
            case DIGIT_SS_UNITS:
            {
                int8_t units = (int8_t)(s_ss % 10) + delta;
                if (units < 0) units = 9;
                if (units > 9) units = 0;
                s_ss = (uint8_t)((s_ss / 10u) * 10u + (uint8_t)units);
                if (s_ss > 59) s_ss = 59;
                break;
            }
            default:
                break;
        }
        stage2_draw(0);
        return;
    }

    /* --- CLICK ------------------------------------------------------------ */
    if (click)
    {
        if (s_digit_pos == DIGIT_BACK)
        {
            enter_stage(MENU_SELECT_PRESET);
        }
        else if (s_digit_pos == DIGIT_NEXT)
        {
            s_settings.time_per_side_ms = mmss_to_ms(s_mm, s_ss);
            char buf[24];
            snprintf(buf, sizeof(buf), "time=%lums", s_settings.time_per_side_ms);
            ULOG_Info("MENU", "SetTime", buf);
            enter_stage(MENU_SET_INCREMENT);
        }
        else
        {
            /* Click on a digit → increment it (shortcut for UP) */
            stage2_increment_active_digit();
            stage2_draw(0);
        }
    }
}

/* =========================================================================
 * Stage 3 — SET INCREMENT
 *
 * Row 0: "SET INCREMENT       "
 * Row 1: "  + SS              "   (static label)
 * Row 2: ">  + 02             "   (value; > always shown — it's the only editable item)
 * Row 3: "   BACK      NEXT   "   (brackets appear when cursor is there)
 *
 * UP/DOWN   — always adjust s_inc_s (even when nav cursor is on BACK/NEXT)
 * LEFT      — move nav cursor to BACK
 * RIGHT     — move nav cursor to NEXT
 * UP from BACK or NEXT — move nav cursor back to VALUE (INC_VALUE)
 * CLICK on BACK  — return to SET TIME
 * CLICK on NEXT or VALUE — save and advance to TOGGLE EVAL
 * ========================================================================= */

static void stage3_draw(uint8_t full)
{
    if (full)
    {
        LCD4_Clear();
        LCD4_SetCursor(0, 0);
        LCD4_PrintString("SET INCREMENT       ");
        LCD4_SetCursor(1, 0);
        LCD4_PrintString("  + SS              ");
    }

    /* Row 2 — value row; > always present (it's always the editable item) */
    char val_row[21];
    snprintf(val_row, sizeof(val_row), ">  + %02d             ", (int)s_inc_s);
    LCD4_SetCursor(2, 0);
    LCD4_PrintString(val_row);

    /* Row 3 — nav */
    uint8_t on_back = (s_inc_pos == INC_BACK);
    uint8_t on_next = (s_inc_pos == INC_NEXT);
    char nav_row[21];
    snprintf(nav_row, sizeof(nav_row), "  %sBACK%s   %sNEXT%s   ",
             on_back ? "[" : " ", on_back ? "]" : " ",
             on_next ? "[" : " ", on_next ? "]" : " ");
    LCD4_SetCursor(3, 0);
    LCD4_PrintString(nav_row);
}

static void stage3_handle(JOY_Direction dir, uint8_t click)
{
    /* UP/DOWN always adjust value, regardless of where the nav cursor is */
    if (dir == JOY_UP)
    {
        if (s_inc_s < 99) s_inc_s++;
        stage3_draw(0);
        return;
    }

    if (dir == JOY_DOWN)
    {
        if (s_inc_s > 0) s_inc_s--;
        stage3_draw(0);
        return;
    }

    /* LEFT → BACK; RIGHT → NEXT */
    if (dir == JOY_LEFT)
    {
        if (s_inc_pos != INC_BACK)
        {
            s_inc_pos = INC_BACK;
            stage3_draw(0);
        }
        return;
    }

    if (dir == JOY_RIGHT)
    {
        if (s_inc_pos != INC_NEXT)
        {
            s_inc_pos = INC_NEXT;
            stage3_draw(0);
        }
        return;
    }

    if (click)
    {
        if (s_inc_pos == INC_BACK)
        {
            enter_stage(MENU_SET_TIME);
        }
        else
        {
            /* INC_VALUE or INC_NEXT: save and advance */
            s_settings.increment_ms = (uint32_t)s_inc_s * 1000UL;
            char buf[24];
            snprintf(buf, sizeof(buf), "inc=%lums", s_settings.increment_ms);
            ULOG_Info("MENU", "SetInc", buf);
            enter_stage(MENU_TOGGLE_EVAL);
        }
    }
}

/* =========================================================================
 * Stage 4 — TOGGLE EVAL
 *
 * Row 0: "TOGGLE EVAL         "
 * Row 1: "> HIDDEN  *         "   (* when HIDDEN selected; > when cursor here)
 * Row 2: "  VISIBLE           "   (* when VISIBLE selected; > when cursor here)
 * Row 3: "   BACK      NEXT   "   (brackets when cursor is there)
 *
 * s_eval_cursor — where the > cursor is (ECUR_HIDDEN/VISIBLE/BACK/NEXT)
 * s_eval_sel    — which option has the tick (0=HIDDEN, 1=VISIBLE)
 *
 * UP/DOWN move cursor between HIDDEN and VISIBLE only.
 * From BACK or NEXT, UP jumps back to VISIBLE.
 * DOWN on BACK → no-op. DOWN on NEXT → no-op.
 * LEFT from NEXT → BACK. LEFT elsewhere → no-op.
 * RIGHT from BACK → NEXT. RIGHT elsewhere → no-op.
 * CLICK on HIDDEN  → select HIDDEN (tick), stay.
 * CLICK on VISIBLE → select VISIBLE (tick), stay.
 * CLICK on BACK    → return to SET INCREMENT.
 * CLICK on NEXT    → confirm and advance (DONE).
 * ========================================================================= */

static void stage4_draw(uint8_t full)
{
    if (full)
    {
        LCD4_Clear();
        LCD4_SetCursor(0, 0);
        LCD4_PrintString("TOGGLE EVAL         ");
    }

    char row[21];

    /* Row 1 — HIDDEN: > if cursor here; * if selected */
    snprintf(row, sizeof(row), "%c HIDDEN  %c         ",
             (s_eval_cursor == ECUR_HIDDEN)  ? '>' : ' ',
             (s_eval_sel == 0)               ? '*' : ' ');
    LCD4_SetCursor(1, 0);
    LCD4_PrintString(row);

    /* Row 2 — VISIBLE: > if cursor here; * if selected */
    snprintf(row, sizeof(row), "%c VISIBLE %c         ",
             (s_eval_cursor == ECUR_VISIBLE) ? '>' : ' ',
             (s_eval_sel == 1)               ? '*' : ' ');
    LCD4_SetCursor(2, 0);
    LCD4_PrintString(row);

    /* Row 3 — nav */
    uint8_t on_back = (s_eval_cursor == ECUR_BACK);
    uint8_t on_next = (s_eval_cursor == ECUR_NEXT);
    snprintf(row, sizeof(row), "  %sBACK%s   %sNEXT%s   ",
             on_back ? "[" : " ", on_back ? "]" : " ",
             on_next ? "[" : " ", on_next ? "]" : " ");
    LCD4_SetCursor(3, 0);
    LCD4_PrintString(row);
}

static void stage4_handle(JOY_Direction dir, uint8_t click)
{
    if (dir == JOY_UP)
    {
        if (s_eval_cursor == ECUR_HIDDEN)
        {
            /* already at top — no-op */
        }
        else if (s_eval_cursor == ECUR_VISIBLE)
        {
            s_eval_cursor = ECUR_HIDDEN;
            stage4_draw(0);
        }
        else
        {
            /* BACK or NEXT: jump back up to VISIBLE */
            s_eval_cursor = ECUR_VISIBLE;
            stage4_draw(0);
        }
        return;
    }

    if (dir == JOY_DOWN)
    {
        if (s_eval_cursor == ECUR_HIDDEN)
        {
            s_eval_cursor = ECUR_VISIBLE;
            stage4_draw(0);
        }
        else if (s_eval_cursor == ECUR_VISIBLE)
        {
            s_eval_cursor = ECUR_BACK;
            stage4_draw(0);
        }
        /* BACK, NEXT: no-op */
        return;
    }

    if (dir == JOY_LEFT)
    {
        if (s_eval_cursor == ECUR_NEXT)
        {
            s_eval_cursor = ECUR_BACK;
            stage4_draw(0);
        }
        return;
    }

    if (dir == JOY_RIGHT)
    {
        if (s_eval_cursor == ECUR_BACK)
        {
            s_eval_cursor = ECUR_NEXT;
            stage4_draw(0);
        }
        return;
    }

    if (click)
    {
        if (s_eval_cursor == ECUR_BACK)
        {
            enter_stage(MENU_SET_INCREMENT);
        }
        else if (s_eval_cursor == ECUR_NEXT)
        {
            s_settings.eval_visible = s_eval_sel;
            ULOG_Info("MENU", "Eval", s_eval_sel ? "VISIBLE" : "HIDDEN");
            enter_stage(MENU_STAGE_DONE);
        }
        else
        {
            /* HIDDEN or VISIBLE: update selection tick, stay on same cursor */
            s_eval_sel = (s_eval_cursor == ECUR_VISIBLE) ? 1 : 0;
            ULOG_Info("MENU", "EvalSel", s_eval_sel ? "VISIBLE" : "HIDDEN");
            stage4_draw(0);
        }
    }
}

/* =========================================================================
 * Helpers
 * ========================================================================= */

static void ms_to_mmss(uint32_t ms, uint8_t *mm, uint8_t *ss)
{
    uint32_t total_s = ms / 1000UL;
    *mm = (uint8_t)(total_s / 60UL);
    if (*mm > 99) *mm = 99;
    *ss = (uint8_t)(total_s % 60UL);
}

static uint32_t mmss_to_ms(uint8_t mm, uint8_t ss)
{
    return ((uint32_t)mm * 60UL + (uint32_t)ss) * 1000UL;
}
