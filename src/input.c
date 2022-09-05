/*
 * Copyright (c) 2020-2022 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include <SDL.h>
#include <jg/jg.h>

#include "tconfig.h"

#include "jgrf.h"
#include "cheats.h"
#include "menu.h"
#include "video.h"
#include "input.h"

#define MAXPORTS 12

#if SDL_VERSION_ATLEAST(2,0,18)
    #define jgrf_getticks SDL_GetTicks64
#else
    #define jgrf_getticks SDL_GetTicks
#endif

// Pointers to members of core input state
typedef struct jgrf_jsmap_t {
    int16_t *axis[JG_AXES_MAX];
    uint8_t *abtn[JG_AXES_MAX * 2]; // Axes acting as buttons
    uint8_t *hatpos[4];
    uint8_t *button[JG_BUTTONS_MAX];
} jgrf_jsmap_t;

typedef struct jgrf_kbmap_t {
    uint8_t *key[SDL_NUM_SCANCODES];
} jgrf_kbmap_t;

typedef struct jgrf_msmap_t {
    uint8_t index;
    uint8_t *button[JG_BUTTONS_MAX];
} jgrf_msmap_t;

void (*jgrf_input_audio)(int, const int16_t*, size_t);

static jg_videoinfo_t *vidinfo; // Video Info for calculating mouse input
static jg_inputinfo_t *inputinfo[MAXPORTS]; // Core Input Info
static jg_inputstate_t coreinput[MAXPORTS]; // Input states shared with core

static jgrf_jsmap_t jsmap[MAXPORTS]; // Pointer maps for joystick/gamepad input
static uint8_t rumblemap[MAXPORTS]; // Ensure force feedback lines up properly
static jgrf_kbmap_t kbmap; // Pointer map for keyboard input
static jgrf_msmap_t msmap; // Pointer map for mouse input

static jgrf_gdata_t *gdata; // Global data pointer

// SDL Joystick and Haptic pointers
static SDL_Joystick *joystick[MAXPORTS];
static SDL_Haptic *haptic[MAXPORTS];

// Arrays to keep track of what joystick ports have a device plugged in
static int jsports[MAXPORTS];
static int jsiid[MAXPORTS]; // Joystick Instance ID

// Array to keep track of what axes are triggers vs regular "stick" axes
static uint8_t trigger[MAXPORTS];

// Undefined inputs point here - this is like /dev/null for input events
static uint8_t undef8;
static int16_t undef16;

// Configuration related globals
static ini_table_s *iconf;
static int confactive = 0;
static int confchanged = 0;
static int confindex = 0;
static int confport = 0;
static uint64_t conftimer = 0;
static int menuactive = 0;

void jgrf_input_menu_enable(int e) {
    menuactive = e;
}

// Map a core input axis definition
void jgrf_input_map_axis(int index, uint32_t dnum, const char* value) {
    //printf("axis: %d, %d, %s\n", index, dnum, value);
    if (value[0] == 'j') { // Joystick
        uint8_t inum = value[1] - '0'; // Index (Frontend)
        if (value[2] == 'a') { // Axis
            int anum = atoi(&value[3]);
            jsmap[inum].axis[anum] = &(coreinput[index].axis[dnum]);
            rumblemap[inum] = index; // Synchronize force feedback with axes
        }
    }

    conftimer = jgrf_getticks();
}

// Map a core input button definition
void jgrf_input_map_button(int index, uint32_t dnum, const char* value) {
    //printf("button: %d, %d, %s\n", index, dnum, value);
    if (value[0] == 'j') { // Joystick
        uint8_t inum = value[1] - '0'; // Index (Frontend)
        if (value[2] == 'a') { // Axis acting as Button
            int anum = atoi(&value[3]) * 2;
            if (value[4] == '+')
                anum++;
            jsmap[inum].abtn[anum] = &(coreinput[index].button[dnum]);
        }
        else if (value[2] == 'b') { // Button
            int bnum = atoi(&value[3]);
            jsmap[inum].button[bnum] = &(coreinput[index].button[dnum]);
        }
        else if (value[2] == 'h') { // Hat switch
            uint8_t hinum = value[1] - '0'; // Index (Frontend)
            // Assume hat switch 0 - are there devices with more than one?
            jsmap[hinum].hatpos[value[4] - '0'] =
                &(coreinput[index].button[dnum]);
        }
    }
    else if (value[0] == 'm') { // Mouse
        if (value[1] == 'b') {
            int bnum = atoi(&value[2]);
            msmap.index = index;
            msmap.button[bnum] = &(coreinput[index].button[dnum]);
        }
    }
    else { // Keyboard
        int knum = atoi(value);
        kbmap.key[knum] = &(coreinput[index].button[dnum]);
        *kbmap.key[knum] = 0;
    }

    conftimer = jgrf_getticks();
}

// Read an input config file and assign inputs
static void jgrf_inputcfg_read(jg_inputinfo_t *iinfo) {
    char path[256];
    snprintf(path, sizeof(path), "%sinput_%s.ini",
        gdata->configpath, gdata->sys);

    for (int i = 0; i < iinfo->numaxes; ++i) {
        if (ini_table_check_entry(iconf, iinfo->name, iinfo->defs[i])) {
            jgrf_input_map_axis(iinfo->index, i,
                ini_table_get_entry(iconf, iinfo->name, iinfo->defs[i]));
        }
    }

    for (int i = 0; i < iinfo->numbuttons; ++i) {
        if (ini_table_check_entry(iconf, iinfo->name,
            iinfo->defs[i + iinfo->numaxes])) {
            jgrf_input_map_button(iinfo->index, i,
                ini_table_get_entry(iconf, iinfo->name,
                iinfo->defs[i + iinfo->numaxes]));
        }
    }
}

// Unmap/undefine all joystick map pointers
static void jgrf_input_undef_port(int port) {
    for (int j = 0; j < JG_AXES_MAX; ++j) {
        jsmap[port].axis[j] = &undef16;
        jsmap[port].abtn[j * 2] = jsmap[port].abtn[(j * 2) + 1] = &undef8;
    }

    for (int j = 0; j < 4; ++j)
        jsmap[port].hatpos[j] = &undef8;

    for (int j = 0; j < JG_BUTTONS_MAX; ++j)
        jsmap[port].button[j] = &undef8;
}

// Initialize input
int jgrf_input_init(void) {
    // Set all joystick mappings to undefined
    for (int i = 0; i < MAXPORTS; ++i) {
        jgrf_input_undef_port(i);
    }

    // Set all keyboard mappings to undefined
    for (int j = 0; j < SDL_NUM_SCANCODES; ++j)
        kbmap.key[j] = &undef8;

    // Set all mouse mappings to undefined
    for (int j = 0; j < JG_BUTTONS_MAX; ++j)
        msmap.button[j] = &undef8;

    // Initialize the input configuration structure
    char path[256];
    snprintf(path, sizeof(path), "%sinput_%s.ini",
        gdata->configpath, gdata->sys);

    iconf = ini_table_create();
    if (!ini_table_read_from_file(iconf, path))
        jgrf_log(JG_LOG_WRN, "Input configuration file not found: %s\n", path);

    // Set pointer to video info - Move coordinate computation?
    vidinfo = jgrf_video_get_info();

    return 1;
}

// Deinitialize input and save changes if necessary
void jgrf_input_deinit(void) {
    // Deinitialize joysticks
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_JoystickIsHaptic(joystick[i]))
            SDL_HapticClose(haptic[i]);

        SDL_JoystickClose(joystick[i]);
    }

    // Write out the input config file
    if (confchanged) {
        char path[256];
        snprintf(path, sizeof(path), "%sinput_%s.ini",
            gdata->configpath, gdata->sys);

        ini_table_write_to_file(iconf, path);
    }

    // Clean up the input config data
    ini_table_destroy(iconf);
}

// Handle joystick hotplug additions
static void jgrf_input_hotplug_add(SDL_Event *event) {
    int port = 0;

    for (int i = 0; i < MAXPORTS; ++i) {
        if (!jsports[i]) {
            joystick[i] = SDL_JoystickOpen(event->jdevice.which);
            SDL_JoystickSetPlayerIndex(joystick[i], i);
            jsports[i] = 1;
            jsiid[i] = SDL_JoystickInstanceID(joystick[i]);
            port = i;

            jgrf_log(JG_LOG_INF, "Joystick %d Connected: %s\n",
                port + 1, SDL_JoystickName(joystick[port]));

            if (SDL_JoystickIsHaptic(joystick[port])) {
                haptic[port] =
                    SDL_HapticOpenFromJoystick(joystick[port]);
                SDL_HapticRumbleInit(haptic[port]) < 0 ?
                jgrf_log(JG_LOG_DBG, "Force Feedback Enable Failed\n"):
                jgrf_log(JG_LOG_DBG, "Force Feedback Enabled\n");
            }

            /* Handle analog input seed values - fixes trigger input
               values at startup
            */
            for (int j = 0; j < SDL_JoystickNumAxes(joystick[i]); ++j) {
                int16_t aval = SDL_JoystickGetAxis(joystick[i], j);
                if (aval <= -(DEADZONE)) {
                    *jsmap[i].axis[j] = aval;
                    trigger[i] |= 1 << j; // it's a trigger
                }
            }
            break;
        }
    }
}

