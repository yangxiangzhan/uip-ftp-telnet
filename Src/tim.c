/**
  ******************************************************************************
  * File Name          : TIM.c
  * Description        : This file provides code for the configuration
  *                      of the TIM instances.
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
#include "tim.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* TIM6 init function */
void MX_TIM6_Init(void)
{
  LL_TIM_InitTypeDef TIM_InitStruct;

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM6);

  TIM_InitStruct.Prescaler = 89;
  TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
  TIM_InitStruct.Autoreload = 0xffff;
  LL_TIM_Init(TIM6, &TIM_InitStruct);

  LL_TIM_SetTriggerOutput(TIM6, LL_TIM_TRGO_RESET);

  LL_TIM_DisableMasterSlaveMode(TIM6);

}
/* TIM7 init function */
void MX_TIM7_Init(void)
{
  LL_TIM_InitTypeDef TIM_InitStruct;

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM7);

  /* TIM7 interrupt Init */
  NVIC_SetPriority(TIM7_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),0, 0));
  NVIC_EnableIRQ(TIM7_IRQn);

  TIM_InitStruct.Prescaler = 89;
  TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
  TIM_InitStruct.Autoreload = 999;
  LL_TIM_Init(TIM7, &TIM_InitStruct);

  LL_TIM_SetTriggerOutput(TIM7, LL_TIM_TRGO_RESET);

  LL_TIM_DisableMasterSlaveMode(TIM7);

}

/* USER CODE BEGIN 1 */



void delay_us(unsigned short us)
{
	unsigned short usTimedelay = (us)  ;
	LL_TIM_SetCounter(TIM6,0);
	LL_TIM_EnableCounter(TIM6); 	

	while( LL_TIM_GetCounter(TIM6) <= usTimedelay);
	
	LL_TIM_DisableCounter(TIM6); 	
//	LL_TIM_SetCounter(TIM5,0);
}


void delay_ms(unsigned short ms)
{
	for (unsigned int msCnt = 0; msCnt < ms;++msCnt)
	{
		LL_TIM_SetCounter(TIM6,0);
		LL_TIM_EnableCounter(TIM6); 	

		while( LL_TIM_GetCounter(TIM6) <= 999);//1ms
		
		LL_TIM_DisableCounter(TIM6); 	
//		LL_TIM_SetCounter(TIM6,0);		
	}
}


/* USER CODE END 1 */

/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
