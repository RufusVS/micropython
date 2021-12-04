/*
 * This file is part of the Micro Python project, http://micropython.org/
 * This file was generated by micropython-extmod-generator https://github.com/prusnak/micropython-extmod-generator
 * from Python stab file pcnt.py
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
ESP32 Pulse Counter
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/pcnt.html
Wrapped around
https://github.com/espressif/esp-idf/blob/master/components/driver/include/driver/pcnt.h
https://github.com/espressif/esp-idf/blob/master/components/hal/include/hal/pcnt_types.h
https://github.com/espressif/esp-idf/blob/master/components/driver/pcnt.c
See also
https://github.com/espressif/esp-idf/tree/master/examples/peripherals/pcnt/pulse_count_event
*/

/*
ESP32 Quadrature Counter based on Pulse Counter(PCNT)
Based on
https://github.com/madhephaestus/ESP32Encoder
https://github.com/bboser/MicroPython_ESP32_psRAM_LoBo/blob/quad_decoder/MicroPython_BUILD/components/micropython/esp32/machine_dec.c
See also
https://github.com/espressif/esp-idf/tree/master/examples/peripherals/pcnt/rotary_encoder
*/

#include "py/obj.h"
#include "py/runtime.h"
#include "driver/pcnt.h"
#include "mphalport.h"
#include "modmachine.h"

#include "esp_err.h"
#include "driver/rmt.h"

#include "esp32_pcnt.h"

#define MP_PRN_LEVEL 1000

static pcnt_isr_handle_t pcnt_isr_handle = NULL;
static mp_pcnt_obj_t *pcnts[PCNT_UNIT_MAX + 1] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

// ***********************************
/* Decode what PCNT's unit originated an interrupt
 * and pass this information together with the event type
 * the main program using a queue.
 */
static void IRAM_ATTR pcnt_intr_handler(void *arg) {
    mp_pcnt_obj_t *self;
    uint32_t intr_status = PCNT.int_st.val;

    for (int i = 0; i <= PCNT_UNIT_MAX; ++i) {
        if (intr_status & (1 << i)) {
            self = pcnts[i];
            // Save the PCNT event type that caused an interrupt to pass it to the main program

            int64_t status = 0;
            if (PCNT.status_unit[i].h_lim_lat) {
                status = self->r_enc_config.counter_h_lim;
            } else if (PCNT.status_unit[i].l_lim_lat) {
                status = self->r_enc_config.counter_l_lim;
            }
            // pcnt_counter_clear(self->unit);
            PCNT.int_clr.val |= 1 << i; // clear the interrupt
            self->count = status + self->count;

            break;
        }
    }
}

// -------------------------------------------------------------------------------------------------------------
// Calculate the filter parameters based on an ns value
// 1 / 80MHz = 12.5ns - min filter period
// 12.5ns * 1023 = 12787.5ns - max filter period
//
#define ns_to_filter(ns) ((ns * (APB_CLK_FREQ / 1000000) + 500) / 1000)
#define filter_to_ns(filter) (filter * 1000 / (APB_CLK_FREQ / 1000000))

STATIC uint16_t get_filter_value(pcnt_unit_t unit) {
    uint16_t value;
    check_esp_err(pcnt_get_filter_value(unit, &value));

    return filter_to_ns(value);
}

STATIC void set_filter_value(pcnt_unit_t unit, int16_t value) {
    if ((value < 0) || (value > 1023)) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("correct filter value is 0..%d ns"), filter_to_ns(1023));
    }

    check_esp_err(pcnt_set_filter_value(unit, value));
    if (value)
        check_esp_err(pcnt_filter_enable(unit));
    else
        check_esp_err(pcnt_filter_disable(unit));
}