// Handle joystick hotplug removals
static void jgrf_input_hotplug_remove(SDL_Event *event) {
    for (int i = 0; i < MAXPORTS; ++i) {
        // If it's the one that got disconnected...
        if (jsiid[i] == event->jdevice.which) {
            if (SDL_JoystickIsHaptic(joystick[i]))
                SDL_HapticClose(haptic[i]);

            jsports[i] = 0; // This is unplugged
            jgrf_log(JG_LOG_INF, "Joystick %d Disconnected\n", i + 1);
            SDL_JoystickClose(joystick[i]);
            joystick[i] = NULL;
            jsports[i] = 0;

            break;
        }
    }
}

// Retrieve inputinfo data so the frontend knows what the core has plugged in
void jgrf_input_query(jg_inputinfo_t* (*get_inputinfo)(int)) {
    for(int i = 0; i < gdata->numinputs; ++i) {
        inputinfo[i] = get_inputinfo(i);
        if (inputinfo[i]->name) {
            jgrf_log(JG_LOG_INF,
                "Emulated Input %d: %s, %s, %d axes, %d buttons\n",
                i + 1, inputinfo[i]->name, inputinfo[i]->fname,
                inputinfo[i]->numaxes, inputinfo[i]->numbuttons);
            jgrf_inputcfg_read(inputinfo[i]);
        }
        if (inputinfo[i]->type == JG_INPUT_GUN) {
            jgrf_video_set_cursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
        }
    }
}

