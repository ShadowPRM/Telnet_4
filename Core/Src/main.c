/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "lwip.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "telnet_server.h"
#include <string.h>
#include <stdio.h>
#include "api.h"
#include "mcli.h"
#include "slipif.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SIZE_LOGIN 17
#define SIZE_COMM 4
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */
uint8_t preBUFF = 0;
uint8_t preBUFF2 = 0;
extern struct netif slnetif;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USART2_UART_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
/*---------------------------------------------------------------------------*/
/*Comand handlers*/
static int fun_cmd_main (int argc, char ** argv);
static int cmd_quit     (int argc, char ** argv);
static int cmd_help     (int argc, char ** argv);
static int cmd_mode_set (int argc, char ** argv);
static int cmd_temp     (int argc, char ** argv);
static int cmd_dist     (int argc, char ** argv);
static int cmd_cfg_set  (int argc, char ** argv);
static int cmd_cfg_get  (int argc, char ** argv);

//static err_t slipif_output(struct netif *netif, struct pbuf *p);

/*Command descriptors*/
mcli_cmd_st wo_cmd[] = {
    {
        .name = "led",
        .desc = "\"led a b\"\t a-number led(1,2,3or0-all)\tb-1(ON) or 0(OFF)",
        .cmain = fun_cmd_main
    },
    {
        .name = "help",
        .desc = "The command <help> prints other commands description.\r\n Call: help <COMMAND>",
        .cmain = cmd_help
    },
    {
        .name = "quit",
        .desc = "The <quit> halts debug console.",
        .cmain = cmd_quit
    },
    {
        .name = "mode_set",
        .desc = "The command <mode_set> selects next debug mode.\r\n Call: mode_set <N>\r\n N - mode number (2,3,4).",
        .cmain = cmd_mode_set
    },
    {
        .name = "temp",
        .desc = "The commmand <temp> measures and prints current MCU temperature.",
        .cmain = cmd_temp
    },
    {
        .name = "dist",
        .desc = "The command <dist> measures and prints distance.",
        .cmain = cmd_dist
    },
    {
        .name = "cfg_set",
        .desc = "The commmand <cfg_set> enables configuration mode.",
        .cmain = cmd_cfg_set
    },
    {
        .name = "cfg_get",
        .desc = "The command <cfg_get> prints the device configuration.",
        .cmain = cmd_cfg_get
    }
    //WORKER_CFG_CMD /*Device specific command descriptors*/
};
/*---------------------------------------------------------------------------*/
/*argv buffer*/
char * abuf[] = {0,0,0,0,0};


/**
  * @brief  Объявление для формирования структуры типа mcli_shell_st.
  *
  * @note   define MCLI_SHELL_DECL(shell, cmd, argv) 
  *         mcli_shell_st  shell = {cmd,    argv,    sizeof(cmd)/sizeof(mcli_cmd_st),        sizeof(argv)/sizeof(char *)};
  *                     wo_shell = {wo_cmd, abuf,    sizeof(wo_cmd)/sizeof(mcli_cmd_st),    sizeof(abuf)/sizeof(char *)};
  *                     wo_shell = {wo_cmd, abuf,    количество записей массива wo_cmd ,    количество элементов массива abuf};
  *
  * @param  tst_shell название структуры
  * @param  wo_cmd структура из массивов типа mcli_cmd_st с искомой командой(.name), описанием и указателем на функцию-обработчик команды
  * @param  abuf указатель на массив в котором временно хранятся указатели на команду и опции после парсинга входящих данных 
  * 
  * @retval None
  */
MCLI_SHELL_DECL(wo_shell, wo_cmd, abuf);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

typedef struct  
{
  char name[SIZE_LOGIN];
  char pas[SIZE_LOGIN];
  char etap;
}tn_user;

tn_user tn_client = {{0,}, {0,}, 0};
tn_user tn_admin = {"admin", "admin", 0};
tn_user tn_vadim = {"vadim", "qwerty", 0};


