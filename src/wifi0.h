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


#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1

//static bool Ap_up = false;
//static bool Sta_up = false;
static bool Wifi_started = false;

static const char *Wifi_auth[] = {
    "- <g>NONE</>",  "* <i>WEP</>", "* WPA",  "* WPA2",
    "* WPA1/2",  "$ WPA2",  "* WPA3",       "* WPA2/3",
    "* WAPI",    "  <g>OWE</>", "$ WPA3_192", "* WPA3 Ext",
    "+ WPA3 Ext","  DPP",       "$ WPA3",     "$ WPA2/3",
    "$ WPA",
};

static const char *Wifi_cipher[] = {
    "NONE",    "WEP40",     "WEP104",      "TKIP",
    "CCMP",    "TKIP_CCMP", "AES_CMAC128", "SMS4",
    "GCMP",    "GCMP256",   "AES_GMAC128", "AES_GMAC256"
};
// TODO: static_assert(WIFI_CIPHER_UNKNOWN == 12, "Code review is required");
// "wifi ap|sta"
static int cmd_wifi_if(int argc, char **argv) {

  if (argc < 2)
    return CMD_MISSING_ARG;

  // Set appropriate keywords (keywords_ap or keywords_sta), store interface type in /Context/
  if (argv[1][0] == 's') 
    change_command_directory(WIFI_IF_STA, KEYWORDS(sta), PROMPT_WIFISTA, "WiFi STAtion");
  else
    change_command_directory(WIFI_IF_AP, KEYWORDS(ap), PROMPT_WIFIAP, "WiFi Access Point");
  return 0;
}

static bool start_wifi_stack() {

  if (!Wifi_started) {

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    // NVS must be initialized for WiFi driver to be happy;
    // it is done in _nv_storage_init() constructor
    esp_event_loop_create_default();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_start();
    Wifi_started = true;
  }
  // TODO: check errors
  return true;
}

static void stop_wifi_stack() {
  if (Wifi_started) {
    esp_wifi_stop();
    esp_wifi_deinit();
    Wifi_started = false;
  }
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

// "mac AABB:CCDD:EEFF"
// Depending on the /Context/ sets either AP or STA mac address
//
static int cmd_wifi_mac(int argc, char **argv) {
  uint8_t mac[6];
  // Context is set to WIFI interface number: WIFI_IF_STA (i.e. 0) or WIFI_IF_AP (i.e. 1)
  wifi_interface_t wif = (wifi_interface_t )context_get_uint();

  if (argc < 2)
    return CMD_MISSING_ARG;
  
  if (!q_atomac(argv[1], mac)) {
    q_print("% MAC address AABB:CCDD:EEFF (or AA:BB:CC:DD:EE:FF) expected\r\n");
    return CMD_FAILED;
  }
  if (esp_wifi_set_mac(wif, mac) == ESP_OK)
    q_printf("%% New MAC address (%s, %s) set\r\n", wif == WIFI_IF_STA ? "STA" : "AP", argv[1]);
  else
    q_print("% Can not change current mac address\r\n");
  return 0;
}

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

#endif // if compiling espshell

