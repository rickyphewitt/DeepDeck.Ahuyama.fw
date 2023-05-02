#include <string.h>
#include <stdlib.h>
#include "server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "keymap.h"
#include "nvs_flash.h"

#include <sys/param.h>

#include "esp_netif.h"

#include "esp_http_server.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "cJSON.h"

#include "keyboard_config.h"
#include "nvs_keymaps.h"
#include "key_definitions.h"
#include "nvs_funcs.h"
#include "gesture_handles.h"

// #include "mdns.h"
#include "esp_vfs.h"
#define ROWS 4
#define COLS 4

static const char *TAG = "webserver";
extern xSemaphoreHandle Wifi_initSemaphore;

void *json_malloc(size_t size)
{
	return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

void json_free(void *ptr)
{
	heap_caps_free(ptr);
}

void json_response(char *j_response)
{
	cJSON *response_json = cJSON_CreateObject();
	cJSON *item_reason = cJSON_CreateString("reason");
	cJSON_AddItemToObject(response_json, " TO DO ", item_reason);
	cJSON *item_message = cJSON_CreateString("message");
	cJSON_AddItemToObject(response_json, "TO DO", item_message);
	if (response_json != NULL)
		j_response = cJSON_Print(response_json);

	if (j_response == NULL)
	{
		fprintf(stderr, "Failed to print monitor.\n");
	}
	// printf("%s",j_response);
	cJSON_Delete(response_json);
}

/* An HTTP GET handler */

/**
 * @brief
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t connect_url_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "connect handler");

	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*")); //
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
	// httpd_resp_set_hdr(req, "Access-Control-Max-Age", "3600");
	// httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-Custom-Header");
	// httpd_resp_set_hdr(req, "Vary", "Origin");

	// Read the URI line and get the host
	char *string = NULL;
	char *buf;
	size_t buf_len;

	json_response(string);
	buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
	if (buf_len > 1)
	{
		buf = malloc(buf_len);
		if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK)
		{
			ESP_LOGI(TAG, "Host: %s", buf);
		}
		free(buf);
	}

	// Read the URI line and get the parameters
	buf_len = httpd_req_get_url_query_len(req) + 1;
	if (buf_len > 1)
	{
		buf = malloc(buf_len);
		nvs_flash_init();
		nvs_handle_t nvs;
		nvs_open("wifiCreds", NVS_READWRITE, &nvs);
		if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
		{
			ESP_LOGI(TAG, "Found URL query: %s", buf);
			char param[32];
			if (httpd_query_key_value(buf, "ssid", param, sizeof(param)) == ESP_OK)
			{
				ESP_LOGI(TAG, "Saving SSID");
				ESP_LOGI(TAG, "The string value = %s", param);
				nvs_set_str(nvs, "ssid", param);
			}
			if (httpd_query_key_value(buf, "pass", param, sizeof(param)) == ESP_OK)
			{
				ESP_LOGI(TAG, "Saving pass");
				ESP_LOGI(TAG, "The int value = %s", param);
				nvs_set_str(nvs, "pass", param);
			}
			nvs_close(nvs);
		}
		free(buf);
	}

	// The response
	ESP_LOGI(TAG, "Wifi Credentials Saved");

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);
	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_send(req, NULL, 0);
	wifi_reset = true;
	xSemaphoreGive(Wifi_initSemaphore);
	return ESP_OK;
}



esp_err_t config_url_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "HTTP GET  --> /api/config");

	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*")); //
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
	// httpd_resp_set_hdr(req, "Access-Control-Max-Age", "3600");
	// httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-Custom-Header");
	// httpd_resp_set_hdr(req, "Vary", "Origin");

	uint8_t layers_num = nvs_read_num_layers();
	char *string = NULL;
	cJSON *layer_data = NULL;
	cJSON *layer_name = NULL;
	cJSON *layout_pos = NULL;
	cJSON *layers = NULL;
	cJSON *is_active = NULL;
	cJSON *layout_uuid = NULL;
	cJSON *monitor = cJSON_CreateObject();
	if (monitor == NULL)
	{
		httpd_resp_set_status(req, HTTPD_400);
		httpd_resp_send(req, NULL, 0);
		return ESP_OK;
	}

	cJSON_AddItemToObject(monitor, "ssid", cJSON_CreateString("ssid"));
	cJSON_AddItemToObject(monitor, "FWVersion", cJSON_CreateString(FIRMWARE_VERSION));
	cJSON_AddItemToObject(monitor, "Mac", cJSON_CreateString("MAC"));


	string = cJSON_Print(monitor);
	if (string == NULL)
	{
		fprintf(stderr, "Failed to print monitor.\n");
	}

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);
	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_send(req, NULL, 0);
	cJSON_Delete(monitor);
	return ESP_OK;
}

/*
esp_err_t connect_url_handler(httpd_req_t *req)
{

	ESP_LOGI(TAG, "connect handler");

	char buffer[1024];
	httpd_req_recv(req, buffer, req->content_len);

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	cJSON *payload = cJSON_Parse(buffer);

	if (NULL == payload)
	{
		const char *err = cJSON_GetErrorPtr();
		if (err != NULL)
		{
			ESP_LOGE(TAG, "Error parsing json before %s", err);
			cJSON_Delete(payload);

			httpd_resp_set_status(req, "500");
			return -1;
		}
	}

	cJSON *ssid = cJSON_GetObjectItem(payload, "ssid");
	cJSON *pass = cJSON_GetObjectItem(payload, "pass");

	if (cJSON_IsString(ssid) && (ssid->valuestring != NULL))
	{
		printf("WIFI SSID \"%s\"\n", ssid->valuestring);

		if (cJSON_IsString(pass) && (pass->valuestring != NULL))
		{
			printf("WIFI PASS \"%s\"\n", pass->valuestring);

			nvs_flash_init();
			nvs_handle_t nvs;
			nvs_open("wifiCreds", NVS_READWRITE, &nvs);
			nvs_set_str(nvs, "ssid", ssid->valuestring);
			nvs_set_str(nvs, "pass", pass->valuestring);
			nvs_close(nvs);
			ESP_LOGI("nvs", "Wifi Credentials Saved");
			httpd_resp_set_status(req, HTTPD_200);
			httpd_resp_send(req, NULL, 0);
			wifi_reset = true;
			xSemaphoreGive(Wifi_initSemaphore);
		}
	}

	else
	{
		httpd_resp_set_status(req, "error");
		httpd_resp_send(req, NULL, 0);
	}

	return ESP_OK;
}


*/

/**
 * @brief Get the layer url handler object
 *
 * @param req
 * @return esp_err_t
 */

esp_err_t get_layer_url_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "HTTP GET LAYER INFO --> /api/layers");

	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*")); //
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
	// httpd_resp_set_hdr(req, "Access-Control-Max-Age", "3600");
	// httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-Custom-Header");
	// httpd_resp_set_hdr(req, "Vary", "Origin");

	// Read the URI line and get the host
	char *buf;
	size_t buf_len;

	char str_param[UUID_STR_LEN];
	char int_param[3];
	int position = 0;

	// Read the URI line and get the parameters
	buf_len = httpd_req_get_url_query_len(req) + 1;
	if (buf_len > 1)
	{
		buf = malloc(buf_len);
		if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
		{
			ESP_LOGI(TAG, "Found URL query: %s", buf);

			if (httpd_query_key_value(buf, "uuid", str_param, sizeof(str_param)) == ESP_OK)
			{
				ESP_LOGI(TAG, "The string value = %s", str_param);
			}
			if (httpd_query_key_value(buf, "pos", int_param, sizeof(int_param)) == ESP_OK)
			{
				ESP_LOGI(TAG, "The int value = %s", int_param);
				position = atoi(int_param);
			}
		}
		free(buf);
	}

	if (strcmp(key_layouts[position].uuid_str, str_param) != 0)
	{

		ESP_LOGI(TAG, "key layout uuid %s", key_layouts[position].uuid_str);
		ESP_LOGI(TAG, "The string value = %s", str_param);

		httpd_resp_set_status(req, HTTPD_400);
		httpd_resp_send(req, NULL, 0);
		return ESP_OK;
	}

	char *string = NULL;
	cJSON *encoder_item = NULL;
	cJSON *gesture_item = NULL;
	cJSON *is_active = NULL;
	int index = 0;
	int index_col = 0;

	cJSON *layer_object = cJSON_CreateObject();
	if (layer_object == NULL)
	{
		ESP_LOGI(TAG, "error creando layer_object");

		httpd_resp_set_status(req, "500");
		httpd_resp_send(req, NULL, 0);
		return ESP_OK;
	}

	cJSON *_name = cJSON_CreateString(key_layouts[position].name);
	cJSON_AddItemToObject(layer_object, "name", _name);

	// is_active = cJSON_CreateBool(key_layouts[index]->active);
	is_active = cJSON_CreateBool(key_layouts[position].active);
	if (is_active == NULL)
	{
		cJSON_Delete(layer_object);
		httpd_resp_set_status(req, HTTPD_400);
		httpd_resp_send(req, NULL, 0);
		return ESP_OK;
	}
	cJSON_AddItemToObject(layer_object, "active", is_active);

	for (index = 0; index < MATRIX_ROWS; ++index)
	{
		char key_name[7] = {'\0'};
		snprintf(key_name, sizeof(key_name), "row%d", index);
		cJSON *row = cJSON_CreateArray();
		if (row == NULL)
		{
			cJSON_Delete(layer_object);
			httpd_resp_set_status(req, HTTPD_400);
			httpd_resp_send(req, NULL, 0);
			return ESP_OK;
		}
		cJSON_AddItemToObject(layer_object, key_name, row);
		for (index_col = 0; index_col < MATRIX_COLS; index_col++)
		{
			cJSON *key = cJSON_CreateObject();
			cJSON_AddStringToObject(key, "name", key_layouts[position].key_map_names[index][index_col]);
			cJSON_AddNumberToObject(key, "key_code", key_layouts[position].key_map[index][index_col]);
			cJSON_AddItemToArray(row, key);
		}
	}

	cJSON *encoder_map = cJSON_CreateObject();
	cJSON_AddItemToObject(layer_object, "left_encoder_map", encoder_map);

	for (index = 0; index < ENCODER_SIZE; index++)
	{
		encoder_item = cJSON_CreateNumber(key_layouts[position].left_encoder_map[index]);
		if (encoder_item == NULL)
		{
			cJSON_Delete(layer_object);
			httpd_resp_set_status(req, HTTPD_400);
			httpd_resp_send(req, NULL, 0);
			return ESP_OK;
		}
		cJSON_AddItemToObject(encoder_map, encoder_items_names[index], encoder_item);
	}

	cJSON *r_encoder_map = cJSON_CreateObject();
	cJSON_AddItemToObject(layer_object, "right_encoder_map", r_encoder_map);

	for (index = 0; index < ENCODER_SIZE; index++)
	{
		encoder_item = cJSON_CreateNumber(key_layouts[position].right_encoder_map[index]);
		if (encoder_item == NULL)
		{
			cJSON_Delete(layer_object);
			httpd_resp_set_status(req, HTTPD_400);
			httpd_resp_send(req, NULL, 0);
			return ESP_OK;
		}
		cJSON_AddItemToObject(r_encoder_map, encoder_items_names[index], encoder_item);
	}

	cJSON *gesture_map = cJSON_CreateObject();
	cJSON_AddItemToObject(layer_object, "gesture_map", gesture_map);

	for (index = 0; index < GESTURE_SIZE; index++)
	{
		gesture_item = cJSON_CreateNumber(key_layouts[position].gesture_map[index]);
		if (gesture_item == NULL)
		{

			cJSON_Delete(layer_object);
			httpd_resp_set_status(req, HTTPD_400);
			httpd_resp_send(req, NULL, 0);
			return ESP_OK;
		}
		cJSON_AddItemToObject(gesture_map, gesture_items_names[index], gesture_item);
	}

	string = cJSON_Print(layer_object);
	if (string == NULL)
	{
		fprintf(stderr, "Failed to print monitor.\n");
	}

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);
	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_send(req, NULL, 0);
	cJSON_Delete(layer_object);

	return ESP_OK;
}

