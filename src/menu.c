/*
 * Copyright (c) 2020-2022 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef __APPLE__
#define _POSIX_C_SOURCE 200112L
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>
#include <jg/jg.h>

#include "ezmenu.h"

#include "jgrf.h"
#include "input.h"
#include "settings.h"
#include "video.h"
#include "menu.h"

#define DESCSIZE 256
#define NUMLINES 256
#define TEXTSIZE 1024

enum _menumode {
    FRONTEND,
    EMULATOR,
    INPUT,
    SAVESETTINGS
};

typedef struct _menunode_t {
    struct _menunode_t *parent;
    struct _menunode_t *child;
    struct _menunode_t *next;
    char desc[DESCSIZE];
    int val;
} menunode_t;

// ezmenu context
static struct ezmenu ezm;

// Menu can remember its path in the menu 4 levels deep (8 bits per level)
static uint32_t menupath = 0;

// Pointer to the core's video information
static jg_videoinfo_t *vidinfo = NULL;

static jg_setting_t *settings = NULL;
static jg_setting_t *emusettings = NULL;
static size_t numemusettings = 0;

static menunode_t *menuroot = NULL;
static menunode_t *menulevel = NULL;

static char textbuf[TEXTSIZE]; // Buffer for all text on screen
static char *linebuf[NUMLINES]; // 64 lines of 255 + '\0' characters

static int menumode = 0;
static int settinglevel = 0;
static int settingactive = 0;
static int skip_redraw = 0;

static menunode_t* jgrf_menu_node_create(void) {
    return (menunode_t*)calloc(1, sizeof(menunode_t));
}

// Add a child node to another node, or a sibling if there is already a child
static menunode_t* jgrf_menu_node_add_child(menunode_t *parent) {
    // Create the new node and assign the pointer to its parent
    menunode_t *newnode = jgrf_menu_node_create();
    newnode->parent = parent;

    // If the parent node already had a child, add a sibling instead
    if (parent->child) {
        menunode_t *current = parent->child;
        while (current->next)
            current = current->next;
        current->next = newnode;
    }
    else {
        parent->child = newnode;
    }

    return newnode;
}

// Free a node and every node to the right or below it
static void jgrf_menu_node_free(menunode_t *node) {
    if (node->next)
        jgrf_menu_node_free(node->next);

    if (node->child)
        jgrf_menu_node_free(node->child);

    free(node);
    node = NULL;
}

// Generate the nodes for a settings subtree
static void jgrf_menu_gen(menunode_t *node, jg_setting_t *s, unsigned num) {
    // Generate the text to be displayed
    for (unsigned i = 0; i < num; ++i) {
        // Create a menu item node
        menunode_t *menuitem = jgrf_menu_node_add_child(node);
        snprintf(menuitem->desc, DESCSIZE, "%s", s[i].name);
        menuitem->val = i;

        // Add child nodes containing the settings for each menu item
        char buf[DESCSIZE];
        snprintf(buf, DESCSIZE, "%s", s[i].opts);
        char *bufptr;
        char *token = strtok_r(buf, ",", &bufptr);

        while (token != NULL) {
            while (*token == ' ')
                ++token;

            if (token[0] == 'N') { // Special case for numerical ranges
                int range = (s[i].max - s[i].min) + 1;
                for (int j = 0; j < range; ++j) {
                    menunode_t *setting = jgrf_menu_node_add_child(menuitem);
                    snprintf(setting->desc, DESCSIZE, "%d", j + s[i].min);
                    setting->val = j + s[i].min;
                }
            }
            else { // Regular nodes
                menunode_t *setting = jgrf_menu_node_add_child(menuitem);
                snprintf(setting->desc, DESCSIZE, "%s", token);
                char vbuf[DESCSIZE];
                snprintf(vbuf, DESCSIZE, "%s", token);
                char *vbufptr;
                char *vtoken = strtok_r(vbuf, " =", &vbufptr);
                setting->val = atoi(vtoken);
            }
            token = strtok_r(NULL, ",", &bufptr);
        }
    }
}

// Generate the nodes for Input Mapping
static void jgrf_menu_gen_input(menunode_t *node) {
    jgrf_gdata_t *gdata = jgrf_gdata_ptr();
    jg_inputinfo_t **inputinfo = jgrf_input_info_ptr();

    for (int i = 0; i < gdata->numinputs; ++i) {
        if (inputinfo[i]->name != NULL) {
            menunode_t *menuitem = jgrf_menu_node_add_child(node);
            snprintf(menuitem->desc, DESCSIZE, "%s", inputinfo[i]->name);
            menuitem->val = i;
        }
    }
}

// Change menu level
static void jgrf_menu_level(void) {
    menunode_t *node = menulevel;
    unsigned numlines = 0;
    ezmenu_setheader(&ezm, node->parent->desc);
    while (node) {
        snprintf(linebuf[numlines++], DESCSIZE, "%s", node->desc);
        node = node->next;
    }
    ezmenu_setlines(&ezm, linebuf, numlines);
}

// Redraw the text to be displayed on screen
void jgrf_menu_text_redraw(void) {
    textbuf[0] = '\0';
    for (int i = 0; i < ezm.h; ++i) {
        // menu line being drawn (offset from header)
        int mline = i - ezm.start;

        // active setting in relation to scroll offset
        int active = settingactive - ezm.yscroll;

        if (i == ezm.vissel)
            strcat(textbuf, "+ ");
        else if (i > 1 && strlen(ezm.vislines[i]))
            strcat(textbuf, "- ");

        strcat(textbuf, ezm.vislines[i]);

        if (settinglevel && i >= ezm.start && i < ezm.end && active == mline)
            strcat(textbuf, " *\n");
        else
            strcat(textbuf, "\n");
    }
    jgrf_video_text(2, 1, textbuf);

    /* Ensure the menu can be redrawn if this function is called explicitly
       after redrawing has been disabled
    */
    skip_redraw = 0;
}