// ====================================================================================
// class Counter(object):
static void attach_Counter(mp_pcnt_obj_t *self) {
    if (self->attached) {
        mp_raise_msg(&mp_type_Exception, MP_ERROR_TEXT("already attached"));
    }

    int index = 0;
    for (; index <= PCNT_UNIT_MAX; index++) {
        if (pcnts[index] == NULL) {
            break;
        }
    }
    if (index > PCNT_UNIT_MAX) {
        mp_raise_msg(&mp_type_Exception, MP_ERROR_TEXT("too many counters"));
    }

    // Set data now that pin attach checks are done
    self->unit = (pcnt_unit_t)index;

    // Prepare configuration for the PCNT unit
    self->r_enc_config.pulse_gpio_num = self->aPinNumber; // Pulses
    self->r_enc_config.ctrl_gpio_num = self->bPinNumber;  // Direction

    self->r_enc_config.unit = self->unit;
    self->r_enc_config.channel = PCNT_CHANNEL_0;

    // What to do on the positive / negative edge of pulse input?
    if (self->edge != FALL) {
        self->r_enc_config.pos_mode = PCNT_COUNT_INC; // Count up on the positive edge
    } else {
        self->r_enc_config.pos_mode = PCNT_COUNT_DIS; // Keep the counter value on the positive edge
    }
    if (self->edge != RAISE) {
        self->r_enc_config.neg_mode = PCNT_COUNT_INC; // Count up on the negative edge
    } else {
        self->r_enc_config.neg_mode = PCNT_COUNT_DIS; // Keep the counter value on the negative edge

    }
    // What to do when control input is low or high?
    self->r_enc_config.lctrl_mode = PCNT_MODE_REVERSE, // Reverse counting direction if low
    self->r_enc_config.hctrl_mode = PCNT_MODE_KEEP,    // Keep the primary counter mode if high

    // Set the maximum and minimum limit values to watch
    self->r_enc_config.counter_h_lim = _INT16_MAX;
    self->r_enc_config.counter_l_lim = _INT16_MIN;

    check_esp_err(pcnt_unit_config(&self->r_enc_config));

    // Filter out bounces and noise
    set_filter_value(self->unit, self->filter);  // Filter Runt Pulses

    // Enable events on maximum and minimum limit values
    check_esp_err(pcnt_event_enable(self->unit, PCNT_EVT_H_LIM));
    check_esp_err(pcnt_event_enable(self->unit, PCNT_EVT_L_LIM));

    check_esp_err(pcnt_counter_pause(self->unit)); // Initial PCNT init
    // Register ISR handler and enable interrupts for PCNT unit
    if (pcnt_isr_handle == NULL) {
        check_esp_err(pcnt_isr_register(pcnt_intr_handler, (void *)NULL, (int)0, (pcnt_isr_handle_t *)&pcnt_isr_handle));
        if (pcnt_isr_handle == NULL) {
            mp_raise_msg(&mp_type_Exception, MP_ERROR_TEXT("encoder wrap interrupt failed"));
        }
    }
    check_esp_err(pcnt_intr_enable(self->unit));
    check_esp_err(pcnt_counter_clear(self->unit));
    self->count = 0;
    check_esp_err(pcnt_counter_resume(self->unit));

    pcnts[index] = self;
    self->attached = true;
}

// Defining Counter methods
STATIC void mp_machine_Counter_init_helper(mp_pcnt_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    enum { ARG_direction, ARG_edge, ARG_filter, ARG_scale };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_direction, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_edge, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_filter, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_scale, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_obj_t direction = args[ARG_direction].u_obj;
    if (direction != MP_OBJ_NULL) {
        if (mp_obj_is_type(direction, &mp_type_int)) {
            mp_obj_get_int(direction); // TODO
        } else {
            self->bPinNumber = machine_pin_get_id(direction);
        }
    }

    if (args[ARG_edge].u_int != -1) {
        if (!self->attached) {
            self->edge = args[ARG_edge].u_int;
        } else {
            mp_raise_msg(&mp_type_Exception, MP_ERROR_TEXT("use 'edge=' kwarg in Counter constructor"));
        }
    }

    if (args[ARG_filter].u_int != -1) {
        self->filter = ns_to_filter(args[ARG_filter].u_int);
        if (self->attached) {
            set_filter_value(self->unit, self->filter);
        }
    }

    if (args[ARG_scale].u_obj != MP_OBJ_NULL) {
        if (mp_obj_is_type(args[ARG_scale].u_obj, &mp_type_float)) {
            self->scale = mp_obj_get_float_to_f(args[ARG_scale].u_obj);
        } else if (mp_obj_is_type(args[ARG_scale].u_obj, &mp_type_int)) {
            self->scale = mp_obj_get_int(args[ARG_scale].u_obj);
        } else {
            mp_raise_TypeError(MP_ERROR_TEXT("scale argument muts be a number"));
        }
    }
}

