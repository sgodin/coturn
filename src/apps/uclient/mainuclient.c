/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * https://opensource.org/license/bsd-3-clause
 *
 * Copyright (C) 2011, 2012, 2013 Citrix Systems
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "apputils.h"
#include "ns_turn_utils.h"
#include "session.h"
#include "uclient.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_MSC_VER)
#include <getopt.h>
#else
#include <unistd.h>
#endif

/////////////// extern definitions /////////////////////

int clmessage_length = 100;
bool do_not_use_channel = false;
bool c2c = false;
int clnet_verbose = TURN_VERBOSE_NONE;
bool use_tcp = false;
bool use_sctp = false;
bool use_secure = false;
bool hang_on = false;
ioa_addr peer_addr;
bool no_rtcp = false;
int default_address_family = STUN_ATTRIBUTE_REQUESTED_ADDRESS_FAMILY_VALUE_DEFAULT;
bool dont_fragment = false;
uint8_t g_uname[STUN_MAX_USERNAME_SIZE + 1];
password_t g_upwd;
char g_auth_secret[1025] = "\0";
bool g_use_auth_secret_with_timestamp = false;
bool use_fingerprints = true;

static char ca_cert_file[1025] = "";
static char cipher_suite[1025] = "";
char cert_file[1025] = "";
char pkey_file[1025] = "";
SSL_CTX *root_tls_ctx[32];
int root_tls_ctx_num = 0;

uint8_t relay_transport = STUN_ATTRIBUTE_TRANSPORT_UDP_VALUE;
unsigned char client_ifname[1025] = "";
bool passive_tcp = false;
bool mandatory_channel_padding = false;
bool negative_test = false;
bool negative_protocol_test = false;
bool dos = false;
bool random_disconnect = false;

SHATYPE shatype = SHATYPE_DEFAULT;

bool mobility = false;

bool no_permissions = false;

bool extra_requests = false;

char origin[STUN_MAX_ORIGIN_SIZE + 1] = "\0";

band_limit_t bps = 0;

bool dual_allocation = false;

int oauth = 0;
oauth_key okey_array[3];

static oauth_key_data_raw okdr_array[3] = {
    {"north", "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEK", 0, 0, "A256GCM", "crinna.org"},
    {"union", "MTIzNDU2Nzg5MDEyMzQ1Ngo=", 0, 0, "A128GCM", "north.gov"},
    {"oldempire", "MTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIK", 0, 0, "A256GCM", ""}};

//////////////// local definitions /////////////////

static char Usage[] =
    "Usage: uclient [flags] [options] turn-server-ip-address\n"
    "Flags:\n"
    "	-t	TCP (default - UDP).\n"
    "	-b	SCTP (default - UDP).\n"
    "	-T	TCP relay transport (default - UDP). Implies options -t, -y, -c, and ignores \n"
    "		options -s, -e, -r and -g. Can be used together with -b\n"
    "	-P	Passive TCP (RFC6062 with active peer). Implies -T.\n"
    "	-S	Secure connection: TLS for TCP, DTLS for UDP.\n"
    "	-U	Secure connection with eNULL cipher.\n"
    "	-v	Verbose.\n"
    "	-s	Use send method.\n"
    "	-y	Use client-to-client connections.\n"
    "	-h	Hang on indefinitely after the last sent packet.\n"
    "	-c	No rtcp connections.\n"
    "	-x	IPv6 relay address requested.\n"
    "	-X	IPv4 relay address explicitly requested.\n"
    "	-g	Include DONT_FRAGMENT option.\n"
    "	-D	Mandatory channel padding (like in pjnath).\n"
    "	-N	Negative tests (some limited cases only).\n"
    "	-R	Negative protocol tests.\n"
    "	-O	DOS attack mode (quick connect and exit).\n"
    "	-M	ICE Mobility engaged.\n"
    "	-I	Do not set permissions on TURN relay endpoints\n"
    "		(for testing the non-standard server relay functionality).\n"
    "	-G	Generate extra requests (create permissions, channel bind).\n"
    "	-B	Random disconnect after a few initial packets.\n"
    "	-Z	Dual allocation (implies -c).\n"
    "	-J	Use oAuth with default test keys kid='north', 'union' or 'oldempire'.\n"
    "Options:\n"
    "	-l	Message length (Default: 100 Bytes).\n"
    "	-i	Certificate file (for secure connections only, optional).\n"
    "	-k	Private key file (for secure connections only).\n"
    "	-E	CA file for server certificate verification, \n"
    "		if the server certificate to be verified.\n"
    "	-p	TURN server port (Default: 3478 unsecure, 5349 secure).\n"
    "	-n	Number of messages to send (Default: 5).\n"
    "	-d	Local interface device (optional).\n"
    "	-L	Local address.\n"
    "	-m	Number of clients (default is 1).\n"
    "	-e	Peer address.\n"
    "	-r	Peer port (default 3480).\n"
    "	-z	Per-session packet interval in milliseconds (default is 20 ms).\n"
    "	-u	STUN/TURN user name.\n"
    "	-w	STUN/TURN user password.\n"
    "	-W	TURN REST API \"plain text\" secret.\n"
    "	-C	TURN REST API timestamp/username separator symbol (character). The default value is ':'.\n"
    "	-F	<cipher-suite> Cipher suite for TLS/DTLS. Default value is DEFAULT.\n"
    "	-o	<origin> - the ORIGIN STUN attribute value.\n"
    "	-a	<bytes-per-second> Bandwidth for the bandwidth request in ALLOCATE. The default value is zero.\n";

