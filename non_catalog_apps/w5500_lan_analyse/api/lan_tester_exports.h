#pragma once

/*
 * C-linkage declarations of the app's private functions exposed to plugins,
 * for the C++ API-table translation unit. Kept separate from the real headers
 * so the table does not pull in mlib's C++ templates, and separate from the
 * plugin-side includes so there are no redundant declarations. The ioLibrary
 * subset lives in lan_tester_ioshim.h (shared with plugins).
 */

#include <stdint.h>
#include <stdbool.h>
#include "lan_tester_ioshim.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LanTesterApp LanTesterApp;
typedef struct TextBox TextBox;
typedef struct FuriString FuriString;

/* app helpers (lan_tester_app.c) */
bool lan_tester_ensure_w5500(LanTesterApp* app);
bool lan_tester_check_w5500(LanTesterApp* app);
void lan_tester_update_view(TextBox* tb, FuriString* text);
void lan_tester_count_frame(LanTesterApp* app, const uint8_t* frame, uint16_t len);
void lan_tester_save_and_notify(LanTesterApp* app, const char* type, FuriString* text);

/* W5500 HAL (hal/w5500_hal.c) */
void w5500_hal_get_phy_info(bool* link_up, uint8_t* speed, uint8_t* duplex);
void w5500_hal_get_mac(uint8_t* mac);
bool w5500_hal_get_link_status(void);
bool w5500_hal_open_macraw(void);
void w5500_hal_close_macraw(void);
uint16_t w5500_hal_macraw_recv(uint8_t* buf, uint16_t buf_size);
uint16_t w5500_hal_macraw_send(const uint8_t* buf, uint16_t len);

/* Scan category — host helpers */
bool lan_tester_check_dhcp(LanTesterApp* app);
bool lan_tester_ensure_dhcp(LanTesterApp* app);
bool lan_tester_save_results(const char* type, const char* content);
void lan_tester_progress_bar(char* buf, uint8_t bar_len, uint16_t current, uint16_t total);
void scan_results_clear(void);
bool scan_results_open_writer(void);
void scan_results_close_writer(void);
void scan_results_add(const uint8_t* ip, const uint8_t* mac);
bool parse_cidr(const char* str, uint8_t* base_ip, uint8_t* prefix);

/* Scan protocols (use ioLibrary → stay in host). PingResult opaque for the table. */
typedef struct PingResult PingResult;
uint16_t arp_build_request(
    uint8_t* buf,
    const uint8_t* src_mac,
    const uint8_t* src_ip,
    const uint8_t* target_ip);
bool arp_parse_reply(
    const uint8_t* frame,
    uint16_t frame_len,
    uint8_t* sender_mac,
    uint8_t* sender_ip);
uint16_t
    arp_calc_scan_range(const uint8_t* ip, const uint8_t* mask, uint8_t* start_ip, uint8_t* end_ip);
uint8_t arp_mask_to_prefix(const uint8_t* mask);
bool icmp_ping(
    uint8_t socket_num,
    const uint8_t* target_ip,
    uint16_t seq,
    uint32_t timeout_ms,
    PingResult* result,
    const volatile bool* running);
int port_scan_tcp(uint8_t socket_num, const uint8_t* target_ip, uint16_t port, uint32_t timeout_ms);
extern const uint16_t PORT_PRESET_TOP20[];
extern const uint16_t PORT_PRESET_TOP100[];
typedef struct DiscoveryDevice DiscoveryDevice;
bool mdns_send_query(uint8_t socket_num);
bool mdns_parse_response(
    const uint8_t* buf,
    uint16_t len,
    const uint8_t* from_ip,
    DiscoveryDevice* device);
bool ssdp_send_msearch(uint8_t socket_num);
bool ssdp_parse_response(
    const uint8_t* buf,
    uint16_t len,
    const uint8_t* from_ip,
    DiscoveryDevice* device);