/*
static int16_t comUserPars(uint8_t* buffCom, uint16_t lenCom){
  struct comUser prishli;
  uint16_t posSpace[8]={0,};
  uint16_t countB=0;
  uint16_t countS=0;
  //uint8_t buffPars[]=buffCom;
  char bufPars[64];
  uint8_t lenbufPars;

    for (countB=1; countB<lenCom; countB++){
      if (buffCom[countB]==' '){posSpace[countS++]=countB;}
    }
    strncpy(prishli.com1, (char*)buffCom, posSpace[0]);
    strncpy(prishli.com2, (char*)(buffCom+posSpace[0]+1), (lenCom-posSpace[0]-1));
    lenbufPars = sprintf(bufPars, "Com1:%s|Com2:%s\r\n", prishli.com1, prishli.com2);
    telnet_transmit((uint8_t*)(bufPars), lenbufPars);
    osDelay(5);
    return (int16_t)(countS);
}*/

static void (TNreceiver_callback)( uint8_t* buff, uint16_t len ){
  //В логине и пароле используются символы латиницы и цифры
  //но проверяется только первый символ
  //хорошо бы проверять все символы!
  char bufTN[64];
  uint8_t lenbufTN;
  uint8_t counti=0;
  uint8_t* buff2;
  uint8_t re_buff[len+1];
  
  switch (tn_client.etap){
    case 0:
      telnet_transmit((uint8_t*)("User Name> "), 11);
      tn_client.etap=1;
      break;
  
    case 1: //сюда заходит по имени
      if (buff[0]<0x30) {tn_client.etap=1;} //если не имя, ждём ещё раз
      else {
        memset(tn_client.name,'\0',SIZE_LOGIN); //очистка
        memcpy(tn_client.name, buff, len);
        tn_client.etap=2;
      }
      break;
    
    case 2: //сюда заходит по \r\n
      telnet_transmit((uint8_t*)("User pasw> "), 11);
      tn_client.etap=3;
      break;
  
    case 3: //заходит по паролю
      if (buff[0]<0x30) {tn_client.etap=3;} //если не пароль, ждём ещё раз
      else {
        memset(tn_client.pas,'\0', SIZE_LOGIN);
        memcpy(tn_client.pas, buff, len);
        tn_client.etap=4;
      }
      break;
  
    case 4: //сюда заходит по \r\n
      //сравнение введённого логина с заложеным
      if (!( (memcmp(tn_client.name, tn_admin.name,(SIZE_LOGIN-1)))||(memcmp(tn_client.pas, tn_admin.pas,(SIZE_LOGIN-1))) ) ) {
        lenbufTN = sprintf(bufTN, "\r\nHi, %s! Enter the Command...\r\n", tn_client.name);
        telnet_transmit((uint8_t*)(bufTN), lenbufTN);
        tn_client.etap=5;
      }
      else {
        lenbufTN = sprintf(bufTN, "\r\nIdi nah, %s!!! Press to Enter...\r\n", tn_client.name);
        telnet_transmit((uint8_t*)(bufTN), lenbufTN);
        tn_client.etap=0;
      }
      break;
  
    case 5:
      buff2 = buff;
      while (len--) {re_buff[counti++]=*(buff++);}
      re_buff[len]='\0';

      if (buff2[0]<0x30) {tn_client.etap=5;}
      else {
        //printf("                  \nResult is: %d\n",   mcli_shell_parse(&tst_shell, tst_cmd_str2, sizeof(tst_cmd_str2)));
        lenbufTN = sprintf(bufTN,"------------\r\nResult is: %d\r\n------------\r\n", mcli_shell_parse(&wo_shell, (char*)re_buff, len+1));
        telnet_transmit((uint8_t*)(bufTN), lenbufTN);
      }
      break;

    default:
      break;
  }
}

static void (TNcommand_callback) ( uint8_t* cmd,  uint16_t len ){
}

