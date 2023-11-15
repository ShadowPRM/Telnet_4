/**
 * @file
 * SLIP Interface
 *
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
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
 * This file is built upon the file: src/arch/rtxc/netif/sioslip.c
 *
 * Author: Magnus Ivarsson <magnus.ivarsson(at)volvo.com>
 *         Simon Goldschmidt
 */


/**
 * @defgroup slipif SLIP netif
 * @ingroup addons
 *
 * This is an arch independent SLIP netif. The specific serial hooks must be 
 * provided by another file. They are sio_open, sio_read/sio_tryread and sio_send
 * 
 * Это независимая арха.Конкретные последовательные крючки должны быть 
 * предоставлены другим файлом.Это sio_open, sio_read/sio_tryread и sio_send
 *
 * Использование: этот Netif можно использовать тремя способами: \ n
 * Usage: This netif can be used in three ways:\n
 * 
 *        1) For NO_SYS==0, an RX thread can be used which blocks on sio_read()
 *           until data is received.\n
 * 
 *        2) In your main loop, call slipif_poll() to check for new RX bytes,
 *           completed packets are fed into netif->input().\n
 * 
 *        3) Call slipif_received_byte[s]() from your serial RX ISR and
 *           slipif_process_rxqueue() from your main loop. ISR level decodes
 *           packets and puts completed packets on a queue which is fed into
 *           the stack from the main loop (needs SYS_LIGHTWEIGHT_PROT for
 *           pbuf_alloc to work on ISR level!).
 *
 */

#include "netif/slipif.h"
#include "lwip/opt.h"

#include "lwip/def.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/sys.h"
#include "lwip/sio.h"

#include "telnet_server.h"


#define SLIP_END     0xC0 /* 0300: start and end of every packet */
#define SLIP_ESC     0xDB /* 0333: escape start (one byte escaped data follows) */
#define SLIP_ESC_END 0xDC /* 0334: following escape: original byte is 0xC0 (END) */
#define SLIP_ESC_ESC 0xDD /* 0335: following escape: original byte is 0xDB (ESC) */

/** Maximum packet size that is received by this netif */
#ifndef SLIP_MAX_SIZE
#define SLIP_MAX_SIZE 1500
#endif

/** Define this to the interface speed for SNMP
 * (sio_fd is the sio_fd_t returned by sio_open).
 * The default value of zero means 'unknown'.
 */
#ifndef SLIP_SIO_SPEED
#define SLIP_SIO_SPEED(sio_fd) 0
#endif

enum slipif_recv_state {
    SLIP_RECV_NORMAL,
    SLIP_RECV_ESCAPE
};

struct slipif_priv {
  sio_fd_t sd;
  /* q is the whole pbuf chain for a packet, p is the current pbuf in the chain / Q - целая цепь PBUF для пакета, P - текущий PBUF в цепи*/
  struct pbuf *p, *q;
  u8_t state;
  u16_t i, recved;
#if SLIP_RX_FROM_ISR
  struct pbuf *rxpackets;
#endif
};

// extern err_t etharp_output(struct netif *netif, struct pbuf *q, const ip4_addr_t *ipaddr);
// extern err_t tcpip_input(struct pbuf *p, struct netif *inp);
// extern low_level_output(struct netif *netif, struct pbuf *p);
// extern void ethernetif_update_config(struct netif *netif);

////////////////////////////////////////////////////////////////////////////////////////////////// ИНКАПСУЛЯЦИЯ IP пакета (побайтово) и отправка в UART
/**
 * Send a pbuf doing the necessary SLIP encapsulation
 * Отправить PBUF, выполняющий необходимую инкапсуляцию скольжения
 *
 * Uses the serial layer's sio_send()
 * Использует sio_send () серийный слой ()
 *
 * @param netif the lwip network interface structure for this slipif
 * @param p the pbuf chain packet to send
 * @return always returns ERR_OK since the serial layer does not provide return values
 */