// def Counter.__init__(pulsePin: int, dirPin: int=PCNT_PIN_NOT_USED, edge:int, filter:int=12787, scale:float=1.0)
STATIC mp_obj_t machine_Counter_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    MP_PRN(3, "1 machine_Counter_make_new n_args=%u n_kw=%u", n_args, n_kw);
    mp_arg_check_num(n_args, n_kw, 1, 2, true);

    // create Counter object for the given unit
    mp_pcnt_obj_t *self = m_new_obj(mp_pcnt_obj_t);
    self->base.type = &machine_Counter_type;

    self->attached = false;
    self->aPinNumber = machine_pin_get_id(args[0]);
    self->bPinNumber = PCNT_PIN_NOT_USED;
    if (n_args >= 2) {
        self->bPinNumber = machine_pin_get_id(args[1]);
    }
    self->edge = RAISE;
    self->scale = 1.0;
    self->filter = 1023;

    self->unit = (pcnt_unit_t)-1;

    // Process the remaining parameters
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    mp_machine_Counter_init_helper(self, n_args - n_args, args + n_args, &kw_args);

    attach_Counter(self);

    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t pcnt_PCNT_deinit(mp_obj_t self_obj) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);

    check_esp_err(pcnt_set_pin(self->unit, PCNT_CHANNEL_0, PCNT_PIN_NOT_USED, PCNT_PIN_NOT_USED));
    check_esp_err(pcnt_set_pin(self->unit, PCNT_CHANNEL_1, PCNT_PIN_NOT_USED, PCNT_PIN_NOT_USED));

    pcnts[self->unit] = NULL;

    m_del_obj(mp_pcnt_obj_t, self); // ???

    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pcnt_PCNT_deinit_obj, pcnt_PCNT_deinit);

STATIC void common_print_pin(const mp_print_t *print, mp_pcnt_obj_t *self) {
    mp_printf(print, "Pin(%u)", self->aPinNumber);
    if (self->bPinNumber != PCNT_PIN_NOT_USED) {
        mp_printf(print, ", Pin(%u)", self->bPinNumber);
    }
}

STATIC void common_print_kw(const mp_print_t *print, mp_pcnt_obj_t *self) {
    mp_printf(print, ", filter=%u", get_filter_value(self->unit));
    mp_printf(print, ", scale=%f", self->scale);
}

STATIC void machine_Counter_print(const mp_print_t *print, mp_obj_t self_obj, mp_print_kind_t kind) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);

    mp_printf(print, "Counter(");
    common_print_pin(print, self);
    mp_printf(print, ", edge=%s", self->edge == 1 ? "RAISE" : self->edge == 2 ? "FALL" : "RAISE | FALL");
    common_print_kw(print, self);
    mp_printf(print, ")");
}

// def PCNT.event_disable(self, evt_type: int)
/*
Disable PCNT event of PCNT unit

 @param evt_type Watch point event type.
                 All enabled events share the same interrupt (one interrupt per pulse counter unit).
 @note
     Can raise EspException:
     - ESP_ERR_INVALID_STATE pcnt driver has not been initialized
     - ESP_ERR_INVALID_ARG Parameter error
*/
/*
STATIC mp_obj_t pcnt_PCNT_event_disable(mp_obj_t self_obj, mp_obj_t evt_type_obj) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);
    mp_int_t evt_type = mp_obj_get_int(evt_type_obj);

    check_esp_err(pcnt_event_disable(self->unit, evt_type));

    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pcnt_PCNT_event_disable_obj, pcnt_PCNT_event_disable);
*/

// def PCNT.event_enable(self, evt_type: int)
/*
Enable PCNT event of PCNT unit

 @param evt_type Watch point event type.
                 All enabled events share the same interrupt (one interrupt per pulse counter unit).
 @note
     Can raise EspException:
     - ESP_ERR_INVALID_STATE pcnt driver has not been initialized
     - ESP_ERR_INVALID_ARG Parameter error
*/
/*
STATIC mp_obj_t pcnt_PCNT_event_enable(mp_obj_t self_obj, mp_obj_t evt_type_obj) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);
    mp_int_t evt_type = mp_obj_get_int(evt_type_obj);

    check_esp_err(pcnt_event_enable(self->unit, evt_type));

    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pcnt_PCNT_event_enable_obj, pcnt_PCNT_event_enable);
*/

// def PCNT.get_event_value(self, evt_type: int) -> int
/*
Get PCNT event value of PCNT unit

 @param evt_type Watch point event type.
                 All enabled events share the same interrupt (one interrupt per pulse counter unit).

 @return Value for PCNT event

 @note
     Can raise EspException:
     - ESP_ERR_INVALID_STATE pcnt driver has not been initialized
     - ESP_ERR_INVALID_ARG Parameter error
*/
/*
STATIC mp_obj_t pcnt_PCNT_get_event_value(mp_obj_t self_obj, mp_obj_t evt_type_obj) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);
    mp_int_t evt_type = mp_obj_get_int(evt_type_obj);

    int16_t count;
    check_esp_err(pcnt_get_event_value(self->unit, evt_type, &count));

    return MP_OBJ_NEW_SMALL_INT(count);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pcnt_PCNT_get_event_value_obj, pcnt_PCNT_get_event_value);
*/