static void jgrf_menu_select_frontend(int item) {
    menunode_t *node = menulevel;
    // Seek to the correct node
    for (int i = 0; i < item; ++i)
        node = node->next;

    if (!settinglevel && !node->child->child)
        settinglevel = 1;

    // Check if there is a level below
    if (node->child) {
        menulevel = node->child;
        jgrf_menu_level();
        if (settinglevel)
            settingactive = settings[item].val - settings[item].min;
    }
    else { // No child, means it has a value to set
        settings[node->parent->val].val = node->val;
        settinglevel = 1;
        settingactive = item;
        jgrf_rehash_frontend();
        jgrf_log(JG_LOG_SCR, "%s: %s%s",
            node->parent->desc, node->desc,
            settings[node->parent->val].restart ?
            " (restart required)" : ""
        );
        menupath >>= 8;
    }
}

static void jgrf_menu_select_emu(int item) {
    menunode_t *node = menulevel;
    // Seek to the correct node
    for (int i = 0; i < item; ++i)
        node = node->next;

    if (!settinglevel && !node->child->child)
        settinglevel = 1;

    // Check if there is a level below
    if (node->child) {
        menulevel = node->child;
        jgrf_menu_level();
        if (settinglevel)
            settingactive = emusettings[item].val - emusettings[item].min;
    }
    else { // No child, means it has a value to set or there are no settings
        if (numemusettings) {
            emusettings[node->parent->val].val = node->val;
            settinglevel = 1;
            settingactive = item;
            jgrf_rehash_core();
            jgrf_log(JG_LOG_SCR, "%s: %s%s",
                node->parent->desc, node->desc,
                emusettings[node->parent->val].restart ?
                " (restart required)" : ""
            );
        }
        else {
            jgrf_log(JG_LOG_SCR, "No Emulator Settings");
        }
        menupath >>= 8;
    }
}

static void jgrf_menu_select_input(int item) {
    menunode_t *node = menulevel;
    // Seek to the correct node
    for (int i = 0; i < item; ++i) {
        node = node->next;
    }

    // Check if there is a level below
    if (node->child) {
        menulevel = node->child;
        jgrf_menu_level();
    }
    else { // No child, means it can be configured
        jgrf_input_config_enable(1);
        jgrf_input_config(item);
        skip_redraw = 1;
    }
}

static void jgrf_menu_select_save(int item) {
    menunode_t *node = menulevel;
    // Seek to the correct node
    for (int i = 0; i < item; ++i)
        node = node->next;

    // Check if there is a level below
    if (node->child) {
        menulevel = node->child;
        jgrf_menu_level();
    }
    else {
        switch (item) {
            case 0: {
                jgrf_settings_write(SETTINGS_FRONTEND);
                jgrf_log(JG_LOG_SCR, "Saved Frontend Settings");
                break;
            }
            case 1: {
                if (numemusettings) {
                    jgrf_settings_write(SETTINGS_EMULATOR);
                    jgrf_log(JG_LOG_SCR, "Saved Emulator Settings");
                }
                else {
                    jgrf_log(JG_LOG_SCR, "No Emulator Settings");
                }
                break;
            }
            case 2: {
                jgrf_settings_write(SETTINGS_FRONTEND|SETTINGS_EMULATOR);
                jgrf_log(JG_LOG_SCR, "Saved Combined Settings");
                break;
            }
            case 3: {
                jgrf_settings_default(SETTINGS_FRONTEND);
                jgrf_log(JG_LOG_SCR, "Restored Frontend Defaults");
                break;
            }
            case 4: {
                jgrf_settings_default(SETTINGS_EMULATOR);
                jgrf_log(JG_LOG_SCR, "Restored Emulator Defaults");
                break;
            }
        }
        menupath >>= 8;
    }
}

