/**
  ******************************************************************************
  * File Name          : gpio.h
  * Description        : This file contains all the functions prototypes for 
  *                      the gpio  
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __gpio_H
#define __gpio_H
#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_exti.h"
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

	 
//���Ų����궨�塣ע�⣺���� LL ��.ioctrl,iostatus
#define ioset_Output(Gpio,Pin) vGpio_OutputInit(Gpio,Pin)
#define ioset_Input(Gpio,Pin)    vGpio_InputInit(Gpio,Pin)

#define ioset_1(Gpio,Pin) LL_GPIO_SetOutputPin(Gpio,Pin)
#define ioset_H(Gpio,Pin) LL_GPIO_SetOutputPin(Gpio,Pin)

#define ioset_0(Gpio,Pin) LL_GPIO_ResetOutputPin(Gpio,Pin)
#define ioset_L(Gpio,Pin) LL_GPIO_ResetOutputPin(Gpio,Pin)


// stm32cubemx ������������ͳһΪ which_GPIO_Port,which_Pin,����ֻ��Ҫ��ǰ׺���б��
// which: ����ǰ׺ ��what: 0,L,1,H,Input,Output
#define ioctrl(which,what)  ioset_##what(which##_GPIO_Port,which##_Pin)

// ��ȡ��������������ƽ״̬��which: ����ǰ׺��inout:Input,Output
#define iostatus(which,inout) LL_GPIO_Is##inout##PinSet(which##_GPIO_Port,which##_Pin)


#define LED1_Pin LL_GPIO_PIN_12
#define LED1_GPIO_Port GPIOD
#define LED2_Pin LL_GPIO_PIN_7
#define LED2_GPIO_Port GPIOG

#define ETH_Reset_Pin        LL_GPIO_PIN_4
#define ETH_Reset_GPIO_Port  GPIOD


#define   vLED1(x)      ioctrl(LED1,x)
#define   vLED1_Loop()  iostatus(LED1,Output) ?  ioctrl(LED1,L) :  ioctrl(LED1,H)

#define   vLED2(x)      ioctrl(LED2,x)
#define   vLED2_Loop()  iostatus(LED2,Output) ?  ioctrl(LED2,0) :  ioctrl(LED2,1)

	 
	 
	 
/* USER CODE END Private defines */

void MX_GPIO_Init(void);

/* USER CODE BEGIN Prototypes */


void vGpio_OutputInit(GPIO_TypeDef * GPIOx,uint32_t Pinx);
void vGpio_InputInit(GPIO_TypeDef * GPIOx,uint32_t Pinx);



/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif
#endif /*__ pinoutConfig_H */

/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
