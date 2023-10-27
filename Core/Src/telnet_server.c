/*
 * telnet_server.c
 *
 *  Created on: Oct 19, 2023
 *      Author:
 */


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
static telnet_t telnet_instance;
static telnet_t* instance = &telnet_instance;

/*
 *  Период опроса буфера TX = 10 мс
 *
 *  Этот параметр в сочетании с размером буфера 1024 байта
 *  приведет к средней пропускной способности 100 кбайт в секунду.
 */
static const TickType_t tx_cycle_period = pdMS_TO_TICKS(10);

// Process input bytes
static void process_incoming_bytes(uint8_t *data, int data_len, telnet_t* inst_ptr);

// Callback for netconn interface
static void netconn_cb(struct netconn *conn, enum netconn_evt evt, u16_t len);

// Task and attributes
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
    .stack_size = (256) * 4
};

// Task functions
static void wrt_task(void *arg);
static void rcv_task(void *arg);

void telnet_create(uint16_t port,
                   void (*receiver_callback)(uint8_t* buff, uint16_t len),
                   void (*command_callback)(uint8_t* cmd,  uint16_t len))
{
    err_t err;

    // Stores the callback pointers
    instance->receiver_callback = receiver_callback;
    instance->command_callback  = command_callback;

    // Initializes command buffer size
    instance->cmd_len = 0;

    // Stores the port of the TCP connection to the global array
    instance->tcp_port = port;

    // Starts local listening
    instance->conn = netconn_new_with_callback(NETCONN_TCP, netconn_cb);
    if (instance->conn == NULL)
    {
        return;
    }

    err = netconn_bind(instance->conn, NULL, port);
    if (err != ERR_OK)
    {
        return;
    }

    netconn_listen(instance->conn);

    // No connection still established
    instance->status = TELNET_CONN_STATUS_NONE;

    // Create the accept sentd task
    instance->wrt_task_handle = osThreadNew(wrt_task, NULL, &wrt_task_attributes);
    instance->rcv_task_handle = osThreadNew(rcv_task, NULL, &rcv_task_attributes);
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
            if (instance->status == TELNET_CONN_STATUS_ACCEPTING)
            {
                instance->status = TELNET_CONN_STATUS_CONNECTED;
            }
            else
            {
                if (instance->newconn == conn)
                {
                    instance->status = TELNET_CONN_STATUS_CLOSING;
                }
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
static void wrt_task(void *arg)
{
    err_t accept_err;
    //err_t err;

    // create the buffer to accumulate bytes to be sent
    instance->buff       = (uint8_t *)pvPortMalloc(sizeof(uint8_t) * TELNET_BUFF_SIZE);
    instance->buff_mutex = xSemaphoreCreateMutex();
    instance->buff_count = 0;

    /*
     * Accept loop
     * Stays waiting for a connection. If connection breaks, it waits for a new one.
     */
    for(;;)
    {
        instance->status = TELNET_CONN_STATUS_ACCEPTING;
        accept_err = netconn_accept(instance->conn, &instance->newconn);
        if (accept_err == ERR_OK)
        {
            telnet_transmit((uint8_t*)("Hi user! Press to Enter...\r\n"), 28);
			
            // Transfer loop
            for(;;)
            {
                //netconn_close(instance->conn); // Stop listening.
                vTaskDelay(tx_cycle_period);

                xSemaphoreTake(instance->buff_mutex, portMAX_DELAY);

                // Проверьте статус соединений перед отправкой байтов, если таковые имеются
                if ((instance->status == TELNET_CONN_STATUS_CONNECTED) && (instance->buff_count > 0))
                {
                    netconn_write(instance->newconn, instance->buff, instance->buff_count, NETCONN_COPY);
                }

                instance->buff_count = 0;

                xSemaphoreGive(instance->buff_mutex);

                // Принудительно подключите соединения, если от клиента было обнаружено завершение
                if (instance->status == TELNET_CONN_STATUS_CLOSING)
                {
                    break;
                }
            }

            netconn_close (instance->newconn);
            netconn_delete(instance->newconn);

/*             // Start listening again
            netconn_delete(instance->conn);
            instance->conn = netconn_new_with_callback(NETCONN_TCP, netconn_cb);

            if (instance->conn == NULL)
            {
                return;
            }

            err = netconn_bind(instance->conn, NULL, instance->tcp_port);
            if (err != ERR_OK)
            {
                return;
            }

            netconn_listen(instance->conn); */

            // Никакой связи все еще не установлено
            instance->status = TELNET_CONN_STATUS_NONE;
        }
    
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
        if (instance->status != TELNET_CONN_STATUS_CONNECTED)
        {
            vTaskDelay(100); // * ничего не делай* задержка, если нет соединения.
        }
        else
        {
            // Итеративно считывает все доступные данные
            recv_err = netconn_recv(instance->newconn, &rx_netbuf);
            if (recv_err == ERR_OK)
            {
                // Navigate trough netbuffs until dump all data
                do
                {
                    netbuf_data(rx_netbuf, &rx_data, &rx_data_len);
                    process_incoming_bytes(rx_data, rx_data_len, instance);
                    HAL_GPIO_TogglePin(LD1_GPIO_Port,LD1_Pin);
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

    if (instance->buff_mutex == NULL)
    {
        return 0;
    }

    if (instance->status != TELNET_CONN_STATUS_CONNECTED)
    {
        return 0;
    }

    // Iterates until all bytes is fed to the buffer
    do
    {
        xSemaphoreTake(instance->buff_mutex, portMAX_DELAY);
        while ((len > 0) && (instance->buff_count < TELNET_BUFF_SIZE))
        {
            instance->buff[instance->buff_count] = data[sent];
            ++instance->buff_count;
            ++sent;
            --len;
        }
        xSemaphoreGive(instance->buff_mutex);

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
static void process_incoming_bytes (uint8_t *data, int data_len, telnet_t* inst_ptr)
{
    //по наблюдениям из Шарк: команды состоят из 3 байт, идущих подряд:	0xff 'команда' 'значение клманды'
	const uint8_t IAC  = 255; // See RFC 854 for details
    const uint8_t WILL = 251; //Подкоманда: договориться о размере окна (и другие)
    const uint8_t DONT = 254;
    //const uint8_t SB   = 254; TODO: Parse Subnegotiation commands.
	//253 - Эхо

    uint8_t* pbuf_payload = data;
    uint16_t pbuf_len     = data_len;

    /*
     * Если обратный вызов команды не определен, все байты отправляются на обратный вызов получателя.
     *
     * В этом случае пользователь несет ответственность за отделение их от символов.
     */
    if (inst_ptr->command_callback == NULL)
    {
        inst_ptr->receiver_callback(pbuf_payload, pbuf_len);
    }
    else
    {
        //If command callback IS defined, all bytes are filtered out from the characters
		//Если определяется командный обратный вызов, все байты отфильтрованы из символов
        uint16_t char_offset   = 0;
        uint16_t char_ctr      = 0;

        for (int i = 0; i < pbuf_len; i++)
        {
            // Counting characters: Command buffer is empty and IAC not found
			// подсчет символов: командный буфер пуст, а IAC не найден
            if ((inst_ptr->cmd_len == 0) && (pbuf_payload[i] != IAC))
            {
                ++char_ctr;
                // Counting command bytes
				// подсчет командных байтов
            }
            else
            {
                // Нашел IAC с командным буфером пустым:
                if ((inst_ptr->cmd_len == 0) && (pbuf_payload[i] == IAC))
                {
                    // Обработайте символы, найденные до этого момента
                    if (char_ctr != 0)
                    {
                        inst_ptr->receiver_callback(&pbuf_payload[char_offset], char_ctr);
                    }

                    inst_ptr->cmd_buff[inst_ptr->cmd_len] = pbuf_payload[i];
                    inst_ptr->cmd_len++;
                }
                else if ((inst_ptr->cmd_len == 1) && (pbuf_payload[i] >= WILL) && (pbuf_payload[i] <= DONT))
                {
                    inst_ptr->cmd_buff[inst_ptr->cmd_len] = pbuf_payload[i];
                    inst_ptr->cmd_len++;
                }
                else if (inst_ptr->cmd_len == 2 && (pbuf_payload[1] >= WILL) && (pbuf_payload[1] <= DONT))
                {
                    inst_ptr->cmd_buff[inst_ptr->cmd_len] = pbuf_payload[i];
                    inst_ptr->cmd_len++;

                    // Process the command
                    inst_ptr->command_callback(inst_ptr->cmd_buff,  inst_ptr->cmd_len);

                    // Restart counting characters. Erase command buffer.
                    inst_ptr->cmd_len = 0;
                    char_offset = i+1;
                    char_ctr = 0;
                }
            }
        }

        // Process the characters found at the end of buffer scanning
        if (char_ctr != 0)
        {
            inst_ptr->receiver_callback(&pbuf_payload[char_offset], char_ctr);
        }
    }
}