// Pass function pointer to input audio samples into the core
void jgrf_input_set_audio(void (*inp)(int, const int16_t*, size_t)) {
    jgrf_input_audio = inp;
}

// Pass pointers to input states into the core
void jgrf_input_set_states(void (*set_inputstate)(jg_inputstate_t*, int)) {
    gdata = jgrf_gdata_ptr();
    for(int i = 0; i < gdata->numinputs; ++i)
        set_inputstate(&coreinput[i], i);
}

// Calculate mouse input coordinates, taking into account aspect ratio and scale
static inline void jgrf_input_coords_scaled(int32_t x, int32_t y,
    int32_t *xcoord, int32_t *ycoord) {

    float xscale, yscale, xo, yo;
    jgrf_video_get_scale_params(&xscale, &yscale, &xo, &yo);
    *xcoord = (x - xo) /
        ((vidinfo->aspect * vidinfo->h * xscale)/(float)vidinfo->w);
    *ycoord = ((y - yo) / yscale) + vidinfo->y;
}

// Run an input configuration iteration to set up a specific definition
static void jgrf_inputcfg(jg_inputinfo_t *iinfo) {
    if (confindex >= (iinfo->numaxes + iinfo->numbuttons)) {
        confactive = 0; // Turn off input config mode
        jgrf_video_text(2, 0, ""); // Disable display of input config info
        return;
    }
    else {
        confactive = 1;
        conftimer = jgrf_getticks();
    }

    // Display input config information on screen
    if (iinfo->name) {
        char msg[128];
        snprintf(msg, sizeof(msg),
            "%s\n%s\n%d axes, %d buttons\nConfigure %s",
            iinfo->fname, iinfo->name,
            iinfo->numaxes, iinfo->numbuttons, iinfo->defs[confindex]);
        jgrf_video_text(2, 1, msg);
    }
}

