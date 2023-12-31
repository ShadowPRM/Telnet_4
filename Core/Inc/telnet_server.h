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
#ifndef _TELNET_H_
#define _TELNET_H_

#include "FreeRTOS.h"
#include "stream_buffer.h"

#include "semphr.h"

// LwIP includes
#include "lwip/api.h"

#define TELNET_BUFF_SIZE 1024

typedef void (*telnetCallBack)(uint8_t *data, uint16_t len);

typedef struct {
    telnetCallBack receiver;
    telnetCallBack command;
    telnetCallBack begin;
    telnetCallBack end;
} telnetCallBacksSt;

typedef struct
{
    telnetCallBacksSt callback;

	struct netconn *conn;
	struct netconn *client;

    osThreadId_t srv_task_handle;
	osThreadId_t wrt_task_handle;
	osThreadId_t rcv_task_handle;

    SemaphoreHandle_t buff_mutex;    // Output buffer mutex

	enum
	{
		 TELNET_CONN_STATUS_NONE = 0,
		 TELNET_CONN_STATUS_ACCEPTING,
		 TELNET_CONN_STATUS_CONNECTED,
		 TELNET_CONN_STATUS_CLOSING,
		 TELNET_CONN_STATUS_ERROR
	} status;

    uint16_t buff_count;             //Write buffer count
    uint16_t cmd_len;                //command len

	uint16_t tcp_port;               // Port do be listened in this telnet connection

	uint8_t buff[TELNET_BUFF_SIZE];  // Output buffer
	uint8_t cmd_buff[10];            // Command buffer
} telnetDscSt;

/**
 * @brief Create a new instance of telnet server in a defined TCP port
 *
 * @param port               Number of the TCP connection Port
 * @param receiver_callback  Pointer to the receiver callback function.
 * @param command_callback   Exclusive callback to receive the telnet commands from client.
 *                           If defined as NULL commands will be sent to "receiver_callback".
 *                           In this case, user is responsible to filter commands form
 *                           characters.
 */
void telnet_create(uint16_t port, telnetCallBacksSt * user_callback);

/**
 * @brief Sends data  to the client.
 *
 * returns: the amount of bytes actually received by the driver.
 *
 * This functions can be user to send characters or telnet commands.
 */
uint16_t telnet_transmit(uint8_t* data, uint16_t len);



#endif /* _TELNET_H_ */
