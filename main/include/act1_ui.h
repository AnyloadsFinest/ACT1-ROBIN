#pragma once

/** Called from display boot delays so buttons stay polled during animation. */
void Act1_BootYield(void);

/** Button debounce, menu shortcuts, and edge logging. */
void Act1_ProcessButtons(void);
