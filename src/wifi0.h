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


#if COMPILING_ESPSHELL

// -- WiFi support --
//
// Creation/deletion/configuring of WiFi interfaces. Access Point and/or Station mode,
// NAT router, NTP client and DHCP server. It is compatible with WiFi Arduino library.
//
// TODO: static_assert(WIFI_CIPHER_UNKNOWN == 12, "Code review is required");
// TODO: check error codes for all esp-idf functions

#include "freertos/event_groups.h"
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_phy.h>
#include <esp_wifi_ap_get_sta_list.h>
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
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

// -- MACROS --

// espshell can either obtain esp_netif_t pointers via *search* every time OR search once and *cache* these pointers
// assuming that interfaces are not deleted/recreated again. If a host sketch (sketch which uses espshell) **deletes**
// WiFi network interfaces then CACHE_NETIFS must be set to 0: it will result in a tiny perfomance impact because of
// searching
//
#define CACHE_NETIFS 1

// Max number of simultaneous WiFi connections (when in AP mode)
#define MAX_CONN 4

// 24 hours default IP address lease time
#define DEF_LEASE (24*3600)

// DHCP offer option for DNS server
#define DHCP_OPT_OFFER_DNS 0x02                    

// ESP-IDF LwIP has MACSTR but I prefer cisco flavour. Used in conjuntion with LwIP's MAC2STR
// like this: printf("This is a mac " MACSTR " ", MAC2STR(...))
#ifdef MACSTR
#  undef MACSTR
#endif
#define MACSTR "%02x%02x:%02x%02x:%02x%02x"

// Backup servers, if not set
#define NTP_SERVER1 "time.windows.com"
#define NTP_SERVER2 "pool.ntp.org"

// Prologue macro for cmd_wifi_...() handlers.
// NOTE!: has "return" statement in it. Supposed to be the first line of WiFi command handlers.
//
// Get current interface index (WIFI_IF_STA or WIFI_IF_AP) and also fetch corresponding esp_netif_t
// structure address: commands "wifi ap|sta" and "show wifi ap|sta" autocreate AP/STA netifs so the 
// address must not be NULL.
//
#define THIS_INTERFACE(_Ifx) \
  wifi_interface_t _Ifx = (wifi_interface_t )context_get(); /* get command directory context */ \
  __attribute__((unused)) esp_netif_t *ni = (_Ifx == WIFI_IF_AP) ? get_apif() : get_staif(); \
  if (unlikely(_Ifx >= WIFI_IF_NAN || _Ifx < 0 || ni == NULL)) {\
    q_print("% THIS_INTERFACE() : disrupted Context!\r\n"); \
    return CMD_FAILED; \
  }

// a or b?
#define up_down_str(_X) ((_X) == 0 ? "DOWN" : "UP")
#define ap_sta_str(_X) ((_X) == WIFI_IF_AP ? "AP" : "STA")


// -- Globals --

// WiFi settings 
//
static struct {
  bool sta_reconnect:1;     // Auto-reconnect if connection was lost?
  bool ap_static_dns:1;     // AP has static DNS addresses set? (STA will NOT propagate its DNS to AP)
  bool sta_static_dns:1;    // STA has static DNS addresses set? (DHCP offer will be ignored)
  bool prepared:1;          // is WiFi stack is fully initialized?
  bool started:1;           // is WiFi task started?
  bool sntp_enabled:1;      // is NTP enabled or not?
  bool log:1;               // Display WiFi/IP events or not
  bool reserved:1;
  uint32_t sta_dns1;        // Static DNS addresses. Uploaded to LwIP when IP address is received via DHCP
  uint32_t sta_dns2;
} Wifi = { 0 };             // All values are /false/ by default


// These pointers hold strdup()ed NTP server names. LwIP SNTP only stores pointers to server names so
// it is up to the user to make sure that this memory is always valid.
//
static char *ntp_servers[SNTP_MAX_SERVERS] = { NULL };

// sntp configuration
// Two servers are hardcoded in addition to DHCP-obtained DNS configuration
// If servers are not obtained (e.g. sntp was started after DHCP finished its work) then these two will be used
// If there was DNS server address obtained from a DHCP server, then DHCP's become highest priority while 
// statically set time.windows.com and pool.ntp.org become backup servers
//
static esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,
                                          ESP_SNTP_SERVER_LIST(NTP_SERVER1, NTP_SERVER2 ));



// WiFi auth types in human-readable form. First byte is the security type:
// - = open, * = psk, + = psk mm, or $ = enterprise
// (see legend in "scan" command output)
// Second byte is space (" ")
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


// -- 
//

#if CACHE_NETIFS
// "Search once and cache results"
// In most use-cases network interfaces are created and never deleted so
// it is safe to fetch pointers once and cache their value
//
// Returns NULL if WiFi is not initialized (prepare_wifi_stack()) otherwise returns
// pointer to the STA's netif
//
static esp_netif_t *get_staif() {
  static esp_netif_t *cached = NULL;
  if (unlikely(cached == NULL))
    cached = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  return cached;
}

// Return pointer to AP's netif or NULL if WiFi is not initialized
//
static esp_netif_t *get_apif() {
  static esp_netif_t *cached = NULL;
  if (unlikely(cached == NULL))
    cached = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  return cached;
}
// Safer but slower version
//
#else
#  define get_staif() esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")
#  define get_apif() esp_netif_get_handle_from_ifkey("WIFI_AP_DEF")
#endif

#define is_sta_here() (get_staif() != NULL)
#define is_ap_here() (get_apif() != NULL)


// Update AP's DNS settings from STA's DHCP reply. 
// This one is called when STA receives GOT_IP event (see ip_event_handler() below).
// When STA gets its IP/mask/GW/.. settings  it *propagates* DNS settings to the AP interface, so AP
// can include it in its DHCP reply (AP+STA mode, NAT router)
//
// NOTE: DNS entries are propagated only if AP has no static DNS servers configured.
// NOTE: only 1 DNS server entry is propagated. 
//
static bool sta_propagate_dns(esp_netif_t *sta) {
  if (!Wifi.ap_static_dns) {
    esp_netif_dns_info_t dnsi = { 0 };
    esp_netif_t *apif = get_apif();
    if (sta && apif)
      if (esp_netif_get_dns_info(sta,ESP_NETIF_DNS_MAIN,&dnsi) == ESP_OK)
        return (ESP_OK == esp_netif_set_dns_info(apif,ESP_NETIF_DNS_MAIN,&dnsi));
  }
  return false;
}