static err_t
slipif_output(struct netif *netif, struct pbuf *p)
{
  struct slipif_priv *priv;
  struct pbuf *q;
  u16_t i;
  u8_t c;

  LWIP_ASSERT("netif != NULL", (netif != NULL));
  LWIP_ASSERT("netif->state != NULL", (netif->state != NULL));
  LWIP_ASSERT("p != NULL", (p != NULL));

  LWIP_DEBUGF(SLIP_DEBUG, ("slipif_output(%"U16_F"): sending %"U16_F" bytes\n", (u16_t)netif->num, p->tot_len));
  priv = (struct slipif_priv *)netif->state;

  /* Send pbuf out on the serial I/O device. */
  /* Start with packet delimiter. */
  sio_send(SLIP_END, priv->sd);

  for (q = p; q != NULL; q = q->next) {
    for (i = 0; i < q->len; i++) {
      c = ((u8_t *)q->payload)[i];
      switch (c) {
      case SLIP_END:
        /* need to escape this byte (0xC0 -> 0xDB, 0xDC) */
        sio_send(SLIP_ESC, priv->sd);
        sio_send(SLIP_ESC_END, priv->sd);
        break;
      case SLIP_ESC:
        /* need to escape this byte (0xDB -> 0xDB, 0xDD) */
        sio_send(SLIP_ESC, priv->sd);
        sio_send(SLIP_ESC_ESC, priv->sd);
        break;
      default:
        /* normal byte - no need for escaping */
        sio_send(c, priv->sd);
        break;
      }
    }
  }
  /* End with packet delimiter. */
  sio_send(SLIP_END, priv->sd);
  return ERR_OK;
}

#if LWIP_IPV4

///////////////////////////////////////////////////////////////////////////////////////////////////////// ОТПРАВКА В UART
/**
 * Send a pbuf doing the necessary SLIP encapsulation
 * Отправить PBUF, выполняющий необходимую инкапсуляцию скольжения
 * Uses the serial layer's sio_send()
 *
 * @param netif the lwip network interface structure for this slipif
 * @param p the pbuf chain packet to send
 * @param ipaddr the ip address to send the packet to (not used for slipif)
 * @return always returns ERR_OK since the serial layer does not provide return values
 */
static err_t
slipif_output_v4(struct netif *netif, struct pbuf *p, const ip4_addr_t *ipaddr)
{
  LWIP_UNUSED_ARG(ipaddr);
  return slipif_output(netif, p);
}
#endif /* LWIP_IPV4 */

#if LWIP_IPV6
/**
 * Send a pbuf doing the necessary SLIP encapsulation
 *
 * Uses the serial layer's sio_send()
 *
 * @param netif the lwip network interface structure for this slipif
 * @param p the pbuf chain packet to send
 * @param ipaddr the ip address to send the packet to (not used for slipif)
 * @return always returns ERR_OK since the serial layer does not provide return values
 */
static err_t
slipif_output_v6(struct netif *netif, struct pbuf *p, const ip6_addr_t *ipaddr)
{
  LWIP_UNUSED_ARG(ipaddr);
  return slipif_output(netif, p);
}
#endif /* LWIP_IPV6 */

/////////////////////////////////////////////////////////////////////////////////////////////////// ОБРАБОТКА принятых из UART
/**
 * Handle the incoming SLIP stream character by character
 * Обработайте символ входящего потока скольжения по персонажу
 *
 * @param netif the lwip network interface structure for this slipif
 * @param c received character (multiple calls to this function will
 *        return a complete packet, NULL is returned before - used for polling)
 * @return The IP packet when SLIP_END is received / IP -пакет при получении slip_end
 */
