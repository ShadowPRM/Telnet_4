#include "lwip/sio.h"
//#include "uart.h"
#include "netif/slipif.h"
#include "common.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"


extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart2;

#define numUART huart2

void task_rrintErrorUart(void *p);

QueueHandle_t queueUART = NULL; ///очередь приемного буфера QueueHandle_t - тип - дескриптор очереди

////////////////////////////////////////// 
////////////////////////////////////////// 

/// @brief фу-я: инициализации UART
/// создание ОЧЕРЕДИ (сохранение дескриптора, через которого обращаются к очереди)
/// и создание задачи "task_rrintErrorUart"
/// @param devnum 
/// @return 
sio_fd_t sio_open(u8_t devnum)  
{
    LWIP_UNUSED_ARG(devnum);
    //setRxInterupt(UART_SLIP, true);
    //configureUart(APBPERF_SPEED, UART_SLIP_SPEED, UART_SLIP);
    LWIP_DEBUGF(SLIP_DEBUG, ("slipif_init:speed UART sleep = %"U32_F" \n", UART_SLIP_SPEED));

      /// инициализация очериди для приема (SIZE_QUEUE_RX_IN_SIO_C - длина очереди/максимальное кол-во элементов, 1 - размер в байтах каждого элемента)
      /// возвращает дескриптор созданой очереди
    queueUART = xQueueCreate(/*configQUEUE_REGISTRY_SIZE*/SIZE_QUEUE_RX_IN_SIO_C, 1);
    LWIP_ASSERT("Error initialization queue for UART!!!\n(whit out queue ferst 5 bytes[2-7] don't have time to process)\n", queueUART != NULL);
      /// создание задачи: task_rrintErrorUart-имя функ, ""-имя задачи(не использ ОС), 64-размер стека выделяемое задаче,
      /// NULL-значение, передаваемое в задачу(видимо не обязательное), 1-приоритет задачи, NULL-передаёт дескриптор создаваемой задачи
      /// возвращает pdPASS или pdFAIL тип BaseType_t
    
    //xTaskCreate(task_rrintErrorUart, "print error uart", 64, NULL, 1, NULL);

    return (sio_fd_t)UART_SLIP_SPEED;
}

/// @brief ф-я ОТПРАВКИ u8_t c в УАРТ fd ///////////////////////////////////////////////////
/// @param c отправляемый символ
/// @param fd параметры УАРТа (номер)
void sio_send(u8_t c, sio_fd_t fd)
{
    LWIP_UNUSED_ARG(fd);
    //writeUartData(c, UART_SLIP);
    HAL_UART_Transmit(&huart3, &c, 1, 1);
    HAL_UART_Transmit(&huart2, &c, 1, 1);
}

/// @brief фун-я СЧИТЫВАНИЯ байта из очереди ///////////////////////////////////////////////////
/// @param fd параметры УАРТа (номер)
/// @return считаный байт (u8_t)
u8_t sio_recv(sio_fd_t fd)
{
    LWIP_UNUSED_ARG(fd);    //хз что это!
    uint8_t dataFromUART = 0;   /// просто инициализированная "нулём" переменная / (uint8_t) NULL
        /// ПОЛУЧЕНИЕ данных из очереди "queueUART", в переменную(но может быть и массив и структура) по указателю "&dataFromUART", с таймаутом "portMAX_DELAY"
        /// и очищает прочитанную ячейку в очереди (в данном случае ячейка одна)
        /// с проверкой на pdPASS (данные были успешно получены)
    while (xQueueReceive(queueUART, (void *)&dataFromUART, portMAX_DELAY) != pdTRUE) {;}

    return dataFromUART;
}

/// @brief фун-я СЧИТЫВАНИЯ байтов из очереди ///////////////////////////////////////////////////
/// @param fd параметры УАРТа (номер)
/// @param data указатель на массив
/// @param len длина принимаемого буфера
/// @return их количества (u32_t)
u32_t sio_read(sio_fd_t fd, u8_t *data, u32_t len)
{
    LWIP_UNUSED_ARG(fd);

    for(u32_t i = 0; i < len; i++)
        data[i] = sio_recv(fd);     //вычитывания из очереди

    return len;
}