// Restore static DNS servers that was overwritten by a DHCP server
// When STA's address is configured as "ip address dhcp dns 8.8.8.8" then interface uses statically configured DNS
// servers. This functions is a callback which is called upon IP address assignment (GOT_IP event)
//
static bool sta_restore_static_dns(esp_netif_t *staif) {
  if (Wifi.sta_static_dns) {
    esp_netif_dns_info_t dnsi = { 0 };
    if (staif) {
      dnsi.ip.u_addr.ip4.addr = q_htonl(Wifi.sta_dns1);
      esp_netif_set_dns_info(staif,ESP_NETIF_DNS_MAIN,&dnsi);
      dnsi.ip.u_addr.ip4.addr = q_htonl(Wifi.sta_dns2);
      esp_netif_set_dns_info(staif,ESP_NETIF_DNS_BACKUP,&dnsi);
    } //TODO: check ESP-IDF return codes
  }
  return true;
}


// DHCP server must be stopped before reconfiguring
// This functions stops the server and returns /true/ if server was started
// or /false/ if it was not. This return value is then used with dhcp_server_restart_if_was_started(X)
//
static bool dhcp_server_stop_if_started(esp_netif_t *apif) {

  if (likely(apif != NULL)) {
    // failsafe: assume started in case of any errors
    esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_STARTED;
    esp_netif_dhcps_get_status(apif, &status);
    if (status != ESP_NETIF_DHCP_STOPPED) {
      //VERBOSE(q_print("% Stopping DHCP server..Ok \r\n"));
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
    //VERBOSE(q_print("% Restarting AP's DHCP server.."));
    if (esp_netif_dhcps_start(apif) == ESP_OK) {
      //VERBOSE(q_print("Ok\r\n"));
    } else {
      //VERBOSE(q_print("Failed\r\n"));
    }
  }
}

// Tells DHCP server (AP interface) to include DNS server information in its DHCP offer.
// Results in DHCP server restart if it was started
//
static bool dhcp_server_set_dns_option() {

  esp_netif_t *apif = get_apif();
  uint8_t dhcps_offer_option = DHCP_OPT_OFFER_DNS;
  bool was_started;

  if (apif == NULL)
    return false;

  was_started = dhcp_server_stop_if_started(apif);

  VERBOSE(q_print("% DHCP : DNS is included in DHCP Offer, restarting server\r\n"));
  esp_netif_dhcps_option(apif, 
                        ESP_NETIF_OP_SET,
                        ESP_NETIF_DOMAIN_NAME_SERVER,
                        &dhcps_offer_option, sizeof(dhcps_offer_option));

  dhcp_server_restart_if_was_started(apif, was_started);

  return true;
}

// Set custom lease time in seconds. Unlike function above which starts/stops DHCP server 
// this function expects that DHCP server is already stopped
//
static bool dhcp_server_set_lease(esp_netif_t *apif, uint32_t lease0) {

  esp_err_t err;
  dhcps_time_t lease = lease0 ? lease0 : DEF_LEASE;

  if (apif == NULL)
    return false;

  if (ESP_OK != (err = esp_netif_dhcps_option(apif,
                                        ESP_NETIF_OP_SET,
                                        ESP_NETIF_IP_ADDRESS_LEASE_TIME,
                                        &lease,
                                        sizeof(lease)))) {
    HELP(q_print("% Failed to add DHCP option (\"IP address lease time\")"));                                          
  }

  return ESP_OK == err;
}

// Set DHCP IP address range
// Again - this function expects DHCP server to stopped or this function fails
//
static bool dhcp_server_set_ip_pool(esp_netif_t *apif, uint32_t ip_start, uint32_t count) {

  uint32_t ip_end;
  
  esp_netif_ip_info_t ipx;

  dhcps_lease_t dhcps_pool = {
    .enable = true
  };

  if (apif == NULL)
    return false;
    
  // Read AP's IP address & subnet mask to verify if pool address range
  // is on the same subnet
  if (esp_netif_get_ip_info(apif, &ipx) == ESP_OK) {
    uint32_t mask = q_ntohl(ipx.netmask.addr);
    if ((ip_start & mask) == (q_ntohl(ipx.ip.addr) & mask)) {
      esp_err_t err;

      // If pool size is not set then we assume maximum size. Size depends on the network class.
      // If pool size is set then we check if ending address is still on the same subnet and clamp if necessary
      if (count == 0)
        ip_end = (ip_start | (~mask)) - 1;
      else {
        if ((count + (ip_start & (~mask))) > (~mask - 1))
          ip_end = (ip_start | (~mask)) - 1;
        else
          ip_end = ip_start + count;
      }

      dhcps_pool.start_ip.addr = q_htonl(ip_start);
      dhcps_pool.end_ip.addr = q_htonl(ip_end);

      if (ESP_OK != (err = esp_netif_dhcps_option(apif, ESP_NETIF_OP_SET, REQUESTED_IP_ADDRESS, &dhcps_pool, sizeof( dhcps_lease_t )))) {
        HELP(q_print("% Failed to add a DHCP server option (\"IP address range\")"));
      }

      return err == ESP_OK;
    } else
      q_print("% <e>Requested IP range must be on the interface subnet</>\r\n");
  } else
    q_print("% <e>Set interface IP address first</>\r\n");
  return false;
}

// IP Events handler instance
//
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (likely(event_base == IP_EVENT))
    switch(event_id) {

      // STA has IP address assigned (static or dynamic)
      // Fetch DNS server from the STA interface, add DHCP server option
      case IP_EVENT_STA_GOT_IP:

        ip_event_got_ip_t *got = (ip_event_got_ip_t *)event_data;
        HELP(if (Wifi.log) q_printf("\r\n%% WIFI STA: Protocol UP (assigned: " IPSTR "/%u, gate: " IPSTR "\r\n",
                          IP2STR(&got->ip_info.ip),
                          __builtin_popcount(got->ip_info.netmask.addr),
                          IP2STR(&got->ip_info.gw)));

        if (!Wifi.ap_static_dns) {
          HELP(if (Wifi.log) q_print("% WIFI STA: propagating DNS servers to the AP interface..\r\n"));
          sta_propagate_dns(got->esp_netif);
          dhcp_server_set_dns_option();
        }

        if (Wifi.sta_static_dns) {
          if (sta_restore_static_dns(got->esp_netif)) {
            HELP(if (Wifi.log) q_print("\r\n% DHCP's DNS offer ignored, static DNS addresses are set\r\n"));            
          }
        }

        break;

      case IP_EVENT_STA_LOST_IP:
        HELP(if (Wifi.log) q_print("\r\n% WIFI STA: Protocol DOWN (IP address lost)\r\n"));
        break;

      default:
        //VERBOSE(q_printf("%% IP-EVENT: unhandled (base=%x, id=%x)\r\n",(unsigned int)event_base,(unsigned int)event_id));
        break;
    };

}

