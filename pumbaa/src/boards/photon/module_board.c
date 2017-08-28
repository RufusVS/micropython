/**
 * @file module_board.c
 *
 * @section License
 * Copyright (C) 2016, Erik Moqvist
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * This file is part of the Pumbaa project.
 */

#include "pumbaa.h"

/**
 * Function called when this module is imported.
 */
static mp_obj_t module_init(void)
{
    return (mp_const_none);
}

static MP_DEFINE_CONST_FUN_OBJ_0(module_init_obj, module_init);

/**
 * PIN_LED
 */
static const mp_rom_map_elem_t module_board_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_board) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&module_init_obj) },
    
    /* Pins. */
    { MP_ROM_QSTR(MP_QSTR_PIN_D0), MP_ROM_INT(23) },
    { MP_ROM_QSTR(MP_QSTR_PIN_D1), MP_ROM_INT(22) },
    { MP_ROM_QSTR(MP_QSTR_PIN_D2), MP_ROM_INT(21) },
    { MP_ROM_QSTR(MP_QSTR_PIN_D3), MP_ROM_INT(20) },
    { MP_ROM_QSTR(MP_QSTR_PIN_D4), MP_ROM_INT(19) },
    { MP_ROM_QSTR(MP_QSTR_PIN_D5), MP_ROM_INT(15) },
    { MP_ROM_QSTR(MP_QSTR_PIN_D6), MP_ROM_INT(14) },
    { MP_ROM_QSTR(MP_QSTR_PIN_D7), MP_ROM_INT(13) },

    { MP_ROM_QSTR(MP_QSTR_PIN_LED), MP_ROM_INT(13) },
};

static MP_DEFINE_CONST_DICT(module_board_globals, module_board_globals_table);

/**
 * Board module type.
 */
const mp_obj_module_t module_board = {
    { &mp_type_module },
    .globals = (mp_obj_t)&module_board_globals,
};
