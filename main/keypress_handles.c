/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 * Copyright 2018 Gal Zaidenstein.
 */

#ifndef KEYPRESS_HANDLES_C
#define KEYPRESS_HANDLES_C
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "keymap.c"
#include "matrix.h"
#include "hal_ble.h"
#include "oled_tasks.h"
#include "nvs_keymaps.h"
#include "nvs_funcs.h"
#include "plugin_manager.h"
#include "rgb_led.h"
#include "gesture_handles.h"
#include "esp_timer.h"

#include "keys.h"

static const char *TAG = "KEY_PRESS";

#define TRUNC_SIZE 20
#define DEBUG_REPORT

/*
 * Current state of the keymap,each cell will hold the location of the key in the key report,
 *if a key is not in the report it will be set to 0
 */

uint8_t KEY_STATE[MATRIX_ROWS][KEYMAP_COLS] = {0};
uint16_t led_status = 0;
uint8_t modifier = 0;
uint16_t keycode = 0;

// Sizing the report for N-key rollover
uint8_t current_report[REPORT_LEN] = {0};

// Array to send when releasing macros
uint8_t macro_release[3] = {0};

// Flag in order to know when to ignore layer change on layer hold
uint8_t layer_hold_flag = 0;
uint8_t prev_layout = 0;

// checking if a modifier key was pressed
uint16_t check_modifier(uint16_t key)
{

	uint8_t cur_mod = 0;
	// these are the modifier keys
	if ((KC_LCTRL <= key) && (key <= KC_RGUI))
	{
		cur_mod = (1 << (key - KC_LCTRL));
		return cur_mod;
	}
	return 0;
}

// checking if a led status key was pressed, returning current led status
uint16_t check_led_status(uint16_t key)
{

	switch (key)
	{
	case KC_NLCK:
		return 1;

	case KC_CAPS:
		return 2;

	case KC_SLCK:
		return 3;
	}
	return 0;
}

// what to do on a media key press
void media_control_send(uint16_t keycode)
{

	uint8_t media_state[2] = {0};
	if (keycode == KC_MEDIA_NEXT_TRACK)
	{
		media_state[1] = 10;
	}
	if (keycode == KC_MEDIA_PREV_TRACK)
	{
		media_state[1] = 11;
	}
	if (keycode == KC_MEDIA_STOP)
	{
		media_state[1] = 12;
	}
	if (keycode == KC_MEDIA_PLAY_PAUSE)
	{
		media_state[1] = 5;
	}
	if (keycode == KC_AUDIO_MUTE)
	{
		media_state[1] = 1;
	}
	if (keycode == KC_AUDIO_VOL_UP)
	{
		SET_BIT(media_state[0], 6);
	}
	if (keycode == KC_AUDIO_VOL_DOWN)
	{
		SET_BIT(media_state[0], 7);
	}

	xQueueSend(media_q, (void *)&media_state, (TickType_t)0);
}

void media_control_release(uint16_t keycode)
{
	uint8_t media_state[2] = {0};
	xQueueSend(media_q, (void *)&media_state, (TickType_t)0);
}

// used for debouncing
static uint32_t millis()
{
	return esp_timer_get_time() / 1000;
}

uint32_t prev_time = 0;
// adjust current layer
void layer_adjust(uint16_t keycode)
{
	uint32_t cur_time = millis();
	int layers_num = nvs_read_num_layers();
	if (cur_time - prev_time > DEBOUNCE)
	{
		if (layer_hold_flag == 0)
		{
			switch (keycode)
			{
			case DEFAULT:
				current_layout = 0;
				break;

			case LOWER:
				if (current_layout == 0)
				{
					for (int m = (layers_num - 1); m > 0; m--)
					{
						if (key_layouts[m - current_layout].active)
						{
							current_layout = m;
							break;
						}
					}
				}
				else
				{
					current_layout--;
				}

				break;

			case RAISE:

				if (current_layout == (layers_num - 1))
				{
					current_layout = 0;
					break;
				}
				if (current_layout < (layers_num - 1))
				{
					if (key_layouts[current_layout + 1].active)
					{
						current_layout++;
						break;
					}
					else
					{
						current_layout = 0;
						break;
					}
				}
				current_layout++;

				break;
			}
#ifdef OLED_ENABLE
			xQueueSend(layer_recieve_q, &current_layout, (TickType_t)0);
#endif

#ifdef RGB_LEDS
			rgb_mode_t led_mode;
			nvs_load_led_mode(&led_mode);
			xQueueSend(keyled_q, &led_mode, 0);

#endif
			ESP_LOGI(TAG, "Layer modified!, Current layer: %d",
					 current_layout);
		}
	}
	prev_time = cur_time;
}

