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
#include "dhcpserver/dhcpserver.h"
#include "dhcpserver/dhcpserver_options.h"

#if COMPILING_ESPSHELL

#define is_sta_here() (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") != NULL)
#define is_ap_here() (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") != NULL)

#if 0
// Get esp_netif_t by its name:
// Wifi STA interface : "sta", "Sta", "station", "sta0" etc.
// Wifi AP interface : "ap", "AP", "AccessPoint", "ap0" etc.
//
static esp_netif_t *netif_by_name(const char *name) {
  if (likely(name))
    switch(*name) {
      //eth, eth0, ethernet
      case 'E':
      case 'e': return esp_netif_get_handle_from_ifkey("ETH_DEF");
      //ap, ap0, accesspoint
      case 'A':
      case 'a': return esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
      //sta, sta0, station
      case 'S':
      case 's': return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
      //all others
      default: /* FALLTHROUGH */
    };
  return NULL;
}
#endif

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


#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1

//static bool Ap_up = false;
//static bool Sta_up = false;
static bool Wifi_started = false;

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

// IP Events handler instance
//
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (likely(event_base == IP_EVENT))
    switch(event_id) {
      case IP_EVENT_STA_GOT_IP:
        // ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        // start_sntp();
        return ;
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
        if (is_sta_here()) {
          VERBOSE(q_print("% STA started, connecting...\r\n"));
        } else {
          VERBOSE(q_print("% STA started, but not interface created yet. Delayed.\r\n"));
        }
        //esp_wifi_connect();
        return;

      case WIFI_EVENT_STA_CONNECTED:
        VERBOSE(q_print("% STA connected, starting DHCP client...\r\n"));
        //start_sta_dhcp();
        return ;

      case WIFI_EVENT_STA_DISCONNECTED:
        VERBOSE(q_print("% STA connected, stopping DHCP/SNTP client...\r\n"));
        //stop_sntp();
        //stop_sta_dhcp();
        //esp_wifi_connect(); // optional auto-reconnect
        return ;
    
    // AP events
      case WIFI_EVENT_AP_START:
        if (is_ap_here()) {
          VERBOSE(q_print("% AP started, starting DHCP server...\r\n"));
        } else {
          VERBOSE(q_print("% AP started, but no AP interface created yet. Delayed.\r\n"));
        }
        //start_ap_dhcp();
        return ;

      case WIFI_EVENT_AP_STOP:
        VERBOSE(q_print("% AP stopped, stopping DHCP server...\r\n"));
        //stop_ap_dhcp();
        return ;

      default:
        // FALLTHRU
    };
    VERBOSE(q_printf("%% WIFI-EVENT: arg=%p, base=%x, id=%x, edata=%p\r\n",arg,(unsigned int)event_base,(unsigned int)event_id,event_data));
}

// Initialize and start the WiFi stack, perform minimal initial config, create netifs.
// Can be called multiple times
//
static bool start_wifi_stack() {

  if (!Wifi_started) {

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    // NVS must be initialized for WiFi driver to be happy;
    // it is done in _nv_storage_init() constructor
    esp_event_loop_create_default();

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,  &ip_event_handler, NULL, NULL);

    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_channel(11, 0);
#if SOC_WIFI_SUPPORT_5G
    esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
#endif
    if (esp_wifi_start() == ESP_OK) {
      Wifi_started = true;
      VERBOSE(q_print("% WIFI initialized, driver loaded\r\n"));
    } else {
      VERBOSE(q_print("% WIFI failed to initialize\r\n"));
    }
  }
  // TODO: check other errors
  return Wifi_started;
}

static void stop_wifi_stack() {
  if (Wifi_started) {
    esp_wifi_stop();
    esp_wifi_deinit();
    Wifi_started = false;
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler);

    VERBOSE(q_print("% WIFI deinit"));
  }
}