/// @brief фун-я СЧИТЫВАНИЯ байтов из очереди (прослойка или что... хз) ///////////////////////////////////////////////////
/// @param fd параметры УАРТа (номер)
/// @param data указатель на массив
/// @param len длина принимаемого буфера
/// @return их количества (u32_t)
u32_t sio_tryread(sio_fd_t fd, u8_t *data, u32_t len)
{
    return sio_read(fd, data, len);
}

/// @brief фун-я ОТПРАВКИ байтов ///////////////////////////////////////////////////
/// @param fd параметры УАРТа (номер)
/// @param data указатель на массив
/// @param len длина отправляемого массива
/// @return их количества (u32_t)
u32_t sio_write(sio_fd_t fd, u8_t *data, u32_t len)
{
    LWIP_UNUSED_ARG(fd);

    for(u32_t i = 0; i < len; i++)
        sio_send(data[i], fd);

    return len;
}


uint32_t errorUartSR = 0;
#define FLAG_ERROR_QUEUE        (1 << 16)
extern uint8_t preBUFF;
extern uint8_t preBUFF2;

/// @brief Калбэк по завершению приёма
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
  if (huart==&numUART){
    uint8_t data = (uint8_t)preBUFF2; //сохранённый байт из UART
    HAL_UART_Receive_IT(&numUART, &preBUFF2, 1); //запуск следующего приёма
    if (queueUART != NULL) //если у меня есть очередь
      {
        /// тут почемуто в аргумент Таймаут вставлен pdTRUE (логическая 1 или просто 1)
        /// используется специальная ф-я SendFromISR - для работы в прерывании
        if(xQueueSendFromISR(queueUART, (void *)(&data), (BaseType_t)pdTRUE) != pdTRUE)    //если есть очередь то кладу информацию в нее
          {
            //почему то не получилось положить в очередь
            errorUartSR |= FLAG_ERROR_QUEUE;
          }
      }
    else {;} /// если нет очереди
  }
}


/// @brief прерывание по приёму (правильное, со всеми проверками) и
/// отправка в очередь полученного байта "uint8_t data" ///////////////////////////////////////////////////
/// @param  
/*void USART3_IRQHandler(void)
{
    if (UART_SLIP->SR & USART_SR_RXNE)                     // Прерывание по приему?
    {
        if ((UART_SLIP->SR & (USART_SR_NE | USART_SR_FE | USART_SR_PE | USART_SR_ORE)) == 0) //проверяем нет ли ошибок
        {
            uint8_t data = (uint8_t)(UART_SLIP->DR); //считываем данные в буфер, инкрементируя хвост буфера
            if (queueUART != NULL) //если у меня есть очередь
            {
                /// тут почемуто в аргумент Таймаут вставлен pdTRUE (логическая 1 или просто 1)
                /// используется специальная ф-я SendFromISR - для работы в прерывании
                if(xQueueSendFromISR(queueUART, (void *)(&data), pdTRUE) != pdTRUE)    //если есть очередь то кладу информацию в нее
                {
                    //почему то не получилось положить в очередь
                    errorUartSR |= FLAG_ERROR_QUEUE;
                }
            }
            else {;} /// если нет очереди
        }
        else
        {
            errorUartSR |= UART_SLIP->SR;
            (void)UART_SLIP->DR;                                 // вычитываем данные и на всякий случай
            UART_SLIP->SR &= ~USART_SR_RXNE;               // ещё и сбрасываем флаг прерывания
        }

        /// по пиему данных надо делать чтобы отправлялись в функцию в uart.c  ?????????????????????????????
    }
}*/


/// @brief задача проверяющая состояние УАРТА ///////////////////////////////////////////////////
/// @param p 
void task_rrintErrorUart(void *p)
{
    (void)p;

    while(1)
    {
        if(errorUartSR != 0)
        {
            if(errorUartSR & FLAG_ERROR_QUEUE)
                ///xprintf("UART ERROR:queue can't send from ISR\n");
            if(errorUartSR & USART_SR_ORE)
                ///xprintf("UART ERROR:Rx bufer overflow\n");
            //... other error

            //...

            errorUartSR = 0;
        }
        vTaskDelay(100 / portTICK_RATE_MS);
    }
}