// WiFi events handler instance
//
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

  if (likely(event_base == WIFI_EVENT))
    switch(event_id) {

      case WIFI_EVENT_STA_CONNECTED:
        HELP(if (Wifi.log) q_print("\r\n% WIFI STA: Connected, link UP. Waiting for protocol layer..\r\n"));
        break;

      case WIFI_EVENT_STA_DISCONNECTED:
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t*)event_data;
        HELP(if (Wifi.log) q_printf("\r\n%% WIFI STA: Disconnected (reason=%u, rssi=%d), link DOWN\r\n",event->reason, event->rssi));
        if (event->reason == WIFI_REASON_ROAMING) {
          HELP(if (Wifi.log) q_print("% WIFI STA: station roaming\r\n"));
        } else
        // optional auto-reconnect
         if (Wifi.sta_reconnect) {
           HELP(if (Wifi.log) q_print("% WIFI STA: Trying to reconnect.. (disable reconnect with \"down\")\r\n"));
           // TODO: make a delayed call to the esp_wifi_connect() to prevent "connect" storm.
           esp_wifi_connect(); 
         }
        break;
    
    // AP events
      case WIFI_EVENT_AP_START:
        //VERBOSE(q_print("% Access Point starting..\r\n"));
        break;

      case WIFI_EVENT_AP_STOP:
        //VERBOSE(q_print("% Access Point shutting down..\r\n"));
        break;


      case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t*) event_data;
        HELP(if (Wifi.log) q_printf("\r\n%% WIFI AP: " MACSTR " connected, (aid=%d, auth=OK))\r\n",
                                    MAC2STR(event->mac),
                                    event->aid)); 
        break;
      }

      case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t*) event_data;
        HELP(if (Wifi.log) q_printf("\r\n%% WIFI AP: Station#%d (" MACSTR ") disconnected (reason=%u))\r\n",
                                    event->aid, 
                                    MAC2STR(event->mac),
                                    event->reason)); 
        break;
      }

      case WIFI_EVENT_AP_WRONG_PASSWORD:
        HELP(if (Wifi.log) q_print("\r\n% WIFI AP: client connection failed (wrong password)\r\n"));
        break;

      default:
        break;
    };
    //VERBOSE(q_printf("%% WIFI-EVENT: arg=%p, base=%x, id=%x, edata=%p\r\n",arg,(unsigned int)event_base,(unsigned int)event_id,event_data));
}

// Tell espshell that we have new time source and date/time values
// Called by SNTP client when NTP server reply is received
//
static void time_sync_notification_cb(UNUSED struct timeval *tv) {
    time_has_been_updated("NTP server");
}


// Initialize the WiFi stack, perform minimal initial config.
// Can be called multiple times
//
static bool prepare_wifi_stack() {

  static bool ni = false;


  if (!ni) {
    // it is ok to call esp_netif_init() twice: if sketch performs esp_netif_init() then
    // *this* esp_netif_init() just does nothing
   esp_netif_init();
   ni = true;
  }

  // Lets check if network interfaces are created already. We check for presence AP and STA interfaces,
  // and if found - skip WiFi initialization, set Wifi.prepared flag
  if ((get_staif() || get_apif()) && !Wifi.prepared) {
    HELP(q_print("% WiFi seems to be initialized by the sketch. Skipping WiFi init\r\n"));
    Wifi.prepared = true;
  }

  if (!Wifi.prepared) {

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    sntp_cfg.start = false;                       // start SNTP service explicitly (after connecting)
    sntp_cfg.server_from_dhcp = true;             // accept NTP offers from DHCP server, if any (need to enable *before* connecting)
    sntp_cfg.renew_servers_after_new_IP = true;   // let esp-netif update configured SNTP server(s) after receiving DHCP lease
    sntp_cfg.index_of_first_server = 1;
    sntp_cfg.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
    sntp_cfg.sync_cb = time_sync_notification_cb;

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

    if (!get_staif())
      esp_netif_create_default_wifi_sta();

    if (!get_apif())
      esp_netif_create_default_wifi_ap();

    Wifi.prepared = true;
  }
  // TODO: check other errors
  return Wifi.prepared;
}

static bool start_wifi_stack() {

  if (!Wifi.prepared) {
    VERBOSE(q_print("% start_wifi_stack() called without prepare_wifi_stack()\r\n"));
    return false;
  }

  if (!Wifi.started) {
    if (esp_wifi_start() == ESP_OK)
      Wifi.started = true;
    else {
      HELP(if (Wifi.log) q_print("% WIFI failed to initialize\r\n"));
    }
  }
  return Wifi.started;
}


