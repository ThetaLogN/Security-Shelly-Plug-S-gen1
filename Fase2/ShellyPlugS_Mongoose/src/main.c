#include "mgos.h"
#include "mgos_adc.h"
#include "mgos_gpio.h"
#include "mgos_http_server.h"
#include "mgos_mqtt.h"
#include "mgos_ro_vars.h"
#include "mgos_sys_config.h"
#include "mgos_timers.h"
#include "mgos_net.h"
#include "mgos_system.h"
#include "frozen.h"
#include <math.h>

extern bool mgos_ota_commit(void);

#define RELAY_PIN 15
#define SEL_PIN 12
#define CF1_PIN 14
#define CF_PIN 5
#define LED_PIN 0
#define BTN_PIN 13
#define ANALOG_PIN 0 // A0 usually 0 in Mongoose OS ADC

// --- NTC Configuration ---
const double R_PULLUP = 9480.0;
const double R_NTC_NOMINAL = 10000.0;
const double TEMP_NOMINAL = 25.0;
const double BETA_COEFFICIENT = 3350.0;

// --- HLW8012 Variables ---
static volatile uint32_t cf_pulse_count = 0;
static volatile uint32_t cf1_pulse_count = 0;
static volatile uint32_t last_cf_time = 0;
static volatile uint32_t last_cf1_time = 0;
static volatile uint32_t cf_period = 0;
static volatile uint32_t cf1_period = 0;

static double voltage_multiplier = 888.07;
static double current_multiplier = 431086.01;
static double power_multiplier = 930.0;

static float last_correct_power = 0.0;

static void cf_int_handler(int pin, void *arg) {
  uint32_t now = mgos_uptime_micros();
  if (last_cf_time > 0 && now > last_cf_time) {
    cf_period = now - last_cf_time;
  }
  last_cf_time = now;
  cf_pulse_count++;
  (void) pin;
  (void) arg;
}

static void cf1_int_handler(int pin, void *arg) {
  uint32_t now = mgos_uptime_micros();
  if (last_cf1_time > 0 && now > last_cf1_time) {
    cf1_period = now - last_cf1_time;
  }
  last_cf1_time = now;
  cf1_pulse_count++;
  (void) pin;
  (void) arg;
}

static float get_voltage() {
  if (cf1_period == 0) return 0.0;
  float freq = 1000000.0 / cf1_period;
  return freq * voltage_multiplier;
}

static float get_current() {
  if (cf1_period == 0) return 0.0;
  float freq = 1000000.0 / cf1_period;
  return freq * current_multiplier;
}

static float get_power() {
  if (cf_period == 0) return 0.0;
  float freq = 1000000.0 / cf_period;
  return freq * power_multiplier;
}

static float get_valid_power(float voltage, float current, bool relay_on) {
  float calc_power = voltage * current;
  if (relay_on) {
    if (calc_power >= 0.1 && calc_power <= 25000.0) {
      last_correct_power = calc_power;
    }
  } else {
    last_correct_power = 0.0;
  }
  return last_correct_power;
}

static double get_real_temperature() {
  int rawADC = mgos_adc_read(ANALOG_PIN);
  if (rawADC >= 1023) return -273.0;
  if (rawADC <= 0) return 999.0;
  
  double R_ntc = ((double)rawADC * R_PULLUP) / (1024.0 - (double)rawADC);
  double steinhart = (double)BETA_COEFFICIENT / ((BETA_COEFFICIENT / (TEMP_NOMINAL + 273.15)) + log(R_ntc / R_NTC_NOMINAL));
  steinhart -= 273.15;
  return steinhart;
}

// --- Relay Control ---
static void publish_status();
static void set_relay(bool state) {
  mgos_gpio_write(RELAY_PIN, state ? 1 : 0);
  publish_status();
}

static void btn_cb(int pin, void *arg) {
  bool current_state = mgos_gpio_read(RELAY_PIN);
  set_relay(!current_state);
  (void) pin;
  (void) arg;
}

