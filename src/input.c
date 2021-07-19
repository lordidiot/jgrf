/*
 * Copyright (c) 2020 Rupert Carmichael
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
#include "video.h"
#include "input.h"

#define MAXPORTS 12

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

// Array to keep track of what axes are triggers vs regular "stick" axes
static uint8_t trigger[MAXPORTS];

// Undefined inputs point here - this is like /dev/null for input events
static uint8_t undef8;
static int16_t undef16;

// Configuration related globals
static ini_table_s *iconf;
static int confactive = 0;
static int confchanged = 0;
static int confbtnactive = 0;
static int confhatactive = 0;
static int confindex = 0;
static int confport = 0;
static int axis = 0, axisnoise = 0;

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
    
    // Initialize joysticks
    jgrf_log(JG_LOG_INF, "%d joystick(s) found:\n", SDL_NumJoysticks());
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        joystick[i] = SDL_JoystickOpen(i);
        jgrf_log(JG_LOG_INF, "%s\n", SDL_JoystickName(joystick[i]));
        if (SDL_JoystickIsHaptic(joystick[i])) {
            haptic[i] = SDL_HapticOpenFromJoystick(joystick[i]);
            SDL_HapticRumbleInit(haptic[i]) < 0 ?
            jgrf_log(JG_LOG_DBG, "Force Feedback Enable Failed\n"):
            jgrf_log(JG_LOG_DBG, "Force Feedback Enabled\n");
        }
    }
    
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

// Retrieve inputinfo data so the frontend knows what the core has plugged in
void jgrf_input_query(jg_inputinfo_t* (*get_inputinfo)(int)) {
    for(int i = 0; i < gdata->numinputs; ++i) {
        inputinfo[i] = get_inputinfo(i);
        if (inputinfo[i]->name) {
            jgrf_log(JG_LOG_INF,
                "Core Input Port %d: %s, %s, %d axes, %d buttons\n",
                i + 1, inputinfo[i]->name, inputinfo[i]->fname,
                inputinfo[i]->numaxes, inputinfo[i]->numbuttons);
            jgrf_inputcfg_read(inputinfo[i]);
        }
        if (inputinfo[i]->type == JG_INPUT_GUN) {
            jgrf_video_set_cursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
        }
    }
    
    // Handle analog input seed values - fixes trigger input values at startup
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        for (int j = 0; j < SDL_JoystickNumAxes(joystick[i]); ++j) {
            int16_t aval = SDL_JoystickGetAxis(joystick[i], j);
            if (aval <= -(DEADZONE)) {
                *jsmap[i].axis[j] = aval;
                trigger[i] |= 1 << j; // it's a trigger
            }
        }
    }
}

// Pass pointers to input states into the core
void jgrf_input_set_states(void (*set_inputstate)(jg_inputstate_t*, int)) {
    gdata = jgrf_gdata_ptr();
    for(int i = 0; i < gdata->numinputs; ++i)
        set_inputstate(&coreinput[i], i);
}

// Calculate mouse input coordinates, taking into account aspect ratio and scale
static inline void jgrf_input_coords_relative(int32_t x, int32_t y,
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
        confbtnactive = 0;
        confhatactive = 0;
        jgrf_video_text(2, 0, ""); // Disable display of input config info
        return;
    }
    else
        confactive = 1;
    
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
        case SDL_JOYBUTTONUP: {
            confbtnactive = 0; // Set "button active" flag off
            break;
        }
        case SDL_JOYBUTTONDOWN: {
            if (confindex < inputinfo[confport]->numaxes) {
                jgrf_log(JG_LOG_WRN, "Trying to assign digital inputs to axes"
                    " is a losing endeavour. ESC to skip.\n");
                break;
            }
            
            /* Set the "button active" flag so that axis input associated with
               the button can be ignored
            */
            confbtnactive = 1;
            
            snprintf(defbuf, sizeof(defbuf), "j%db%d",
                event->jbutton.which, event->jbutton.button);
            
            ini_table_create_entry(iconf, inputinfo[confport]->name,
                inputinfo[confport]->defs[confindex], defbuf);
            
            jgrf_input_map_button(confport,
                confindex - inputinfo[confport]->numaxes, defbuf);
            
            confindex++;
            jgrf_inputcfg(inputinfo[confport]);
            break;
        }
        case SDL_JOYAXISMOTION: {
            /* Some gamepads report button + axis or hat + axis for the same
               input. Do not assign axis input for these cases.
            */
            if (confbtnactive || confhatactive)
                break;
            
            // Triggers require special handling
            if (trigger[event->jaxis.which] & (1 << event->jaxis.axis)) {
                // Axes set to axis input
                if (confindex < inputinfo[confport]->numaxes) {
                    if (event->jaxis.value >= BDEADZONE) {
                        axisnoise = 1;
                        axis = event->jaxis.axis;
                    }
                    else if (event->jaxis.value < -(BDEADZONE) && axisnoise &&
                        event->jaxis.axis == axis) {
                        axisnoise = 0;
                        
                        snprintf(defbuf, sizeof(defbuf), "j%da%d",
                            event->jaxis.which, event->jaxis.axis);
                        
                        ini_table_create_entry(iconf, inputinfo[confport]->name,
                            inputinfo[confport]->defs[confindex], defbuf);
                        
                        jgrf_input_map_axis(confport, confindex, defbuf);
                        
                        confindex++;
                        jgrf_inputcfg(inputinfo[confport]);
                    }
                }
                else { // Axes set to button input
                    if (event->jaxis.value >= BDEADZONE) {
                        axisnoise = 1;
                        axis = event->jaxis.axis;
                    }
                    else if (event->jaxis.value < -(BDEADZONE) && axisnoise &&
                        event->jaxis.axis == axis) {
                        axisnoise = 0;
                        
                        snprintf(defbuf, sizeof(defbuf), "j%da%d+",
                            event->jaxis.which, event->jaxis.axis);
                        
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
                        axisnoise = 1;
                        axis = event->jaxis.axis;
                    }
                    else if (abs(event->jaxis.value) < BDEADZONE && axisnoise &&
                        event->jaxis.axis == axis) {
                        axisnoise = 0;
                        
                        snprintf(defbuf, sizeof(defbuf), "j%da%d",
                            event->jaxis.which, event->jaxis.axis);
                        
                        ini_table_create_entry(iconf, inputinfo[confport]->name,
                            inputinfo[confport]->defs[confindex], defbuf);
                        
                        jgrf_input_map_axis(confport, confindex, defbuf);
                        
                        confindex++;
                        jgrf_inputcfg(inputinfo[confport]);
                    }
                }
                else { // Axes set to button input
                    if (!axisnoise && abs(event->jaxis.value) >= 32767) {
                        // Handle the case of hat switches pretending to be axes
                        snprintf(defbuf, sizeof(defbuf), "j%da%d%c",
                            event->jaxis.which, event->jaxis.axis,
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
                        axisnoise = 1;
                        axis = event->jaxis.axis;
                    }
                    else if (abs(event->jaxis.value) < BDEADZONE && axisnoise &&
                        event->jaxis.axis == axis) {
                        axisnoise = 0;
                        
                        snprintf(defbuf, sizeof(defbuf), "j%da%d%c",
                            event->jaxis.which, event->jaxis.axis,
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
            
            /* Set the "hat active" flag so that axis input associated with
               the hat switch can be ignored if necessary
            */
            confhatactive = event->jhat.value;
            
            if (event->jhat.value & SDL_HAT_UP)
                snprintf(defbuf, sizeof(defbuf), "j%dh00",
                    event->jaxis.which);
            
            else if (event->jhat.value & SDL_HAT_DOWN)
                snprintf(defbuf, sizeof(defbuf), "j%dh01",
                    event->jaxis.which);
            
            else if (event->jhat.value & SDL_HAT_LEFT)
                snprintf(defbuf, sizeof(defbuf), "j%dh02",
                    event->jaxis.which);
            
            else if (event->jhat.value & SDL_HAT_RIGHT)
                snprintf(defbuf, sizeof(defbuf), "j%dh03",
                    event->jaxis.which);
            
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
    }
}

// Main input event handler
void jgrf_input_handler(SDL_Event *event) {
    if (confactive) {
        jgrf_inputcfg_handler(event);
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
            case SDL_SCANCODE_F11: {
                if (event->type == SDL_KEYUP) jgrf_cheats_activate();
                break;
            }
            case SDL_SCANCODE_F12: {
                if (event->type == SDL_KEYUP) jgrf_cheats_deactivate();
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
                    event->key.keysym.mod & (KMOD_SHIFT|KMOD_CTRL)) {
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
            *jsmap[event->jbutton.which].button[event->jbutton.button] = 0;
            break;
        }
        case SDL_JOYBUTTONDOWN: {
            *jsmap[event->jbutton.which].button[event->jbutton.button] = 1;
            break;
        }
        case SDL_JOYAXISMOTION: {
            *jsmap[event->jaxis.which].axis[event->jaxis.axis] =
                abs(event->jaxis.value) > DEADZONE ? event->jaxis.value : 0;
            if (abs(event->jaxis.value) > BDEADZONE) {
                *jsmap[event->jaxis.which].abtn[event->jaxis.axis * 2] =
                    event->jaxis.value < 0;
                *jsmap[event->jaxis.which].abtn[(event->jaxis.axis * 2) + 1] =
                    event->jaxis.value > 0;
            }
            else {
                *jsmap[event->jaxis.which].abtn[event->jaxis.axis * 2] = 0;
                *jsmap[event->jaxis.which].abtn[(event->jaxis.axis * 2) +1] = 0;
            }
            break;
        }
        case SDL_JOYHATMOTION: {
            *jsmap[event->jhat.which].hatpos[0] =
                event->jhat.value & SDL_HAT_UP;
            *jsmap[event->jhat.which].hatpos[1] =
                event->jhat.value & SDL_HAT_DOWN;
            *jsmap[event->jhat.which].hatpos[2] =
                event->jhat.value & SDL_HAT_LEFT;
            *jsmap[event->jhat.which].hatpos[3] =
                event->jhat.value & SDL_HAT_RIGHT;
            break;
        }
        case SDL_MOUSEMOTION: {
            jgrf_input_coords_relative(event->motion.x, event->motion.y,
                &coreinput[msmap.index].coord[0],
                &coreinput[msmap.index].coord[1]);
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
        default: {
            break;
        }
    }
}

// Callback used by core to rumble the physical input device
void jgrf_input_rumble(int port, float strength, size_t len) {
    SDL_HapticRumblePlay(haptic[rumblemap[port]], strength, len);
}