// TODO: static_assert(WIFI_CIPHER_UNKNOWN == 12, "Code review is required");
// "wifi ap|sta"
static int cmd_wifi_if(int argc, char **argv) {

  if (argc < 2)
    return CMD_MISSING_ARG;

  start_wifi_stack();

  // Set appropriate keywords (keywords_ap or keywords_sta), store interface type in /Context/
  // Create corresponding esp_netif_t
  if (argv[1][0] == 's')  {
    
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"))
      if (!esp_netif_create_default_wifi_sta()) {
        q_print("% Can not create default STA network interface\r\n");
        return CMD_FAILED;
      }
    change_command_directory(WIFI_IF_STA, KEYWORDS(sta), PROMPT_WIFISTA, "WiFi STAtion");

  } else if (argv[1][0] == 'a') { 

    if (!esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"))
      if (!esp_netif_create_default_wifi_ap()) {
        q_print("% Can not create default AP network interface\r\n");
        return CMD_FAILED;
      }
    change_command_directory(WIFI_IF_AP, KEYWORDS(ap), PROMPT_WIFIAP, "WiFi Access Point");

  } else {
    HELP(q_print("% Two options: \"wifi sta\" or \"wifi ap\"\r\n"));
    return CMD_FAILED;
  }

  return 0;
}


static void display_ap_details(wifi_ap_record_t *ap, const char *requested_bssid) {

  const char *bw_desc[] = { "unknown", "20MHz", "40MHz", "80MHz", "160MHz", "80+80MHz", "unknown", "unknown" };

  if (likely(ap)) {

    // SSID, BSSID and Security
    q_printf("%%\r\n%% Access point \"<i>%s</>\" (BSSID: %s)\r\n%%\r\n", ap->ssid[0] ? (char *)ap->ssid : "[Hidden name]", requested_bssid);
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

// "show wifi ap|sta"
//
//
static int cmd_show_wifi(int argc, char **argv) {
  return 0;
}


// "mac AABB:CCDD:EEFF"
// Depending on the /Context/ sets either AP or STA mac address
//
static int cmd_wifi_mac(int argc, char **argv) {
  uint8_t mac[6];
  THIS_INTERFACE(wif);

  if (argc < 2)
    return CMD_MISSING_ARG;
  
  if (!q_atomac(argv[1], mac)) {
    q_print("% MAC address AABB:CCDD:EEFF (or AA:BB:CC:DD:EE:FF) expected\r\n");
    return CMD_FAILED;
  }
  if (esp_wifi_set_mac(wif, mac) == ESP_OK)
    q_printf("%% New MAC address (%s, %s) set\r\n", wif == WIFI_IF_STA ? "STA" : "AP", argv[1]);
  else {
    q_print("% Can not set the new mac address\r\n");
    if (mac[0] & 1)
      q_print("% Bit 0 of the first byte in MAC address must be 0 (zero)\r\n");
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

// ip address dhcp|A.B.C.D/M [renew|gw A.B.C.D|dns A.B.C.D [A.B.C.D]]*
//
static int cmd_wifi_ip_address(int argc, char **argv) {

  uint32_t ip = 0, mask = 0xffffff00, gw = 0, dns1 = 0, dns2 = 0;
  THIS_INTERFACE(ifx);

  if (ni)

  if (argc < 3)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[2],"dhcp")) {
    if (ifx == WIFI_IF_AP) {
      q_print("% AP must have a static IP address (e.g. default 192.168.4.1/24)\r\n");
      return CMD_FAILED;
    }

  } else {
    

    ip = q_atoip(argv[2], &mask);
    if (ip == 0) {
      q_print("% Invalid address/mask. (a valid example: \"192.168.4.1/24\")\r\n");
      return CMD_FAILED;
    }
    VERBOSE(q_print("% Static IP address and subnet mask requested\r\n"));
  }

  return 0;
}

// ip natp enable|disable
// ip natp INTERNAL_IP INTERNAL_PORT EXTERNAL_PORT
// ip natp 192.168.4.55 80 8080
//
static int cmd_wifi_natp(int argc, char **argv) {
  return 0;
}
// "ntp server ADDRESS|dhcp [ADDRESS]"
// "ntp disable|enable"
// 
static int cmd_wifi_ntp(int argc, char **argv) {
  return 0;
}

// "dhcp 192.168.4.1 [MAX_CLIENTS [LEASE_TIME]]"
// "dhcp enable|disable"
//
static int cmd_wifi_dhcp(int argc, char **argv) {

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[1],"enable")) {
    VERBOSE(q_print("% Enabling DHCP server..\r\n"));
  } else if (!q_strcmp(argv[1],"disable")) {
    VERBOSE(q_print("% Disabling DHCP server..\r\n"));
  } else {
    uint32_t ip, mask;
    uint32_t lease       = 36000, // 10 hours lease interval
             max_clients = 252;   //.0, .1, .254 and .255 are reserved

    if ((ip = q_atoip(argv[1],&mask)) == 0) {
      q_print("% Keywords \"enable\", \"enable\" or a valid IP address expected\r\n");
      return CMD_FAILED;
    }

    if (argc > 2)
      max_clients = q_atol(argv[2], max_clients);

    if (argc > 3)
      lease = q_atol(argv[3], lease);

    // TODO: set the new DHCPS configuration
    VERBOSE(q_print("% Configuring DHCP server..\r\n"));
  }
  return 0;
}

#endif // if compiling espshell