uint8_t matrix_prev_state[MATRIX_ROWS][MATRIX_COLS] = {0};





typedef enum {
    /** Press event issued */
    NO_ITERATION = 0,
    TAPDANCE_ITERATION,
    MODTAP_ITERATION    
} iteration_mode_t;

void keys_get_report_from_event(dd_layer *keymap, keys_event_struct_t key_event,uint8_t * report_state)
{

	// Send RGB notification on key changed.
	uint8_t row = key_event.key_pos/MATRIX_ROWS;
	uint8_t col = key_event.key_pos%MATRIX_ROWS;

	iteration_mode_t iteration_type = NO_ITERATION;
	uint8_t iteration_times = 1; //Aux variable to iterate several times

#ifdef RGB_LEDS
	rgb_key_led_press(row , col); // report the pressed key. --> This should be somewhere else. ToDo.
#endif

	uint16_t report_index = (2 + key_event.key_pos);
	keycode = keymap->key_map[row][col];

	// led_status = check_led_status(keycode); ----> ToDo: To be checked

	// Check if is on the special conditions (TAPDANCE, MODTAP, SEQUENCE_MACRO)
	//ToDo: verification of 
	
	
	// Check if it is tapdance
	if ((keycode >= TAPDANCE_BASE_VAL) && (keycode <= TAPDANCE_MAX_VAL) && key_event.event == KEY_TAP_DANCE)
	{
		ESP_LOGE(TAG,"TAPDANCE EVENT with keycode %d", keycode);

		uint8_t tapdance_list[5] = {1,2,3,10,15};  							//dummy to test will change with memory information!!
		uint16_t tapdance_actions[5] = {KC_1, KC_2, KC_AUDIO_MUTE, KC_3, KC_A};	//dummy to test

		uint8_t found_flag=0;
		for(uint8_t i=0;i<5;i++)
		{
			if (tapdance_list[i] == key_event.counter)
			{
				// Valid tapdance action. Set first the variables
				iteration_type = TAPDANCE_ITERATION;
				iteration_times = 2; //Just need 2 iterations. one for key set, other for key reset.
				// Change the keycode to desired action. Act as it was a single pressed event
				keycode = tapdance_actions[i];
				key_event.event = KEY_PRESSED;
				found_flag = 1;
				
				break;
			}
		}
		if(!found_flag)
		{
			keycode = KC_NO;
			return;
		}
		ESP_LOGE(TAG,"REDO TO TAPDANCE EVENT with keycode %d and iterations %d", keycode, iteration_times);
	}
	// Check if its modtap
	else if((keycode >= MODTAP_BASE_VAL) && (keycode <= MODTAP_MAX_VAL))
	{
		ESP_LOGE(TAG,"MODTAP EVENT with keycode %d", keycode);

		uint8_t modtap_actions[2] = {KC_1,KC_2};  							//dummy to test will change with memory information!!

		switch (key_event.event)
		{
		case KEY_MT_SHORT:
			iteration_type = MODTAP_ITERATION;
			iteration_times = 2; //Just need 2 iterations. one for key set, other for key reset.
			// Change the keycode to desired action. Act as it was a single pressed event
			keycode = modtap_actions[0];
			key_event.event = KEY_PRESSED;
		break;
		case KEY_MT_LONG_TIMEOUT:
			keycode = modtap_actions[1];
			key_event.event = KEY_PRESSED;
		break;
		case KEY_MT_LONG:
			keycode = modtap_actions[1];
			key_event.event = KEY_RELEASED;
		break;
		
		default:
			ESP_LOGE(TAG,"Why am i here?");
			break;
		}
	}
	// else
	// {
	// 	//Todo Add assert and error here!
	// 	ESP_LOGE(TAG,"Tapdance Action but no corresponding key. Should never arrive here!");
	// 	keycode = KC_NO;
	// }
	
	

	do
	{
		//First check the iterations and prepare if needed
		iteration_times --;
		if ((iteration_type == TAPDANCE_ITERATION || iteration_type == MODTAP_ITERATION )&& iteration_times==0)
		{
			key_event.event = KEY_RELEASED; //On the second iteration act as a key release.
		}
		


		// Check whether state is pressed or unpressed
		//if pressed:
		if (key_event.event == KEY_PRESSED)
		{
			// Check if layer hold (ToDo)

			// Check if keycode is layer adjust
			if ((keycode > LAYER_ADJUST_MIN) && (keycode < LAYER_ADJUST_MAX))
			{
				layer_adjust(keycode);
			}

			// Check if is a macro
			else if ((keycode >= MACRO_BASE_VAL) && (keycode <= MACRO_HOLD_MAX_VAL))
			{
				for (uint8_t i = 0; i < MACRO_LEN; i++)
				{
					uint16_t key = user_macros[keycode - MACRO_BASE_VAL].key[i];

					if (key == KC_NO)
					{
						break;
					}
					
					report_state[i + 2] = key; // 2 is an offset, as 0 and 1 are used for other reasons
					modifier |= check_modifier(key);
				}
			}

			// Check if media key
			else if ((keycode >= KC_MEDIA_NEXT_TRACK) && (keycode <= KC_AUDIO_VOL_DOWN))
			{
				media_control_send(keycode);
			}

			else if (report_state[report_index] == 0)
			{
				modifier |= check_modifier(keycode);
				report_state[report_index] = keycode;
			}
		}

		// If unpressed
		if (key_event.event == KEY_RELEASED)
		{
			// Check if layer hold (ToDo)

			// Check Macro
			if ((keycode >= MACRO_BASE_VAL) && (keycode <= MACRO_HOLD_MAX_VAL))
			{
				for (uint8_t i = 0; i < MACRO_LEN; i++)
				{
					uint16_t key = user_macros[keycode - MACRO_BASE_VAL].key[i];
					report_state[i + 2] = 0; // 2 is an offset, as 0 and 1 are used for other reasons
					modifier &= ~check_modifier(key);
				}
			}
			
			// checking for media control keycodes
			else if ((keycode >= KC_MEDIA_NEXT_TRACK) && (keycode <= KC_AUDIO_VOL_DOWN))
			{
				media_control_release(keycode);
			}
			
			// Check other keys
			else if (report_state[report_index] != 0)
			{
				led_status = 0;
				

				modifier &= ~check_modifier(keycode);
				report_state[KEY_STATE[row][col]] = 0;
				report_state[report_index] = 0;

			}
		
		}
		report_state[0] = modifier;
		report_state[1] = led_status;
		
		void *pReport;	
		pReport = (void *)&report_state;

	#ifndef NKRO
		uint8_t trunc_report[REPORT_LEN] = {0};
		trunc_report[0] = report_state[0];
		trunc_report[1] = report_state[1];

		uint16_t cur_index = 2;
		// Phone's mtu size is usually limited to 20 bytes
		for (uint16_t i = 2; i < REPORT_LEN && cur_index < TRUNC_SIZE;
			++i)
		{
			if (report_state[i] != 0)
			{
				trunc_report[cur_index] = report_state[i];
				++cur_index;
			}
		}

		pReport = (void *)&trunc_report;
	#endif
	#ifdef NKRO
				pReport = (void *)&report_state;
	#endif

		#ifdef DEBUG_REPORT //print the report 
			printf("Keyboard Report: ");
			for(int i=0; i< TRUNC_SIZE; i++)
			{
				printf("%02x ",trunc_report[i]);
			}
			printf("\n");
		#endif

		xQueueSend(keyboard_q, trunc_report, (TickType_t)0);

		// While there are still iterations pending, add a delay
		if (iteration_times > 0)
		{
			vTaskDelay(pdMS_TO_TICKS(40));
		}
		

	} while (iteration_times > 0);

}
	

#endif