// --- Web Endpoints ---
static void api_status_cb(struct mg_connection *nc, int ev, void *ev_data, void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  struct http_message *hm = (struct http_message *) ev_data;
  
  bool ison = mgos_gpio_read(RELAY_PIN);
  float voltage = get_voltage();
  float current = get_current();
  float power = get_valid_power(voltage, current, ison);
  float temp = get_real_temperature();
  int uptime = (int)mgos_uptime();
  
  char ip[16] = "";
  struct mgos_net_ip_info ip_info;
  if (mgos_net_get_ip_info(MGOS_NET_IF_TYPE_WIFI, 0, &ip_info)) {
    mgos_net_ip_to_str(&ip_info.ip, ip);
  }
  if (strcmp(ip, "0.0.0.0") == 0 || strlen(ip) == 0) {
    if (mgos_net_get_ip_info(MGOS_NET_IF_TYPE_WIFI, 1, &ip_info)) {
      mgos_net_ip_to_str(&ip_info.ip, ip);
    }
  }
  
  mg_printf(nc,
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
            "{\"ison\":%s,\"voltage\":%.2f,\"current\":%.3f,\"power\":%.2f,\"temp\":%.1f,\"uptime\":%d,\"ip\":\"%s\"}",
            ison ? "true" : "false", voltage, current, power, temp, uptime, ip);
  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) user_data;
  (void) hm;
}

static void api_relay_cb(struct mg_connection *nc, int ev, void *ev_data, void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  struct http_message *hm = (struct http_message *) ev_data;
  
  char turn[10];
  if (mg_get_http_var(&hm->query_string, "turn", turn, sizeof(turn)) > 0) {
    if (strcmp(turn, "on") == 0) set_relay(true);
    else if (strcmp(turn, "off") == 0) set_relay(false);
    else if (strcmp(turn, "toggle") == 0) set_relay(!mgos_gpio_read(RELAY_PIN));
  }
  
  bool ison = mgos_gpio_read(RELAY_PIN);
  float power = get_power();
  
  mg_printf(nc,
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
            "{\"ison\":%s,\"power\":%.2f}",
            ison ? "true" : "false", power);
  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) user_data;
}

static void api_config_cb(struct mg_connection *nc, int ev, void *ev_data, void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  mg_printf(nc,
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
            "{\"mqtt_server\":\"%s\",\"mqtt_port\":\"1883\",\"device_id\":\"%s\",\"udp_ip\":\"%s\",\"udp_port\":\"%d\"}",
            mgos_sys_config_get_mqtt_server(),
            mgos_sys_config_get_mqtt_client_id(),
            mgos_sys_config_get_app_udp_ip(),
            mgos_sys_config_get_app_udp_port());
  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) ev_data;
  (void) user_data;
}

static void api_wifi_cb(struct mg_connection *nc, int ev, void *ev_data, void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  struct http_message *hm = (struct http_message *) ev_data;
  
  char *ssid = NULL;
  char *pass = NULL;
  
  int num_parsed = json_scanf(hm->body.p, hm->body.len, "{ssid: %Q, pass: %Q}", &ssid, &pass);
  
  if (num_parsed > 0 && ssid != NULL) {
    mgos_sys_config_set_wifi_sta_ssid(ssid);
    if (pass != NULL) {
      mgos_sys_config_set_wifi_sta_pass(pass);
    } else {
      mgos_sys_config_set_wifi_sta_pass("");
    }
    mgos_sys_config_set_wifi_sta_enable(true);
    
    char *err = NULL;
    bool save_res = mgos_sys_config_save(&mgos_sys_config, false, &err);
    if (save_res) {
      mg_printf(nc,
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
                "{\"msg\":\"WiFi salvato! Riconnessione...\"}");
      mgos_system_restart_after(2000);
    } else {
      mg_printf(nc,
                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n"
                "{\"msg\":\"Errore salvataggio config: %s\"}", err ? err : "sconosciuto");
      free(err);
    }
  } else {
    mg_printf(nc,
              "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n"
              "{\"msg\":\"SSID non valido o mancante\"}");
  }
  
  free(ssid);
  free(pass);
  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) user_data;
  (void) hm;
}