// Create config definitions from input events, then configure them to be used
static void jgrf_inputcfg_handler(SDL_Event *event) {
    char defbuf[32];

    switch(event->type) {
        case SDL_KEYDOWN: {
            if (event->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                ini_table_create_entry(iconf, inputinfo[confport]->name,
                    inputinfo[confport]->defs[confindex], "");
                confindex++;
                jgrf_inputcfg(inputinfo[confport]);
                break;
            }

            if (event->key.repeat)
                break;

            if (confindex < inputinfo[confport]->numaxes) {
                jgrf_log(JG_LOG_WRN, "Trying to assign digital inputs to axes"
                    " is a losing endeavour. ESC to skip.\n");
                break;
            }

            snprintf(defbuf, sizeof(defbuf), "%d",
                event->key.keysym.scancode);

            ini_table_create_entry(iconf, inputinfo[confport]->name,
                inputinfo[confport]->defs[confindex], defbuf);

            jgrf_input_map_button(confport,
                confindex - inputinfo[confport]->numaxes, defbuf);

            confindex++;
            jgrf_inputcfg(inputinfo[confport]);
            break;
        }
        case SDL_JOYBUTTONDOWN: {
            if (confindex < inputinfo[confport]->numaxes) {
                jgrf_log(JG_LOG_WRN, "Trying to assign digital inputs to axes"
                    " is a losing endeavour. ESC to skip.\n");
                break;
            }

            SDL_Joystick *js = SDL_JoystickFromInstanceID(event->jbutton.which);
            int port = SDL_JoystickGetPlayerIndex(js);

            snprintf(defbuf, sizeof(defbuf), "j%db%d",
                port, event->jbutton.button);

            ini_table_create_entry(iconf, inputinfo[confport]->name,
                inputinfo[confport]->defs[confindex], defbuf);

            jgrf_input_map_button(confport,
                confindex - inputinfo[confport]->numaxes, defbuf);

            confindex++;
            jgrf_inputcfg(inputinfo[confport]);
            break;
        }
        case SDL_JOYAXISMOTION: {
            SDL_Joystick *js = SDL_JoystickFromInstanceID(event->jaxis.which);
            int port = SDL_JoystickGetPlayerIndex(js);

            // Triggers require special handling
            if (trigger[port] & (1 << event->jaxis.axis)) {
                // Axes set to axis input
                if (confindex < inputinfo[confport]->numaxes) {
                    if (event->jaxis.value >= BDEADZONE) {
                        snprintf(defbuf, sizeof(defbuf), "j%da%d",
                            port, event->jaxis.axis);

                        ini_table_create_entry(iconf, inputinfo[confport]->name,
                            inputinfo[confport]->defs[confindex], defbuf);

                        jgrf_input_map_axis(confport, confindex, defbuf);

                        confindex++;
                        jgrf_inputcfg(inputinfo[confport]);
                    }
                }
                else { // Axes set to button input
                    if (event->jaxis.value >= BDEADZONE) {
                        snprintf(defbuf, sizeof(defbuf), "j%da%d+",
                            port, event->jaxis.axis);

                        ini_table_create_entry(iconf, inputinfo[confport]->name,
                            inputinfo[confport]->defs[confindex], defbuf);

                        jgrf_input_map_button(confport,
                            confindex - inputinfo[confport]->numaxes,
                            defbuf);

                        confindex++;
                        jgrf_inputcfg(inputinfo[confport]);
                    }
                }
            }
            else { // Normal axis
                // Axes set to axis input
                if (confindex < inputinfo[confport]->numaxes) {
                    if (abs(event->jaxis.value) >= BDEADZONE) {
                        snprintf(defbuf, sizeof(defbuf), "j%da%d",
                            port, event->jaxis.axis);

                        ini_table_create_entry(iconf, inputinfo[confport]->name,
                            inputinfo[confport]->defs[confindex], defbuf);

                        jgrf_input_map_axis(confport, confindex, defbuf);

                        confindex++;
                        jgrf_inputcfg(inputinfo[confport]);
                    }
                }
                else { // Axes set to button input
                    if (abs(event->jaxis.value) >= 32767) {
                        // Handle the case of hat switches pretending to be axes
                        snprintf(defbuf, sizeof(defbuf), "j%da%d%c",
                            port, event->jaxis.axis,
                            event->jaxis.value > 0 ? '+' : '-');

                        ini_table_create_entry(iconf, inputinfo[confport]->name,
                            inputinfo[confport]->defs[confindex], defbuf);

                        jgrf_input_map_button(confport,
                            confindex - inputinfo[confport]->numaxes,
                            defbuf);

                        confindex++;
                        jgrf_inputcfg(inputinfo[confport]);
                        break;
                    }
                    if (abs(event->jaxis.value) >= BDEADZONE) {
                        snprintf(defbuf, sizeof(defbuf), "j%da%d%c",
                            port, event->jaxis.axis,
                            event->jaxis.value > 0 ? '+' : '-');

                        ini_table_create_entry(iconf, inputinfo[confport]->name,
                            inputinfo[confport]->defs[confindex], defbuf);

                        jgrf_input_map_button(confport,
                            confindex - inputinfo[confport]->numaxes,
                            defbuf);

                        confindex++;
                        jgrf_inputcfg(inputinfo[confport]);
                    }
                }
            }
            break;
        }
        case SDL_JOYHATMOTION: {
            if (confindex < inputinfo[confport]->numaxes) {
                jgrf_log(JG_LOG_WRN, "Trying to assign digital inputs to axes"
                    " is a losing endeavour. ESC to skip.\n");
                break;
            }

            SDL_Joystick *js = SDL_JoystickFromInstanceID(event->jhat.which);
            int port = SDL_JoystickGetPlayerIndex(js);

            if (event->jhat.value & SDL_HAT_UP)
                snprintf(defbuf, sizeof(defbuf), "j%dh00", port);

            else if (event->jhat.value & SDL_HAT_DOWN)
                snprintf(defbuf, sizeof(defbuf), "j%dh01", port);

            else if (event->jhat.value & SDL_HAT_LEFT)
                snprintf(defbuf, sizeof(defbuf), "j%dh02", port);

            else if (event->jhat.value & SDL_HAT_RIGHT)
                snprintf(defbuf, sizeof(defbuf), "j%dh03", port);

            if (event->jhat.value != 0) {
                jgrf_input_map_button(confport,
                    confindex - inputinfo[confport]->numaxes, defbuf);

                ini_table_create_entry(iconf, inputinfo[confport]->name,
                    inputinfo[confport]->defs[confindex], defbuf);

                confindex++;
                jgrf_inputcfg(inputinfo[confport]);
            }
            break;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if (confindex < inputinfo[confport]->numaxes)
                break;

            snprintf(defbuf, sizeof(defbuf), "mb%d", event->button.button);

            ini_table_create_entry(iconf, inputinfo[confport]->name,
                inputinfo[confport]->defs[confindex], defbuf);

            jgrf_input_map_button(confport,
                confindex - inputinfo[confport]->numaxes, defbuf);

            confindex++;
            jgrf_inputcfg(inputinfo[confport]);
            break;
        }
        default: {
            break;
        }
        case SDL_JOYDEVICEADDED: {
            jgrf_input_hotplug_add(event);
            break;
        }
        case SDL_JOYDEVICEREMOVED: {
            jgrf_input_hotplug_remove(event);
            break;
        }
    }
}

