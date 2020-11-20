/*
 * This file was generated by micropython-extmod-generator https://github.com/prusnak/micropython-extmod-generator
 * from Python stab file esp_err.py
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Ihor Nehrutsa
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
Error Codes and Helper Functions
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_err.html

Wrapped around
https://github.com/espressif/esp-idf/blob/master/components/esp_common/include/esp_err.h
*/

#define MODULE_ESP_ERR_ENABLED (1)
#if MODULE_ESP_ERR_ENABLED

// Include required definitions first
#include "py/obj.h"
#include "py/objexcept.h"


/*
// Example how to raise ESP exception from C code

#include "esp_error.h"

ESP_EXCEPTIONS(pcnt_event_disable(self->unit, evt_type))

// is equivalent to

esp_err_t err = pcnt_event_disable(self->unit, evt_type);
if (err != ESP_OK)
    mp_raise_EspError(err);
*/

// Defining classes
// class EspError(Exception):
typedef struct _mp_obj_esp_err_EspError_t {
    mp_obj_base_t base;
} mp_obj_esp_err_EspError_t;

const mp_obj_type_t mp_type_EspError;
/*
// Defining EspError methods
// def EspError.__init__(self, error_code: int=0, error_msg: str='')
STATIC mp_obj_t esp_err_EspError_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 2, false);
    self->base.type = &mp_type_EspError;

	mp_obj_esp_err_EspError_t *self = MP_OBJ_TO_PTR(args[0]);
	mp_int_t error_code = mp_obj_get_int(args[1]);
	const char* error_msg = mp_obj_str_get_str(args[2]);

	//TODO: Your code here
    return MP_OBJ_FROM_PTR(self);
}
*/
/*
STATIC mp_obj_t esp_err_EspError_print(const mp_print_t *print, mp_obj_t self_obj, mp_print_kind_t kind) {
    esp_err_EspError_obj_t *self = MP_OBJ_TO_PTR(self_obj);
    mp_printf(print, "EspError()");

	//TODO: Your code here
}
*/

STATIC mp_obj_t esp_err_EspError_esp_err_to_name(mp_obj_t self_obj, mp_obj_t err_obj) {
	//mp_obj_esp_err_EspError_t *self = MP_OBJ_TO_PTR(self_obj);
	mp_int_t err = mp_obj_get_int(err_obj);

    const char *s = esp_err_to_name(err);

    return mp_obj_new_str(s, strlen(s));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(esp_err_EspError_esp_err_to_name_obj, esp_err_EspError_esp_err_to_name);

// EspError stuff
// Register class methods
STATIC const mp_rom_map_elem_t esp_err_EspError_locals_dict_table[] = {
    //{ MP_ROM_QSTR(MP_QSTR_ESP_ERR_FLASH_BASE), MP_ROM_INT(ESP_ERR_FLASH_BASE) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_INVALID_ARG), MP_ROM_INT(ESP_ERR_INVALID_ARG) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_INVALID_CRC), MP_ROM_INT(ESP_ERR_INVALID_CRC) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_INVALID_MAC), MP_ROM_INT(ESP_ERR_INVALID_MAC) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_INVALID_RESPONSE), MP_ROM_INT(ESP_ERR_INVALID_RESPONSE) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_INVALID_SIZE), MP_ROM_INT(ESP_ERR_INVALID_SIZE) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_INVALID_STATE), MP_ROM_INT(ESP_ERR_INVALID_STATE) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_INVALID_VERSION), MP_ROM_INT(ESP_ERR_INVALID_VERSION) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_MESH_BASE), MP_ROM_INT(ESP_ERR_MESH_BASE) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_NOT_FOUND), MP_ROM_INT(ESP_ERR_NOT_FOUND) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_NOT_SUPPORTED), MP_ROM_INT(ESP_ERR_NOT_SUPPORTED) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_NO_MEM), MP_ROM_INT(ESP_ERR_NO_MEM) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_TIMEOUT), MP_ROM_INT(ESP_ERR_TIMEOUT) },
    { MP_ROM_QSTR(MP_QSTR_ESP_ERR_WIFI_BASE), MP_ROM_INT(ESP_ERR_WIFI_BASE) },
    { MP_ROM_QSTR(MP_QSTR_ESP_FAIL), MP_ROM_INT(ESP_FAIL) },
    { MP_ROM_QSTR(MP_QSTR_ESP_OK), MP_ROM_INT(ESP_OK) },
    { MP_ROM_QSTR(MP_QSTR_err_to_name), MP_ROM_PTR(&esp_err_EspError_esp_err_to_name_obj) }, \
};
STATIC MP_DEFINE_CONST_DICT(esp_err_EspError_locals_dict, esp_err_EspError_locals_dict_table);

// Create the class-object itself
const mp_obj_type_t mp_type_EspError = {
    { &mp_type_type },
    .name = MP_QSTR_EspError,

    //.make_new = esp_err_EspError_make_new,
    //.print = esp_err_EspError_print,
    .locals_dict = (mp_obj_dict_t*)&esp_err_EspError_locals_dict,

    .print = mp_obj_exception_print,
    .make_new = mp_obj_exception_make_new,
    .attr = mp_obj_exception_attr,
    .parent = &mp_type_Exception,
};

/*
// To call from C modules use like:
    mp_raise_msg_varg(&mp_type_EspError, MP_ERROR_TEXT("An error message %d"), val);
*/
/*
// Function to call from C modules
NORETURN void mp_raise_EspError(mp_rom_error_text_t msg) {
    mp_raise_msg(&mp_type_EspError, msg);
}
*/

#endif // MODULE_ESP_ERR_ENABLED