static void api_udp_cb(struct mg_connection *nc, int ev, void *ev_data, void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  struct http_message *hm = (struct http_message *) ev_data;
  
  char *ip = NULL;
  char *port_str = NULL;
  int port = -1;
  
  int num_parsed = json_scanf(hm->body.p, hm->body.len, "{ip: %Q, port: %Q}", &ip, &port_str);
  if (num_parsed == 2 && port_str != NULL) {
    port = atoi(port_str);
  } else {
    // try parsing port as number
    json_scanf(hm->body.p, hm->body.len, "{port: %d}", &port);
  }
  
  if (ip != NULL && port != -1) {
    mgos_sys_config_set_app_udp_ip(ip);
    mgos_sys_config_set_app_udp_port(port);
    
    char *err = NULL;
    bool save_res = mgos_sys_config_save(&mgos_sys_config, false, &err);
    if (save_res) {
      mg_printf(nc,
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
                "{\"msg\":\"UDP aggiornato!\"}");
    } else {
      mg_printf(nc,
                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n"
                "{\"msg\":\"Errore salvataggio config: %s\"}", err ? err : "sconosciuto");
      free(err);
    }
  } else {
    mg_printf(nc,
              "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n"
              "{\"msg\":\"Dati UDP non validi o mancanti\"}");
  }
  
  free(ip);
  free(port_str);
  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) user_data;
  (void) hm;
}

static void api_mqtt_cb(struct mg_connection *nc, int ev, void *ev_data, void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  struct http_message *hm = (struct http_message *) ev_data;
  
  char *server = NULL;
  char *port_str = NULL;
  char *devid = NULL;
  int port = 1883;
  
  int num_parsed = json_scanf(hm->body.p, hm->body.len, "{server: %Q, port: %Q, devid: %Q}", &server, &port_str, &devid);
  if (num_parsed < 3) {
    // try port as integer
    json_scanf(hm->body.p, hm->body.len, "{port: %d}", &port);
  } else if (port_str != NULL) {
    port = atoi(port_str);
  }
  
  if (server != NULL && devid != NULL) {
    char server_addr[128];
    snprintf(server_addr, sizeof(server_addr), "%s:%d", server, port);
    
    mgos_sys_config_set_mqtt_server(server_addr);
    mgos_sys_config_set_mqtt_client_id(devid);
    mgos_sys_config_set_mqtt_enable(true);
    
    char *err = NULL;
    bool save_res = mgos_sys_config_save(&mgos_sys_config, false, &err);
    if (save_res) {
      mg_printf(nc,
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
                "{\"msg\":\"MQTT aggiornato! Riavvio consigliato.\"}");
    } else {
      mg_printf(nc,
                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n"
                "{\"msg\":\"Errore salvataggio config: %s\"}", err ? err : "sconosciuto");
      free(err);
    }
  } else {
    mg_printf(nc,
              "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n"
              "{\"msg\":\"Dati MQTT non validi o incompleti\"}");
  }
  
  free(server);
  free(port_str);
  free(devid);
  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) user_data;
  (void) hm;
}

static void api_reboot_cb(struct mg_connection *nc, int ev, void *ev_data, void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  
  mg_printf(nc,
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
            "{\"msg\":\"Riavvio in corso...\"}");
  
  mgos_system_restart_after(1000);
  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) ev_data;
  (void) user_data;
}

