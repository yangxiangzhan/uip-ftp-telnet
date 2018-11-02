
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  ** This notice applies to any and all portions of this file
  * that are not between comment pairs USER CODE BEGIN and
  * USER CODE END. Other portions of this file, whether 
  * inserted by the user or by software development tools
  * are owned by their respective copyright owners.
  *
  * COPYRIGHT(c) 2018 STMicroelectronics
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *   1. Redistributions of source code must retain the above copyright notice,
  *      this list of conditions and the following disclaimer.
  *   2. Redistributions in binary form must reproduce the above copyright notice,
  *      this list of conditions and the following disclaimer in the documentation
  *      and/or other materials provided with the distribution.
  *   3. Neither the name of STMicroelectronics nor the names of its contributors
  *      may be used to endorse or promote products derived from this software
  *      without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_hal.h"
#include "dma.h"
#include "eth.h"
#include "fatfs.h"
#include "sdio.h"
#include "gpio.h"

/* USER CODE BEGIN Includes */
#include "AtomROS.h"
#include "shell.h"
#include "serial_hal.h"
#include "serial_console.h"

#include "uip.h"
#include "tapdev.h"
#include "timer.h"
#include "tapdev.h"
#include "uip_arp.h"
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

#define BUF ((struct uip_eth_hdr *)&uip_buf[0])
	
uip_ipaddr_t ipaddr;
struct uip_eth_addr ethaddr;
struct timer periodic_timer,arp_timer;


// 控制台图标
const static char acSTM32LOGO[]=
"\r\n\
	 ____  _____  __  __  ____  ____\r\n\
	/ ___||__ __||  \\/  ||___ \\|___ \\COPYRIGHT(c):\r\n\
	\\___ \\  | |  | |\\/| | |__ |/ ___/GoodMorning\r\n\
	|____/  |_|  |_|  |_||____/\\____|2018/09\r\n";



void vShell_GetLocalIp(void * arg)
{
	printk("ip:%d.%d.%d.%d\r\n",uip_hostaddr[0]&0x00ff,uip_hostaddr[0]>>8,uip_hostaddr[1]&0x00ff,uip_hostaddr[1]>>8);
}


void uip_base_init(void)
{
	tapdev_init();
	uip_init();
	uip_arp_init();


	ethaddr.addr[0] = 0x00;
	ethaddr.addr[1] = 0x80;
	ethaddr.addr[2] = 0xE1;
	ethaddr.addr[3] = 0x00;
	ethaddr.addr[4] = 0x00;
	ethaddr.addr[5] = 0x00;
	
	uip_setethaddr(ethaddr);

	uip_ipaddr(ipaddr, 192,168,3,123);
	uip_sethostaddr(ipaddr);
	uip_ipaddr(ipaddr, 192,168,3,1);
	uip_setdraddr(ipaddr);
	uip_ipaddr(ipaddr, 255,255,255,0);
	uip_setnetmask(ipaddr);

	timer_set(&periodic_timer, CLOCK_SECOND / 2);
	timer_set(&arp_timer, CLOCK_SECOND * 10);

	shell_register_command("ifconfig",vShell_GetLocalIp);
}

void uip_base_poll(void)
{
	uip_len = tapdev_read();
	if(uip_len > 0) 
	{
		if(BUF->type == htons(UIP_ETHTYPE_IP)) 
		{
			//Debug_Here;
			uip_arp_ipin();
			uip_input();
			/* If the above function invocation resulted in data that
			should be sent out on the network, the global variable
			uip_len is set to a value > 0. */
			if(uip_len > 0) {
				uip_arp_out();
				tapdev_send();
			}
		} 
		else 
		if(BUF->type == htons(UIP_ETHTYPE_ARP)) 
		{
			//printk("htons(UIP_ETHTYPE_ARP)\r\n");
			uip_arp_arpin();
			/* If the above function invocation resulted in data that
			should be sent out on the network, the global variable
			uip_len is set to a value > 0. */
			if(uip_len > 0) tapdev_send();
		}
	} 
	else 
	if(timer_expired(&periodic_timer)) 
	{
		timer_reset(&periodic_timer);
		for(int i = 0; i < UIP_CONNS; i++) 
		{
			uip_periodic(i);
			/* If the above function invocation resulted in data that
			should be sent out on the network, the global variable
			uip_len is set to a value > 0. */
			if(uip_len > 0) 
			{
				uip_arp_out();
				tapdev_send();
			}
		}
		
		#if UIP_UDP
			//处理udp超时程序
			for(int i = 0; i < UIP_UDP_CONNS; i++) 
			{
				uip_udp_periodic(i);
				if(uip_len > 0) 
				{
					uip_arp_out();
					tapdev_send();
				}
			}
		#endif /* UIP_UDP */
		
		/* Call the ARP timer function every 10 seconds. */
		if(timer_expired(&arp_timer)) 
		{
			timer_reset(&arp_timer);
			uip_arp_timer();
			color_printk(yellow,"time to update arp\r\n");
		}
	}
}

ros_task_t stLEDtask;

int task_LED(void *arg)
{
	TASK_BEGIN();
	
	ioctrl(LED1,0);
	ioctrl(LED2,1);
	
	while(1)
	{
		task_sleep(1250);
		vLED1_Loop();
		printk("1250ms print test\r\n");
	}
	
	TASK_END();
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  *
  * @retval None
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* 如果动用了 sd 卡 ，初始化引脚要设为上拉 */
  /* 如果动用了  fatfs ，在 f_write  的时候，所传入的缓冲区指着需要 4 字节对齐，否则容易出错*/
  /* 在 uip-conf.h 添加 UIP_CONF_HARDWARE_CHECKSUMS 宏 ，修改 uip.c 1000 行代码 */
  SCB->VTOR = FLASH_BASE|0x20000; 
  __enable_irq();//打开总中断
  /* USER CODE END 1 */

  /* MCU Configuration----------------------------------------------------------*/

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
  MX_DMA_Init();
  MX_SDIO_SD_Init();
  MX_FATFS_Init();
  /* USER CODE BEGIN 2 */
	shell_init("uIP >",vUsartHal_Output);
	vSerialConsole_Init((char *)acSTM32LOGO);
	
	uip_base_init();
	uip_app_init();
	
	ioctrl(LED2,1);
	
	task_create(&stLEDtask,NULL,task_LED,NULL);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	uip_base_poll();
	uip_app_poll();
	OS_scheduler();
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

  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;

    /**Configure the main internal regulator output voltage 
    */
  __HAL_RCC_PWR_CLK_ENABLE();

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 360;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**Activate the Over-Drive mode 
    */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**Configure the Systick interrupt time 
    */
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000);

    /**Configure the Systick 
    */
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  file: The file name as string.
  * @param  line: The line in file as a number.
  * @retval None
  */
void _Error_Handler(char *file, int line)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  while(1)
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
void assert_failed(uint8_t* file, uint32_t line)
{ 
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