static void show_sntp_servers() {

  int found = 0;
  char buff[INET6_ADDRSTRLEN];

  if (Wifi.sntp_enabled) {
    q_print("% List of configured NTP servers: ");
    for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i){
      if (esp_sntp_getservername(i)) {
        found++;
        q_printf("%s, ", esp_sntp_getservername(i));
      } else {
        ip_addr_t const *ip = esp_sntp_getserver(i);
        if (ipaddr_ntoa_r(ip, buff, sizeof(buff)) != NULL) {
          found++;
          q_printf("%s, ", buff);
        }
      }
    }
    if (!found)
      q_print(" none\r\n");
    else
      q_print("\r\n");
  }
}


// Display AP details
//
static void display_ap_details(wifi_ap_record_t *ap, const char *requested_bssid) {

  const char *bw_desc[] = { "unknown", "20MHz", "40MHz", "80MHz", "160MHz", "80+80MHz", "unknown", "unknown" };
  char bssid_text[16];

  if (likely(ap)) {

    if (requested_bssid == NULL) { // TODO: refactor display_ap_details, get rid of second parameter
      snprintf(bssid_text,sizeof(bssid_text), MACSTR, MAC2STR(ap->bssid));
      requested_bssid = bssid_text;
    }

    // SSID, BSSID and Security
    //q_printf("%%\r\n%%<r>Access point \"%s\" (BSSID: %s)    </>\r\n%%\r\n", ap->ssid[0] ? (char *)ap->ssid : "[Hidden name]", requested_bssid);
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
    if (ap->ftm_responder || ap->ftm_initiator) {
      q_print("%\r\n% FTM role: ");
      if (ap->ftm_responder && ap->ftm_initiator)
        q_print("responder and initiator\r\n");
      else if (ap->ftm_initiator)
        q_print("initiator only\r\n");
      else
        q_print("responder\r\n");
    }
    // TODO: display country code from /ap->country/
  }
}

// Handlers
//


// Set WiFi driver storage: NVS or RAM
// "wifi storage ram|flash"
//
// NOTE: This handler MUST be called from cmd_wifi_if() is it is done right now, because only cmd_wifi_if() ensures 
//       proper WiFi stack initialization
// NOTE: This handler MUST NOT be used as is in "keywords.h"!
//
static int cmd_wifi_storage(int argc, char **argv) {

  wifi_storage_t storage = WIFI_STORAGE_RAM;

  if (argc < 3)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[2],"flash"))
    storage = WIFI_STORAGE_FLASH;

  return ESP_OK == esp_wifi_set_storage(storage) ? 0 : CMD_FAILED;
}

// Enable/disable verbose event logging
// "wifi log enable|disable"
//
// NOTE: This handler MUST be called from cmd_wifi_if() is it is done right now, because only cmd_wifi_if() ensures 
//       proper WiFi stack initialization
// NOTE: This handler MUST NOT be used as is in "keywords.h"!

static int cmd_wifi_log(int argc, char **argv) {

  if (argc < 3) {
    const char *t0 = Wifi.log ? "en" : "dis";
    const char *t1 = Wifi.log ? "dis" : "en";

    q_printf("%% WiFi verbose log is <i>%sabled</>.\r\n"
             "%% Use \"wifi log %sable\" to %sable it\r\n",t0 , t1, t1);
    return 0;
  }
  Wifi.log = !q_strcmp(argv[2],"enable");
  return 0;
}


// "wifi ap|sta"
//
static int cmd_wifi_if(int argc, char **argv) {

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[1],"log"))
    return cmd_wifi_log(argc, argv);

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
//    if (!q_strcmp(argv[1],"log"))
//      return cmd_wifi_log(argc, argv);
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
    HELP(q_print("% Failed to start WiFi; the WiFi interface may be non-functional\r\n"));
    return CMD_FAILED;
  }

  return 0;
}



// Kick STA from the AP interface.
//
// TODO: they will reconnect immediately. Must have some sort of auto-removable decay timer to stop them from reconnecting:
// TODO: each connect attempt resets the decay timer (which is 1 minute) and when timer is >0 new connection attempts are blocked.
// TODO: To block we must use STA's MAC because AID may be different every time
//
// esp32-ap>kick AID [auto]
//
static int cmd_wifi_kick(int argc, char **argv) {

  uint16_t aid;
  esp_err_t err = ESP_OK + 1;

  if (argc < 2)
    return CMD_MISSING_ARG;

  //"kick all"
  if (!q_strcmp(argv[1],"all"))
    aid = 0;
  else { // kick NUMBER [auto]
    if (0 == (aid = q_atol(argv[1],0)))
      q_print("% A valid AID is required (see \"show wifi clients\" output)\r\n");
    else {
      // if ((argc > 2) && !q_strcmp(argv[2],"auto")) {
      //  char mac[6];
      //  if (mac_of_aid(aid, mac))
      //    if (block_list_add(mac, DEF_WIFI_BLOCKTIME))
      //      q_printf("%% Client " MACSTR " has been added to a block list\r\n", MAC2STR(mac))
      // }
      err = esp_wifi_deauth_sta(aid);
    }
  }

  if (err != ESP_OK) {
    q_printf("%% Failed to kick STA AID %u\r\n",aid);
    return CMD_FAILED;
  }

  return 0;
}