static void api_reset_cb(struct mg_connection *nc, int ev, void *ev_data, void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  
  mgos_sys_config_set_wifi_sta_ssid("");
  mgos_sys_config_set_wifi_sta_pass("");
  mgos_sys_config_set_wifi_sta_enable(false);
  mgos_sys_config_set_wifi_ap_enable(true);
  
  char *err = NULL;
  mgos_sys_config_save(&mgos_sys_config, false, &err);
  if (err != NULL) {
    free(err);
  }
  
  mg_printf(nc,
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
            "{\"msg\":\"WiFi resettato. Riavvio in corso...\"}");
  
  mgos_system_restart_after(1500);
  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) ev_data;
  (void) user_data;
}

// --- Telemetry ---
static void publish_status() {
  if (!mgos_mqtt_global_is_connected()) return;
  bool on = mgos_gpio_read(RELAY_PIN);
  
  char topic_state[64];
  snprintf(topic_state, sizeof(topic_state), "shellies/%s/relay/0", mgos_sys_config_get_mqtt_client_id());
  mgos_mqtt_pub(topic_state, on ? "1" : "0", 1, 1, false);
  
  char topic_status[64];
  snprintf(topic_status, sizeof(topic_status), "shellies/%s/status", mgos_sys_config_get_mqtt_client_id());
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"ison\":%s,\"uptime\":%d}", on ? "true" : "false", (int)mgos_uptime());
  mgos_mqtt_pub(topic_status, buf, strlen(buf), 1, false);
}

static void publish_energy() {
  if (!mgos_mqtt_global_is_connected()) return;
  bool on = mgos_gpio_read(RELAY_PIN);
  float voltage = get_voltage();
  float current = get_current();
  float power = get_valid_power(voltage, current, on);
  double temp = get_real_temperature();
  
  char buf[32];
  char topic[64];
  const char *dev_id = mgos_sys_config_get_mqtt_client_id();
  
  snprintf(topic, sizeof(topic), "shellies/%s/power", dev_id);
  snprintf(buf, sizeof(buf), "%.2f", power);
  mgos_mqtt_pub(topic, buf, strlen(buf), 1, false);
  
  snprintf(topic, sizeof(topic), "shellies/%s/temperature", dev_id);
  snprintf(buf, sizeof(buf), "%.1f", temp);
  mgos_mqtt_pub(topic, buf, strlen(buf), 1, false);
}

static void send_udp_telemetry(void *arg) {
  if (!mgos_gpio_read(RELAY_PIN)) return;
  
  float voltage = get_voltage();
  float current = get_current();
  float power = get_valid_power(voltage, current, true);
  double temp = get_real_temperature();
  
  char payload[256];
  snprintf(payload, sizeof(payload), 
           "{\"device_id\":\"%s\",\"voltage\":%.2f,\"current\":%.3f,\"power\":%.2f,\"temperature\":%.1f,\"relay\":true,\"uptime\":%d}",
           mgos_sys_config_get_mqtt_client_id(), voltage, current, power, temp, (int)mgos_uptime());
           
  char addr[32];
  snprintf(addr, sizeof(addr), "udp://%s:%d", mgos_sys_config_get_app_udp_ip(), mgos_sys_config_get_app_udp_port());
  
  struct mg_connection *nc = mg_connect(mgos_get_mgr(), addr, NULL, NULL);
  if (nc != NULL) {
    mg_send(nc, payload, strlen(payload));
    nc->flags |= MG_F_SEND_AND_CLOSE;
  }
  
  (void) arg;
}

static void timer_cb(void *arg) {
  publish_energy();
  send_udp_telemetry(NULL);
  (void) arg;
}

