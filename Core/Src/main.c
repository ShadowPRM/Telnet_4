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
UART_HandleTypeDef huart3;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */
//////////////////////////////////////////////////////////
//для теста парсинга
char a[] = "mars";
char b[] = "marsian";
char c[] = "earth";
char d[] = "\0";
char e[] = "ololo";

const char d0[] = "";
const char d1[] = " ,_.\"";

char f[] = "   Mars is_not_a \"star\"";

char tst_cmd_str1[] = "rm -rf /";
char tst_cmd_str2[] = "cmd0 a b c";
char tst_cmd_str3[] = "cmd1 ololo ololo";
char tst_cmd_str4[] = "cmd0 a \"b c\" d \"e f\" gg";
char tst_cmd_str5[] = "cmd0 a \"b\"\"c\" d \"e f\" gg";
char tst_cmd_str6[] = "cmd0 a b \"c";
char tst_cmd_str7[] = "cmd1";


//////////////////////////////////////////////////////////

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART3_UART_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
//////////////////////////////////////////////////////////
//для теста парсинга
int tst_cmd_main(int argc, char ** argv){
    char bufPars[64];
    //char comand1[8]= argv;
    //char comand2[8]= argv+1;
  uint8_t lenbufPars;

    if (**argv=='l') {
      HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET);
    }
    while (argc--){
        lenbufPars = sprintf(bufPars,"%s\r\n", *argv++);
        telnet_transmit((uint8_t*)(bufPars), lenbufPars);
    }
    
    return 77;
}

mcli_cmd_st tst_cmd[] = {
    {
        .name = "led1",
        .desc = "Test command 0",
        .cmain = tst_cmd_main
    },
    {
        .name = "led3",
        .desc = "Test command 1",
        .cmain = tst_cmd_main
    }
};

char * abuf[] = {0,0,0,0,0,0,0,0,0,0};

MCLI_SHELL_DECL(tst_shell, tst_cmd, abuf);

//////////////////////////////////////////////////////////
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

struct tn_user 
{
  char name[SIZE_LOGIN];
  char pas[SIZE_LOGIN];
  char etap;
};

struct comUser{
  char com1[SIZE_COMM];
  char com2[SIZE_COMM];
  char com3[SIZE_COMM];
  char com4[SIZE_COMM];
};

struct tn_user tn_client = {{0,}, {0,}, 0};
struct tn_user tn_admin = {"admin", "admin", 0};
struct tn_user tn_vadim = {"vadim", "qwerty", 0};

struct comUser comGpio = {"led","on", "off", "?"};

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
}

static void (TNreceiver_callback)( uint8_t* buff, uint16_t len ){

  //В логине и пароле используются символы латиницы и цифры
  //но проверяется только первый символ
  //хорошо бы проверять все символы!
  char bufTN[64];
  //int16_t res=0;
  uint8_t lenbufTN;
  
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
      if (buff[0]<0x30){tn_client.etap=5;}
      else {
        // int mcli_strlen(a, sizeof(a)) - выводит колво символов в строке а, если оно не больше 2го арг, иначе ошибка, а так же провер, не пустая ли строка а
        // int mcli_strcmp(a, a, sizeof(a)) - сравнивает строки 1й и 2й аргумент и входит ли он в размер 3й арг. Возв: 0-равны и входит,1-не равны,менше 0 -ошибка
        // _mcli_is_in - проверяет символ строки, идентичен ли контрольному символу из строки d1. Возвращ: 1-да, 0-нет.
        // MCLI_STRTOK(pts, d, slim) (mcli_strtok(pts, d, slim, sizeof(d) - 1))
        // int mcli_strtok(char ** pts,const char * d, int slim, int dlen);
        // - находит в строке Арг1 символы строки Арг2 с ограничением длины проверки Арг3
        // - возвращает колво символов литерации, если они входят в ограничение Арг3
        //
   

    lenbufTN = sprintf(bufTN,"\r\nResult is: %d\r\n", mcli_shell_parse(&tst_shell, (char*)buff, sizeof(tst_cmd_str2)));
    telnet_transmit((uint8_t*)(bufTN), lenbufTN);

    //lenbufTN = sprintf(bufTN,"CMP(%s, %s) = %d\r\n", e, c, mcli_strcmp(a, e, sizeof(e)));
    //telnet_transmit((uint8_t*)(bufTN), lenbufTN);

    //lenbufTN = sprintf(bufTN,"STRTOK(%s, d1, 4) = %d\r\n", f, MCLI_STRTOK(&s, d1, 7));
    //telnet_transmit((uint8_t*)(bufTN), lenbufTN);

        //lenbufTN = sprintf(bufTN, "CMP(%s, %s) = %d error\r\n", a, a, mcli_strcmp(a, a, 0));
        //telnet_transmit((uint8_t*)(bufTN), lenbufTN);


            //lenbufTN = sprintf(bufTN, "Size: %d", res);
        //telnet_transmit((uint8_t*)(bufTN), lenbufTN);
        //comUserPars(buff,len);
        // if (res != 0) {
        //   lenbufTN = sprintf(bufTN, "Size: %d", res);
        //   telnet_transmit((uint8_t*)(bufTN), lenbufTN);
        // }
      }
      break;

    default:
      break;
  }

}

static void (TNcommand_callback) ( uint8_t* cmd,  uint16_t len ){
  }
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
  /* USER CODE BEGIN 2 */

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
  char buf_uart [64];

  HAL_UART_Transmit(&huart3, (uint8_t*)"LWIP comlite!\r\n", 15, 10);
  sprintf(buf_uart, "My ip: %s\r\n", ip4addr_ntoa(&gnetif.ip_addr));
  HAL_UART_Transmit(&huart3, (uint8_t*)buf_uart, strlen(buf_uart), 10);

  telnet_create(23, TNreceiver_callback, TNcommand_callback);
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
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