static void (TNbegin_callback) ( uint8_t* cmd,  uint16_t len ){
  char bufTN[64];
  uint8_t lenbufTN;
  /*
  uint8_t counti=0;
  // тут сброс логина ВРЕМЕННО, т.к. существует опастность что вновь подключившийся(сразу отвалится) обнулит ЛОГ�?Н
  while (counti++ != (SIZE_LOGIN-1)) {tn_client.name[counti]=0; tn_client.pas[counti]=0;}
  tn_client.etap=0;
  */
  lenbufTN = sprintf(bufTN, "Hi user! Press to Enter...\r\n");
  telnet_transmit((uint8_t*)(bufTN), lenbufTN);
}

static void (TNend_callback) ( uint8_t* cmd,  uint16_t len ){
  //char bufTN[64];
  //uint8_t lenbufTN;
  uint8_t counti=0;
  // тут сброс логина
  while (counti++ != (SIZE_LOGIN-1)) {tn_client.name[counti]=0; tn_client.pas[counti]=0;}
  tn_client.etap=0;
  //lenbufTN = sprintf(bufTN, "Poka!\r\n");
  //telnet_transmit((uint8_t*)(bufTN), lenbufTN);
}

telnetCallBacksSt funcCB = {
  TNreceiver_callback,
  TNcommand_callback,
  TNbegin_callback,
  TNend_callback
};

/*===========================================================================*/
/*Command handlers*/
static int fun_cmd_main(int argc, char ** argv){
    char bufPars[64];
    uint8_t lenbufPars;

    if (*argv[1]=='1') {HAL_GPIO_WritePin(LD3_GPIO_Port, LD1_Pin, (GPIO_PinState)(*argv[2] - 0x30) );}
    if (*argv[1]=='2') {HAL_GPIO_WritePin(LD3_GPIO_Port, LD2_Pin, (GPIO_PinState)(*argv[2] - 0x30) );}
    if (*argv[1]=='3') {HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, (GPIO_PinState)(*argv[2] - 0x30) );}
    if (*argv[1]=='0') {
      HAL_GPIO_WritePin(LD3_GPIO_Port, LD1_Pin, (GPIO_PinState)(*argv[2] - 0x30) );
      HAL_GPIO_WritePin(LD3_GPIO_Port, LD2_Pin, (GPIO_PinState)(*argv[2] - 0x30) );
      HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, (GPIO_PinState)(*argv[2] - 0x30) );
      }
    while (argc--){
        lenbufPars = sprintf(bufPars,"%s\r\n", *argv++);
        telnet_transmit((uint8_t*)(bufPars), lenbufPars);
    }
    return 0;
}
/*Quit the console*/
static int cmd_quit(int argc, char ** argv){
    char bufPars[64];
    uint8_t lenbufPars;
    uint8_t counti=0;

    lenbufPars = sprintf(bufPars,"Halting the debug console!\r\n");
    telnet_transmit((uint8_t*)(bufPars), lenbufPars);
    
    while (counti++ != (SIZE_LOGIN-1)) {tn_client.name[counti]=0; tn_client.pas[counti]=0;}// тут сброс логина
    tn_client.etap=0;

    return 0;
}
/*Help*/
static int cmd_help(int argc, char ** argv){
    int i;
    char bufPars[128];
    uint8_t lenbufPars;

    if (2 != argc){   //команда из одной литерации, я так понмаю... можно навероно так '>'
        /*Default behaviour*/
        //lenbufPars = sprintf(bufPars,"%s\r\n", wo_shell.cmd[0].desc);
        //telnet_transmit((uint8_t*)(bufPars), lenbufPars);
        lenbufPars = sprintf(bufPars,"=====Command list:\r\n");
        telnet_transmit((uint8_t*)(bufPars), lenbufPars);
        for (i = 0; i < wo_shell.csz; i++){
            lenbufPars = sprintf(bufPars,"%s\r\n", wo_shell.cmd[i].name);
            telnet_transmit((uint8_t*)(bufPars), lenbufPars);
        }
        lenbufPars = sprintf(bufPars,"=====Call description: help <COMMAND>:\r\n");
        telnet_transmit((uint8_t*)(bufPars), lenbufPars);
        return 0;
    }

    for (i = 0; i < wo_shell.csz; i++){
        const mcli_cmd_st * cmd;

        cmd = wo_shell.cmd + i;
        if (0 == mcli_strcmp(
                    cmd->name,
                    wo_shell.argv[1],
                    //mcli_strlen(wo_shell.argv[1], sizeof(wo_data_buf) - (wo_shell.argv[1] - wo_data_buf)) //
                    strlen(wo_shell.argv[1])
                ))
        {
            /*Found the command, will print help string*/
            lenbufPars = sprintf(bufPars,"=%s\r\n", cmd->desc);
            telnet_transmit((uint8_t*)(bufPars), lenbufPars);
            return 0;
        }
    }
    lenbufPars = sprintf(bufPars,"Help: error: command not found!\r\n");
    telnet_transmit((uint8_t*)(bufPars), lenbufPars);
    return 0;
}
int cmd_mode_set (int argc, char ** argv){return 0;}
int cmd_temp     (int argc, char ** argv){return 0;}
int cmd_dist     (int argc, char ** argv){return 0;}
int cmd_cfg_set  (int argc, char ** argv){return 0;}
int cmd_cfg_get  (int argc, char ** argv){return 0;}


