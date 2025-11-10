/* 
 * This file is a part of the ESPShell Arduino library (Espressif's ESP32-family CPUs)
 *
 * Latest source code can be found at Github: https://github.com/vvb333007/espshell/
 * Stable releases: https://github.com/vvb333007/espshell/tags
 *
 * Feel free to use this code as you wish: it is absolutely free for commercial and 
 * non-commercial, education purposes.  Credits, however, would be greatly appreciated.
 *
 * Author: Viacheslav Logunov <vvb333007@gmail.com>
 */

// -- WiFi support
// Creation/deletion/configuring of WiFi interfaces. Access Point and/or Station mode
// We need WiFi for our FTP server to work, which in turn is required for filesystem
// upload
//

#include "freertos/event_groups.h"
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_phy.h>

#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/ip_addr.h"
#include "lwip/opt.h"
#include "lwip/err.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "lwip/lwip_napt.h"
#include "dhcpserver/dhcpserver.h"
#include "dhcpserver/dhcpserver_options.h"

#if COMPILING_ESPSHELL

#define get_staif() esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")
#define get_apif() esp_netif_get_handle_from_ifkey("WIFI_AP_DEF")

#define is_sta_here() (get_staif() != NULL)
#define is_ap_here() (get_apif() != NULL)


//static wifi_config_t AP_config = { 0 };
//static wifi_config_t STA_config = { 0 };

static bool Sta_reconnect = false;
static bool AP_static_dns = false;

// Get current interface index (WIFI_IF_STA or WIFI_IF_AP) and also fetch corresponding esp_netif
// structure address: command "wifi ap|sta" autocreates AP/STA netifs so the address must not be NULL.
// The reason why we don't simply store pointers to netifs (like it is done e.g. in Espressif WiFi 
// Arduino library) is because interfaces may be controlled by the user sketch and be created or
// destroyed unexpectedly
//
#define THIS_INTERFACE(_Ifx) \
  wifi_interface_t _Ifx = (wifi_interface_t )context_get(); \
  __attribute__((unused)) esp_netif_t *ni = esp_netif_get_handle_from_ifkey( \
      _Ifx == WIFI_IF_AP ? "WIFI_AP_DEF" \
                         : (_Ifx == WIFI_IF_STA ? "WIFI_STA_DEF" \
                                                : "UNDEF")); \
  if (unlikely(_Ifx >= WIFI_IF_NAN || _Ifx < 0 || ni == NULL)) {\
    q_print("% THIS_INTERFACE() : disrupted Context!\r\n"); \
    return CMD_FAILED; \
  }


//static bool Ap_up = false;
//static bool Sta_up = false;
static bool Wifi_started = false;   // TODO: _Atomic
static bool Wifi_prepared = false;  // TODO: _Atomic

// WiFi auth types in human=readable form. First byte is the security type: -open, *psk, +psk mm, or $enterprise
// see legend in "scan" command output
//
static const char *Wifi_auth[] = {
    "- <g>NONE</>",  "* <i>WEP</>", "* WPA",  "* WPA2",
    "* WPA1/2",  "$ WPA2",  "* WPA3",       "* WPA2/3",
    "* WAPI",    "  <g>OWE</>", "$ WPA3_192", "* WPA3 Ext",
    "+ WPA3 Ext","  DPP",       "$ WPA3",     "$ WPA2/3",
    "$ WPA",
};

// WiFi cipher type in human readanle form
//
static const char *Wifi_cipher[] = {
    "NONE",    "WEP40",     "WEP104",      "TKIP",
    "CCMP",    "TKIP_CCMP", "AES_CMAC128", "SMS4",
    "GCMP",    "GCMP256",   "AES_GMAC128", "AES_GMAC256"
};

// Update AP's DNS settings from STA's DHCP reply.
// DNS entries are updated only if AP ha sno static DNS servers configured.
// This one is called when STA receives GOT_IP event (see ip_event_handler() below)
//
static bool sta_propagate_dns(esp_netif_t *sta) {
  if (!AP_static_dns) {
    esp_netif_dns_info_t dnsi = { 0 };
    esp_netif_t *apif = get_apif();
    if (sta && apif)
      if (esp_netif_get_dns_info(sta,ESP_NETIF_DNS_MAIN,&dnsi) == ESP_OK)
        return (ESP_OK == esp_netif_set_dns_info(apif,ESP_NETIF_DNS_MAIN,&dnsi));
  }
  return false;
}

// DHCP server must be stopped before reconfiguring
// This functions stops the server and saves returns /true/ if server was started
// or /false/ if it was not. This return value is then used with dhcp_server_restart_if_was_started(X)
//
static bool dhcp_server_stop_if_started(esp_netif_t *apif) {

  if (likely(apif != NULL)) {
    // failsafe: assume started in case of any errors
    esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_STARTED;
    esp_netif_dhcps_get_status(apif, &status);
    if (status != ESP_NETIF_DHCP_STOPPED) {
      VERBOSE(q_print("% Stopping DHCP server..Ok \r\n"));
      esp_netif_dhcps_stop(apif);
      return true;
    }
  }
  return false;
}

// Opposite of the above function. Starts server, which is currently stopped but was started before
// (i.e. second argument must be the value, returned by the above function)
//
static void dhcp_server_restart_if_was_started(esp_netif_t *apif, bool was_started) {

  if (was_started) {
    VERBOSE(q_print("%% Restarting AP's DHCP server.."));
    if (esp_netif_dhcps_start(apif) == ESP_OK) {
      VERBOSE(q_print("Ok\r\n"));
    } else {
      VERBOSE(q_print("Failed\r\n"));
    }
  }
}

#ifndef OFFER_DNS
#  define OFFER_DNS 0x02
#endif

static bool dhcp_server_set_dns_option() {

  esp_netif_t *apif = get_apif();
  uint8_t dhcps_offer_option = OFFER_DNS;
  bool was_started;

  if (apif == NULL)
    return false;

  was_started = dhcp_server_stop_if_started(apif);

  VERBOSE(q_print("%% Reconfiguring DHCP server (adding DNS option)..\r\n"));
  esp_netif_dhcps_option(apif, 
                        ESP_NETIF_OP_SET,
                        ESP_NETIF_DOMAIN_NAME_SERVER,
                        &dhcps_offer_option, sizeof(dhcps_offer_option));

  dhcp_server_restart_if_was_started(apif, was_started);

  return true;
}