// Initialize and display the menu on screen
void jgrf_menu_display(void) {
    // Grab the frontend settings
    settings = jgrf_settings_ptr();

    // Grab the emulator settings
    emusettings = jgrf_settings_emu_ptr(&numemusettings);

    menuroot = jgrf_menu_node_create();
    snprintf(menuroot->desc, DESCSIZE, "Jolly Good Menu");
    menunode_t *node;

    node = jgrf_menu_node_add_child(menuroot);
    snprintf(node->desc, DESCSIZE, "Frontend Settings");
    jgrf_menu_gen(node, settings, JGRF_SETTINGS_MAX);

    node = jgrf_menu_node_add_child(menuroot);
    snprintf(node->desc, DESCSIZE, "Emulator Settings");
    jgrf_menu_gen(node, emusettings, numemusettings);

    node = jgrf_menu_node_add_child(menuroot);
    snprintf(node->desc, DESCSIZE, "Map Inputs");
    jgrf_menu_gen_input(node);

    menunode_t *savenode = jgrf_menu_node_add_child(menuroot);
    snprintf(savenode->desc, DESCSIZE, "Save/Restore Settings");

    node = jgrf_menu_node_add_child(savenode);
    snprintf(node->desc, DESCSIZE, "Save Frontend Settings");

    node = jgrf_menu_node_add_child(savenode);
    snprintf(node->desc, DESCSIZE, "Save Emulator Settings");

    node = jgrf_menu_node_add_child(savenode);
    snprintf(node->desc, DESCSIZE, "Save Combined Settings");

    node = jgrf_menu_node_add_child(savenode);
    snprintf(node->desc, DESCSIZE, "Restore Frontend Defaults");

    node = jgrf_menu_node_add_child(savenode);
    snprintf(node->desc, DESCSIZE, "Restore Emulator Defaults");

    for (unsigned i = 0; i < NUMLINES; ++i)
        linebuf[i] = (char*)calloc(DESCSIZE, 1);

    menulevel = menuroot->child;
    menupath = 0;
    ezmenu_init(&ezm, vidinfo->w, vidinfo->h, 10, 12);
    ezmenu_setheader(&ezm, menuroot->desc);
    ezmenu_setfooter(&ezm, "");
    ezm.wraparound = 1;
    jgrf_menu_level();
    ezmenu_update(&ezm);
    jgrf_menu_text_redraw();
}

void jgrf_menu_input_handler(SDL_Event *event) {
    switch (event->key.keysym.scancode) {
        case SDL_SCANCODE_TAB: case SDL_SCANCODE_LEFT: {
            settinglevel = 0;
            if (menulevel != menuroot->child) {
                menulevel = menulevel->parent->parent->child;
                jgrf_menu_level();
                ezm.sel = menupath & 0xff;
                menupath >>= 8;
                ezmenu_update(&ezm);
                jgrf_menu_text_redraw();
                break;
            }
        }
        // fallthrough
        case SDL_SCANCODE_ESCAPE: {
            // Free the memory allocated for the menu tree
            jgrf_menu_node_free(menuroot);
            jgrf_video_text(2, 0, "");
            free(ezm.vislines);
            for (unsigned i = 0; i < NUMLINES; ++i)
                free(linebuf[i]);
            jgrf_input_menu_enable(0);
            settinglevel = 0;
            break;
        }
        case SDL_SCANCODE_UP: {
            ezmenu_userinput(&ezm, EZM_UP);
            jgrf_menu_text_redraw();
            break;
        }
        case SDL_SCANCODE_DOWN: {
            ezmenu_userinput(&ezm, EZM_DOWN);
            jgrf_menu_text_redraw();
            break;
        }
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_RIGHT: {
            menupath = (menupath << 8) | ezm.sel;
            if (menulevel == menuroot->child)
                menumode = ezm.sel;

            switch (menumode) {
                case FRONTEND: jgrf_menu_select_frontend(ezm.sel); break;
                case EMULATOR: jgrf_menu_select_emu(ezm.sel); break;
                case INPUT: jgrf_menu_select_input(ezm.sel); break;
                case SAVESETTINGS: jgrf_menu_select_save(ezm.sel); break;
            }

            ezmenu_update(&ezm);
            if (!skip_redraw)
                jgrf_menu_text_redraw();
            break;
        }
        default: {
            break;
        }
    }
}

void jgrf_menu_set_vinfo(jg_videoinfo_t *ptr) {
    vidinfo = ptr;
}