/**
 * @brief Get the layerName url handler object
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t get_layerName_url_handler(httpd_req_t *req)
{
	esp_log_level_set(TAG, ESP_LOG_DEBUG);
	ESP_LOGI(TAG, "HTTP GET  --> /api/layers/layer_names");

	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*")); //
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
	// httpd_resp_set_hdr(req, "Access-Control-Max-Age", "3600");
	// httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-Custom-Header");
	// httpd_resp_set_hdr(req, "Vary", "Origin");

	uint8_t layers_num = nvs_read_num_layers();
	char *string = NULL;
	cJSON *layer_data = NULL;
	cJSON *layer_name = NULL;
	cJSON *layout_pos = NULL;
	cJSON *layers = NULL;
	cJSON *is_active = NULL;
	cJSON *layout_uuid = NULL;
	cJSON *monitor = cJSON_CreateObject();
	if (monitor == NULL)
	{
		httpd_resp_set_status(req, HTTPD_400);
		httpd_resp_send(req, NULL, 0);
		return ESP_OK;
	}

	layers = cJSON_CreateArray();
	if (layers == NULL)
	{
		cJSON_Delete(monitor);
		httpd_resp_set_status(req, HTTPD_400);
		httpd_resp_send(req, NULL, 0);
		return ESP_OK;
	}
	cJSON_AddItemToObject(monitor, "data", layers);

	for (int index = 0; index < ((int)layers_num); ++index)
	{
		layer_data = cJSON_CreateObject();
		if (layer_data == NULL)
		{
			cJSON_Delete(monitor);
			httpd_resp_set_status(req, HTTPD_400);
			httpd_resp_send(req, NULL, 0);
			return ESP_OK;
		}
		cJSON_AddItemToArray(layers, layer_data);

		// layer_name = cJSON_CreateString((key_layouts[index]->name));
		layer_name = cJSON_CreateString((key_layouts[index].name));
		if (layer_name == NULL)
		{
			cJSON_Delete(monitor);
			httpd_resp_set_status(req, HTTPD_400);
			httpd_resp_send(req, NULL, 0);
			return ESP_OK;
		}
		/* after creation was successful, immediately add it to the monitor,
		 * thereby transferring ownership of the pointer to it */
		// char layer_key[10] = {'\0'};
		// snprintf(layer_key, sizeof(layer_key), "layer", index);
		cJSON_AddItemToObject(layer_data, "name", layer_name);

		layout_pos = cJSON_CreateNumber(index);
		if (layout_pos == NULL)
		{
			cJSON_Delete(monitor);
			httpd_resp_set_status(req, HTTPD_400);
			httpd_resp_send(req, NULL, 0);
			return ESP_OK;
		}
		cJSON_AddItemToObject(layer_data, "pos", layout_pos);

		// is_active = cJSON_CreateBool(key_layouts[index]->active);
		is_active = cJSON_CreateBool(key_layouts[index].active);
		if (is_active == NULL)
		{
			cJSON_Delete(monitor);
			httpd_resp_set_status(req, HTTPD_400);
			httpd_resp_send(req, NULL, 0);
			return ESP_OK;
		}
		cJSON_AddItemToObject(layer_data, "active", is_active);

		layout_uuid = cJSON_CreateString((key_layouts[index].uuid_str));
		if (layout_uuid == NULL)
		{
			cJSON_Delete(monitor);
			httpd_resp_set_status(req, HTTPD_400);
			httpd_resp_send(req, NULL, 0);
			return ESP_OK;
		}
		/* after creation was successful, immediately add it to the monitor,
		 * thereby transferring ownership of the pointer to it */
		// char layer_key[10] = {'\0'};
		// snprintf(layer_key, 6, "uuid", index);
		cJSON_AddItemToObject(layer_data, "uuid", layout_uuid);
	}

	string = cJSON_Print(monitor);
	if (string == NULL)
	{
		fprintf(stderr, "Failed to print monitor.\n");
	}

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);
	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_send(req, NULL, 0);
	cJSON_Delete(monitor);
	return ESP_OK;
}