#if 0
// Set custom lease time in seconds
//
static bool dhcp_server_set_lease_option(uint32_t lease) {

  esp_netif_t *apif = get_apif();
  
  bool was_started;

  if (apif == NULL)
    return false;

  was_started = dhcp_server_stop_if_started(apif);

  if (ESP_OK != esp_netif_dhcps_option(apif,
                                        ESP_NETIF_OP_SET,
                                        ESP_NETIF_IP_ADDRESS_LEASE_TIME,
                                        &lease,
                                        sizeof(lease)))
    q_print("% Failed to set lease time\n");
    
  dhcp_server_restart_if_was_started(apif, was_started);
  return true;
}

// Set custom lease time in seconds
//
static bool dhcp_server_set_ip_pool(esp_netif_t *apif, uint32_t ip_start, uint32_t count) {

  if (apif == NULL)
    return false;

  bool was_started = dhcp_server_stop_if_started(apif);

  //TODO:

  dhcp_server_restart_if_was_started(apif, was_started);
  return true;
}
#endif

// IP Events handler instance
//
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (likely(event_base == IP_EVENT))
    switch(event_id) {

      // STA has IP address assigned (static or dynamic)
      // Fetch DNS server from the STA interface, add DHCP server option
      case IP_EVENT_STA_GOT_IP:

        ip_event_got_ip_t *got = (ip_event_got_ip_t *)event_data;
        VERBOSE(q_printf("%% Interface WIFI STA got IP " IPSTR ", mask " IPSTR ", gw " IPSTR " (%schanged)\r\n",
                          IP2STR(&got->ip_info.ip),
                          IP2STR(&got->ip_info.netmask),
                          IP2STR(&got->ip_info.gw),
                          got->ip_changed ? "" : "not "));
        if (!AP_static_dns) {
          HELP(q_print("% WIFI STA: propagating DNS servers to the AP interface..\r\n"));
          sta_propagate_dns(got->esp_netif);
          dhcp_server_set_dns_option();
        }
        break;

      case IP_EVENT_STA_LOST_IP:
        HELP(q_print("% WIFI STA: IP address lost, protocol DOWN\r\n"));
        break;

      default:
        break;
    };

  VERBOSE(q_printf("%% IP-EVENT: arg=%p, base=%x, id=%x, edata=%p\r\n",arg,(unsigned int)event_base,(unsigned int)event_id,event_data));
}

// WiFi events handler instance
//
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

  if (likely(event_base == WIFI_EVENT))
    switch(event_id) {
      case WIFI_EVENT_STA_START:
        break;

      case WIFI_EVENT_STA_CONNECTED:
        HELP(q_print("% WIFI STA: Connected to the network. Configuring IP..\r\n"));
        break;

      case WIFI_EVENT_STA_DISCONNECTED:
        VERBOSE(q_print("% WIFI STA: disconnected, link DOWN\r\n"));
        // optional auto-reconnect
         if (Sta_reconnect) {
           VERBOSE(q_print("% WIFI STA: trying to reconnect..\r\n"));
           esp_wifi_connect(); 
         }
        break;
    
    // AP events
      case WIFI_EVENT_AP_START:
        VERBOSE(q_print("% Access Point starting..\r\n"));
        break;

      case WIFI_EVENT_AP_STOP:
        VERBOSE(q_print("% Access Point shutting down..\r\n"));
        break;


      case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t*) event_data;
        VERBOSE(q_printf("%% WIFI AP: " MACSTR " connected, (aid=%d, auth=OK))\r\n",MAC2STR(event->mac), event->aid)); 
        break;
      }

      case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t*) event_data;
        VERBOSE(q_printf("%% WIFI AP: " MACSTR " disconnected, (aid=%d, deauth))\r\n",MAC2STR(event->mac), event->aid)); 
        break;
      }

      case WIFI_EVENT_AP_WRONG_PASSWORD:
        VERBOSE(q_print("% WIFI AP: client connection failed (wrong password)\r\n"));
        break;

      default:
        break;
    };
    VERBOSE(q_printf("%% WIFI-EVENT: arg=%p, base=%x, id=%x, edata=%p\r\n",arg,(unsigned int)event_base,(unsigned int)event_id,event_data));
}

// Initialize the WiFi stack, perform minimal initial config.
// Can be called multiple times
//
static bool prepare_wifi_stack() {

  static bool ni = false;
  if (!ni) {
   esp_netif_init();
   ni = true;
  }

  if (!Wifi_prepared) {

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    // NVS must be initialized for WiFi driver to be happy;
    // it is done in _nv_storage_init() constructor

    // Create default event loop
    esp_event_loop_create_default();

    // Attach IP and WiFi event handlers. This module relies on the ESP32 event system for its operation
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,  &ip_event_handler, NULL);

    // Initialize WiFi
    esp_wifi_init(&cfg);

    // Use RAM, not flash otherwise your STA will try to connect to AP remembered from the previous connection
    // This can be very annoying
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    // We don't know yet what mode user will use so initialize both
    esp_wifi_set_mode(WIFI_MODE_STA);
    //esp_wifi_set_channel(11, 0);
#if SOC_WIFI_SUPPORT_5G
    esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
#endif
    Wifi_prepared = true;
  }
  // TODO: check other errors
  return Wifi_prepared;
}

static bool start_wifi_stack() {

  if (!Wifi_prepared) {
    VERBOSE(q_print("% start_wifi_stack() called without prepare_wifi_stack()\r\n"));
    return false;
  }

  if (!Wifi_started) {
    if (esp_wifi_start() == ESP_OK) {
      Wifi_started = true;
      VERBOSE(q_print("% WIFI started\r\n"));
    } else {
      VERBOSE(q_print("% WIFI failed to initialize\r\n"));
    }
  }
  return Wifi_started;
}

// Performs "unprepare" and "stop"
// This is mainly for development purposes 
static void stop_wifi_stack() {

  if (Wifi_started)
    esp_wifi_stop();

  if (Wifi_prepared) {
    esp_wifi_deinit();
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler);
  }
  
  Wifi_started = Wifi_prepared = false;
}

