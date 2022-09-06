/*
 * Copyright (c) 2020-2022 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#define _POSIX_C_SOURCE 200112L
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
#define NUMLINES 64
#define TEXTSIZE 1024

enum _menumode {
    FRONTEND,
    EMULATOR,
    INPUT
};

typedef struct _menunode_t {
    struct _menunode_t *parent;
    struct _menunode_t *child;
    struct _menunode_t *next;
    char desc[DESCSIZE];
    int val;
} menunode_t;

static struct ezmenu ezm;

static jg_setting_t *emusettings = NULL;
static size_t numemusettings = 0;

static menunode_t *menuroot = NULL;
static menunode_t *menulevel = NULL;

static char textbuf[TEXTSIZE]; // Buffer for all text on screen
static char *linebuf[NUMLINES]; // 64 lines of 255 + '\0' characters

static int menumode = 0;

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

// Generate the nodes for Emulator Settings
static void jgrf_menu_emusettings_generate(menunode_t *node) {
    // Grab the emulator settings
    emusettings = jgrf_settings_emu_ptr(&numemusettings);

    // Generate the text to be displayed
    for (unsigned i = 0; i < numemusettings; ++i) {
        // Create a menu item node
        menunode_t *menuitem = jgrf_menu_node_add_child(node);
        snprintf(menuitem->desc, DESCSIZE, "%s", emusettings[i].name);
        menuitem->val = i;

        // Add child nodes containing the settings for each menu item
        char buf[DESCSIZE];
        snprintf(buf, DESCSIZE, "%s", emusettings[i].desc);
        char *bufptr;
        char *token = strtok_r(buf, ",", &bufptr);

        while (token != NULL) {
            while (*token == ' ')
                ++token;

            if (token[0] == 'N') { // Special case for numerical ranges
                int range = (emusettings[i].max - emusettings[i].min) + 1;
                for (int j = 0; j < range; ++j) {
                    menunode_t *setting = jgrf_menu_node_add_child(menuitem);
                    snprintf(setting->desc, DESCSIZE, "%d",
                        j + emusettings[i].min);
                    setting->val = j + emusettings[i].min;
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
static void jgrf_menu_text_redraw(void) {
    textbuf[0] = '\0';
    for (int i = 0; i < ezm.h; ++i) {
        if (i != ezm.vissel && i > 1)
            strcat(textbuf, " ");
        strcat(textbuf, ezm.vislines[i]);
        strcat(textbuf, "\n");
    }
    jgrf_video_text(2, 1, textbuf);
}

static void jgrf_menu_select_emu(int item) {
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
    else { // No child, means it has a value to set or there are no settings
        if (numemusettings) {
            emusettings[node->parent->val].val = node->val;
            jgrf_rehash();
        }
        else {
            jgrf_log(JG_LOG_SCR, "No Emulator Settings");
        }
    }
}

// Initialize and display the menu on screen
void jgrf_menu_display(void) {
    menuroot = jgrf_menu_node_create();
    snprintf(menuroot->desc, DESCSIZE, "Jolly Good Menu");
    menunode_t *node;

    node = jgrf_menu_node_add_child(menuroot);
    snprintf(node->desc, DESCSIZE, "Frontend Settings");

    node = jgrf_menu_node_add_child(menuroot);
    snprintf(node->desc, DESCSIZE, "Emulator Settings");
    jgrf_menu_emusettings_generate(node);

    /*node = jgrf_menu_node_add_child(menuroot);
    snprintf(node->desc, DESCSIZE, "Map Inputs");*/

    for (unsigned i = 0; i < NUMLINES; ++i)
        linebuf[i] = (char*)calloc(DESCSIZE, 1);

    menulevel = menuroot->child;
    ezmenu_init(&ezm, 400, 320, 20, 24); // FIXME arbitrary values...
    ezmenu_setheader(&ezm, menuroot->desc);
    ezmenu_setfooter(&ezm, "");
    ezm.wraparound = 1;
    jgrf_menu_level();
    ezmenu_update(&ezm);
    jgrf_menu_text_redraw();
}

void jgrf_menu_input_handler(SDL_Event *event) {
    switch (event->key.keysym.scancode) {
        case SDL_SCANCODE_TAB: case SDL_SCANCODE_ESCAPE: {
            if (menulevel != menuroot->child) {
                menulevel = menulevel->parent->parent->child;
                jgrf_menu_level();
                ezmenu_update(&ezm);
                jgrf_menu_text_redraw();
                return;
            }
            // Free the memory allocated for the menu tree
            jgrf_menu_node_free(menuroot);
            jgrf_video_text(2, 0, "");
            free(ezm.vislines);
            for (unsigned i = 0; i < NUMLINES; ++i)
                free(linebuf[i]);
            jgrf_input_menu_enable(0);
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
        case SDL_SCANCODE_RETURN: {
            if (menulevel == menuroot->child)
                menumode = ezm.sel;

            switch (menumode) {
                case FRONTEND: jgrf_log(JG_LOG_SCR, "Unavailable"); break;
                case EMULATOR: jgrf_menu_select_emu(ezm.sel); break;
                case INPUT: break;
            }

            ezmenu_update(&ezm);
            jgrf_menu_text_redraw();
            break;
        }
        default: {
            break;
        }
    }
}