/**
 * @brief
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t delete_layer_url_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "HTTP DELETE LAYER --> /api/layers");
	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*")); //
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
	// httpd_resp_set_hdr(req, "Access-Control-Max-Age", "3600");
	// httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-Custom-Header");
	// httpd_resp_set_hdr(req, "Vary", "Origin");

	// Read the URI line and get the host
	char *buf;
	size_t buf_len;

	char str_param[UUID_STR_LEN];
	char int_param[3];
	int position = 0;

	// Read the URI line and get the parameters
	buf_len = httpd_req_get_url_query_len(req) + 1;
	if (buf_len > 1)
	{
		buf = malloc(buf_len);
		if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
		{
			ESP_LOGI(TAG, "Found URL query: %s", buf);

			if (httpd_query_key_value(buf, "uuid", str_param, sizeof(str_param)) == ESP_OK)
			{
				ESP_LOGI(TAG, "The string value = %s", str_param);
			}
			if (httpd_query_key_value(buf, "pos", int_param, sizeof(int_param)) == ESP_OK)
			{
				ESP_LOGI(TAG, "The int value = %s", int_param);
				position = atoi(int_param);
			}
		}
		free(buf);
	}

	if (strcmp(key_layouts[position].uuid_str, str_param) != 0)
	{

		ESP_LOGI(TAG, "key layout uuid %s", key_layouts[position].uuid_str);
		ESP_LOGI(TAG, "The string value = %s", str_param);

		httpd_resp_set_status(req, HTTPD_400);
		httpd_resp_send(req, NULL, 0);
		return ESP_OK;
	}

	nvs_delete_layer(position);

	char *string = NULL;
	// cJSON *response_json = cJSON_CreateObject();
	// if (response_json == NULL)
	// {
	// 	httpd_resp_set_status(req, HTTPD_400);
	// 	httpd_resp_send(req, NULL, 0);
	// 	return ESP_OK;
	// }

	// string = cJSON_Print(response_json);
	// if (string == NULL)
	// {
	// 	fprintf(stderr, "Failed to print monitor.\n");
	// }
	json_response(string);
	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);

	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_send(req, NULL, 0);

	current_layout = 0;
	xQueueSend(layer_recieve_q, &current_layout,
			   (TickType_t)0);

	return ESP_OK;
}



void fill_row(cJSON *row, char names[][10], int codes[])
{
	int i;
	cJSON *item;
	for (i = 0; i < COLS; i++)
	{
		item = cJSON_GetArrayItem(row, i);
		strcpy(names[i], cJSON_GetObjectItem(item, "name")->valuestring);
		codes[i] = cJSON_GetObjectItem(item, "key_code")->valueint;
	}
}

/**
 * @brief
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t update_layer_url_handler(httpd_req_t *req)
{

	ESP_LOGI(TAG, "HTTP PUT --> /api/layers");

	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*")); //
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
	// httpd_resp_set_hdr(req, "Access-Control-Max-Age", "3600");
	// httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-Custom-Header");
	// httpd_resp_set_hdr(req, "Vary", "Origin");

	char *string = NULL;
	json_response(string);

	int position = 0;
	// char buffer[1024];
	// httpd_req_recv(req, buffer, req->content_len);
	char *buf;
	size_t buf_len;

	buf_len = (req->content_len) + 1;
	buf = malloc(buf_len);
	httpd_req_recv(req, buf, req->content_len);

	cJSON *payload = cJSON_Parse(buf);
	dd_layer temp_layaout;

	if (NULL == payload)
	{
		const char *err = cJSON_GetErrorPtr();
		if (err != NULL)
		{
			ESP_LOGE(TAG, "Error parsing json before %s", err);
			cJSON_Delete(payload);

			httpd_resp_set_status(req, "500");
			return -1;
		}
	}

	cJSON *layer_uuid = cJSON_GetObjectItem(payload, "uuid");
	if (cJSON_IsString(layer_uuid) && (layer_uuid->valuestring != NULL))
	{
		printf("Layer uuid = \"%s\"\n", layer_uuid->valuestring);
	}

	cJSON *layer_pos = cJSON_GetObjectItem(payload, "pos");
	if (cJSON_IsNumber(layer_pos) && (layer_pos->valueint))
	{
		printf("Layer pos = \"%d\"\n", layer_pos->valueint);
		position = layer_pos->valueint;
	}

	if (strcmp(key_layouts[position].uuid_str, layer_uuid->valuestring) != 0)
	{

		ESP_LOGI(TAG, "key layout uuid %s", key_layouts[position].uuid_str);
		ESP_LOGI(TAG, "The string value = %s", layer_uuid->valuestring);

		httpd_resp_set_status(req, HTTPD_400);
		httpd_resp_send(req, NULL, 0);
		return ESP_OK;
	}
	strcpy(temp_layaout.uuid_str, layer_uuid->valuestring);

	cJSON *new_layer_name = cJSON_GetObjectItem(payload, "new_name");
	if (cJSON_IsString(new_layer_name) && (new_layer_name->valuestring != NULL))
	{
		strcpy(temp_layaout.name, new_layer_name->valuestring);
	}

	cJSON *row0 = cJSON_GetObjectItemCaseSensitive(payload, "row0");
	cJSON *row1 = cJSON_GetObjectItemCaseSensitive(payload, "row1");
	cJSON *row2 = cJSON_GetObjectItemCaseSensitive(payload, "row2");
	cJSON *row3 = cJSON_GetObjectItemCaseSensitive(payload, "row3");

	char names[ROWS][COLS][10];
	int codes[ROWS][COLS];

	fill_row(row0, names[0], codes[0]);
	fill_row(row1, names[1], codes[1]);
	fill_row(row2, names[2], codes[2]);
	fill_row(row3, names[3], codes[3]);

	int i, j;
	// printf("Names:\n");
	for (i = 0; i < ROWS; i++)
	{
		for (j = 0; j < COLS; j++)
		{
			// printf("%s\t", names[i][j]);

			strcpy(temp_layaout.key_map_names[i][j], names[i][j]);
		}
		// printf("\n");
	}

	// printf("\nCodes:\n");
	for (i = 0; i < ROWS; i++)
	{
		for (j = 0; j < COLS; j++)
		{
			// printf("%d\t", codes[i][j]);

			temp_layaout.key_map[i][j] = codes[i][j];
		}
		// printf("\n");
	}
	cJSON *item;
	cJSON *left_encoder_map = cJSON_GetObjectItem(payload, "left_encoder_map");
	if (left_encoder_map == NULL)
	{
		printf("No se encontró el objeto 'left_encoder_map'.\n");
		return 1;
	}
	printf("Elementos del objeto 'left_encoder_map':\n");
	cJSON_ArrayForEach(item, left_encoder_map)
	{
		printf("%s: %d\n", item->string, item->valueint);
		temp_layaout.left_encoder_map[i] = item->valueint;
		i++;
	}

	i = 0;
	cJSON *right_encoder_map = cJSON_GetObjectItem(payload, "right_encoder_map");
	cJSON_ArrayForEach(item, right_encoder_map)
	{
		printf("%s: %d\n", item->string, item->valueint);
		temp_layaout.right_encoder_map[i] = item->valueint;
		i++;
	}
	i = 0;
	cJSON *gesture_map = cJSON_GetObjectItem(payload, "gesture_map");
	cJSON_ArrayForEach(item, gesture_map)
	{
		printf("%s: %d\n", item->string, item->valueint);
		temp_layaout.gesture_map[i] = item->valueint;
		i++;
	}

	cJSON *is_active = cJSON_GetObjectItem(payload, "active");
	bool active = cJSON_IsTrue(is_active);
	if (active)
	{
		temp_layaout.active = active;
	}
	else
	{
		temp_layaout.active = false;
	}
	cJSON_Delete(payload);
	free(buf);
	nvs_write_layer(temp_layaout, position);
	// apds9960_free();

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, string);
	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_send(req, NULL, 0);

	current_layout = 0;
	xQueueSend(layer_recieve_q, &current_layout,
			   (TickType_t)0);

	return ESP_OK;
}


/**
 * @brief Create a layer url handler object
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t create_layer_url_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "HTTP POST  Create Layer --> /api/layers");
	// 	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");

	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*")); //
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
	// httpd_resp_set_hdr(req, "Access-Control-Max-Age", "3600");
	// httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-Custom-Header");
	// httpd_resp_set_hdr(req, "Vary", "Origin");
	// char buffer[1024];
	char *buf;
	size_t buf_len;
	char *string = NULL;
	json_response(string);

	buf_len = (req->content_len) + 1;
	buf = malloc(buf_len);
	httpd_req_recv(req, buf, req->content_len);

	dd_layer new_layer;
	esp_err_t res;
	cJSON *payload = cJSON_Parse(buf);

	if (NULL == payload)
	{
		const char *err = cJSON_GetErrorPtr();
		if (err != NULL)
		{
			ESP_LOGE(TAG, "Error parsing json before %s", err);
			cJSON_Delete(payload);

			httpd_resp_set_status(req, "500");
			return -1;
		}
	}

	cJSON *layer_name = cJSON_GetObjectItem(payload, "name");
	if (cJSON_IsString(layer_name) && (layer_name->valuestring != NULL))
	{
		// printf("Layer Name = \"%s\"\n", layer_name->valuestring);

		strcpy(new_layer.name, layer_name->valuestring);
		// printf("ddLayer Name = \"%s\"\n", new_layer.name);
	}

	cJSON *layer_uuid = cJSON_GetObjectItem(payload, "uuid");
	if (cJSON_IsString(layer_uuid) && (layer_uuid->valuestring != NULL))
	{
		// printf("Layer uuid = \"%s\"\n", layer_uuid->valuestring);
		strcpy(new_layer.uuid_str, layer_uuid->valuestring);
		// printf("ddLayer uuid = \"%s\"\n", new_layer.uuid_str);
	}

	cJSON *is_active = cJSON_GetObjectItem(payload, "active");
	bool active = cJSON_IsTrue(is_active);
	if (active)
	{
		new_layer.active = active;
	}
	else
	{
		new_layer.active = false;
	}

	cJSON *row0 = cJSON_GetObjectItemCaseSensitive(payload, "row0");
	cJSON *row1 = cJSON_GetObjectItemCaseSensitive(payload, "row1");
	cJSON *row2 = cJSON_GetObjectItemCaseSensitive(payload, "row2");
	cJSON *row3 = cJSON_GetObjectItemCaseSensitive(payload, "row3");

	char names[ROWS][COLS][10];
	int codes[ROWS][COLS];

	fill_row(row0, names[0], codes[0]);
	fill_row(row1, names[1], codes[1]);
	fill_row(row2, names[2], codes[2]);
	fill_row(row3, names[3], codes[3]);

	int i, j;
	// printf("Names:\n");
	for (i = 0; i < ROWS; i++)
	{
		for (j = 0; j < COLS; j++)
		{
			// printf("%s\t", names[i][j]);
			// strcpy(key_layouts[edit_layer]->key_map_names[i][j], names[i][j]);
			strcpy(new_layer.key_map_names[i][j], names[i][j]);
		}
		// printf("\n");
	}

	// printf("\nCodes:\n");
	for (i = 0; i < ROWS; i++)
	{
		for (j = 0; j < COLS; j++)
		{
			// printf("%d\t", codes[i][j]);
			// key_layouts[edit_layer]->key_map[i][j] = codes[i][j];
			new_layer.key_map[i][j] = codes[i][j];
		}
		printf("\n");
	}
	cJSON_Delete(payload);
	free(buf);
	current_layout = 0;
	res = nvs_create_new_layer(new_layer);
	if (res == ESP_OK)
	{
		httpd_resp_set_type(req, "application/json");
		httpd_resp_sendstr(req, string);
		httpd_resp_set_status(req, HTTPD_200);
		httpd_resp_send(req, NULL, 0);
		xQueueSend(layer_recieve_q, &current_layout,
				   (TickType_t)0);
	}
	else
	{
		xQueueSend(layer_recieve_q, &current_layout,
				   (TickType_t)0);
		httpd_resp_set_status(req, HTTPD_400);
		httpd_resp_send(req, NULL, 0);
	}

	return ESP_OK;
}

/**
 * @brief
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t restore_default_layer_url_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "HTTP POST  Restore Default Layouts --> /api/layers/restore");

	ESP_ERROR_CHECK(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*")); //
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
	// httpd_resp_set_hdr(req, "Access-Control-Max-Age", "3600");
	// httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-Custom-Header");
	// httpd_resp_set_hdr(req, "Vary", "Origin");

	char *string = NULL;
	json_response(string);
	esp_err_t error = nvs_restore_default_layers();
	if (error == ESP_OK)
	{

		httpd_resp_set_type(req, "application/json");
		httpd_resp_sendstr(req, string);
		httpd_resp_set_status(req, HTTPD_200);
		httpd_resp_send(req, NULL, 0);
	}
	else
	{

		httpd_resp_set_status(req, HTTPD_400);
		httpd_resp_send(req, NULL, 0);
	}
	current_layout = 0;
	xQueueSend(layer_recieve_q, &current_layout,
			   (TickType_t)0);

	return ESP_OK;
}

/**
 * @brief
 *
 * @param req
 * @return esp_err_t
 */
