/*
 * GFAmigaRDP: the Basic Settings Exchange, Channel Connection and Secure
 * Settings Exchange phases of the RDP Connection Sequence ([MS-RDPBCGR]
 * 1.3.1.1): MCS Connect Initial with the GCC Conference Create Request and
 * the client data blocks (2.2.1.3), the Connect Response (2.2.1.4), Erect
 * Domain, Attach User and Channel Join (2.2.1.5 to 2.2.1.9) and the Client
 * Info PDU (2.2.1.11), up to the first licensing PDU (2.2.1.12). Only the
 * TLS and NLA security protocols, where the server selects no RDP
 * encryption (5.4), so there is no Security Exchange PDU (2.2.1.10).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_MCS_H
#define RDP_MCS_H

#include <stdint.h>
#include "connect.h"

#define RDP_E_MCS (-120)     /* the server's MCS answer was a refusal; see the result */
#define RDP_E_LICENSE (-121) /* licensing did not end with STATUS_VALID_CLIENT: lic_type, lic_error say how */

#define RDP_MCS_MAX_CHANNELS 31

/* how far rdp_mcs_connect got */
enum
{
	RDP_MCS_ST_NONE,
	RDP_MCS_ST_CONNECT_SENT,     /* Connect Initial written */
	RDP_MCS_ST_CONNECTED,        /* Connect Response with result rt-successful */
	RDP_MCS_ST_ATTACHED,         /* Erect Domain written, Attach User confirmed */
	RDP_MCS_ST_JOINED,           /* every channel joined */
	RDP_MCS_ST_INFO_SENT,        /* Client Info PDU written */
	RDP_MCS_ST_LICENSING         /* the first licensing PDU read */
};

struct rdp_mcs_result
{
	int stage;
	unsigned connect_result;     /* T.125 Result of the Connect Response, 0 rt-successful */
	uint32_t server_version;     /* SC_CORE */
	uint32_t client_requested;   /* SC_CORE clientRequestedProtocols */
	uint32_t early_caps;         /* SC_CORE earlyCapabilityFlags */
	uint32_t enc_method, enc_level;
	uint16_t io_channel, msg_channel, user_channel;
	uint16_t channel_count;
	uint16_t channels[RDP_MCS_MAX_CHANNELS];
	unsigned attach_result, join_result;
	uint16_t join_failed;        /* the channel a join was refused for */
	unsigned disconnect_reason;  /* Disconnect Provider Ultimatum, +1 so 0 is none */
	uint32_t error_info;         /* Set Error Info PDU (2.2.5.1.1), 0 none */
	/* the licensing PDU (2.2.1.12, [MS-RDPELE] 2.2.2) */
	uint16_t sec_flags;
	uint8_t lic_type, lic_flags;
	uint16_t lic_size;
	uint32_t lic_error, lic_state;
};

/* After the negotiation, TLS and (for HYBRID) CredSSP on t: runs the phases
 * above with selected the server's selectedProtocol. The password goes
 * only into the Client Info PDU and is not kept. */
int rdp_mcs_connect(struct rdp_transport* t, const struct rdp_settings* s, uint32_t selected,
                    const char* password, struct rdp_mcs_result* r);

const char* rdp_mcs_stage_name(int stage);

/* ---- after licensing ---- */

/* one PDU the server sent: a fast-path output PDU (fastpath 1, data its
 * updates) or an MCS Send Data Indication (channel, data its userData) */
struct rdp_pdu
{
	int fastpath;
	uint8_t choice;              /* the MCS DomainMCSPDU choice (26 SDIN), 0 for fast-path */
	uint16_t channel;
	const uint8_t* data;
	size_t len;
};
/* Reads one PDU into buf (size 65536 holds any). 0 (pdu->len 0 for an MCS
 * PDU the session has no use for), AMIPR_ECLOSED after a Disconnect
 * Provider Ultimatum (r->disconnect_reason) or when the connection ends,
 * or < 0. */
int rdp_pdu_read(struct rdp_transport* t, uint8_t* buf, size_t size, struct rdp_mcs_result* r, struct rdp_pdu* pdu);
/* An MCS Send Data Request from user on channel with data (len < 32768) */
int rdp_mcs_send_data(struct rdp_transport* t, uint8_t* buf, size_t size, uint16_t user, uint16_t channel,
                      const uint8_t* data, size_t len);
/* The client's Disconnect Provider Ultimatum (rn-user-requested) */
int rdp_mcs_disconnect(struct rdp_transport* t);
const char* rdp_license_error_text(uint32_t code);

#endif