// def PCNT.set_filter_value(self, filter_val: int)
/*
Set PCNT filter value

 @param filter_val PCNT signal filter value, counter in APB_CLK cycles.
  Any pulses lasting shorter than this will be ignored when the filter is enabled.
  @note
   filter_val is a 10-bit value, so the maximum filter_val should be limited to 1023.

 @note
     Can raise EspException:
     - ESP_ERR_INVALID_STATE pcnt driver has not been initialized
     - ESP_ERR_INVALID_ARG Parameter error
*/
STATIC mp_obj_t pcnt_PCNT_filter(size_t n_args, const mp_obj_t *args) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t val = get_filter_value(self->unit);
    if (n_args > 1) {
        set_filter_value(self->unit, ns_to_filter(mp_obj_get_int(args[1])));
    }
    return MP_OBJ_NEW_SMALL_INT(val);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pcnt_PCNT_filter_obj, 1, 2, pcnt_PCNT_filter);

// def PCNT.isr_handler_add(self, isr_handler: int, _args: int)
/*
Add ISR handler for specified unit.

 Call this function after using pcnt_isr_service_install() to
 install the PCNT driver's ISR handler service.

 The ISR handlers do not need to be declared with IRAM_ATTR,
 unless you pass the ESP_INTR_FLAG_IRAM flag when allocating the
 ISR in pcnt_isr_service_install().

 This ISR handler will be called from an ISR. So there is a stack
 size limit (configurable as "ISR stack size" in menuconfig). This
 limit is smaller compared to a global PCNT interrupt handler due
 to the additional level of indirection.

 @param isr_handler Interrupt handler function.
 @param args Parameter for handler function

 @note
     Can raise EspException:
     - ESP_ERR_INVALID_STATE pcnt driver has not been initialized
     - ESP_ERR_INVALID_ARG Parameter error
*/
/*
STATIC mp_obj_t pcnt_PCNT_isr_handler_add(mp_obj_t self_obj, mp_obj_t isr_handler_obj, mp_obj_t _args_obj) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);
    void *isr_handler = MP_OBJ_TO_PTR(isr_handler_obj);
    void *_args = MP_OBJ_TO_PTR(_args_obj);

    check_esp_err(pcnt_isr_handler_add(self->unit, isr_handler, _args));

    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pcnt_PCNT_isr_handler_add_obj, pcnt_PCNT_isr_handler_add);
*/

// def PCNT.isr_handler_remove(self)
/*
Delete ISR handler for specified unit.

 @note
     Can raise EspException:
     - ESP_ERR_INVALID_STATE pcnt driver has not been initialized
     - ESP_ERR_INVALID_ARG Parameter error
*/
/*
STATIC mp_obj_t pcnt_PCNT_isr_handler_remove(mp_obj_t self_obj) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);

    check_esp_err(pcnt_isr_handler_remove(self->unit));

    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pcnt_PCNT_isr_handler_remove_obj, pcnt_PCNT_isr_handler_remove);
*/

// def PCNT.isr_register(self, fn: int, arg: int, intr_alloc_flags: int, handle: int)
/*
Register PCNT interrupt handler, the handler is an ISR.
        The handler will be attached to the same CPU core that this function is running on.
        Please do not use pcnt_isr_service_install if this function was called.

 @param fn Interrupt handler function.
 @param arg Parameter for handler function
 @param intr_alloc_flags Flags used to allocate the interrupt. One or multiple (ORred)
        ESP_INTR_FLAG_* values. See esp_intr_alloc.h for more info.
 @param handle Pointer to return handle. If non-NULL, a handle for the interrupt will
        be returned here. Calling pcnt_isr_unregister to unregister this ISR service if needed,
        but only if the handle is not NULL.

 @note
     Can raise EspException:
     - ESP_ERR_NOT_FOUND Can not find the interrupt that matches the flags.
     - ESP_ERR_INVALID_ARG Function pointer error.
*/
/*
STATIC mp_obj_t pcnt_PCNT_isr_register(size_t n_args, const mp_obj_t *args) {
    // mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    void *fn = MP_OBJ_TO_PTR(args[1]);
    void *arg = MP_OBJ_TO_PTR(args[2]);
    mp_int_t intr_alloc_flags = mp_obj_get_int(args[3]);
    pcnt_isr_handle_t *handle = MP_OBJ_TO_PTR(args[4]);

    check_esp_err(pcnt_isr_register(fn, arg, intr_alloc_flags, handle));

    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pcnt_PCNT_isr_register_obj, 5, 5, pcnt_PCNT_isr_register);
*/

