/*
 * MIT License
 *
 * Copyright (c) 2022 André Cascadan and Bruno Augusto Casu
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 * This file is part of the lwIP based telnet server.
 *
 */

/* 1 TAB = 4 Spaces */
// FreeRTOS includes
#include "cmsis_os.h"  // TODO: Решите, какой API использовать: Freertos или CMSIS.Здесь они смешаны.
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "stream_buffer.h"

// LwIP includes
#include "lwip/api.h"
#include "main.h"

#include "telnet_server.h"

// Текущая реализация позволяет только одно одно соединение Telnet (один экземпляр).
static telnetDscSt telnet_instance;

/*
 *  Период опроса буфера TX = 10 мс
 *
 *  Этот параметр в сочетании с размером буфера 1024 байта
 *  приведет к средней пропускной способности 100 кбайт в секунду.
 */
static const TickType_t tx_cycle_period = pdMS_TO_TICKS(10);

// Process input bytes
static void process_incoming_bytes(uint8_t *data, int data_len);

// Callback for netconn interface
static void netconn_cb(struct netconn *conn, enum netconn_evt evt, u16_t len);

// Task and attributes
static const osThreadAttr_t srv_task_attributes =
{
    .name = "TelnetServerTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_size = 256 * 4
};

static const osThreadAttr_t wrt_task_attributes =
{
    .name = "TelnetWrtTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_size = 256 * 4
};

static const osThreadAttr_t rcv_task_attributes =
{
    .name = "TelnetRcvTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_size = 256 * 4
};

// Task functions
static void srv_task(void *arg);
static void wrt_task(void *arg);
static void rcv_task(void *arg);

void telnet_create(uint16_t port, telnetCallBacksSt * user_callback)
{
    // Stores the port of the TCP connection to the global array
    telnet_instance.tcp_port = port;

    // Stores the callback pointers
    telnet_instance.callback = *user_callback;

    // Start server task
    telnet_instance.srv_task_handle = osThreadNew(srv_task, NULL, &srv_task_attributes);
}

/*
 * Netconn callback
 *
 * Этот проект использует NetConn с обратным вызовом для управления событиями Open/Close
 */
void netconn_cb(struct netconn *conn, enum netconn_evt evt, u16_t len)
{
    if (evt == NETCONN_EVT_RCVPLUS)
    {
        if (len == 0) // len = 0 means the connections is being opened or closed by the client
        {
            // Switch the connection status according to the current one
            if (telnet_instance.status == TELNET_CONN_STATUS_ACCEPTING)
            {
                telnet_instance.status = TELNET_CONN_STATUS_CONNECTED;
                telnet_instance.callback.begin(NULL, 0);
            }
            else
            {
                if (telnet_instance.client == conn)
                {
                    telnet_instance.status = TELNET_CONN_STATUS_CLOSING;
                }
            }
        }
    }
}

/*
Сервер, ждёт входящих запросов
 */


static void srv_task(void *arg)
{
    struct netconn * client;
    err_t err;

    // Initializes data buffer
    telnet_instance.buff_mutex = xSemaphoreCreateMutex();
    telnet_instance.buff_count = 0;

    // Initializes command buffer
    telnet_instance.cmd_len = 0;

    // No client right now
    telnet_instance.client = NULL;

    // Starts local listening
    telnet_instance.conn = netconn_new_with_callback(NETCONN_TCP, netconn_cb);
    if (telnet_instance.conn == NULL)
    {
        telnet_instance.status = TELNET_CONN_STATUS_ERROR;
        return;
    }

    err = netconn_bind(telnet_instance.conn, NULL, telnet_instance.tcp_port);
    if (err != ERR_OK)
    {
        telnet_instance.status = TELNET_CONN_STATUS_ERROR;
        return;
    }

    netconn_listen(telnet_instance.conn);

    telnet_instance.wrt_task_handle = osThreadNew(wrt_task, NULL, &wrt_task_attributes);
    telnet_instance.rcv_task_handle = osThreadNew(rcv_task, NULL, &rcv_task_attributes);

    /*
     * Accept loop
     * Stays waiting for a connection. If connection breaks, it waits for a new one.
     */
    telnet_instance.status = TELNET_CONN_STATUS_ACCEPTING;
    for (;;)
    {
        err = netconn_accept(telnet_instance.conn, &client);
        if (err == ERR_OK)
        {
            if (NULL == telnet_instance.client)
            {
                telnet_instance.client = client;
            }
            else
            {
                /*We can serve only one client*/
                netconn_close(client);
                netconn_delete(client);
            }
        }
    }
}