// Display AP details
//
static void display_ap_details(wifi_ap_record_t *ap, const char *requested_bssid) {

  const char *bw_desc[] = { "unknown", "20MHz", "40MHz", "80MHz", "160MHz", "80+80MHz", "unknown", "unknown" };
  char bssid_text[16];

  if (likely(ap)) {

    if (requested_bssid == NULL) { // TODO: refactor display_ap_details, get rid of second parameter
      snprintf(bssid_text,sizeof(bssid_text),"%02x%02x:%02x%02x:%02x%02x", ap->bssid[0], ap->bssid[1], ap->bssid[2], ap->bssid[3], ap->bssid[4], ap->bssid[5]);
      requested_bssid = bssid_text;
    }

    // SSID, BSSID and Security
    q_printf("%%\r\n%%<r>Information on AP \"%s\" (BSSID: %s)    </>\r\n%%\r\n", ap->ssid[0] ? (char *)ap->ssid : "[Hidden name]", requested_bssid);
    q_printf("%% Security: [%s], Pairwise cipher: %s, Group cipher: %s\r\n",
          Wifi_auth[ap->authmode],
          Wifi_cipher[ap->pairwise_cipher],
          Wifi_cipher[ap->group_cipher]);

    // WPS capabilities
    q_printf("%% WPS is %ssupported\r\n", ap->wps ? "" : "<i>not </>");

    // Channels
    q_printf("%% Channels: <i>%u</> (primary), secondary channel is %s\r\n",
              ap->primary, 
              ap->second == WIFI_SECOND_CHAN_NONE ? "not used"
                                                  : (ap->second == WIFI_SECOND_CHAN_ABOVE ? "above primary"
                                                                                          : "below primary"));

    // Signal power & bandwidth
    q_printf("%%\r\n%% Signal power (RSSI): %d dBm, used antenna#%d\r\n",ap->rssi,(int)ap->ant);
    q_printf("%% Bandwidth: %s\r\n", bw_desc[((unsigned)ap->bandwidth) & 7]);

    // PHY modes as advertised by an AP
    q_print("% PHY enabled modes:");
    if (ap->phy_11b || ap->phy_11g || ap->phy_11n) {
      q_print(" 802.11");
      if (ap->phy_11b) q_print("b");
      if (ap->phy_11g) q_print("g");
      if (ap->phy_11n) q_print("n");
    }

    if (ap->phy_11a || ap->phy_11ac || ap->phy_11ax ) {
      q_print(" 802.11");
      if (ap->phy_11a) q_print("a");
      if (ap->phy_11ac) q_print("ac");
      if (ap->phy_11ax) q_print("ax");
    }
    if (ap->phy_lr) q_print(" Low rate");
    q_print("\r\n");

    // FTM role
    q_print("%\r\n% FTM role: ");
    if (ap->ftm_responder && ap->ftm_initiator)
      q_print("responder and initiator\r\n");
    else if (ap->ftm_initiator)
      q_print("initiator only\r\n");
    else if (ap->ftm_responder)
      q_print("responder\r\n");
    else
      q_print("no support for Fine Time Measurement\r\n");
    // TODO: display country code from /ap->country/
  }
}

// Set WiFi driver storage: NVS or RAM
//
static int cmd_wifi_storage(int argc, char **argv) {
  wifi_storage_t storage = WIFI_STORAGE_RAM;
  if (argc < 3)
    return CMD_MISSING_ARG;
  if (!q_strcmp(argv[2],"flash"))
    storage = WIFI_STORAGE_FLASH;
  return ESP_OK == esp_wifi_set_storage(storage) ? 0 : CMD_FAILED;
}



// TODO: static_assert(WIFI_CIPHER_UNKNOWN == 12, "Code review is required");
// "wifi ap|sta"
static int cmd_wifi_if(int argc, char **argv) {

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!prepare_wifi_stack()) {
    q_print("% Failed to prepare the WiFi stack. WiFi is not available\r\n");
    return CMD_FAILED;
  }

  if (!get_staif())
    esp_netif_create_default_wifi_sta();

  if (!get_apif())
    esp_netif_create_default_wifi_ap();

  if (!get_apif() || !get_staif()) {
    q_print("% Can not create default AP/STA network interfaces\r\n");
    return CMD_FAILED;
  }

  if (argc > 2) {
    if (!q_strcmp(argv[1],"storage"))
      return cmd_wifi_storage(argc, argv);
    return 1;
  }

  // Set appropriate keywords (keywords_ap or keywords_sta), store interface type in /Context/
  // Create corresponding esp_netif_t
  if (argv[1][0] == 's')
    change_command_directory(WIFI_IF_STA, KEYWORDS(sta), PROMPT_WIFISTA, "WiFi STAtion");
  else if (argv[1][0] == 'a') { 
    change_command_directory(WIFI_IF_AP, KEYWORDS(ap), PROMPT_WIFIAP, "WiFi Access Point");
  } else {
    HELP(q_printf("%% Unknown mode \"%s\". Supported modes are \"sta\" and \"ap\"\r\n", argv[1]));
    return CMD_FAILED;
  }

  if (!start_wifi_stack()) {
    HELP(q_print("% Failed to start WiFi\r\n"));
    return CMD_FAILED;
  }

  return 0;
}


#include "esp_wifi_ap_get_sta_list.h"

static int cmd_show_wifi_clients(UNUSED int argc, UNUSED char **argv) {


    esp_netif_t *apif = get_apif();
    wifi_sta_list_t sta_list;
    esp_err_t err;

    if (!apif) {
      q_print("% Access Point was never set up\r\n");
      return 0;
    }

    if ((err = esp_wifi_ap_get_sta_list(&sta_list)) != ESP_OK) {
list_is_empty:
        q_print("% Stations list is empty\r\n");
        return 0;
    }

    if (!sta_list.num)
      goto list_is_empty;

    if (sta_list.num > ESP_WIFI_MAX_CONN_NUM)
      sta_list.num = ESP_WIFI_MAX_CONN_NUM;

    
    wifi_sta_mac_ip_list_t pairs = { 0 };
    esp_wifi_ap_get_sta_list_with_ip(&sta_list, &pairs);
    
    q_print("%<r> # | MAC address   | RSSI | IP Address </>\r\n"
               "% --+---------------+------+------------\r\n");

    for (int i = 0; i < sta_list.num; i++) {
        wifi_sta_info_t *info = &sta_list.sta[i];

        q_printf("%%%3d| %02X%02X:%02X%02X:%02X%02X | %4d | " IPSTR "\r\n",
                 i+1,
                 info->mac[0], info->mac[1], info->mac[2],
                 info->mac[3], info->mac[4], info->mac[5],
                 info->rssi,
                 IP2STR(&pairs.sta[i].ip));
    }
    q_print("% --+--------+-------+------+------------\r\n");
    q_printf("%% Connected: %d station%s\r\n", PPA(sta_list.num));

    return 0;
}


 