static void mqtt_ev_handler(struct mg_connection *nc, int ev, void *ev_data, void *user_data) {
  if (ev == MG_EV_MQTT_CONNACK) {
    char topic[64];
    snprintf(topic, sizeof(topic), "shellies/%s/relay/0/command", mgos_sys_config_get_mqtt_client_id());
    mgos_mqtt_sub(topic, NULL, NULL);
    
    snprintf(topic, sizeof(topic), "shellies/%s/online", mgos_sys_config_get_mqtt_client_id());
    mgos_mqtt_pub(topic, "1", 1, 1, true);
    publish_status();
  } else if (ev == MG_EV_MQTT_PUBLISH) {
    struct mg_mqtt_message *msg = (struct mg_mqtt_message *) ev_data;
    char topic[64];
    snprintf(topic, sizeof(topic), "shellies/%s/relay/0/command", mgos_sys_config_get_mqtt_client_id());
    if (mg_vcmp(&msg->topic, topic) == 0) {
      if (mg_vcasecmp(&msg->payload, "on") == 0 || mg_vcasecmp(&msg->payload, "1") == 0) {
        set_relay(true);
      } else if (mg_vcasecmp(&msg->payload, "off") == 0 || mg_vcasecmp(&msg->payload, "0") == 0) {
        set_relay(false);
      } else if (mg_vcasecmp(&msg->payload, "toggle") == 0) {
        set_relay(!mgos_gpio_read(RELAY_PIN));
      }
    }
  }
  (void) nc;
  (void) user_data;
}

enum mgos_app_init_result mgos_app_init(void) {
  // GPIO Init
  mgos_gpio_set_mode(RELAY_PIN, MGOS_GPIO_MODE_OUTPUT);
  mgos_gpio_write(RELAY_PIN, 0);
  
  mgos_gpio_set_mode(LED_PIN, MGOS_GPIO_MODE_OUTPUT);
  mgos_gpio_write(LED_PIN, 1);
  
  mgos_gpio_set_mode(BTN_PIN, MGOS_GPIO_MODE_INPUT);
  mgos_gpio_set_pull(BTN_PIN, MGOS_GPIO_PULL_UP);
  mgos_gpio_set_button_handler(BTN_PIN, MGOS_GPIO_PULL_UP, MGOS_GPIO_INT_EDGE_NEG, 50, btn_cb, NULL);
  
  // HLW8012 Init
  mgos_gpio_set_mode(CF_PIN, MGOS_GPIO_MODE_INPUT);
  mgos_gpio_set_mode(CF1_PIN, MGOS_GPIO_MODE_INPUT);
  mgos_gpio_set_mode(SEL_PIN, MGOS_GPIO_MODE_OUTPUT);
  mgos_gpio_write(SEL_PIN, 0); // Current mode
  
  mgos_gpio_set_int_handler(CF_PIN, MGOS_GPIO_INT_EDGE_ANY, cf_int_handler, NULL);
  mgos_gpio_enable_int(CF_PIN);
  
  mgos_gpio_set_int_handler(CF1_PIN, MGOS_GPIO_INT_EDGE_ANY, cf1_int_handler, NULL);
  mgos_gpio_enable_int(CF1_PIN);
  
  // ADC Init
  mgos_adc_enable(ANALOG_PIN);
  
  // HTTP Endpoints
  mgos_register_http_endpoint("/status", api_status_cb, NULL);
  mgos_register_http_endpoint("/relay/0", api_relay_cb, NULL);
  mgos_register_http_endpoint("/api/config", api_config_cb, NULL);
  mgos_register_http_endpoint("/api/wifi", api_wifi_cb, NULL);
  mgos_register_http_endpoint("/api/udp", api_udp_cb, NULL);
  mgos_register_http_endpoint("/api/mqtt", api_mqtt_cb, NULL);
  mgos_register_http_endpoint("/reboot", api_reboot_cb, NULL);
  mgos_register_http_endpoint("/reset", api_reset_cb, NULL);
  
  // MQTT Event Handler
  mgos_mqtt_add_global_handler(mqtt_ev_handler, NULL);
  
  // Timers
  mgos_set_timer(60000, MGOS_TIMER_REPEAT, timer_cb, NULL);
  
  // Commit firmware update to prevent Mongoose OS rollback on restart
  mgos_ota_commit();
  
  return MGOS_APP_INIT_SUCCESS;
}