/*
 * TX task ()
 *
 * Эта задача ждет, пока байты будут переданы клиенту.
 *
 * Соединение/отключение также управляется этой задачей.
 *
 */
extern struct netif slnetif;
//struct netif* p_slnetif;
//    p_slnetif = &slnetif;

static void wrt_task(void *arg)
{
    struct netconn * client;
    
    
    //err_t err;

    for (;;)
    {
        client = telnet_instance.client;
        if (NULL == client)
        {
            vTaskDelay(tx_cycle_period);
            continue;
        }

        /* We've got a client! */
        for (;;)
        {
                xSemaphoreTake(telnet_instance.buff_mutex, portMAX_DELAY);
                if ((telnet_instance.status == TELNET_CONN_STATUS_CONNECTED) && (telnet_instance.buff_count > 0))
                {
                    //sio_write(NULL, telnet_instance.buff, telnet_instance.buff_count);
                    //netconn_write(client, telnet_instance.buff, telnet_instance.buff_count, NETCONN_COPY);
                    //if (p_slnetif. (p_slnetif, telnet_instance.buff, NULL) != 0){;} 
                    //slipif_output(struct netif *netif, struct pbuf *p);
                    //slipif_output_v4(p_slnetif, telnet_instance.buff, NULL);
                    telnet_instance.buff_count = 0;
                }
                xSemaphoreGive(telnet_instance.buff_mutex);

                if (telnet_instance.status == TELNET_CONN_STATUS_CLOSING)
                {
                    break;
                }

                vTaskDelay(tx_cycle_period);
        }

        telnet_instance.callback.end(NULL, 0);
        telnet_instance.client = NULL;
        telnet_instance.status = TELNET_CONN_STATUS_ACCEPTING;
        netconn_close(client);
        netconn_delete(client);
    }
}

/*
 * TX task ()
 *
 * Эта задача ждет входных байтов от клиента.
 */
static void rcv_task(void *arg)
{
    struct netbuf *rx_netbuf;
    void *rx_data;
    uint16_t rx_data_len;
    err_t recv_err;

    for(;;)
    {
        if ((telnet_instance.status != TELNET_CONN_STATUS_CONNECTED) || (NULL == telnet_instance.client))
        {
            vTaskDelay(100); // * ничего не делай* задержка, если нет соединения.
        }
        else
        {
            // Итеративно считывает все доступные данные
            recv_err = netconn_recv(telnet_instance.client, &rx_netbuf);
            if (recv_err == ERR_OK)
            {
                // Navigate trough netbuffs until dump all data
                do
                {
                    netbuf_data(rx_netbuf, &rx_data, &rx_data_len);
                    //process_incoming_bytes(rx_data, rx_data_len);
                    ///////////////////////////////////////////////
                    sio_write(NULL, &rx_data, rx_data_len);
                    //slipif_output_v4(&slnetif, struct pbuf *p, const ip4_addr_t *ipaddr);
                    
                    //sio_write(NULL, Tx_Bu, sizeof(Tx_Buff[0]));
                    ///////////////////////////////////////////////
                }
                while (netbuf_next(rx_netbuf) >= 0);

                netbuf_delete(rx_netbuf);
            }
        }
    }
}

/*
 * Transmit bytes
 *
 * Эти функции вызываются пользователем для отправки байтов клиенту.
 *
 * Байты команд Telnet также могут быть отправлены через эти функции.
 */