//////////////////////////////////////////////////

int main(int argc, char **argv) {
  int port = 0;
  int messagenumber = 5;
  char local_addr[256];
  int c;
  int mclient = 1;
  char peer_address[129] = "\0";
  int peer_port = PEER_DEFAULT_PORT;

  char rest_api_separator = ':';
  bool use_null_cipher = false;

#if defined(WINDOWS)

  WORD wVersionRequested;
  WSADATA wsaData;
  int err;

  /* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
  wVersionRequested = MAKEWORD(2, 2);

  err = WSAStartup(wVersionRequested, &wsaData);
  if (err != 0) {
    /* Tell the user that we could not find a usable */
    /* Winsock DLL.                                  */
    TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "WSAStartup failed with error: %d\n", err);
    return 1;
  }
#endif

  set_logfile("stdout");
  set_no_stdout_log(1);

  set_execdir();

  set_system_parameters(0);

  memset(local_addr, 0, sizeof(local_addr));

  while ((c = getopt(argc, argv, "a:d:p:l:n:L:m:e:r:u:w:i:k:z:W:C:E:F:o:bZvsyhcxXgtTSAPDNOUMRIGBJ")) != -1) {
    switch (c) {
    case 'J': {

      oauth = 1;

      oauth_key_data okd_array[3];
      convert_oauth_key_data_raw(&okdr_array[0], &okd_array[0]);
      convert_oauth_key_data_raw(&okdr_array[1], &okd_array[1]);
      convert_oauth_key_data_raw(&okdr_array[2], &okd_array[2]);

      char err_msg[1025] = "\0";
      size_t err_msg_size = sizeof(err_msg) - 1;

      if (!convert_oauth_key_data(&okd_array[0], &okey_array[0], err_msg, err_msg_size)) {
        fprintf(stderr, "%s\n", err_msg);
        exit(-1);
      }

      if (!convert_oauth_key_data(&okd_array[1], &okey_array[1], err_msg, err_msg_size)) {
        fprintf(stderr, "%s\n", err_msg);
        exit(-1);
      }

      if (!convert_oauth_key_data(&okd_array[2], &okey_array[2], err_msg, err_msg_size)) {
        fprintf(stderr, "%s\n", err_msg);
        exit(-1);
      }
    } break;
    case 'a':
      bps = (band_limit_t)strtoul(optarg, NULL, 10);
      break;
    case 'o':
      STRCPY(origin, optarg);
      break;
    case 'B':
      random_disconnect = true;
      break;
    case 'G':
      extra_requests = true;
      break;
    case 'F':
      STRCPY(cipher_suite, optarg);
      break;
    case 'I':
      no_permissions = true;
      break;
    case 'M':
      mobility = true;
      break;
    case 'E': {
      char *fn = find_config_file(optarg);
      if (!fn) {
        fprintf(stderr, "ERROR: file %s not found\n", optarg);
        exit(-1);
      }
      STRCPY(ca_cert_file, fn);
    } break;
    case 'O':
      dos = true;
      break;
    case 'C':
      rest_api_separator = *optarg;
      break;
    case 'D':
      mandatory_channel_padding = true;
      break;
    case 'N':
      negative_test = true;
      break;
    case 'R':
      negative_protocol_test = true;
      break;
    case 'z':
      RTP_PACKET_INTERVAL = atoi(optarg);
      break;
    case 'Z':
      dual_allocation = true;
      break;
    case 'u':
      STRCPY(g_uname, optarg);
      break;
    case 'w':
      STRCPY(g_upwd, optarg);
      break;
    case 'g':
      dont_fragment = true;
      break;
    case 'd':
      STRCPY(client_ifname, optarg);
      break;
    case 'x':
      default_address_family = STUN_ATTRIBUTE_REQUESTED_ADDRESS_FAMILY_VALUE_IPV6;
      break;
    case 'X':
      default_address_family = STUN_ATTRIBUTE_REQUESTED_ADDRESS_FAMILY_VALUE_IPV4;
      break;
    case 'l':
      clmessage_length = atoi(optarg);
      break;
    case 's':
      do_not_use_channel = true;
      break;
    case 'n':
      messagenumber = atoi(optarg);
      break;
    case 'p':
      port = atoi(optarg);
      break;
    case 'L':
      STRCPY(local_addr, optarg);
      break;
    case 'e':
      STRCPY(peer_address, optarg);
      break;
    case 'r':
      peer_port = atoi(optarg);
      break;
    case 'v':
      clnet_verbose = TURN_VERBOSE_NORMAL;
      break;
    case 'h':
      hang_on = true;
      break;
    case 'c':
      no_rtcp = true;
      break;
    case 'm':
      mclient = atoi(optarg);
      break;
    case 'y':
      c2c = true;
      break;
    case 't':
      use_tcp = true;
      break;
    case 'b':
      use_sctp = true;
      use_tcp = true;
      break;
    case 'P':
      passive_tcp = true;
      /* implies 'T': */
      /* no break */
      /* Falls through. */
    case 'T':
      relay_transport = STUN_ATTRIBUTE_TRANSPORT_TCP_VALUE;
      break;
    case 'U':
      use_null_cipher = true;
      /* implies 'S' */
      /* no break */
      /* Falls through. */
    case 'S':
      use_secure = true;
      break;
    case 'W':
      g_use_auth_secret_with_timestamp = true;
      STRCPY(g_auth_secret, optarg);
      break;
    case 'i': {
      char *fn = find_config_file(optarg);
      if (!fn) {
        fprintf(stderr, "ERROR: file %s not found\n", optarg);
        exit(-1);
      }
      STRCPY(cert_file, fn);
      free(fn);
    } break;
    case 'k': {
      char *fn = find_config_file(optarg);
      if (!fn) {
        fprintf(stderr, "ERROR: file %s not found\n", optarg);
        exit(-1);
      }
      STRCPY(pkey_file, fn);
      free(fn);
    } break;
    default:
      fprintf(stderr, "%s\n", Usage);
      exit(1);
    }
  }

  if (dual_allocation) {
    no_rtcp = true;
  }

  if (g_use_auth_secret_with_timestamp) {

    {
      char new_uname[1025];
      const unsigned long exp_time = 3600 * 24; /* one day */
      if (g_uname[0]) {
        snprintf(new_uname, sizeof(new_uname), "%lu%c%s", (unsigned long)time(NULL) + exp_time, rest_api_separator,
                 (char *)g_uname);
      } else {
        snprintf(new_uname, sizeof(new_uname), "%lu", (unsigned long)time(NULL) + exp_time);
      }
      STRCPY(g_uname, new_uname);
    }
    {
      uint8_t hmac[MAXSHASIZE];
      unsigned int hmac_len;

      switch (shatype) {
      case SHATYPE_SHA256:
        hmac_len = SHA256SIZEBYTES;
        break;
      case SHATYPE_SHA384:
        hmac_len = SHA384SIZEBYTES;
        break;
      case SHATYPE_SHA512:
        hmac_len = SHA512SIZEBYTES;
        break;
      default:
        hmac_len = SHA1SIZEBYTES;
      };

      hmac[0] = 0;

      if (stun_calculate_hmac(g_uname, strlen((char *)g_uname), (uint8_t *)g_auth_secret, strlen(g_auth_secret), hmac,
                              &hmac_len, shatype)) {
        size_t pwd_length = 0;
        char *pwd = base64_encode(hmac, hmac_len, &pwd_length);

        if (pwd) {
          if (pwd_length > 0) {
            memcpy(g_upwd, pwd, pwd_length);
            g_upwd[pwd_length] = 0;
          }
        }
        free(pwd);
      }
    }
  }

  if (is_TCP_relay()) {
    dont_fragment = false;
    no_rtcp = true;
    c2c = true;
    use_tcp = true;
    do_not_use_channel = true;
  }

  if (port == 0) {
    if (use_secure) {
      port = DEFAULT_STUN_TLS_PORT;
    } else {
      port = DEFAULT_STUN_PORT;
    }
  }

  if (clmessage_length < (int)sizeof(message_info)) {
    clmessage_length = (int)sizeof(message_info);
  }

  const int max_header = 100;
  if (clmessage_length > (int)(STUN_BUFFER_SIZE - max_header)) {
    fprintf(stderr, "Message length was corrected to %d\n", (STUN_BUFFER_SIZE - max_header));
    clmessage_length = (int)(STUN_BUFFER_SIZE - max_header);
  }

  if (optind >= argc) {
    fprintf(stderr, "%s\n", Usage);
    exit(-1);
  }

  if (!c2c) {
    if (!peer_address[0]) {
      fprintf(stderr, "Either -e peer_address or -y must be specified\n");
      return -1;
    }

    if (make_ioa_addr((const uint8_t *)peer_address, peer_port, &peer_addr) < 0) {
      return -1;
    }

    if (peer_addr.ss.sa_family == AF_INET6) {
      default_address_family = STUN_ATTRIBUTE_REQUESTED_ADDRESS_FAMILY_VALUE_IPV6;
    } else if (peer_addr.ss.sa_family == AF_INET) {
      default_address_family = STUN_ATTRIBUTE_REQUESTED_ADDRESS_FAMILY_VALUE_IPV4;
    }
  }

  /* SSL Init ==>> */

  if (use_secure) {

    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    const char *csuite = "ALL"; //"AES256-SHA" "DH"
    if (use_null_cipher) {
      csuite = "eNULL";
    } else if (cipher_suite[0]) {
      csuite = cipher_suite;
    }

    if (use_tcp) {
      root_tls_ctx[root_tls_ctx_num] = SSL_CTX_new(TLS_client_method());
      SSL_CTX_set_cipher_list(root_tls_ctx[root_tls_ctx_num], csuite);
      root_tls_ctx_num++;
    } else {
#if !DTLS_SUPPORTED
      fprintf(stderr, "ERROR: DTLS is not supported.\n");
      exit(-1);
#else
      root_tls_ctx[root_tls_ctx_num] = SSL_CTX_new(DTLS_client_method());
      SSL_CTX_set_cipher_list(root_tls_ctx[root_tls_ctx_num], csuite);
#endif
      root_tls_ctx_num++;
    }
  }

  int use_cert = 0;
  int use_ca_cert = 0;
  if (cert_file[0] && pkey_file[0]) {
    use_cert = 1;
  }
  if (ca_cert_file[0]) {
    use_ca_cert = 1;
  }

  if (use_cert) {
    int sslind = 0;
    for (sslind = 0; sslind < root_tls_ctx_num; sslind++) {
      if (!SSL_CTX_use_certificate_chain_file(root_tls_ctx[sslind], cert_file)) {
        TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "\nERROR: could not load certificate chain file!\n");
        exit(-1);
      }

      if (!SSL_CTX_use_PrivateKey_file(root_tls_ctx[sslind], pkey_file, SSL_FILETYPE_PEM)) {
        TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "\nERROR: could not load private key file!\n");
        exit(-1);
      }

      if (!SSL_CTX_check_private_key(root_tls_ctx[sslind])) {
        TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "\nERROR: invalid private key!\n");
        exit(-1);
      }

      if (use_ca_cert) {
        if (!SSL_CTX_load_verify_locations(root_tls_ctx[sslind], ca_cert_file, NULL)) {
          TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "ERROR: cannot load CA from file: %s\n", ca_cert_file);
          exit(-1);
        }

        /* Set to require peer (client) certificate verification */
        SSL_CTX_set_verify(root_tls_ctx[sslind], SSL_VERIFY_PEER, NULL);

        /* Set the verification depth to 9 */
        SSL_CTX_set_verify_depth(root_tls_ctx[sslind], 9);
      } else {
        SSL_CTX_set_verify(root_tls_ctx[sslind], SSL_VERIFY_NONE, NULL);
      }
    }
  }

  start_mclient(argv[optind], port, client_ifname, local_addr, messagenumber, mclient);

  return 0;
}