// def PCNT.isr_service_install(self, intr_alloc_flags: int)
/*
Install PCNT ISR service.
 @note
  We can manage different interrupt service for each unit.
  This function will use the default ISR handle service, Calling pcnt_isr_service_uninstall to
  uninstall the default service if needed. Please do not use pcnt_isr_register if this function was called.

 @param intr_alloc_flags Flags used to allocate the interrupt. One or multiple (ORred)
        ESP_INTR_FLAG_* values. See esp_intr_alloc.h for more info.

 @note
     Can raise EspException:
     - ESP_ERR_INVALID_STATE pcnt driver has not been initialized
     - ESP_ERR_NO_MEM No memory to install this service
     - ESP_ERR_INVALID_STATE ISR service already installed
*/
/*
STATIC mp_obj_t pcnt_PCNT_isr_service_install(mp_obj_t self_obj, mp_obj_t intr_alloc_flags_obj) {
    // mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);
    mp_int_t intr_alloc_flags = mp_obj_get_int(intr_alloc_flags_obj);

    check_esp_err(pcnt_isr_service_install(intr_alloc_flags));

    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pcnt_PCNT_isr_service_install_obj, pcnt_PCNT_isr_service_install);
*/

// def PCNT.isr_unregister(self, handle: int)
/*
Unregister PCNT interrupt handler (registered by pcnt_isr_register), the handler is an ISR.
The handler will be attached to the same CPU core that this function is running on.
If the interrupt service is registered by pcnt_isr_service_install, please call pcnt_isr_service_uninstall instead

 @param handle handle to unregister the ISR service.

 @note
     Can raise EspException:
     - ESP_ERR_NOT_FOUND Can not find the interrupt that matches the flags.
     - ESP_ERR_INVALID_ARG Function pointer error.
*/
/*
// It does not present in pcnt.h !!!
STATIC mp_obj_t pcnt_PCNT_isr_unregister(mp_obj_t self_obj, mp_obj_t handle_obj) {
	mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);
	mp_int_t handle = mp_obj_get_int(handle_obj);

	check_esp_err(pcnt_isr_unregister(handle));
    if (err != ESP_OK)
       mp_raise_EspError(err);

	return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pcnt_PCNT_isr_unregister_obj, pcnt_PCNT_isr_unregister);
*/

// def PCNT.pcnt_isr_service_uninstall(self)
/*
Uninstall PCNT ISR service, freeing related resources.
*/
/*
STATIC mp_obj_t pcnt_PCNT_pcnt_isr_service_uninstall(mp_obj_t self_obj) {
    // pcnt_PC*selfNT_obj_t  = MP_OBJ_TO_PTR(self_obj);

    pcnt_isr_service_uninstall();

    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pcnt_PCNT_pcnt_isr_service_uninstall_obj, pcnt_PCNT_pcnt_isr_service_uninstall);
*/

// def PCNT.set_event_value(self, evt_type: int, value: int)
/*
Set PCNT event value of PCNT unit

 @param evt_type Watch point event type.
                 All enabled events share the same interrupt (one interrupt per pulse counter unit).

 @param value Counter value for PCNT event

 @note
     Can raise EspException:
     - ESP_ERR_INVALID_STATE pcnt driver has not been initialized
     - ESP_ERR_INVALID_ARG Parameter error
*/
/*
STATIC mp_obj_t pcnt_PCNT_set_event_value(mp_obj_t self_obj, mp_obj_t evt_type_obj, mp_obj_t value_obj) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);
    mp_int_t evt_type = mp_obj_get_int(evt_type_obj);
    mp_int_t value = mp_obj_get_int(value_obj);

    check_esp_err(pcnt_set_event_value(self->unit, evt_type, value));

    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pcnt_PCNT_set_event_value_obj, pcnt_PCNT_set_event_value);
*/

// ====================================================================================
STATIC mp_obj_t pcnt_PCNT_set_count(mp_obj_t self_obj, mp_obj_t value_obj) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);
    int64_t value = mp_obj_get_ll_int(value_obj);

    int16_t count;
    check_esp_err(pcnt_get_counter_value(self->unit, &count));
    self->count = value - count;

    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pcnt_PCNT_set_count_obj, pcnt_PCNT_set_count);

// -----------------------------------------------------------------
STATIC mp_obj_t pcnt_PCNT_count(mp_obj_t self_obj) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);

    int16_t count;
    pcnt_get_counter_value(self->unit, &count);  // no error checking to speed up

    return mp_obj_new_int_from_ll(self->count + count);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pcnt_PCNT_count_obj, pcnt_PCNT_count);