/* utils (utils/packet_utils.c, utils/oui_lookup.c) */
uint16_t pkt_get_ethertype(const uint8_t* frame);
void pkt_get_dst_mac(const uint8_t* frame, uint8_t* dst);
void pkt_get_src_mac(const uint8_t* frame, uint8_t* src);
bool pkt_is_broadcast(const uint8_t* mac);
bool pkt_is_multicast(const uint8_t* mac);
void pkt_format_mac(const uint8_t* mac, char* buf);
void pkt_format_ip(const uint8_t* ip, char* buf);
uint16_t pkt_checksum(const uint8_t* buf, uint16_t len);
uint16_t pkt_read_u16_be(const uint8_t* buf);
uint32_t pkt_read_u32_be(const uint8_t* buf);
void pkt_write_u16_be(uint8_t* buf, uint16_t val);
void pkt_write_u32_be(uint8_t* buf, uint32_t val);
const char* oui_lookup(const uint8_t* mac);

/* protocols that use ioLibrary directly stay in the host and are exposed here.
   SnmpGetResult is opaque for the table (real def in protocols/snmp_client.h). */
typedef struct SnmpGetResult SnmpGetResult;
bool snmp_client_get(
    const uint8_t* target_ip,
    const char* community,
    bool use_v2c,
    SnmpGetResult* result);

/* Diagnostics category */
void lan_tester_get_dns_server(LanTesterApp* app, uint8_t* out_ip);
typedef struct DnsLookupResult DnsLookupResult;
typedef struct TracerouteHop TracerouteHop;
bool dns_lookup(
    uint8_t socket_num,
    const uint8_t* dns_server,
    const char* hostname,
    DnsLookupResult* result);
bool traceroute_send_hop(
    uint8_t socket_num,
    const uint8_t* target_ip,
    uint8_t ttl,
    uint16_t seq,
    uint32_t timeout_ms,
    TracerouteHop* hop);

/* Utilities category (result/state types opaque for the table) */
typedef struct LldpNeighbor LldpNeighbor;
typedef struct CdpNeighbor CdpNeighbor;
typedef struct IpmiResult IpmiResult;
typedef struct NetbiosQueryResult NetbiosQueryResult;
typedef struct NtpDiagResult NtpDiagResult;
typedef struct TftpClientResult TftpClientResult;
bool lldp_parse(const uint8_t* payload, uint16_t payload_len, LldpNeighbor* neighbor);
bool cdp_parse(const uint8_t* payload, uint16_t payload_len, CdpNeighbor* neighbor);
uint16_t cdp_check_frame(const uint8_t* frame, uint16_t frame_len);
bool ipmi_query(const uint8_t* target_ip, IpmiResult* result);
bool netbios_node_status(const uint8_t* target_ip, NetbiosQueryResult* result);
bool ntp_diag_query(const uint8_t* server_ip, NtpDiagResult* result);
bool tftp_client_get(
    const uint8_t* server_ip,
    const char* filename,
    const char* save_path,
    TftpClientResult* result,
    volatile bool* running);
bool wol_send(uint8_t socket_num, const uint8_t* target_mac);
void w5500_hal_set_net_info(
    const uint8_t* ip,
    const uint8_t* subnet,
    const uint8_t* gateway,
    const uint8_t* dns);

/* Security category (result/state types opaque for the table) */
typedef struct DnsPoisonResult DnsPoisonResult;
typedef struct ArpWatchState ArpWatchState;
typedef struct RogueDhcpState RogueDhcpState;
typedef struct RogueRaState RogueRaState;
typedef struct DhcpFpState DhcpFpState;
typedef struct EapolProbeResult EapolProbeResult;
typedef struct VlanHopResult VlanHopResult;
bool dns_poison_check(
    const char* hostname,
    const uint8_t* local_dns,
    const uint8_t* public_dns,
    DnsPoisonResult* result);
void arp_watch_init(ArpWatchState* state);
bool arp_watch_process_frame(ArpWatchState* state, const uint8_t* frame, uint16_t len);
bool rogue_dhcp_detect(const uint8_t* our_mac, RogueDhcpState* state, uint32_t listen_ms);
void rogue_ra_init(RogueRaState* state);
bool rogue_ra_process_frame(RogueRaState* state, const uint8_t* frame, uint16_t len);
void dhcp_fp_init(DhcpFpState* state);
bool dhcp_fp_process_frame(DhcpFpState* state, const uint8_t* frame, uint16_t len);
bool eapol_probe_test(const uint8_t* our_mac, EapolProbeResult* result);
bool vlan_hop_test(
    const uint8_t* our_mac,
    const uint8_t* our_ip,
    const uint8_t* target_ip,
    uint16_t vlan_id,
    VlanHopResult* result);

#ifdef __cplusplus
}
#endif