// Display AP WIFI configuration
//
//
static void show_wifi_ap_config(wifi_ap_config_t *c) {

  q_printf("%%\r\n%% Network: \"%s\"%s, WIFI channel %u, max-conn: %u\r\n",
            c->ssid, 
            c->ssid_hidden ? " (hidden)"
                           : "", 
            c->channel,
            c->max_connection);

    q_printf("%%\r\n%% Authentication: %s , pairwise cipher: %s\r\n"
             "%% SAE PWE derivation method: %d , SAE EXT feature: %sabled\r\n",
            Wifi_auth[c->authmode],
            Wifi_cipher[c->pairwise_cipher],
            (int)c->sae_pwe_h2e,
            c->sae_ext ? "en" : "dis");

    q_printf("%%\r\n%% Beacon interval: %u (TU) , CSA count: %u, dtim period: %u (sec)\r\n"
             "%% FTM responder: %sabled, PMF capable: %s, PMF required: %s\r\n",
              c->beacon_interval,
              c->csa_count,
              c->dtim_period,
              c->ftm_responder ? "en" : "dis",
              c->pmf_cfg.capable ? "Yes" : "No",
              c->pmf_cfg.required ? "Yes" : "No" );
          
}

// Display arbitrary wifi_config_t (STA or AP only)
//
static void show_wifi_sta_config(wifi_sta_config_t *c) {

  q_printf("%%\r\n%% Configured: SSID: \"<i>%s</>\" ",c->ssid);
  if (c->bssid_set)
    q_printf(", BSSID: %02x%02x:%02x%02x:%02x%02x\r\n",c->bssid[0],c->bssid[1],c->bssid[2],c->bssid[3],c->bssid[4],c->bssid[5]);
  else
    q_print(CRLF);

    q_printf("%%\r\n%% Scan method: %s, channel: <i>%u</>\r\n"
             "%% SAE PWE derivation method: %d , SAE PK mode: %u\r\n",
            c->scan_method == WIFI_FAST_SCAN ? "Fast scan" : "All channels",
            c->channel,
            (int)c->sae_pwe_h2e,
            (int)c->sae_pk_mode);

    q_printf("%%\r\n%% Radio measurement: %s , BSS transition mgmt: %s, MBO: %sabled, FT: %sabled\r\n"
             "%% OWE: %sabled, PMF capable: %s, PMF required: %s\r\n",
              c->rm_enabled ? "<i>ON</>" : "Off",
              c->btm_enabled ? "<i>ON</>" : "Off",
              c->mbo_enabled ? "en" : "dis",
              c->ft_enabled ? "en" : "dis",
              c->owe_enabled ? "en" : "dis",
              c->pmf_cfg.capable ? "Yes" : "No",
              c->pmf_cfg.required ? "Yes" : "No" );

}


// "show wifi ap|sta|clients"
//                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     0000000000000000   00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 
static int cmd_show_wifi(int argc, char **argv) {

  wifi_interface_t     ifx;                // interface type
  esp_netif_t         *ni;                 // interface 
  esp_netif_ip_info_t  ipi = { 0 };        // read ip information here
  esp_netif_dns_info_t dnsi = { 0 };       // read dns information
  wifi_config_t        conf = { 0 };       // AP or STA configuration 
  wifi_ap_record_t     ap_info;            // AP, STA is connected to
  uint8_t              mac[6];             // interface mac address
  bool                 link_up = false,    // Link layer is UP?
                       proto_up = false;   // Protocol layer is UP?
  const char          *hostn = NULL;       // Interface host name


  if (argc < 3)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[2],"clients"))
    return cmd_show_wifi_clients(argc, argv);

  // Set /ni/ to be esp_netif_t pointer of the interface we are "show"ing
  // /ifx/ is set to WIFI_IF_AP or WIFI_IF_STA and corresponding wifi config is then read
  //
  if (!q_strcmp(argv[2],"ap")) {

    ni = get_apif();
    ifx = WIFI_IF_AP;
    esp_wifi_get_config(WIFI_IF_AP, &conf); // if this API fails we'll display a null config

  } else if (!q_strcmp(argv[2],"sta")) {

    ni = get_staif();
    ifx = WIFI_IF_STA;
    esp_wifi_get_config(WIFI_IF_STA, &conf); // if this API fails we'll display a null config

  } else {
    HELP(q_print("% <e>\"sta\", \"ap\" or \"clients\" keywords expected</>\r\n"));
    return CMD_FAILED;
  }

  if (!ni) {
    q_print("% Network interface is NULL\r\n");
    return CMD_FAILED;
  }

  // Link == netif status
  link_up = esp_netif_is_netif_up(ni);
  // Procol: AP->always UP, STA->only when associated
  proto_up = (ifx == WIFI_IF_STA) ? (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) : true;

  q_printf("%%<r> Network interface WIFI %s                         </>\r\n",ifx == WIFI_IF_STA ? "STA" : "AP");
  q_printf("%% Link: %s</>, Protocol: %s</>\r\n",link_up ? "<g>UP" : "<i>DOWN", proto_up ? "<g>UP" : "<i>DOWN");

  if (esp_wifi_get_mac(ifx, mac) == ESP_OK)
    q_printf("%% MAC address: <i>%02x%02x:%02x%02x:%02x%02x</>\r\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);


  q_print("%\r\n");
  
  if (ESP_OK == esp_netif_get_ip_info(ni, &ipi)) {
    q_print("% <r>IP information and services                        </>\r\n");
    if (ipi.ip.addr)
      q_printf("%% IP address: <i>" IPSTR "</>, mask: " IPSTR ", gateway: " IPSTR "\r\n",
                IP2STR(&ipi.ip),
                IP2STR(&ipi.netmask),
                IP2STR(&ipi.gw));
    else
      q_print("% No IP address set/obtained\r\n");
  }

  // DNS information

  esp_netif_get_dns_info(ni, ESP_NETIF_DNS_MAIN, &dnsi);
  if (dnsi.ip.u_addr.ip4.addr != 0)
    q_printf("%% Main DNS: <i>" IPSTR "</>\r\n", IP2STR(&dnsi.ip.u_addr.ip4));
  else
    q_printf("%% DNS servers are not set\r\n");

  esp_netif_get_dns_info(ni, ESP_NETIF_DNS_BACKUP, &dnsi);
  if (dnsi.ip.u_addr.ip4.addr != 0)
    q_printf("%% Backup DNS: " IPSTR "\r\n", IP2STR(&dnsi.ip.u_addr.ip4));

  // DHCP information
  // TODO: DHCP address pool
  esp_netif_dhcp_status_t status = 0;

  if (ifx == WIFI_IF_AP)
    esp_netif_dhcps_get_status(ni, &status);
  else
    esp_netif_dhcpc_get_status(ni, &status);

  q_printf("%% DHCP %s is %s on the interface\r\n",
          (ifx == WIFI_IF_AP ? "server" : "client"),
          (status == ESP_NETIF_DHCP_STARTED ? "<i>started</>" : "<w>stopped</>"));

  
  if (esp_netif_get_hostname(ni, &hostn) == ESP_OK)
    q_printf("%% Host name (per-interface): \"<i>%s</>\"\r\n",hostn);


  if (ifx == WIFI_IF_STA) {
    if (proto_up) {
      q_printf("%% Connected to <b>%s</>, BSSID: %02x%02x:%02x%02x:%02x%02x\r\n",
                ap_info.ssid[0] ? (const char *)&ap_info.ssid[0] : "a <i>hidden network",
                ap_info.bssid[0],ap_info.bssid[1],ap_info.bssid[2],ap_info.bssid[3],ap_info.bssid[4],ap_info.bssid[5]);

        display_ap_details(&ap_info, NULL);
    } else {
      q_print("% <i>Not connected</> to any Access Point\r\n");
      show_wifi_sta_config(&conf.sta);
    }

  } else if (ifx == WIFI_IF_AP) {
      q_printf("%% Advertises as <b>%s</>%s\r\n",
                  conf.ap.ssid,
                  !conf.ap.ssid[0] || conf.ap.ssid_hidden ? " (hidden)"
                                                          : "");
      show_wifi_ap_config(&conf.ap);
  }

  return 0;
}