uint16_t telnet_transmit(uint8_t* data, uint16_t len)
{
    //TODO: Feature: add flexibility for ISR context // Функция: Добавьте гибкость для контекста ISR

    int sent = 0;

    if (telnet_instance.buff_mutex == NULL)
    {
        return 0;
    }

    if (telnet_instance.status != TELNET_CONN_STATUS_CONNECTED)
    {
        return 0;
    }

    // Iterates until all bytes is fed to the buffer
    do
    {
        xSemaphoreTake(telnet_instance.buff_mutex, portMAX_DELAY);
        while ((len > 0) && (telnet_instance.buff_count < TELNET_BUFF_SIZE))
        {
            telnet_instance.buff[telnet_instance.buff_count] = data[sent];
            ++telnet_instance.buff_count;
            ++sent;
            --len;
        }
        xSemaphoreGive(telnet_instance.buff_mutex);

        if (len > 0)
        {
            vTaskDelay(tx_cycle_period); // Wait for one TX cycle before going to next iteration
        }

    }
    while (len > 0);

    return sent;
}

/*
 * Process input bytes
 *
 * Отфильтровать команды Telnet из обычных символов и вызывает
 * Правильный обратный вызов пользователя.
 */
static void process_incoming_bytes(uint8_t *data, int data_len)
{
    const uint8_t IAC  = 255; // See RFC 854 for details
    const uint8_t WILL = 251;
    const uint8_t DONT = 254;
    //const uint8_t SB   = 254; TODO: Parse Subnegotiation commands.

    uint8_t* pbuf_payload = data;
    uint16_t pbuf_len     = data_len;

    /*
     * Если обратный вызов команды не определен, все байты отправляются на обратный вызов получателя.
     *
     * В этом случае пользователь несет ответственность за отделение их от символов.
     */
    if (telnet_instance.callback.command == NULL)
    {
        telnet_instance.callback.receiver(pbuf_payload, pbuf_len);
    }
    else
    {
        //If command callback IS defined, all bytes are filtered out from the characters
        uint16_t char_offset   = 0;
        uint16_t char_ctr      = 0;

        for (int i = 0; i < pbuf_len; i++)
        {
            // Counting characters: Command buffer is empty and IAC not found
            if ((telnet_instance.cmd_len == 0) && (pbuf_payload[i] != IAC))
            {
                ++char_ctr;
                // Counting command bytes
            }
            else
            {
                // Нашел IAC с командным буфером пустым:
                if ((telnet_instance.cmd_len == 0) && (pbuf_payload[i] == IAC))
                {
                    // Обработайте символы, найденные до этого момента
                    if (char_ctr != 0)
                    {
                        telnet_instance.callback.receiver(&pbuf_payload[char_offset], char_ctr);
                    }

                    telnet_instance.cmd_buff[telnet_instance.cmd_len] = pbuf_payload[i];
                    telnet_instance.cmd_len++;
                }
                else if ((telnet_instance.cmd_len == 1) && (pbuf_payload[i] >= WILL) && (pbuf_payload[i] <= DONT))
                {
                    telnet_instance.cmd_buff[telnet_instance.cmd_len] = pbuf_payload[i];
                    telnet_instance.cmd_len++;
                }
                else if (telnet_instance.cmd_len == 2 && (pbuf_payload[1] >= WILL) && (pbuf_payload[1] <= DONT))
                {
                    telnet_instance.cmd_buff[telnet_instance.cmd_len] = pbuf_payload[i];
                    telnet_instance.cmd_len++;

                    // Process the command
                    telnet_instance.callback.command(telnet_instance.cmd_buff,  telnet_instance.cmd_len);

                    // Restart counting characters. Erase command buffer.
                    telnet_instance.cmd_len = 0;
                    char_offset = i+1;
                    char_ctr = 0;
                }
            }
        }

        // Process the characters found at the end of buffer scanning
        if (char_ctr != 0)
        {
            telnet_instance.callback.receiver(&pbuf_payload[char_offset], char_ctr);
        }
    }
}
