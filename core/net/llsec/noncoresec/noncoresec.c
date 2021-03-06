/*
 * Copyright (c) 2014, Hasso-Plattner-Institut.
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
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         802.15.4 security implementation, which uses a network-wide key
 * \author
 *         Konrad Krentz <konrad.krentz@gmail.com>
 */

/**
 * \addtogroup noncoresec
 * @{
 */

#include "net/llsec/noncoresec/noncoresec.h"
#include "net/llsec/anti-replay.h"
#include "net/llsec/llsec802154.h"
#include "net/llsec/ccm-star-packetbuf.h"
#include "net/mac/frame802154.h"
#include "net/mac/framer-802154.h"
#include "net/netstack.h"
#include "net/packetbuf.h"
#include "net/nbr-table.h"
#include "net/linkaddr.h"
#include "lib/ccm-star.h"
#include <string.h>

#define WITH_ENCRYPTION (LLSEC802154_SECURITY_LEVEL & (1 << 2))

#ifdef NONCORESEC_CONF_KEY
#define NONCORESEC_KEY NONCORESEC_CONF_KEY
#else /* NONCORESEC_CONF_KEY */
#define NONCORESEC_KEY { 0x00 , 0x01 , 0x02 , 0x03 , \
                         0x04 , 0x05 , 0x06 , 0x07 , \
                         0x08 , 0x09 , 0x0A , 0x0B , \
                         0x0C , 0x0D , 0x0E , 0x0F }
#endif /* NONCORESEC_CONF_KEY */

#define SECURITY_HEADER_LENGTH 5

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else /* DEBUG */
#define PRINTF(...)
#endif /* DEBUG */

/* network-wide CCM* key */
static uint8_t key[16] = NONCORESEC_KEY;
NBR_TABLE(struct anti_replay_info, anti_replay_table);

/*---------------------------------------------------------------------------*/
static int
aead(uint8_t hdrlen, int forward)
{
  uint8_t totlen;
  uint8_t nonce[CCM_STAR_NONCE_LENGTH];
  uint8_t *m;
  uint8_t m_len;
  uint8_t *a;
  uint8_t a_len;
  uint8_t *result;
  uint8_t generated_mic[LLSEC802154_MIC_LENGTH];
  uint8_t *mic;
  
  ccm_star_packetbuf_set_nonce(nonce, forward);
  totlen = packetbuf_totlen();
  a = packetbuf_hdrptr();
#if WITH_ENCRYPTION
  a_len = hdrlen;
  m = a + a_len;
  m_len = totlen - hdrlen;
#else /* WITH_ENCRYPTION */
  a_len = totlen;
  m = NULL;
  m_len = 0;
#endif /* WITH_ENCRYPTION */
  
  mic = a + totlen;
  result = forward ? mic : generated_mic;
  
  CCM_STAR.aead(nonce,
      m, m_len,
      a, a_len,
      result, LLSEC802154_MIC_LENGTH,
      forward);
  
  if(forward) {
    packetbuf_set_datalen(packetbuf_datalen() + LLSEC802154_MIC_LENGTH);
    return 1;
  } else {
    return (memcmp(generated_mic, mic, LLSEC802154_MIC_LENGTH) == 0);
  }
}
/*---------------------------------------------------------------------------*/
static void
add_security_header(void)
{
  if(!packetbuf_attr(PACKETBUF_ATTR_SECURITY_LEVEL)) {
    packetbuf_set_attr(PACKETBUF_ATTR_FRAME_TYPE, FRAME802154_DATAFRAME);
    packetbuf_set_attr(PACKETBUF_ATTR_SECURITY_LEVEL, LLSEC802154_SECURITY_LEVEL);
    anti_replay_set_counter();
  }
}
/*---------------------------------------------------------------------------*/
static void
send(mac_callback_t sent, void *ptr)
{
  NETSTACK_MAC.send(sent, ptr);
}
/*---------------------------------------------------------------------------*/
static int
create(void)
{
  int result;
  
  add_security_header();
  result = framer_802154.create();
  if(result == FRAMER_FAILED) {
    return result;
  }

  aead(result, 1);
  
  return result;
}
/*---------------------------------------------------------------------------*/
static int
parse(void)
{
  int result;
  const linkaddr_t *sender;
  struct anti_replay_info* info;
  
  result = framer_802154.parse();
  if(result == FRAMER_FAILED) {
    return result;
  }
  
  if(packetbuf_attr(PACKETBUF_ATTR_SECURITY_LEVEL) != LLSEC802154_SECURITY_LEVEL) {
    PRINTF("noncoresec: received frame with wrong security level\n");
    return FRAMER_FAILED;
  }
  sender = packetbuf_addr(PACKETBUF_ADDR_SENDER);
  if(linkaddr_cmp(sender, &linkaddr_node_addr)) {
    PRINTF("noncoresec: frame from ourselves\n");
    return FRAMER_FAILED;
  }
  
  packetbuf_set_datalen(packetbuf_datalen() - LLSEC802154_MIC_LENGTH);
  
  if(!aead(result, 0)) {
    PRINTF("noncoresec: received unauthentic frame %"PRIu32"\n",
        anti_replay_get_counter());
    return FRAMER_FAILED;
  }
  
  info = nbr_table_get_from_lladdr(anti_replay_table, sender);
  if(!info) {
    info = nbr_table_add_lladdr(anti_replay_table, sender);
    if(!info) {
      PRINTF("noncoresec: could not get nbr_table_item\n");
      return FRAMER_FAILED;
    }
    
    /*
     * Locking avoids replay attacks due to removed neighbor table items.
     * Unfortunately, an attacker can mount a memory-based DoS attack
     * on this by replaying broadcast frames from other network parts.
     * However, this is not an issue as long as the network size does not
     * exceed NBR_TABLE_MAX_NEIGHBORS.
     *  
     * To avoid locking, we could swap anti-replay information
     * to external flash. Locking is also unnecessary when using
     * pairwise session keys, as done in coresec.
     */
    if(!nbr_table_lock(anti_replay_table, info)) {
      nbr_table_remove(anti_replay_table, info);
      PRINTF("noncoresec: could not lock\n");
      return FRAMER_FAILED;
    }
    
    anti_replay_init_info(info);
  } else {
    if(anti_replay_was_replayed(info)) {
       PRINTF("noncoresec: received replayed frame %"PRIu32"\n",
           anti_replay_get_counter());
       return FRAMER_FAILED;
    }
  }
  
  return result;
}
/*---------------------------------------------------------------------------*/
static void
input(void)
{
  NETSTACK_NETWORK.input();
}
/*---------------------------------------------------------------------------*/
static int
length(void)
{
  add_security_header();
  return framer_802154.length() + LLSEC802154_MIC_LENGTH;
}
/*---------------------------------------------------------------------------*/
static void
init(void)
{
  CCM_STAR.set_key(key);
  nbr_table_register(anti_replay_table, NULL);
}
/*---------------------------------------------------------------------------*/
const struct llsec_driver noncoresec_driver = {
  "noncoresec",
  init,
  send,
  input
};
/*---------------------------------------------------------------------------*/
const struct framer noncoresec_framer = {
  length,
  create,
  parse
};
/*---------------------------------------------------------------------------*/

/** @} */