// "mac AABB:CCDD:EEFF"
// Depending on the /Context/ sets either AP or STA mac address
//
static int cmd_wifi_mac(int argc, char **argv) {
  esp_err_t err;
  uint8_t mac[6];
  THIS_INTERFACE(wif);

  if (argc < 2)
    return CMD_MISSING_ARG;
  
  if (!q_atomac(argv[1], mac)) {
    q_print("% MAC address AABB:CCDD:EEFF (or AA:BB:CC:DD:EE:FF) expected\r\n");
    return CMD_FAILED;
  }
  if ((err = esp_wifi_set_mac(wif, mac)) == ESP_OK)
    q_printf("%% New MAC address (%s, %s) set\r\n", wif == WIFI_IF_STA ? "STA" : "AP", argv[1]);
  else {
    q_printf("%% Can not set the new mac address (error %d)\r\n",err);
    if (mac[0] & 1)
      q_print("% Bit 0 of the first byte in MAC address must be 0 (zero)\r\n");
    else if (wif == WIFI_IF_AP)
      q_print("% WIFI AP interface must be UP to change its MAC address\r\n");

    return CMD_FAILED;
  }
  return 0;
}

// "hostname TEXT"
// Depending on the /Context/ sets either AP or STA mac address
//
static int cmd_wifi_hostname(int argc, char **argv) {

  THIS_INTERFACE(wif);
  const char *name;

  if (argc < 2) {
    if (esp_netif_get_hostname(ni,&name) == ESP_OK) {
      if (name) {
        q_printf("Hostname%d: \"%s\"\r\n",wif,name);
        return 0;
      }
    }
    q_print("Can not obtain system host name\r\n");
    return CMD_FAILED;
  }

  if (esp_netif_set_hostname(ni,argv[1]) == ESP_OK)
    q_print("% Host name updated. Restart interface to apply changes\r\n");
  else {
    q_print("% Failed to set the new host name\r\n");
    return CMD_FAILED;
  }
  
  return 0;
}

// 
//
//static int cmd_wifi_power(int argc, char **argv) {
//}
//extern int ieee80211_raw_frame_sanity_check(int,const void *,int,bool);
//extern void send_packet();