// Main input event handler
void jgrf_input_handler(SDL_Event *event) {
    if (confactive) {
        unsigned delay = 60; // Delay consecutive inputs by 60 ticks
        if (event->type == SDL_JOYAXISMOTION) {
            /* In some cases, there is an extra axis which takes input from
               two other axes (triggers), which shows up in the SDL event queue
               before the legitimate axes. This is a hack to ignore events on
               the extra axis, which will have an even index, since we count
               from 0 and axes typically come in pairs.
               Reference: https://github.com/atar-axis/xpadneo/issues/334
            */
            SDL_Joystick *js = SDL_JoystickFromInstanceID(event->jaxis.which);
            int port = SDL_JoystickGetPlayerIndex(js);
            int extra = SDL_JoystickNumAxes(joystick[port]) - 1;
            if ((event->jaxis.axis == extra) && !(extra & 1))
                return;

            delay = 420; // Larger delay required for axes
        }

        // Determine ticks since the last input definition was configured
        uint64_t delta = jgrf_getticks() - conftimer;

        // If the delta is large enough, pass the event to input config
        if (delta > delay)
            jgrf_inputcfg_handler(event);
        return;
    }
    else if (menuactive) {
        if (event->type == SDL_KEYUP)
            jgrf_menu_input_handler(event);
        return;
    }

    // This needs to be fixed and worked into the rest of the system one day...
    if (event->type == SDL_KEYUP || event->type == SDL_KEYDOWN) {
        switch (event->key.keysym.scancode) {
            case SDL_SCANCODE_ESCAPE: {
                jgrf_schedule_quit();
                break;
            }
            case SDL_SCANCODE_GRAVE: {
                jgrf_set_speed(event->type == SDL_KEYDOWN ? 1 : 0);
                break;
            }
            case SDL_SCANCODE_F1: {
                if (event->type == SDL_KEYUP) jgrf_reset(0);
                break;
            }
            case SDL_SCANCODE_F2: {
                if (event->type == SDL_KEYUP) jgrf_reset(1);
                break;
            }
            case SDL_SCANCODE_F3: {
                if (event->type == SDL_KEYUP) jgrf_media_insert();
                break;
            }
            case SDL_SCANCODE_F4: {
                if (event->type == SDL_KEYUP) jgrf_media_select();
                break;
            }
            case SDL_SCANCODE_F5: {
                if (event->type == SDL_KEYUP) jgrf_state_save(0);
                break;
            }
            case SDL_SCANCODE_F6: {
                if (event->type == SDL_KEYUP) jgrf_state_save(1);
                break;
            }
            case SDL_SCANCODE_F7: {
                if (event->type == SDL_KEYUP) jgrf_state_load(0);
                break;
            }
            case SDL_SCANCODE_F8: {
                if (event->type == SDL_KEYUP) jgrf_state_load(1);
                break;
            }
            case SDL_SCANCODE_F9: {
                if (event->type == SDL_KEYUP) jgrf_video_screenshot();
                break;
            }
            case SDL_SCANCODE_F12: {
                if (event->type == SDL_KEYUP) jgrf_cheats_toggle();
                break;
            }
            case SDL_SCANCODE_F: {
                if (event->type == SDL_KEYUP) jgrf_video_fullscreen();
                break;
            }
            case SDL_SCANCODE_1:
            case SDL_SCANCODE_2:
            case SDL_SCANCODE_3:
            case SDL_SCANCODE_4:
            case SDL_SCANCODE_5:
            case SDL_SCANCODE_6:
            case SDL_SCANCODE_7:
            case SDL_SCANCODE_8: {
            // Want to play 12-player sports games? Hand-edit the config file.
                if (event->type == SDL_KEYUP &&
                    (event->key.keysym.mod & KMOD_SHIFT)) {
                    confport =
                        atoi(SDL_GetScancodeName(event->key.keysym.scancode));

                    if (confport > gdata->numinputs)
                        break;

                    confport--;
                    confindex = 0;

                    jgrf_input_undef_port(confport);
                    jgrf_inputcfg(inputinfo[confport]);
                    confchanged = 1;
                }
                break;
            }
            case SDL_SCANCODE_TAB: {
                if (event->type == SDL_KEYUP) {
                    jgrf_input_menu_enable(1);
                    jgrf_menu_display();
                }
                break;
            }
            default: {
                break;
            }
        }
    }

    // Game input events
    switch(event->type) {
        case SDL_KEYUP: {
            *kbmap.key[event->key.keysym.scancode] = 0;
            break;
        }
        case SDL_KEYDOWN: {
            *kbmap.key[event->key.keysym.scancode] = 1;
            break;
        }
        case SDL_JOYBUTTONUP: {
            SDL_Joystick *js = SDL_JoystickFromInstanceID(event->jbutton.which);
            int port = SDL_JoystickGetPlayerIndex(js);
            *jsmap[port].button[event->jbutton.button] = 0;
            break;
        }
        case SDL_JOYBUTTONDOWN: {
            SDL_Joystick *js = SDL_JoystickFromInstanceID(event->jbutton.which);
            int port = SDL_JoystickGetPlayerIndex(js);
            *jsmap[port].button[event->jbutton.button] = 1;
            break;
        }
        case SDL_JOYAXISMOTION: {
            SDL_Joystick *js = SDL_JoystickFromInstanceID(event->jaxis.which);
            int port = SDL_JoystickGetPlayerIndex(js);
            *jsmap[port].axis[event->jaxis.axis] =
                abs(event->jaxis.value) > DEADZONE ? event->jaxis.value : 0;
            if (abs(event->jaxis.value) > BDEADZONE) {
                *jsmap[port].abtn[event->jaxis.axis * 2] =
                    event->jaxis.value < 0;
                *jsmap[port].abtn[(event->jaxis.axis * 2) + 1] =
                    event->jaxis.value > 0;
            }
            else {
                *jsmap[port].abtn[event->jaxis.axis * 2] = 0;
                *jsmap[port].abtn[(event->jaxis.axis * 2) + 1] = 0;
            }
            break;
        }
        case SDL_JOYHATMOTION: {
            SDL_Joystick *js = SDL_JoystickFromInstanceID(event->jhat.which);
            int port = SDL_JoystickGetPlayerIndex(js);
            *jsmap[port].hatpos[0] = event->jhat.value & SDL_HAT_UP;
            *jsmap[port].hatpos[1] = (event->jhat.value & SDL_HAT_DOWN) >> 2;
            *jsmap[port].hatpos[2] = (event->jhat.value & SDL_HAT_LEFT) >> 3;
            *jsmap[port].hatpos[3] = (event->jhat.value & SDL_HAT_RIGHT) >> 1;
            break;
        }
        case SDL_MOUSEMOTION: {
            jgrf_input_coords_scaled(event->motion.x, event->motion.y,
                &coreinput[msmap.index].coord[0],
                &coreinput[msmap.index].coord[1]);
            coreinput[msmap.index].axis[0] += event->motion.xrel;
            coreinput[msmap.index].axis[1] += event->motion.yrel;
            break;
        }
        case SDL_MOUSEBUTTONUP: {
            *msmap.button[event->button.button] = 0;
            break;
        }
        case SDL_MOUSEBUTTONDOWN: {
            *msmap.button[event->button.button] = 1;
            break;
        }
        case SDL_JOYDEVICEADDED: {
            jgrf_input_hotplug_add(event);
            break;
        }
        case SDL_JOYDEVICEREMOVED: {
            jgrf_input_hotplug_remove(event);
            break;
        }
        default: {
            break;
        }
    }
}

// Callback used by core to rumble the physical input device
void jgrf_input_rumble(int port, float strength, size_t len) {
    SDL_HapticRumblePlay(haptic[rumblemap[port]], strength, len);
}