// -----------------------------------------------------------------
STATIC mp_obj_t pcnt_PCNT_position(size_t n_args, const mp_obj_t *args) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    int16_t count;
    check_esp_err(pcnt_get_counter_value(self->unit, &count));
    if (n_args > 1) {
        int64_t new_count = mp_obj_get_float_to_f(args[1]) / self->scale;
        self->count = new_count - count;
    }
    return mp_obj_new_float_from_f((self->count + count) * self->scale);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pcnt_PCNT_position_obj, 1, 2, pcnt_PCNT_position);

// -----------------------------------------------------------------
STATIC mp_obj_t pcnt_PCNT_pause(mp_obj_t self_obj) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);

    check_esp_err(pcnt_counter_pause(self->unit));

    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pcnt_PCNT_pause_obj, pcnt_PCNT_pause);

// -----------------------------------------------------------------
STATIC mp_obj_t pcnt_PCNT_resume(mp_obj_t self_obj) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);

    check_esp_err(pcnt_counter_resume(self->unit));

    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pcnt_PCNT_resume_obj, pcnt_PCNT_resume);

// ====================================================================================
// Counter stuff
// Counter.init([kwargs])
STATIC mp_obj_t machine_Counter_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    MP_PRN(3, "30 machine_Counter_init n_args=%u", n_args);
    mp_machine_Counter_init_helper(args[0], n_args - 1, args + 1, kw_args);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_Counter_init_obj, 1, machine_Counter_init);

// Register class methods
#define COMMON_METHODS \
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&pcnt_PCNT_deinit_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_value), MP_ROM_PTR(&pcnt_PCNT_count_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_set_value), MP_ROM_PTR(&pcnt_PCNT_set_count_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_position), MP_ROM_PTR(&pcnt_PCNT_position_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_filter), MP_ROM_PTR(&pcnt_PCNT_filter_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_pause), MP_ROM_PTR(&pcnt_PCNT_pause_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_resume), MP_ROM_PTR(&pcnt_PCNT_resume_obj) },
    /*
    { MP_ROM_QSTR(MP_QSTR_intr_disable), MP_ROM_PTR(&pcnt_PCNT_intr_disable_obj) },
    { MP_ROM_QSTR(MP_QSTR_intr_enable), MP_ROM_PTR(&pcnt_PCNT_intr_enable_obj) },
    { MP_ROM_QSTR(MP_QSTR_isr_handler_add), MP_ROM_PTR(&pcnt_PCNT_isr_handler_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_isr_handler_remove), MP_ROM_PTR(&pcnt_PCNT_isr_handler_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_isr_register), MP_ROM_PTR(&pcnt_PCNT_isr_register_obj) },
    { MP_ROM_QSTR(MP_QSTR_isr_unregister), MP_ROM_PTR(&pcnt_PCNT_isr_unregister_obj) },
    { MP_ROM_QSTR(MP_QSTR_isr_service_install), MP_ROM_PTR(&pcnt_PCNT_isr_service_install_obj) },
    { MP_ROM_QSTR(MP_QSTR_pcnt_isr_service_uninstall), MP_ROM_PTR(&pcnt_PCNT_pcnt_isr_service_uninstall_obj) },

    { MP_ROM_QSTR(MP_QSTR_set_mode), MP_ROM_PTR(&pcnt_PCNT_set_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_pin), MP_ROM_PTR(&pcnt_PCNT_set_pin_obj) },

    { MP_ROM_QSTR(MP_QSTR_event_disable), MP_ROM_PTR(&pcnt_PCNT_event_disable_obj) },
    { MP_ROM_QSTR(MP_QSTR_event_enable), MP_ROM_PTR(&pcnt_PCNT_event_enable_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_event_value), MP_ROM_PTR(&pcnt_PCNT_get_event_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_event_value), MP_ROM_PTR(&pcnt_PCNT_set_event_value_obj) },
    */

STATIC const mp_rom_map_elem_t machine_Counter_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_Counter_init_obj) },
    COMMON_METHODS
    { MP_ROM_QSTR(MP_QSTR_RAISE), MP_ROM_INT(RAISE) },
    { MP_ROM_QSTR(MP_QSTR_FALL), MP_ROM_INT(FALL) },
};
STATIC MP_DEFINE_CONST_DICT(machine_Counter_locals_dict, machine_Counter_locals_dict_table);

// Create the class-object itself
const mp_obj_type_t machine_Counter_type = {
    { &mp_type_type },
    .name = MP_QSTR_Counter,
    .make_new = machine_Counter_make_new,
    .print = machine_Counter_print,
    .locals_dict = (mp_obj_dict_t *)&machine_Counter_locals_dict,
};