// "scan [active|passive|bssid MACADDRESS]*"
//
static int cmd_wifi_scan(int argc, char **argv) {

  wifi_ap_record_t *ap_records;
  bool active = true, detail = false, deinit = false;
  uint16_t ap_count = 0;
  uint8_t bssid[6], bidx = 0;
  
  wifi_scan_config_t scan_cfg = {
    .ssid = 0,
    .bssid = 0,
    .channel = 0,
    .show_hidden = true,
    .scan_type = WIFI_SCAN_TYPE_ACTIVE, // default scan type is "active" unless "passive" keyword is used
    .scan_time.active.min = 100, //TODO: no magic numbers
    .scan_time.active.max = 300  //TODO: no magic numbers
  };

  // read and process keywords
  for (int i = 1; i < argc; i++) {
    if (!q_strcmp(argv[i],"deinit"))
      deinit = true;
    if (!q_strcmp(argv[i],"passive"))
      active = false;
    else if (!q_strcmp(argv[i],"active"))
      active = true;
    else if (!q_strcmp(argv[i],"bssid")) {
      // if bssid is set, then we display detailed information on given bssid
      detail = true;
      if (++i >= argc) {
print_error_and_return:
        q_print("% Access Point MAC (BSSID) expected after \"bssid\"\r\n");
        return CMD_FAILED;
      }
      if (!q_atomac(argv[i], bssid))
        goto print_error_and_return;
      // argv index of a bssid in ascii form 
      bidx = i;
      // filter out all but bssid
      scan_cfg.bssid = bssid;
    }
  }
  if (!active) {
    scan_cfg.scan_type = WIFI_SCAN_TYPE_PASSIVE;
    scan_cfg.scan_time.passive = 200; //TODO: no magic numbers
  }

  if (!start_wifi_stack())
    return CMD_FAILED;

  q_printf("%% Starting %s WiFi scan (obtaining %s)...\r\n",
            scan_cfg.scan_type == WIFI_SCAN_TYPE_PASSIVE ? "passive" : "active",
            detail ? "details for the BSSID" : "a list of available networks");

  // Timestamp for "Took X seconds"
  uint64_t tsta = q_micros();

  // Blocking call. Using non-blocking call means proper handling of events, which may interfere with
  // sketch especially when WiFi Arduino library is linked. If user wants for some reason do a background scan
  // it can be done using "&" keyword (bg execution)
  //
  if (ESP_OK == esp_wifi_scan_start(&scan_cfg, true)) {

    tsta = (q_micros() - tsta) / 1000ULL;
    esp_wifi_scan_get_ap_num(&ap_count); // note ao_count must be set to 0 prior this call!

    // Did we found anything? 
    if (ap_count) {
      // Allocate memory  buffer and copy what was found to the buffer
      // TODO: implement q_calloc()
      if (NULL != (ap_records = calloc(ap_count, sizeof(wifi_ap_record_t)))) {
        esp_wifi_scan_get_ap_records(&ap_count, ap_records); // this function frees internal buffer automatically

        // If we are scanning for available networks (detail == 0 table view)
        // then we print out the table header. For a detailed view (detail == 1) we don't print any headers
        //
        if (!detail)
          q_print("%<r> # |Ch| Network Name (SSID)             | AP MAC (BSSID) | RSSI | Security     </>\r\n"
                     "% --+--+---------------------------------+----------------+------+--------------\r\n");

        // Run through array of found networks, print out AP information
        // If we were scanning a specific BSSID then ap array is usually 1 element long (ap_count == 1)
        //
        for (int i = 0; i < ap_count; i++) {

          wifi_ap_record_t *ap = &ap_records[i];
          // For the detailed view we call external "display" routine, but only if BSSID match (what was 
          // found matches with what was requested. just in case :)
          //
          if (detail) {
            if (!memcmp(bssid, ap->bssid, sizeof(bssid)))
              display_ap_details(ap,argv[bidx]);
          } else {
            // For the "table view" we print out lines (1 line for every AP) with brief information
            // on every found network
            q_printf("%% %-2d|%2d| %-32.32s| %02X%02X:%02X%02X:%02X%02X | %3d  |",
                      i + 1,
                      ap->primary,
                      ap->ssid[0] ? (char *)ap->ssid : "hidden",
                      ap->bssid[0], ap->bssid[1], ap->bssid[2],ap->bssid[3], ap->bssid[4], ap->bssid[5],
                      ap->rssi);

            if (ap->authmode < sizeof(Wifi_auth)/sizeof(Wifi_auth[0]))
              q_printf(" %-10.10s\r\n",Wifi_auth[ap->authmode]);
            else
              q_printf(" %d (?)\r\n",ap->authmode);
          }
        }

        // TODO: q_free()
        free(ap_records);
      }

      // Draw table footer: security column legend for the table view
      // or nothing for the "detailed view". Report if we have multiple APs with same BSSID
      //
      if (detail) {
        if (ap_count != 1)
          q_printf("%% Multiple (%u) AP (<w>sharing the same BSSID</>) were found\r\n",ap_count);
      } else {
        q_printf("%%\r\n%% Total: <i>%u</> access point%s\r\n%%\r\n", PPA(ap_count));
        q_print("% Legend (\"security\" column): \r\n"
                "%\"<i>*</>\" : PSK (Preshared key)\r\n"
                "%\"<i>+</>\" : PSK (Preshared key, mixed mode)\r\n"
                "%\"<i>$</>\" : ENT (Enterprise security)\r\n"
                "%\"<i>-</>\" : OPEN (Open access)\r\n");
      }
    }
  }

  q_printf("%% Scanning took %u.%u seconds%s\r\n", 
            (unsigned int)(tsta / 1000),
            ((unsigned int)(tsta % 1000)) / 10,
            ap_count ? ""
                     : ", found nothing suitable");

    if (deinit)
      stop_wifi_stack();

  return 0;
}

static void get_ip_info_ap_sta(esp_netif_ip_info_t *ipi_ap, esp_netif_ip_info_t *ipi_sta) {

  esp_netif_ip_info_t ipi0 = { 0 }, ipi_tmp;

  esp_netif_t *ni_ap  = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"),
              *ni_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

  if (!ipi_ap && !ipi_sta)
    return;

  if (!ipi_ap)
    ipi_ap = &ipi_tmp;
  else if (!ipi_sta)
    ipi_sta = &ipi_tmp;

  if (ni_ap == NULL)
    *ipi_ap = ipi0;
  else if (esp_netif_get_ip_info(ni_ap, ipi_ap) != ESP_OK)
    *ipi_ap = ipi0;

  if (ni_sta == NULL)
    *ipi_sta = ipi0;
  else if (esp_netif_get_ip_info(ni_sta, ipi_sta) != ESP_OK)
    *ipi_sta = ipi0;
}