// "show wifi clients"
//
//
static int cmd_show_wifi_clients(UNUSED int argc, UNUSED char **argv) {


    esp_netif_t *apif = get_apif();
    wifi_sta_list_t sta_list;
    esp_err_t err;

    if (!apif) {
list_is_empty:
      q_print("% No connections. STA list is empty\r\n");
      return 0;
    }

    if ((err = esp_wifi_ap_get_sta_list(&sta_list)) != ESP_OK)
      goto list_is_empty;
    

    if (!sta_list.num)
      goto list_is_empty;

    if (sta_list.num > ESP_WIFI_MAX_CONN_NUM)
      sta_list.num = ESP_WIFI_MAX_CONN_NUM;

    
    wifi_sta_mac_ip_list_t pairs = { 0 };
    esp_wifi_ap_get_sta_list_with_ip(&sta_list, &pairs);
    
    q_print("%<r> # |  MAC address   | RSSI |  AID  | Leased IP Address</>\r\n"
               "% --+----------------+------+-------+------------------\r\n");

    for (int i = 0; i < sta_list.num; i++) {

        uint16_t aid;
        wifi_sta_info_t *info = &sta_list.sta[i];

        // fetch STA AID by its MAC address
        if (ESP_OK != esp_wifi_ap_get_sta_aid(info->mac, &aid))
          aid = 0; // invalid AID (AID numbers start from 1)

        q_printf("%%%3d| " MACSTR " | %4d | %5u | " IPSTR "\r\n",
                 i+1,
                 MAC2STR(info->mac),
                 info->rssi,
                 aid,
                 IP2STR(&pairs.sta[i].ip));
    }
    q_print("% --+----------------+------+-------+------------------\r\n");
    q_printf("%% Connected: %d station%s. Use \"kick AID\" or \"kick all\" to deauth\r\n", PPA(sta_list.num));

    return 0;
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
  int8_t               power;              // PHY tx power, mw
  bool                 link_up = false,    // Link layer is UP?
                       proto_up = false;   // Protocol layer is UP?
  const char          *hostn = NULL;       // Interface host name


  if (argc < 3)
    return CMD_MISSING_ARG;

  // Prepare WiFi stack and network interfaces if not done yet: we need AP and STA netifs to be created
  // in order to get any information on WiFi
  if (!Wifi.prepared)
    prepare_wifi_stack();

  if (!q_strcmp(argv[2],"clients"))
    return cmd_show_wifi_clients(argc, argv);

  // Set /ni/ to be esp_netif_t pointer to the interface we are "show"ing
  // /ifx/ is set to WIFI_IF_AP or WIFI_IF_STA and corresponding wifi config is then read
  //
  if (!q_strcmp(argv[2],"ap")) {
    ni = get_apif();
    ifx = WIFI_IF_AP;
  } else if (!q_strcmp(argv[2],"sta")) {
    ni = get_staif();
    ifx = WIFI_IF_STA;
  } else {
    HELP(q_print("% <e>\"sta\", \"ap\" or \"clients\" keywords expected</>\r\n"));
    return CMD_FAILED;
  }

  if (!ni) {
    q_print("% Something went wrong: No network interfaces.\r\n");
    return CMD_FAILED;
  }

  esp_wifi_get_config(ifx, &conf); // if this API fails we'll display a null config

  // Link == netif status
  link_up = esp_netif_is_netif_up(ni);
  // Protocol: AP->always UP, STA->only when associated
  proto_up = (ifx == WIFI_IF_STA) ? (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) : true;

  q_printf("%%<r> Network interface WIFI %s is %s                      </>\r\n",
             ap_sta_str(ifx),
             up_down_str(proto_up && link_up));

  q_printf("%% Link: <i>%s</>, Protocol: <i>%s</>\r\n",
            up_down_str(link_up),
            up_down_str(proto_up));

  // PHY output power is measured in units of 0.25mW and ranges from 4 to 84
  if (ESP_OK == esp_wifi_get_max_tx_power(&power))
    q_printf("%% PHY TX power: %u mW\r\n", power / 4);

  if (esp_wifi_get_mac(ifx, mac) == ESP_OK)
    q_printf("%% MAC address: <i>" MACSTR "</>\r\n", MAC2STR(mac));
  
  q_print("%\r\n% <r>IP information and services                        </>\r\n");
  if (ESP_OK == esp_netif_get_ip_info(ni, &ipi)) {
    
    if (ipi.ip.addr)
      q_printf("%% IP address: <i>" IPSTR "/%u</>, gateway: " IPSTR "\r\n",
                IP2STR(&ipi.ip),
                __builtin_popcount(ipi.netmask.addr),
                IP2STR(&ipi.gw));
    else
      q_print("% IP address: none\r\n");
  }

  // DNS and NTP information
  esp_netif_get_dns_info(ni, ESP_NETIF_DNS_MAIN, &dnsi);
  if (dnsi.ip.u_addr.ip4.addr != 0)
    q_printf("%% Main DNS  : <i>" IPSTR "</>\r\n", IP2STR(&dnsi.ip.u_addr.ip4));
  else
    q_print("% DNS server: none\r\n");
  show_sntp_servers();
  

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

  q_printf("%% DHCP %s: %s\r\n",
          (ifx == WIFI_IF_AP ? "server" : "client"),
          (status == ESP_NETIF_DHCP_STARTED ? "<i>started</>" : "<w>inactive</>"));

    if (esp_netif_get_hostname(ni, &hostn) == ESP_OK)
    q_printf("%% Host name (per-interface): \"<i>%s</>\"\r\n",hostn);

  q_print("%\r\n% <r>WiFi Network:                                      </>\r\n");
  if (ifx == WIFI_IF_STA) {
    if (proto_up) {
      q_printf("%% Connected to \"<b>%s</>\", (" MACSTR ")\r\n",
                ap_info.ssid[0] ? (const char *)&ap_info.ssid[0] : "a <i>hidden network",
                MAC2STR(ap_info.bssid));

        display_ap_details(&ap_info, NULL);
    } else {
      q_print("% Not connected to any Access Point\r\n");
      
      q_printf("%%\r\n%% Configured: SSID: \"<i>%s</>\" ",conf.sta.ssid);
      if (conf.sta.bssid_set)
        q_printf(", BSSID: " MACSTR "\r\n", MAC2STR(conf.sta.bssid));
      else
        q_print(CRLF);

      q_printf("%% Scan method: %s, channel: <i>%u</>\r\n",
                conf.sta.scan_method == WIFI_FAST_SCAN ? "Fast scan" : "All channels",
                conf.sta.channel);
    }
  } else if (ifx == WIFI_IF_AP) {

    q_printf("%% Network: \"%s\"%s, WIFI channel %u, max-conn: %u\r\n",
            conf.ap.ssid, 
            conf.ap.ssid_hidden ? " (hidden)" : "", 
            conf.ap.channel,
            conf.ap.max_connection);

    q_printf("%% Authentication: [%s] , pairwise cipher: [%s]\r\n",
            Wifi_auth[conf.ap.authmode]+2,
            Wifi_cipher[conf.ap.pairwise_cipher]);

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
    q_printf("%% New MAC address (%s, %s) set\r\n", ap_sta_str(wif), argv[1]);
  else {
    q_printf("%% Can not set the new mac address (error %d)\r\n",err);
    if (mac[0] & 1)
      q_print("% Bit 0 of the first byte in MAC address must be 0 (zero)\r\n");
    else if (wif == WIFI_IF_AP)
    // Most of the time we are in STA mode. We only switch to APSTA when AP is configured and 
    // \"up\". Changing AP MAC is not possible if in STA mode and the only way to achieve this - is
    // to bring AP interface UP.
    //
    // We possibly could switch to APSTA, change MAC and then switch back to STA
    // but this requires extra code which has no any function. Instead just hint the user
    // to "up" the AP interface
      q_print("% WIFI AP interface must be UP to allow MAC address changing\r\n");

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
        q_printf("Host name (%s side): \"%s\"\r\n",ap_sta_str(wif), name);
        return 0;
      }
    }
    q_print("Can not obtain system host name\r\n");
    return CMD_FAILED;
  }

  if (esp_netif_set_hostname(ni,argv[1]) != ESP_OK) {

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
  bool active = true, detail = false;
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
            if (!memcmp(bssid, ap->bssid, sizeof(bssid))) {
              q_printf("%%\r\n%%<r>Access point \"%s\" (BSSID: " MACSTR ")</>\r\n%%\r\n",
                          ap->ssid[0] ? (char *)ap->ssid : "[Hidden name]",
                          MAC2STR(ap->bssid));
              display_ap_details(ap,argv[bidx]);
              // TODO: return here?
            }
          } else {
            // For the "table view" we print out lines (1 line for every AP) with brief information
            // on every found network
            q_printf("%% %-2d|%2d| %-32.32s| " MACSTR " | %3d  |",
                      i + 1,
                      ap->primary,
                      ap->ssid[0] ? (char *)ap->ssid : "hidden",
                      MAC2STR(ap->bssid),
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

#if 0
  //TODO: for testing only (memory requirements estimation). Must be removed.
    if (deinit)
      stop_wifi_stack();
#endif    

  return 0;
}


// ip address dhcp|A.B.C.D/M [gw A.B.C.D|dns A.B.C.D [A.B.C.D]]*
//
// This same function is used as a handler for both STA and AP interface.
// Sets dynamic (DHCP) static IP address, dynamic or static DNS server addresses and the default gateway
// address
//
static int cmd_wifi_ip_address(int argc, char **argv) {

  uint32_t ip = 0,              // ip address value, host order. for DHCP the value of /ip/ is set to 0
           mask = 0xffffff00,   // subnet mask. optional
           gw = 0,              // gateway, optional. If omitted then /gw/ is set to interface IP address
           dns1 = 0,            // Main DNS server address
           dns2 = 0;            // Backup DNS server address


  // all ip/masks are in host byte-order
  esp_netif_ip_info_t ipi = { 0 };
  esp_err_t ret;
  
  // Fetch current interface index (/ifx/) and a pointer to esp_netif_t (/ni/)
  THIS_INTERFACE(ifx);

  if (argc < 3)
    return CMD_MISSING_ARG;

  
  if (!q_strcmp(argv[2],"dhcp")) {
    if (ifx == WIFI_IF_AP) {
      HELP(q_print("% <e>an AP must have a static IP address (e.g. default 192.168.4.1/24)</>\r\n"));
      return CMD_FAILED;
    }
  } else {
    
    // Read IP address and mask. If mask is not provided (i.e. "1.2.3.4" instead of "1.2.3.4/24")
    // then we assume mask to be 255.255.255.0. 
    //
    if ((ip = q_atoip(argv[2], &mask)) == 0) {
      q_print("% <e>Address/mask combination can not be set. (try \"192.168.5.1/24\")</>\r\n");
print_note_and_return:
      HELP(q_print("% Interface address was not changed\r\n"));
      return CMD_FAILED;
    }

    // Check if the netmask is ok:
    // Absence of the mask or invalid masks like /0 or /32 are treated
    // as if there was no subnet mask provided.
    //
    if (mask == 0xffffffffUL || mask == 0) {
      mask = 0xffffff00;
      HELP(q_print("% Subnet mask is not provided, assuming \"/24\" (Class C network)\r\n"));
    }

    // Check if the new IP address is a valid unicast address:
    // 1. /ip/ must NOT be zero (i.e. host portion of the address must not be zero)
    // 2. /ip/ must not be broadcast (i.e. host portion of the address must not be all-one)
    //
    if (/*1.*/ ((ip & ~mask) == 0) || /*2. */(((ip & (~mask)) | mask) == 0xffffffffUL)) {
      q_print("% <e>Interface address must be a valid unicast address</>\r\n");
      goto print_note_and_return;
    }


    // Check if IP address conflicts with other existing addresses: IP address of any interface
    // must be on a separate subnet. Read IP addresses from all interfaces and check if this new
    // does not belong to any of existing subnets. If only one of WiFi interfaces created - then
    // we skip this check entirely.
    //
    // TODO: currently we only check WiFi interfaces (there may be ethernet as well).
    //
    esp_netif_t *otherif = ((ifx == WIFI_IF_AP) ? get_staif() : get_apif());
    if (otherif) {

      esp_netif_ip_info_t ipi = { 0 };

      uint32_t ip_other, mask_other;
      esp_netif_get_ip_info(otherif, &ipi);

      ip_other = q_ntohl(ipi.ip.addr);
      mask_other = q_ntohl(ipi.netmask.addr);

      // Take wider mask
      if (~mask_other < ~mask)
        mask_other = mask;

      if ((ip_other & mask_other) == (ip & mask_other)) {
        q_printf("%% <e>New IP address belongs to %s interface subnet</>\r\n", ifx == WIFI_IF_AP ? "STA" : "AP");
        goto print_note_and_return;
      }
      // Ok all checks were passed. Fallthrugh
    }
  } // if (argv[1] != "dhcp")

  //Read the gateway and dns addresses (if supplied). Start from argv[3] till the end
  // (argv[0]==ip argv[1]==address argv[2]==A.B.C.D/M argv[3]== ... )
  //
  // It is possible to set static DNS and/or a gateway while obtain other settings from a DHCP server.
  // So we always expect to have "gw" and "dns" keywords even if ip address is set to dhcp: "ip address dhcp dns 8.8.8.8"
  //
  for (int i = 3; i < argc; i++) {
    if (!q_strcmp(argv[i],"gw")) {
      i++;
      if (i < argc) {
        if ((gw = q_atoip(argv[i], NULL)) == 0) {
          q_print("% Invalid default gateway address\r\n");
          goto print_note_and_return;
        }
      }
    } else if (!q_strcmp(argv[i],"dns")) {
      i++;
      if (i < argc) {
        uint32_t *p = dns1 ? &dns2 : &dns1;
        if ((*p = q_atoip(argv[i], NULL)) == 0) {
          q_print("% Invalid DNS address\r\n");
          goto print_note_and_return;
        }
      }
    } else
    // unrecognized keyword at position /i/
      return i;
  } // for every "dns" and "gw" keywords


  // Arguments are read. Process them
  //

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
      // Wifi.ap_static_dns is set to /true/ when AP has static DNS servers configured
      if (ifx == WIFI_IF_AP) {
        Wifi.ap_static_dns = true;
        dhcp_server_set_dns_option();
      } else {
        Wifi.sta_dns1 = dns1;
        Wifi.sta_dns2 = dns2;
        Wifi.sta_static_dns = true;
        q_print("% Static DNS servers override DHCP offers\r\n");
      }

    } else {
      // No DNS1 (means there are no DNS2 as well) - reset static_dns flag for the AP or STA
      if (ifx == WIFI_IF_AP)
        Wifi.ap_static_dns = false;
      else
        Wifi.sta_static_dns = false;
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
    // TODO: 
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

// -- NTP support (Time sync) --
//
// NTP is available on both interfaces (AP and STA both have command "ntp") however it does not mean that
// we can have two different NTP configurations: SNTP service is global, not a "per-interface" service. It was 
// made available from both command subdirs (i.e. /keywords_ap/ and /keywords_sta/) for convinience.
//
// NTP is configured via single, multiple-argument command "ntp": enable or disable NTP client, set static or/and 
// dynamic (obtained from a DHCP server) NTP servers
//


// "ntp (SERVER_NAME|IP4_ADDRESS|dhcp|enable|disable)*"
// 
static int cmd_wifi_ntp(int argc, char **argv) {
  
  // No args - show current NTP status
  if (argc < 2) {
    q_printf("%% NTP is %sabled</>\r\n", Wifi.sntp_enabled ? "<g>en" : "<i>dis");
    return 0;
  }

  uint32_t ip;
  int ci = 1, nsi = 0;
  ip_addr_t ip_zero = { 0 };

  while (ci < argc) {

    if (!q_strcmp(argv[ci],"enable")) {
      if (!Wifi.sntp_enabled) {
        sntp_cfg.start = true;
        esp_netif_sntp_init(&sntp_cfg);
        Wifi.sntp_enabled = true;
        VERBOSE(q_print("% SNTP client enabled\r\n"));
      }
    } else if (!q_strcmp(argv[ci],"disable")) {
      if (Wifi.sntp_enabled) {
        esp_netif_sntp_deinit();
        sntp_cfg.start = false;
        Wifi.sntp_enabled = false;
        VERBOSE(q_print("% SNTP client disabled\r\n"));
      }
    } else if (!q_strcmp(argv[ci],"dhcp")) {
      if (sntp_cfg.server_from_dhcp != true) {
        sntp_cfg.server_from_dhcp = true;
        VERBOSE(q_print("% SNTP get servers from a DHCP reply\r\n"));
        if (Wifi.sntp_enabled) {
          VERBOSE(q_print("% Restarting SNTP service..\r\n"));
          esp_netif_sntp_deinit();
          esp_netif_sntp_init(&sntp_cfg);
        }
      }
    } else if ((ip = q_atoip(argv[ci], NULL)) != 0) {
        if (nsi < SNTP_MAX_SERVERS) {
          sntp_cfg.server_from_dhcp = false;
          ip_addr_t ip_addr = { 0 };
          ip_addr.u_addr.ip4.addr = q_htonl(ip);
          esp_sntp_setserver(nsi, &ip_addr);

          if (ntp_servers[nsi])
            q_free(ntp_servers[nsi]);
          ntp_servers[nsi] = NULL;

          esp_sntp_setservername(nsi, NULL);
          VERBOSE(q_printf("%% Static SNTP server set: " IPSTR "\r\n", IP2STR(&ip_addr.u_addr.ip4)));
          nsi++;
        }
    } else if (q_findchar(argv[ci],'.')) {
        sntp_cfg.server_from_dhcp = false;
        if (nsi < SNTP_MAX_SERVERS) {
          esp_sntp_setserver(nsi, &ip_zero);

          // never freed, only reallocated
          if (ntp_servers[nsi])
            q_free(ntp_servers[nsi]);
          ntp_servers[nsi] = q_strdup(argv[ci], MEM_SERVER);

          esp_sntp_setservername(nsi, ntp_servers[nsi]); // NULL is ok
          VERBOSE(q_printf("%% Static SNTP server name set: %s\r\n", argv[ci]));
          nsi++;
        }
    } else {
      HELP(q_print("% Server name (with a dot!), IP address, enable, disable or dhcp expected\r\n"));
      return ci;
    }
    ci++;
  }

  

  return 0;
}

// "dhcp START_IP_ADDRESS [MAX_CLIENTS [TIMESPEC]]"
// "dhcp enable|disable"
//
static int cmd_wifi_dhcp(int argc, char **argv) {

  THIS_INTERFACE(ifx);
  MUST_NOT_HAPPEN((ifx != WIFI_IF_AP)); 

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[1],"enable")) {

    HELP(q_print("% Enabling DHCP server..\r\n"));
    esp_netif_dhcps_start(ni);

  } else if (!q_strcmp(argv[1],"disable")) {

    HELP(q_print("% Disabling DHCP server..\r\n"));
    esp_netif_dhcps_stop(ni);

  } else {

    uint32_t ip, mask;
    uint32_t lease       = 24*3600,     // use default lease interval
             max_clients = 252;   //.0, .1, .254 and .255 are reserved

    if ((ip = q_atoip(argv[1],&mask)) == 0)
      return 1;

    if (argc > 2)
      max_clients = q_atol(argv[2], max_clients);

    bool was_started = dhcp_server_stop_if_started(ni);

    if (dhcp_server_set_ip_pool(ni, ip, max_clients)) {
      HELP(q_print("% New DHCP server IP pool address range set\r\n"));
      if (argc > 3) {
        lease = q_atol(argv[3], lease);
        if (!dhcp_server_set_lease(ni, lease))
          q_print("% <e>Failed to set IP lease time</>\r\n");
      }
    } else
      q_print("% <e>Failed to set IP pool address range</>\r\n");

    dhcp_server_restart_if_was_started(ni, was_started);
  }
  return 0;
}