// ====================================================================================
// class Encoder(object):
static void attach_Encoder(mp_pcnt_obj_t *self) {
    if (self->attached) {
        mp_raise_msg(&mp_type_Exception, MP_ERROR_TEXT("already attached"));
    }

    int index = 0;
    for (; index < PCNT_UNIT_MAX; index++) {
        if (pcnts[index] == NULL) {
            break;
        }
    }
    if (index == PCNT_UNIT_MAX) {
        mp_raise_msg(&mp_type_Exception, MP_ERROR_TEXT("too many counters"));
    }

    // Set data now that pin attach checks are done
    self->unit = (pcnt_unit_t)index;

    // Set up encoder PCNT configuration
    self->r_enc_config.pulse_gpio_num = self->aPinNumber; // Rotary Encoder Chan A
    self->r_enc_config.ctrl_gpio_num = self->bPinNumber;  // Rotary Encoder Chan B

    self->r_enc_config.unit = self->unit;
    self->r_enc_config.channel = PCNT_CHANNEL_0;

    self->r_enc_config.pos_mode = (self->x124 != 1) ? PCNT_COUNT_DEC : PCNT_COUNT_DIS; // Count Only On Rising-Edges // X1
    self->r_enc_config.neg_mode = PCNT_COUNT_INC;   // Discard Falling-Edge

    self->r_enc_config.lctrl_mode = PCNT_MODE_KEEP;    // Rising A on HIGH B = CW Step
    self->r_enc_config.hctrl_mode = PCNT_MODE_REVERSE; // Rising A on LOW B = CCW Step

    // Set the maximum and minimum limit values to watch
    self->r_enc_config.counter_h_lim = _INT16_MAX;
    self->r_enc_config.counter_l_lim = _INT16_MIN;

    check_esp_err(pcnt_unit_config(&self->r_enc_config));

    if (self->x124 == 4) { // X4
        // set up second channel for full quad
        self->r_enc_config.pulse_gpio_num = self->bPinNumber; // make prior control into signal
        self->r_enc_config.ctrl_gpio_num = self->aPinNumber;    // and prior signal into control

        self->r_enc_config.unit = self->unit;
        self->r_enc_config.channel = PCNT_CHANNEL_1; // channel 1

        self->r_enc_config.pos_mode = PCNT_COUNT_DEC; // Count Only On Rising-Edges
        self->r_enc_config.neg_mode = PCNT_COUNT_INC;   // Discard Falling-Edge

        self->r_enc_config.lctrl_mode = PCNT_MODE_REVERSE;    // prior high mode is now low
        self->r_enc_config.hctrl_mode = PCNT_MODE_KEEP; // prior low mode is now high

        self->r_enc_config.counter_h_lim = _INT16_MAX;
        self->r_enc_config.counter_l_lim = _INT16_MIN;

        check_esp_err(pcnt_unit_config(&self->r_enc_config));
    } else { // make sure channel 1 is not set when not full quad
        self->r_enc_config.pulse_gpio_num = self->bPinNumber; // make prior control into signal
        self->r_enc_config.ctrl_gpio_num = self->aPinNumber;    // and prior signal into control

        self->r_enc_config.unit = self->unit;
        self->r_enc_config.channel = PCNT_CHANNEL_1; // channel 1

        self->r_enc_config.pos_mode = PCNT_COUNT_DIS; // disabling channel 1
        self->r_enc_config.neg_mode = PCNT_COUNT_DIS;   // disabling channel 1

        self->r_enc_config.lctrl_mode = PCNT_MODE_DISABLE;    // disabling channel 1
        self->r_enc_config.hctrl_mode = PCNT_MODE_DISABLE; // disabling channel 1

        self->r_enc_config.counter_h_lim = _INT16_MAX;
        self->r_enc_config.counter_l_lim = _INT16_MIN;

        check_esp_err(pcnt_unit_config(&self->r_enc_config));
    }

    // Filter out bounces and noise
    set_filter_value(self->unit, self->filter);  // Filter Runt Pulses

    /* Enable events on maximum and minimum limit values */
    check_esp_err(pcnt_event_enable(self->unit, PCNT_EVT_H_LIM));
    check_esp_err(pcnt_event_enable(self->unit, PCNT_EVT_L_LIM));

    check_esp_err(pcnt_counter_pause(self->unit)); // Initial PCNT init
    /* Register ISR handler and enable interrupts for PCNT unit */
    if (pcnt_isr_handle == NULL) {
        check_esp_err(pcnt_isr_register(pcnt_intr_handler, (void *)NULL, (int)0, (pcnt_isr_handle_t *)&pcnt_isr_handle));
        if (pcnt_isr_handle == NULL) {
            mp_raise_msg(&mp_type_Exception, MP_ERROR_TEXT("encoder wrap interrupt failed"));
        }
    }
    check_esp_err(pcnt_counter_clear(self->unit));
    self->count = 0;
    check_esp_err(pcnt_counter_resume(self->unit));
    check_esp_err(pcnt_intr_enable(self->unit));

    pcnts[index] = self;
    self->attached = true;
}