// ip address dhcp|A.B.C.D/M [gw A.B.C.D|dns A.B.C.D [A.B.C.D]]*
//
static int cmd_wifi_ip_address(int argc, char **argv) {

  uint32_t ip = 0, mask = 0xffffff00, gw = 0, dns1 = 0, dns2 = 0;
  esp_netif_ip_info_t ipi = { 0 };
  esp_err_t ret;
  
  THIS_INTERFACE(ifx);

  if (argc < 3)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[2],"dhcp")) {
    if (ifx == WIFI_IF_AP) {
      q_print("% AP must have a static IP address (e.g. default 192.168.4.1/24)\r\n");
      return CMD_FAILED;
    }
    
  } else {
    
    // Read IP address and mask. If mask is not provided (i.e. "1.2.3.4" instead of "1.2.3.4/24")
    // then we assume mask to be 255.255.255.0
    //
    if ((ip = q_atoip(argv[2], &mask)) == 0) {
bad_static_ip_address:
      q_print("% Invalid address/mask. (a valid example: \"192.168.4.1/24\")\r\n");
      return CMD_FAILED;
    }

    if (mask == 0xffffffffUL) {
      mask = 0xffffff00;
      q_print("% Mask defaults to /24\r\n");
    }

    // Last octet of the IP address can not be zero
    //
    if ((ip & 0xff) == 0)
      goto bad_static_ip_address;


    // Check if IP address conflicts with other existing addresses: IP address of any interface
    // must be on a separate subnet. Read IP addresses from all interfaces and check if this new
    // does not belong to any of existing subnets
    //
    esp_netif_ip_info_t ipx, ipy;    //ipx is for AP, ipy is for STA
    get_ip_info_ap_sta(&ipx,&ipy);

    // If our interface is WIFI STA, then make sure our address is not from WIFI AP
    // If our interface is WIFI AP, then check if our address is not from WIFI STA's subnet
    //
    if ((ifx == WIFI_IF_STA && (q_ntohl(ipx.ip.addr & ipx.netmask.addr) == (ip & mask))) ||
        (ifx == WIFI_IF_AP && (q_ntohl(ipy.ip.addr & ipy.netmask.addr) == (ip & mask)))) {
      q_print("% This IP address belongs to some other interface and can not be used\r\n");
      return CMD_FAILED;
    }
  }

  //Read gateway and dns servers. Start from argv[3] till the end
  //
  for (int i = 3; i < argc; i++) {
    if (!q_strcmp(argv[i],"gw")) {
      i++;
      if (i < argc) {
        if ((gw = q_atoip(argv[i], NULL)) == 0) {
          q_print("% Invalid default gateway address\r\n");
          return CMD_FAILED;
        }
      }
    } else if (!q_strcmp(argv[i],"dns")) {

      i++;
      if (i < argc) {
        uint32_t *p = dns1 ? &dns2 : &dns1;
        if ((*p = q_atoip(argv[i], NULL)) == 0) {
          q_print("% Invalid DNS address\r\n");
          return CMD_FAILED;
        }
      }
    } else
      q_printf("%% Keyword \"%s\" ignored\r\n",argv[i]);
  }

  // Static IP address (ip != 0).
  // DHCP server / DHCP client must be stopped before attempting to set a new IP address
  //
  if (ip) {
    
    if (ifx == WIFI_IF_STA)
      esp_netif_dhcpc_stop(ni); // stop DHCP client, if we are STA
    else
      esp_netif_dhcps_stop(ni); // stop DHCP server, if we are AP

    // ip/mask/gw
    ipi.ip.addr = q_htonl(ip);
    ipi.netmask.addr = q_htonl(mask);
    ipi.gw.addr = gw ? q_htonl(gw) : ipi.ip.addr;

    if ((ret = esp_netif_set_ip_info(ni, &ipi)) != ESP_OK) {
      q_printf("%% Failed to assign a new static IP. (error code %x)\r\n",ret);
      return CMD_FAILED;
    }
    
    // Main and Backup DNS
    //
set_static_dns:
    esp_netif_dns_info_t dnsi = { 0 };

    dnsi.ip.type = IPADDR_TYPE_V4;
    if (dns1) {

      dnsi.ip.u_addr.ip4.addr = q_htonl(dns1);
      if (esp_netif_set_dns_info(ni, ESP_NETIF_DNS_MAIN, &dnsi) != ESP_OK) {
        q_print("% Failed to set main DNS address\r\n");
        return CMD_FAILED;
      }
      // When static DNS servers are set for the AP, we do not propagate STA's DNS servers
      // AP_static_dns is set to /true/ when AP has static DNS servers configured
      if (ifx == WIFI_IF_AP) {
        AP_static_dns = true;
        dhcp_server_set_dns_option();
      }

    } else {
      // No DNS1 (means there are no DNS2 as well) - reset static_dns flag for the AP
      if (ifx == WIFI_IF_AP)
        AP_static_dns = false;
    }

    if (dns2) {
      dnsi.ip.u_addr.ip4.addr = q_htonl(dns2);
      if (esp_netif_set_dns_info(ni, ESP_NETIF_DNS_BACKUP, &dnsi) != ESP_OK)
        q_print("% Failed to set backup DNS address\r\n");    
    }

    // Start DHCP server again (if we are AP)
    if (ifx == WIFI_IF_AP)
      esp_netif_dhcps_start(ni);

  } else {
    // If we are here then: interface is STA, ip/mask/gw are from DHCP.
    // Restart DHCP client.
    //
    HELP(q_print("% Restarting DHCP client on the interface..\r\n"));
    esp_netif_dhcpc_stop(ni);
    q_delay(10);
    esp_netif_dhcpc_start(ni);

    goto set_static_dns;
  }

  return 0;
}

// natp enable|disable
// natp add tcp|udp EXTERNAL_PORT INTERNAL_IP INTERNAL_PORT
// natp del tcp|udp EXTERNAL_PORT
//
static int cmd_wifi_natp(int argc, char **argv) {

  uint8_t ret = 0;
  esp_netif_t *staif;

  THIS_INTERFACE(ifx);
  MUST_NOT_HAPPEN((ifx != WIFI_IF_AP));

  if (argc < 2)
    return CMD_MISSING_ARG;

  staif = get_staif();  

  if (!staif)
    return CMD_FAILED;
  

  if (!q_strcmp(argv[1],"enable")) {

    // Make STA to be default interface and enable NAT/P on the AP
    //
    esp_netif_set_default_netif(staif);
    if (ESP_OK != esp_netif_napt_enable(ni))
      q_print("% Failed to enable NAT/P. Make sure AP and STA interfaces are UP\r\n");
    return 0;

  } else if (!q_strcmp(argv[1],"disable")) {

    // Disable NAT/P
    //
    if (ESP_OK != esp_netif_napt_disable(ni))
      q_print("% Failed to disable NAT/P\r\n");

    return 0;
  } else if (!q_strcmp(argv[1],"add") || !q_strcmp(argv[1],"delete")) {

    // Add/remove NAT/P port mapping
    //
    uint32_t ip, intp, extp;
    uint8_t proto;
    bool add = (argv[1][0] == 'a'); // add or delete?

    if ((add && (argc < 6)) || (!add && (argc < 4)))
      return CMD_MISSING_ARG;

    if (!q_strcmp(argv[2],"tcp"))
      proto = IPPROTO_TCP;
    else if (!q_strcmp(argv[2],"udp"))
      proto = IPPROTO_UDP;
    else
      return 2;

    if ((extp = q_atol(argv[3], 0)) == 0)
      return 3;

    if (!add)
      ret = ip_portmap_remove(proto, extp);
    else {
      if ((ip = q_atoip(argv[4], NULL)) == 0)
        return 4;

      if ((intp = q_atol(argv[5], 0)) == 0)
        return 5;

      esp_netif_ip_info_t ipi;
      if (ESP_OK != esp_netif_get_ip_info(staif, &ipi)) {
        q_print("% Can not obtain STA(WAN) IP address\r\n");
        return CMD_FAILED;
      }
      
      ret = ip_portmap_add(proto, ipi.ip.addr, q_htons(extp), q_htonl(ip), q_htons(intp));
    }

    return ret ? 0 : CMD_FAILED;

  } // if add or delete

  return 1;
}

// "ntp server ADDRESS|dhcp [ADDRESS]"
// "ntp disable|enable"
// 
static int cmd_wifi_ntp(int argc, char **argv) {
  NOT_YET();
  return 0;
}