// up SSID [PASSWORD]                                  <-- sta
// up BSSID [PASSWORD]                                 <-- sta
// up
// up SSID PASSWORD [max-conn NUM | channel NUM ]*      <-- ap
// up SSID                                             <-- ap
//
static int cmd_wifi_up(int argc, char **argv) {

  uint8_t bssid[6];
  bool use_saved = false;     
  wifi_ap_record_t ap_info;

  THIS_INTERFACE(ifx);

  // Check if interface is UP and RUNNING. If it is - do nothing
  //
  if (esp_netif_is_netif_up(ni) || (ifx == WIFI_IF_STA && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)) {
    HELP(q_print("% Interface is UP. (Use \"down\" to shutdown)\r\n"));
    return 0;
  }

  // Command "up" for the STA interface: read SSID/BSSID, PASSWORD and the keyword "no-retry"
  // or use saved values from the flash
  if (ifx == WIFI_IF_STA) {

    // Current config.
    wifi_config_t stac = { 0 };

    // "up" without arguments: use saved config, if available. System may read data from NVS or
    // sketch can configure the wifi; command "up" with no arguments will use current system settings
    //
    while (argc < 2) {
      
      if (esp_wifi_get_config(WIFI_IF_STA, &stac) == ESP_OK) {
        if (stac.sta.bssid_set || stac.sta.ssid[0]) {
          use_saved = true;
          break;
        }
      }

print_error_notice_and_exit:

      q_print("% No valid WiFi configuration found, use \"<i>up SSID [PASSWORD]</>\"\r\n"
              "% Use \"<i>wifi storage flash</>\" to auto-save WiFi configuration\r\n");
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
        HELP(q_print("% Connecting to a network using BSSID\r\n"));
      } else {
        strlcpy((char *)stac.sta.ssid,argv[1],sizeof(stac.sta.ssid));
        HELP(q_print("% Connecting to a network using SSID\r\n"));
      }

      // Password supplied? "auto-connect"?
    
      if (argc > 2) {
        strlcpy((char *)stac.sta.password,argv[2],sizeof(stac.sta.password));
        if (argc > 3)
          if (!q_strcmp(argv[3],"auto-connect"))
            Wifi.sta_reconnect = true;
          HELP(q_print("% Auto-reconnect is enabled\r\n"));
      }
    }

    if (ESP_OK == esp_wifi_set_config(WIFI_IF_STA, &stac))
      esp_wifi_connect();
    else
      q_print("% Can not apply the new WiFi configuration\r\n");

  } else {
    // Command "up" for the AP
    //
    wifi_config_t apc = { 0 };

    // no args. Try to fetch system config and use it
    while (argc < 2) {
      if (esp_wifi_get_config(WIFI_IF_AP, &apc) == ESP_OK) {
        if (apc.ap.ssid[0] || apc.ap.ssid_hidden) {
          use_saved = true;
          break;
        }
      }
      goto print_error_notice_and_exit;
    }

    // Read configuration parameters from the command line
    if (!use_saved) {

      
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
        HELP(if (Wifi.log) q_print("% AP authentication mode set to OPEN (no password supplied)\r\n"));
        apc.ap.authmode = WIFI_AUTH_OPEN;
      } else {
        HELP(if (Wifi.log) q_print("% AP authentication mode set to WPA2-PSK\r\n"));
        apc.ap.authmode = WIFI_AUTH_WPA2_PSK;
      }

      apc.ap.max_connection = MAX_CONN;
      // TODO: add AUTH keyword
      // read all the rest 
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
    }

    // Set mode to AP+STA and configure AP.
    // TODO: should we configure and then set mode?
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &apc);
  }

  return 0;
}

// "down"
// Shutdown AP or STA interface, disable all ongoing connection attempts
static int cmd_wifi_down(int argc, char **argv) {

  THIS_INTERFACE(ifx);

  // Check if interface is UP and RUNNING. If it is down - do nothing, except disabling "auto-reconnect"
  // because it may be connection attempts going in background
  //
  wifi_ap_record_t ap_info;

  // Stop all active connection attempts
  if (ifx == WIFI_IF_STA)
    if (Wifi.sta_reconnect) {
      Wifi.sta_reconnect = false;
      HELP(q_print("% Disabling auto-reconnect\r\n"));
    }

  // If interface is up - shutdown it
  if (esp_netif_is_netif_up(ni) || (ifx == WIFI_IF_STA && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)) {
    if (ifx == WIFI_IF_STA)
      esp_wifi_disconnect();
    else if (ifx == WIFI_IF_AP)
      esp_wifi_set_mode(WIFI_MODE_STA);
  } else {
    HELP(q_print("% Interface is down\r\n"));
  }

  return 0;
}
#endif // if compiling espshell