esp_err_t change_keyboard_led_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "HTTP POST  CHANGE LED MODE --> /api/led");
	int mode_t;
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
	// httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
	// httpd_resp_set_hdr(req, "Access-Control-Max-Age", "3600");
	// httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-Custom-Header");
	// httpd_resp_set_hdr(req, "Vary", "Origin");

	// Read the URI line and get the host
	char *buf;
	size_t buf_len;
	char int_param[3];
	char *string = NULL;

	// cJSON *monitor = cJSON_CreateObject();
	// if (monitor == NULL)
	// {
	// 	httpd_resp_set_status(req, HTTPD_400);
	// 	httpd_resp_send(req, NULL, 0);
	// 	return ESP_OK;
	// }

	// cJSON *_name = cJSON_CreateString("success");
	// cJSON_AddItemToObject(monitor, "name", _name);
	// string = cJSON_Print(monitor);

	json_response(string);

	// Read the URI line and get the parameters
	buf_len = httpd_req_get_url_query_len(req) + 1;
	if (buf_len > 1)
	{
		buf = malloc(buf_len);
		if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
		{
			ESP_LOGI(TAG, "Found URL query: %s", buf);

			if (httpd_query_key_value(buf, "mode", int_param, sizeof(int_param)) == ESP_OK)
			{
				ESP_LOGI(TAG, "Led Mode is = %s", int_param);
				mode_t = atoi(int_param);
				xQueueSend(keyled_q, &mode_t, 0);

				httpd_resp_set_type(req, "application/json");
				httpd_resp_sendstr(req, string);
				httpd_resp_set_status(req, HTTPD_200);
				httpd_resp_send(req, NULL, 0);
			}
			else
			{

				httpd_resp_set_status(req, HTTPD_400);
				httpd_resp_send(req, NULL, 0);
			}
		}
		free(buf);
	}
	// cJSON_Delete(monitor);
	return ESP_OK;
}