static struct pbuf*
slipif_rxbyte(struct netif *netif, u8_t c)
{
  struct slipif_priv *priv;
  struct pbuf *t;

  LWIP_ASSERT("netif != NULL", (netif != NULL));
  LWIP_ASSERT("netif->state != NULL", (netif->state != NULL));

  priv = (struct slipif_priv *)netif->state;

  switch (priv->state) {
  case SLIP_RECV_NORMAL:
    switch (c) {
    case SLIP_END:
      if (priv->recved > 0) {
        /* Received whole packet. | Получил весь пакет */
        /* Trim the pbuf to the size of the received packet. | Обрежьте PBUF до размера полученного пакета. */
        pbuf_realloc(priv->q, priv->recved);

        LINK_STATS_INC(link.recv);

        LWIP_DEBUGF(SLIP_DEBUG, ("slipif: Got packet (%"U16_F" bytes)\n", priv->recved));
        t = priv->q;
        priv->p = priv->q = NULL;
        priv->i = priv->recved = 0;
        return t;
      }
      return NULL;
    case SLIP_ESC:
      priv->state = SLIP_RECV_ESCAPE;
      return NULL;
    default:
      break;
    } /* end switch (c) */
    break;
  case SLIP_RECV_ESCAPE:
    /* un-escape END or ESC bytes, leave other bytes
       (although that would be a protocol error) */
    switch (c) {
    case SLIP_ESC_END:
      c = SLIP_END;
      break;
    case SLIP_ESC_ESC:
      c = SLIP_ESC;
      break;
    default:
      break;
    }
    priv->state = SLIP_RECV_NORMAL;
    break;
  default:
    break;
  } /* end switch (priv->state) */

  /* byte received, packet not yet completely received / Получен байт, пакет еще не полностью получен */
  if (priv->p == NULL) {
    /* allocate a new pbuf  |  ВЫДЕЛЯЕТСЯ новый pbuf */
    LWIP_DEBUGF(SLIP_DEBUG, ("slipif_input: alloc\n"));
    priv->p = pbuf_alloc(PBUF_LINK, (PBUF_POOL_BUFSIZE - PBUF_LINK_HLEN - PBUF_LINK_ENCAPSULATION_HLEN), PBUF_POOL);

    if (priv->p == NULL) {
      LINK_STATS_INC(link.drop);
      LWIP_DEBUGF(SLIP_DEBUG, ("slipif_input: no new pbuf! (DROP)\n"));
      /* don't process any further since we got no pbuf to receive to */
      return NULL;
    }

    if (priv->q != NULL) {
      /* 'chain' the pbuf to the existing chain */
      pbuf_cat(priv->q, priv->p);
    } else {
      /* p is the first pbuf in the chain */
      priv->q = priv->p;
    }
  }

  /* this automatically drops bytes if > SLIP_MAX_SIZE | это автоматически сбрасывает байты, если> slip_max_size */
  if ((priv->p != NULL) && (priv->recved <= SLIP_MAX_SIZE)) {
    ((u8_t *)priv->p->payload)[priv->i] = c;           /// сохраняет принятый по УАРТ байт в массив payload в структуре priv->p
    priv->recved++;                                    /// счётчик +1 записаных данных
    priv->i++;                                         /// или это счётчик

    if (priv->i >= priv->p->len) {
      /* on to the next pbuf */
      priv->i = 0;
      if (priv->p->next != NULL && priv->p->next->len > 0) {
        /* p is a chain, on to the next in the chain */
          priv->p = priv->p->next;
      } else {
        /* p is a single pbuf, set it to NULL so next time a new
         * pbuf is allocated */
          priv->p = NULL;
      }
    }
  }
  return NULL;
}

//////////////////////////////////////////////////////////////////////////////////////////////////// отправка на ОБРАБОТКУ и СОХРАНЕНИЕ принятого байта
/** Like slipif_rxbyte, but passes completed packets to netif->input
 *
* Как slipif_rxbyte, но передает заполненные пакеты на Netif-> вход
 * 
 * @param netif The lwip network interface structure for this slipif
 * @param c received character
 */
