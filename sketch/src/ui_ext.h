#pragma once
// Extra settings screen built in code (not in the SquareLine project), reached from the "More >" button
// on the existing settings screen. Holds the idle/sleep setting and the key mapping.
void ui_ext_init(); // call once after ui_init() and load_config()
void ui_ext_refresh(); // re-read the key map into the dropdowns (after it was changed by a serial packet)
