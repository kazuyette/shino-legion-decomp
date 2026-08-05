#ifndef INPUT_H
#define INPUT_H

/* Keyboard-to-pad scaffolding. NOT wired to real game logic yet -- the
 * actual pad-reading code was never found in A.BIN (see
 * docs/jump_table_functions.md, "Searched for pad/controller input") and
 * is presumed to live in DIRECTOR.PPB/SHINOBI.PPB, which are blocked on
 * the disassembly-reliability wall documented there. This module exists so
 * the plumbing (poll -> PadState -> whatever consumes it) is ready to wire
 * up the moment the real button mapping is known, instead of starting from
 * scratch later.
 *
 * Field names follow the standard Saturn digital pad layout (SMPC
 * peripheral report convention) since that's the one part of "input" that
 * *is* well documented, even though we don't yet know how this specific
 * game reads it internally. */

typedef struct {
    int up, down, left, right;
    int a, b, c;
    int x, y, z;
    int l, r;
    int start;
} PadState;

/* Polls the current SDL keyboard state into a PadState using the default
 * keymap (see input.c for the exact key assignments). */
void Input_Update(void);

/* Read-only accessor for the current frame's pad state. */
const PadState *Input_GetPadState(void);

#endif