// "dhcp START_IP_ADDRESS [MAX_CLIENTS=16 [TIMESPEC]]"
// "dhcp enable|disable"
//
static int cmd_wifi_dhcp(int argc, char **argv) {

  THIS_INTERFACE(ifx);
  MUST_NOT_HAPPEN((ifx != WIFI_IF_AP)); 

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[1],"enable")) {
    VERBOSE(q_print("% Enabling DHCP server..\r\n"));
    esp_netif_dhcps_start(ni);
  } else if (!q_strcmp(argv[1],"disable")) {
    VERBOSE(q_print("% Disabling DHCP server..\r\n"));
    esp_netif_dhcps_stop(ni);
  } else {
    uint32_t ip, mask;
    uint32_t lease       = 0,     // use default lease interval
             max_clients = 252;   //.0, .1, .254 and .255 are reserved

    if ((ip = q_atoip(argv[1],&mask)) == 0) {
      q_print("% Keywords \"enable\", \"enable\" or a valid IP address expected\r\n");
      return CMD_FAILED;
    }

    if (argc > 2)
      max_clients = q_atol(argv[2], max_clients);

    if (argc > 3)
      lease = q_atol(argv[3], lease);

    bool was_started = dhcp_server_stop_if_started(ni);
    // TODO:
    dhcp_server_restart_if_was_started(ni, was_started);
  }
  return 0;
}

// up SSID [PASSWORD]                                  <-- sta
// up BSSID [PASSWORD]                                 <-- sta
// up SSID PASSWORD [max-conn NUM | channel NUM ]*      <-- ap
// up SSID                                             <-- ap
//
static int cmd_wifi_up(int argc, char **argv) {

  uint8_t bssid[6];

  THIS_INTERFACE(ifx);

  // Check if interface is UP and RUNNING. If it is - do nothing
  //
  wifi_ap_record_t ap_info;
  if (esp_netif_is_netif_up(ni) || (ifx == WIFI_IF_STA && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)) {
    HELP(q_print("% Interface is UP and RUNNING. (Use \"down\" before reconfiguring)\r\n"));
    return 0;
  }

  // Command "up" for STA interface: read SSID/BSSID, PASSWORD and the keyword "no-retry"
  //
  if (ifx == WIFI_IF_STA) {

    wifi_config_t stac = { 0 };
    bool use_saved = false;

    // "up" without arguments: use saved config, if available (STA only)
    // AP also can be configured from its default config but that means OPEN security, so it is disabled:
    // "up" without args only allowed for STA interfaces
    //
    while (argc < 2) {
      if (esp_wifi_get_config(WIFI_IF_STA, &stac) == ESP_OK) {
        if (stac.sta.bssid_set || stac.sta.ssid[0]) {
          use_saved = true;
          break;
        }
      }
      q_print("% No valid WiFi STA configuration found, use \"up SSID [PASSWORD]\"\r\n");
      return CMD_MISSING_ARG;
    }

    if (!use_saved) {

      stac.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
      stac.sta.failure_retry_cnt = 3;
      stac.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    
      // Is first parameter SSID or BSSID?
      if (q_atomac(argv[1],bssid)) {
        memcpy(stac.sta.bssid, bssid, sizeof(stac.sta.bssid));
        stac.sta.bssid_set = true;
        HELP(q_print("% Connect to a network using BSSID\r\n"));
      } else {
        strlcpy((char *)stac.sta.ssid,argv[1],sizeof(stac.sta.ssid));
        HELP(q_print("% Connect to a network using SSID\r\n"));
      }

      // Password supplied? "auto-connect"?
    
      if (argc > 2) {
        strlcpy((char *)stac.sta.password,argv[2],sizeof(stac.sta.password));
        if (argc > 3)
          if (!q_strcmp(argv[3],"auto-connect"))
            Sta_reconnect = true;
          HELP(q_print("% Auto-reconnect enabled\r\n"));
      }
    }

    if (ESP_OK == esp_wifi_set_config(WIFI_IF_STA, &stac))
      esp_wifi_connect();
    else
      q_print("% Can not set new WiFi configuration\r\n");

  } else {

    wifi_config_t apc = { 0 };

    if (argc < 2)
      return CMD_MISSING_ARG;

    strlcpy((char *)apc.ap.ssid,argv[1],sizeof(apc.ap.ssid));
    apc.ap.ssid_len = strlen(argv[1]);

    // SSID is empty string?
    if (apc.ap.ssid_len == 0)
      apc.ap.ssid_hidden = 1;

    // password supplied?
    if (argc > 2)
      strlcpy((char *)apc.ap.password,argv[2],sizeof(apc.ap.password));

    if (apc.ap.password[0] /*strlen(argv[2]) > 0*/ == 0) {
      VERBOSE(q_print("% AP authentication mode set to OPEN (no password supplied)\r\n"));
      apc.ap.authmode = WIFI_AUTH_OPEN;
    } else {
      VERBOSE(q_print("% AP authentication mode set to WPA2-PSK\r\n"));
      apc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    apc.ap.max_connection = 4; // TODO: no magic numbers!
    // up "SSID" "Password" [max-conn NUM | channel NUM | auth AUTH]
    //
    //
    for (int i = 3; i < argc; i++) {
      // next token is "max-conn"? convert next token to number and save
      if (!q_strcmp(argv[i],"max-conn")) {
        if (++i < argc)
          apc.ap.max_connection = q_atol(argv[i], 1);
      } else if (!q_strcmp(argv[i],"channel")) {
        if (++i < argc)
          apc.ap.channel = q_atol(argv[i], 0);
      } else
        q_printf("%% Keyword \"%s\" ignored\r\n",argv[i]);
    }

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &apc);
  }

  return 0;
}

// down
//
static int cmd_wifi_down(int argc, char **argv) {

  THIS_INTERFACE(ifx);

  // Check if interface is UP and RUNNING.
  //
  wifi_ap_record_t ap_info;
  if (esp_netif_is_netif_up(ni) || (ifx == WIFI_IF_STA && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)) {
    if (ifx == WIFI_IF_STA) {
      Sta_reconnect = false;
      esp_wifi_disconnect();
    }
    else {
      // TODO: set interface DOWN, link DOWN?
      esp_wifi_set_mode(WIFI_MODE_STA);
    }
  } else
    q_print("% Interface is down\r\n");

  return 0;
}


#endif // if compiling espshell