/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
	return ESP_OK;
	// if (strcmp("/hello", req->uri) == 0)
	// {
	// 	httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
	// 						"/hello URI is not available");
	// 	/* Return ESP_OK to keep underlying socket open */
	// 	return ESP_OK;
	// }
	// else if (strcmp("/echo", req->uri) == 0)
	// {
	// 	httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
	// 						"/echo URI is not available");
	// 	/* Return ESP_FAIL to close underlying socket */
	// 	return ESP_FAIL;
	// }
	// /* For any other URI send 404 and close socket */
	// httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
	// return ESP_FAIL;
}
/**
 * @brief
 *
 * @return httpd_handle_t
 */
httpd_handle_t start_webserver(void)
{
	httpd_handle_t server = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.max_uri_handlers = 20;
	config.stack_size = 1024 * 8;
	config.uri_match_fn = httpd_uri_match_wildcard;

	// Start the httpd server
	ESP_ERROR_CHECK(httpd_start(&server, &config));

	ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
	// if (httpd_start(&server, &config) == ESP_OK)
	// {
	// Set URI handlers
	httpd_uri_t connect_url = {.uri = "/api/connect", .method = HTTP_POST, .handler = connect_url_handler, .user_ctx = NULL};
	// ESP_LOGI(TAG, "Registering URI handlers --> /connect");
	httpd_register_uri_handler(server, &connect_url);


	httpd_uri_t get_config_url = {.uri = "/api/config", .method = HTTP_GET, .handler = config_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &get_config_url);

	////////LED
	httpd_uri_t change_led_color_url = {.uri = "/api/led", .method = HTTP_POST, .handler = change_keyboard_led_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &change_led_color_url);
	httpd_uri_t change_led_color_url__ = {.uri = "/api/led", .method = HTTP_OPTIONS, .handler = change_keyboard_led_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &change_led_color_url__);

	///////LAYERS
	httpd_uri_t get_layer_url = {.uri = "/api/layers", .method = HTTP_GET, .handler = get_layer_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &get_layer_url);
	httpd_uri_t get_layerName_url = {.uri = "/api/layers/layer_names", .method = HTTP_GET, .handler = get_layerName_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &get_layerName_url);
	httpd_uri_t delete_layer_url = {.uri = "/api/layers", .method = HTTP_DELETE, .handler = delete_layer_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &delete_layer_url);
	httpd_uri_t create_layer_url = {.uri = "/api/layers", .method = HTTP_POST, .handler = create_layer_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &create_layer_url);
	httpd_uri_t create_layer_url__ = {.uri = "/api/layers", .method = HTTP_OPTIONS, .handler = create_layer_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &create_layer_url__);

	httpd_uri_t update_layer_url = {.uri = "/api/layers", .method = HTTP_PUT, .handler = update_layer_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &update_layer_url);

	httpd_uri_t restore_default_layer_url = {.uri = "/api/layers/restore", .method = HTTP_PUT, .handler = restore_default_layer_url_handler, .user_ctx = NULL};
	httpd_register_uri_handler(server, &restore_default_layer_url);

	// httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

	return server;
	// }

	// ESP_LOGI(TAG, "Error starting server!");
	// return NULL;
}
/**
 * @brief
 *
 * @param server
 */
void stop_webserver(httpd_handle_t server)
{
	// Stop the httpd server
	httpd_stop(server);
}