// -------------------------------------------------------------------------------------------------------------
// Defining Encoder methods
STATIC void mp_machine_Encoder_init_helper(mp_pcnt_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    enum { ARG_x124, ARG_filter, ARG_scale };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_x124, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_filter, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_scale, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    MP_PRN(3, "10, n_args=%d", n_args);
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    MP_PRN(3, "11, args[ARG_x124].u_int=%d", args[ARG_x124].u_int);
    MP_PRN(3, "11, args[ARG_filter].u_int=%d", args[ARG_filter].u_int);

    if (args[ARG_x124].u_int != -1) {
        if (!self->attached) {
            self->x124 = args[ARG_x124].u_int;
        } else {
            mp_raise_msg(&mp_type_Exception, MP_ERROR_TEXT("use 'x124=' kwarg in Encoder constructor"));
        }
    }

    if (args[ARG_filter].u_int != -1) {
        self->filter = ns_to_filter(args[ARG_filter].u_int);
        if (self->attached) {
            set_filter_value(self->unit, self->filter);
        }
    }

    if (args[ARG_scale].u_obj != MP_OBJ_NULL) {
        if (mp_obj_is_type(args[ARG_scale].u_obj, &mp_type_float)) {
            self->scale = mp_obj_get_float_to_f(args[ARG_scale].u_obj);
        } else if (mp_obj_is_type(args[ARG_scale].u_obj, &mp_type_float)) {
            self->scale = mp_obj_get_int(args[ARG_scale].u_obj);
        } else {
            mp_raise_TypeError(MP_ERROR_TEXT("scale argument muts be a number"));
        }
    }
}

// def Encoder.__init__(aPin: int, bPin: int, x124:int=2, filter:int=12787, scale:float=1.0)
STATIC mp_obj_t machine_Encoder_make_new(const mp_obj_type_t *t_ype, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    MP_PRN(3, "1 machine_Encoder_make_new n_args=%u n_kw=%u", n_args, n_kw);
    mp_arg_check_num(n_args, n_kw, 2, 2, true);

    // create Encoder object for the given unit
    mp_pcnt_obj_t *self = m_new_obj(mp_pcnt_obj_t);
    self->base.type = &machine_Encoder_type;

    self->attached = false;
    self->x124 = 2;
    self->scale = 1.0;
    self->filter = 1023;

    self->unit = (pcnt_unit_t)-1;

    self->aPinNumber = machine_pin_get_id(args[1]);
    self->bPinNumber = machine_pin_get_id(args[0]);  // a <--> b to compatible with IRQ-based encoders

    // Process the remaining parameters
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    mp_machine_Encoder_init_helper(self, n_args - n_args, args + n_args, &kw_args);

    attach_Encoder(self);

    return MP_OBJ_FROM_PTR(self);
}

// Encoder.init([kwargs])
STATIC mp_obj_t machine_Encoder_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    MP_PRN(3, "30 machine_Encoder_init n_args=%u", n_args);
    mp_machine_Encoder_init_helper(args[0], n_args - 1, args + 1, kw_args);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_Encoder_init_obj, 1, machine_Encoder_init);

STATIC void machine_Encoder_print(const mp_print_t *print, mp_obj_t self_obj, mp_print_kind_t kind) {
    mp_pcnt_obj_t *self = MP_OBJ_TO_PTR(self_obj);

    mp_printf(print, "Encoder(");
    common_print_pin(print, self);
    mp_printf(print, ", x124=%d", self->x124);
    common_print_kw(print, self);
    mp_printf(print, ")");
}

STATIC const mp_rom_map_elem_t pcnt_Encoder_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_Encoder_init_obj) },
    COMMON_METHODS
};
STATIC MP_DEFINE_CONST_DICT(pcnt_Encoder_locals_dict, pcnt_Encoder_locals_dict_table);

// Create the class-object itself
const mp_obj_type_t machine_Encoder_type = {
    { &mp_type_type },
    .name = MP_QSTR_Encoder,
    .print = machine_Encoder_print,
    .make_new = machine_Encoder_make_new,
    .locals_dict = (mp_obj_dict_t *)&pcnt_Encoder_locals_dict,
    // .parent = &machine_Counter_type,
};