//uint32_t errorUartSR = 0;
//#define FLAG_ERROR_QUEUE        (1 << 16)
//extern QueueHandle_t queueUART = NULL; ///очередь приемного буфера QueueHandle_t - тип - дескриптор очереди

// /// @brief Калбэк по завершению приёма
// void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
//   if (huart==&huart3){
//     uint8_t data = (uint8_t)preBUFF; //считываем
//     HAL_UART_Receive_IT(&huart3, &preBUFF, 1); //запуск следующего приёма
//     if (queueUART != NULL) //если у меня есть очередь
//       {
//         /// тут почемуто в аргумент Таймаут вставлен pdTRUE (логическая 1 или просто 1)
//         /// используется специальная ф-я SendFromISR - для работы в прерывании
//         if(xQueueSendFromISR(queueUART, (void *)(&data), pdTRUE) != pdTRUE)    //если есть очередь то кладу информацию в нее
//           {
//             //почему то не получилось положить в очередь
//             errorUartSR |= FLAG_ERROR_QUEUE;
//           }
//       }
//     else {;} /// если нет очереди
//   }
// }

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART3_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_UART_Receive_IT(&huart3, &preBUFF, 1);
  HAL_UART_Receive_IT(&huart2, &preBUFF2, 1);
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 240;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 5;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD1_Pin|LD3_Pin|LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : USER_Btn_Pin */
  GPIO_InitStruct.Pin = USER_Btn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USER_Btn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD1_Pin LD3_Pin LD2_Pin */
  GPIO_InitStruct.Pin = LD1_Pin|LD3_Pin|LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */
extern struct netif gnetif;
//struct netif slnetif;

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for LWIP */
  MX_LWIP_Init();
  /* USER CODE BEGIN 5 */
  //char buf_uart [64];

  //HAL_UART_Transmit(&huart3, (uint8_t*)"LWIP comlite!\r\n", 15, 10);
  //sprintf(buf_uart, "My ip: %s\r\n", ip4addr_ntoa(&gnetif.ip_addr));
  //HAL_UART_Transmit(&huart3, (uint8_t*)buf_uart, strlen(buf_uart), 10);

  //telnet_create(23, &funcCB);
  //slnetif = gnetif;
  slipif_init(&gnetif);
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
  
  /* Infinite loop */
  for(;;)
  {
    //slipif_poll(&gnetif);
    //gnetif->input(); - завершённый пакет
    //slipif_process_rxqueue(&gnetif);
    //osDelay(1);
  }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM4 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM4) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