static void
slipif_rxbyte_input(struct netif *netif, u8_t c)
{
  struct pbuf *p;
  p = slipif_rxbyte(netif, c);   /// обработка входящего (по УАРТ) символа и вроде как сохранение в pbuf->payload (или не сохраняет.. хз блин)
  if (p != NULL) {
    //указатель p получаем когда примем весь пакет
    HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);

    if (netif->input(p, netif) != ERR_OK) {     //опа.. похоже input пуст и изза этого сваливается в цикл ERR (тут должна выполнится фу-я, на которую указывает)
      pbuf_free(p);
    }
    
    //if (telnet_transmit(p->payload, p->len) > 0) {pbuf_free(p);}
    //netif->link_callback (netif, p);
    //low_level_output(netif, p);
    //tcp_output(netif, p);
    //etharp_output(netif, p, netif->ip_addr );

    //пакет весь пришёл, можно попробовать отсюда отправить
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////// ЗАДАЧА ПРИЁМА данных по UART
#if SLIP_USE_RX_THREAD
/**
 * The SLIP input thread.
 * Задача приёма данных по УАРТ
 * 
 * Feed the IP layer with incoming packets
 * Передавайте на IP-уровень входящие пакеты
 * 
 * @param nf the lwip network interface structure for this slipif
 */
static void
slipif_loop_thread(void *nf)
{
  u8_t c;
  struct netif *netif = (struct netif *)nf;
  struct slipif_priv *priv = (struct slipif_priv *)netif->state;

  while (1) {
    if (sio_read(priv->sd, &c, 1) > 0) {    //приём от UART (из очереди)
      slipif_rxbyte_input(netif, c);        //обработка и сохранение
    }
  }
}
#endif /* SLIP_USE_RX_THREAD */

//extern struct netif gnetif;
///////////////////////////////////////////////////////////////////////////////////////////////// ИНИЦИАЛИЗАЦИЯ SLIP
/**
 * SLIP netif initialization
 *
 * Call the arch specific sio_open and remember
 * the opened device in the state field of the netif.
 *
 * @param netif the lwip network interface structure for this slipif
 * @return ERR_OK if serial line could be opened,
 *         ERR_MEM if no memory could be allocated,
 *         ERR_IF is serial line couldn't be opened
 *
 * @note netif->num must contain the number of the serial port to open
 *       (0 by default). If netif->state is != NULL, it is interpreted as an
 *       u8_t pointer pointing to the serial port number instead of netif->num.
 *
 */
err_t
slipif_init(struct netif *netif)
{
  struct slipif_priv *priv;
  u8_t sio_num;

  LWIP_DEBUGF(SLIP_DEBUG, ("slipif_init: netif->num=%"U16_F"\n", (u16_t)netif->num));

  /* Allocate private data */
  priv = (struct slipif_priv *)mem_malloc(sizeof(struct slipif_priv));
  if (!priv) {
    return ERR_MEM;
  }

  netif->name[0] = 's';
  netif->name[1] = 'l';
#if LWIP_IPV4
  netif->output = slipif_output_v4;
  ////////////////////////////////////////////////////////// дописал
  struct netif *p_gnetif;
  p_gnetif = &gnetif;
  netif->input = slipif_rxbyte_input;
  netif->ip_addr = p_gnetif->ip_addr;
  netif->netmask = p_gnetif->netmask;
  netif->gw = p_gnetif->gw;
  netif->mtu = p_gnetif->mtu;
  netif->hwaddr_len = p_gnetif->hwaddr_len;
  netif->hwaddr[0] = p_gnetif->hwaddr[0]; netif->hwaddr[3] = p_gnetif->hwaddr[3];
  netif->hwaddr[1] = p_gnetif->hwaddr[1]; netif->hwaddr[4] = p_gnetif->hwaddr[4];
  netif->hwaddr[2] = p_gnetif->hwaddr[2]; netif->hwaddr[5] = p_gnetif->hwaddr[5];
  //мемкопи НЕ РАБОТАЕТ! пропадает свясь, нет коннекта!
  //memcpy(p_gnetif->hwaddr, netif->hwaddr, netif->hwaddr_len);

  
  ////////////////////////////////////////////////////////// дописал,вероятно поле было пустым и сваливалось в ERR
  //netif->input = tcpip_input;   //tcpip_input //slipif_rxbyte
  //ip4addr_aton ( &("169.254.191.223"), &(netif->ip_addr) );
  //ip4addr_aton ( &("255.255.255.000"), &(netif->netmask) );
  //netif->linkoutput = low_level_output;
  //netif->link_callback = ethernetif_update_config;
  ////////////////////////////////////////////////////////// дописал,вероятно поле было пустым и сваливалось в ERR

#endif /* LWIP_IPV4 */
#if LWIP_IPV6
  netif->output_ip6 = slipif_output_v6;
#endif /* LWIP_IPV6 */
  netif->mtu = SLIP_MAX_SIZE;

  /* netif->state or netif->num contain the port number */
  if (netif->state != NULL) {
    sio_num = *(u8_t*)netif->state;
  } else {
    sio_num = netif->num;
  }
  /* Try to open the serial port. */
  priv->sd = sio_open(sio_num);
  if (!priv->sd) {
    /* Opening the serial port failed. */
    mem_free(priv);
    return ERR_IF;
  }

  /* Initialize private data */
  priv->p = NULL;
  priv->q = NULL;
  priv->state = SLIP_RECV_NORMAL;
  priv->i = 0;
  priv->recved = 0;
#if SLIP_RX_FROM_ISR
  priv->rxpackets = NULL;
#endif

  netif->state = priv;

  /* initialize the snmp variables and counters inside the struct netif / Инициализируйте переменные SNMP и счетчики внутри ntuct netif */
  MIB2_INIT_NETIF(netif, snmp_ifType_slip, SLIP_SIO_SPEED(priv->sd));

#if SLIP_USE_RX_THREAD
  /* Create a thread to poll the serial line. / Создайте поток, чтобы опросить последовательную линию. */
  sys_thread_new(SLIPIF_THREAD_NAME, slipif_loop_thread, netif,
    SLIPIF_THREAD_STACKSIZE, 24); //SLIPIF_THREAD_PRIO
#endif /* SLIP_USE_RX_THREAD */
  return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////// функция ОПРОСА UART в общем цикле (цикле деф задачи)
/**
 * Polls the serial device and feeds the IP layer with incoming packets.
 * Опросите последовательное устройство и подает уровень IP с входящими пакетами.
 * @param netif The lwip network interface structure for this slipif
 */
void
slipif_poll(struct netif *netif)
{
  u8_t c;
  struct slipif_priv *priv;

  LWIP_ASSERT("netif != NULL", (netif != NULL));
  LWIP_ASSERT("netif->state != NULL", (netif->state != NULL));

  priv = (struct slipif_priv *)netif->state;

  while (sio_tryread(priv->sd, &c, 1) > 0) {    /// вычитывание байтов (по одному) из очереди (а та их получает из УАРТ)
    slipif_rxbyte_input(netif, c);
  }
}

//////////////////////////////////////////////////////////////////////////////// ПЕРЕДАЁТ на IP-уровень входящие пакеты, которые были получены
#if SLIP_RX_FROM_ISR
/**
 * Feeds the IP layer with incoming packets that were receive / Передает на IP-уровень входящие пакеты, которые были получены
 *
 * @param netif The lwip network interface structure for this slipif
 */
void
slipif_process_rxqueue(struct netif *netif)
{
  struct slipif_priv *priv;
  SYS_ARCH_DECL_PROTECT(old_level);

  LWIP_ASSERT("netif != NULL", (netif != NULL));
  LWIP_ASSERT("netif->state != NULL", (netif->state != NULL));

  priv = (struct slipif_priv *)netif->state;

  SYS_ARCH_PROTECT(old_level);
  while (priv->rxpackets != NULL) {
    struct pbuf *p = priv->rxpackets;
#if SLIP_RX_QUEUE
    /* dequeue packet */
    struct pbuf *q = p;
    while ((q->len != q->tot_len) && (q->next != NULL)) {
      q = q->next;
    }
    priv->rxpackets = q->next;                      //////////// вот он пакет, который пришёл из UART !0x2000f268 <memp_memory_PBUF_POOL_base+9152>
    q->next = NULL;
#else /* SLIP_RX_QUEUE */
    priv->rxpackets = NULL;
#endif /* SLIP_RX_QUEUE */
    SYS_ARCH_UNPROTECT(old_level);
    if (netif->input(p, netif) != ERR_OK) {       /////////// Ф-Я
      pbuf_free(p);
    }
    SYS_ARCH_PROTECT(old_level);
  }
}


/** Like slipif_rxbyte, but queues completed packets. / Как slipif_rxbyte, но очереди заполнены пакетами.
 *
 * @param netif The lwip network interface structure for this slipif
 * @param data Received serial byte
 */
static void
slipif_rxbyte_enqueue(struct netif *netif, u8_t data)
{
  struct pbuf *p;
  struct slipif_priv *priv = (struct slipif_priv *)netif->state;
  SYS_ARCH_DECL_PROTECT(old_level);

  p = slipif_rxbyte(netif, data);
  if (p != NULL) {
    SYS_ARCH_PROTECT(old_level);
    if (priv->rxpackets != NULL) {
#if SLIP_RX_QUEUE
      /* queue multiple pbufs */
      struct pbuf *q = p;
      while (q->next != NULL) {
        q = q->next;
      }
      q->next = p;
    } else {
#else /* SLIP_RX_QUEUE */
      pbuf_free(priv->rxpackets);
    }
    {
#endif /* SLIP_RX_QUEUE */
      priv->rxpackets = p;
    }
    SYS_ARCH_UNPROTECT(old_level);
  }
}

/**
 * Process a received byte, completed packets are put on a queue that is
 * fed into IP through slipif_process_rxqueue().
 * 
 * Обрабатывать полученный байт, заполненные пакеты помещают в очередь, которая
 * Формируется в IP через slipif_process_rxqueue ().
 *
 * This function can be called from ISR if SYS_LIGHTWEIGHT_PROT is enabled.
 *
 * @param netif The lwip network interface structure for this slipif
 * @param data received character
 */
void
slipif_received_byte(struct netif *netif, u8_t data)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));
  LWIP_ASSERT("netif->state != NULL", (netif->state != NULL));
  slipif_rxbyte_enqueue(netif, data);
}

/**
 * Process multiple received byte, completed packets are put on a queue that is
 * fed into IP through slipif_process_rxqueue().
 * 
 * Процесс несколько полученных байтов, заполненные пакеты помещают в очередь, которая
 * Формируется в IP через slipif_process_rxqueue ().
 *
 * This function can be called from ISR if SYS_LIGHTWEIGHT_PROT is enabled.
 *
 * @param netif The lwip network interface structure for this slipif
 * @param data received character
 * @param len Number of received characters
 */
void
slipif_received_bytes(struct netif *netif, u8_t *data, u8_t len)
{
  u8_t i;
  u8_t *rxdata = data;
  LWIP_ASSERT("netif != NULL", (netif != NULL));
  LWIP_ASSERT("netif->state != NULL", (netif->state != NULL));

  for (i = 0; i < len; i++, rxdata++) {
    slipif_rxbyte_enqueue(netif, *rxdata);
  }
}
#endif /* SLIP_RX_FROM_ISR */
