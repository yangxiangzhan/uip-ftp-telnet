/**
  ******************************************************************************
  * @file           serial_hal.c
  * @author         goodmorning
  * @brief          ���ڿ���̨�ײ�Ӳ��ʵ�֡�
  ******************************************************************************
  *
  * COPYRIGHT(c) 2018 GoodMorning
  *
  ******************************************************************************
  */
/* Includes ---------------------------------------------------*/
#include <string.h>
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_dma.h"
#include "serial_hal.h"
#include "shell.h"



//---------------------HAL�����--------------------------
// ���Ҫ��Ӳ��������ֲ�޸ģ��޸����к꣬���ṩ���ų�ʼ������

#define     UsartBaudRate           115200   //������
#define     USART_DMA_ClockInit()    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2)//�� UsartDMAx ��Ӧ

#define     xUSART   1 //���ô��ں�

#if        (xUSART == 1) //����1��ӦDMA

	#define xDMA     2  //��ӦDMA
	#define xDMATxCH 4  //���Ͷ�Ӧ DMA ͨ����
	#define xDMARxCH 4  //���ն�Ӧ DMA ͨ����
	#define xDMATxStream 7
	#define xDMARxStream 2
	
#elif (xUSART == 3) //����3��ӦDMA

//	#define     RemapPartial_USART
	#define xDMA     2
	#define xDMATxCH 2
	#define xDMARxCH 3
	#define xDMATxStream 7
	#define xDMARxStream 2

#endif

//---------------------HAL����滻--------------------------
#define USART_X(x)	                   USART##x
#define USART_x(x)	                   USART_X(x)
	 
#define USART_IRQn(x)                  USART##x##_IRQn
#define USARTx_IRQn(x)                 USART_IRQn(x)
	 
#define USART_IRQHandler(x)            USART##x##_IRQHandler
#define USARTx_IRQHandler(x)           USART_IRQHandler(x)

#define DMA_X(x)	                   DMA##x
#define DMA_x(x)	                   DMA_X(x)
	 
#define DMA_Channel(x)                 LL_DMA_CHANNEL_##x
#define DMA_Channelx(x)                DMA_Channel(x)	 

#define DMA_Stream(x)                  LL_DMA_STREAM_##x
#define DMA_Streamx(x)                 DMA_Stream(x)

#define DMA_Stream_IRQn(x,y)           DMA##x##_Stream##y##_IRQn
#define DMAx_Streamy_IRQn(x,y)         DMA_Stream_IRQn(x,y)

#define DMA_Stream_IRQHandler(x,y)     DMA##x##_Stream##y##_IRQHandler
#define DMAx_Streamy_IRQHandler(x,y)   DMA_Stream_IRQHandler(x,y)
	 
#define DMA_ClearFlag_TC(x,y)          LL_DMA_ClearFlag_TC##y(DMA##x)
#define DMAx_ClearFlag_TCy(x,y)        DMA_ClearFlag_TC(x,y)
	 
#define DMA_IsActiveFlag_TC(x,y)       LL_DMA_IsActiveFlag_TC##y(DMA##x)
#define DMAx_IsActiveFlag_TCy(x,y)     DMA_IsActiveFlag_TC(x,y)

#define UsartDMAx                  DMA_x(xDMA)     //�������� dma ����
#define USARTx                     USART_x(xUSART)   //���ô���
#define UsartIRQn                  USARTx_IRQn(xUSART) //�ж�
#define UsartIRQnFunc              USARTx_IRQHandler(xUSART) //�жϺ�����

#define UsartDmaTxCHx              DMA_Channelx(xDMATxCH)    //���ڷ��� dma ͨ��
#define UsartDmaTxStream           DMA_Streamx(xDMATxStream)
#define UsartDmaTxIRQn             DMAx_Streamy_IRQn(xDMA,xDMATxStream)
#define UsartDmaTxIRQFunc          DMAx_Streamy_IRQHandler(xDMA,xDMATxStream) //�жϺ�����
#define UsartDmaTxClearFlag()      DMAx_ClearFlag_TCy(xDMA,xDMATxStream)
#define UsartDmaTxCompleteFlag()   DMAx_IsActiveFlag_TCy(xDMA,xDMATxStream)

#define UsartDmaRxCHx              DMA_Channelx(xDMARxCH)
#define UsartDmaRxStream           DMA_Streamx(xDMARxStream)
#define UsartDmaRxIRQn             DMAx_Streamy_IRQn(xDMA,xDMARxStream)
#define UsartDmaRxIRQFunc          DMAx_Streamy_IRQHandler(xDMA,xDMARxStream) //�жϺ�����
#define UsartDmaRxClearFlag()      DMAx_ClearFlag_TCy(xDMA,xDMARxStream)
#define UsartDmaRxCompleteFlag()   DMAx_IsActiveFlag_TCy(xDMA,xDMARxStream)

//---------------------------------------------------------

#define HAL_RX_PACKET_SIZE 4     //Ӳ�����յ��Ļ�����У������ݰ�Ϊ��λ
#define HAL_RX_BUF_SIZE    (1024*2+1)  //Ӳ�����ջ�����
//#define HAL_RX_BUF_SIZE    (FLASH_PAGE_SIZE * 2 + 1)//Ӳ�����ջ�����
#define HAL_TX_BUF_SIZE    1024  //Ӳ�����ͻ�����

static struct _stUartTx
{
	uint16_t Tail ;
	uint16_t PktSize ;
	char buf[HAL_TX_BUF_SIZE];
}
stUartTx = {0};
 

static struct _stUartRx
{
	uint16_t Tail;
	uint16_t MaxLen;
	char buf[HAL_RX_BUF_SIZE];
}
stUartRx = {0};


struct
{
	uint16_t Tail ;
	uint16_t Head ;
	
	uint16_t  PktLen[HAL_RX_PACKET_SIZE];
	char    * Pkt[HAL_RX_PACKET_SIZE];
}
stUartRxQueue  = {0};




#if   (xUSART == 1)
static void vUsartHal_USART1_GPIO_Init(void)
{
  LL_GPIO_InitTypeDef GPIO_InitStruct;
  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
  
	#if 0
  /**USART1 GPIO Configuration  
  PA9   ------> USART1_TX
  PA10   ------> USART1_RX 
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_9;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_7;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_10;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_7;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
	#else
  /**USART1 GPIO Configuration  
  PB6   ------> USART1_TX
  PB7   ------> USART1_RX 
  */
	
  GPIO_InitStruct.Pin = LL_GPIO_PIN_6;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_7;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_7;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_7;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	#endif

  /* USART1 interrupt Init 
  NVIC_SetPriority(USART1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),0, 0));
  NVIC_EnableIRQ(USART1_IRQn);*/
}

#endif


/** 
	* @brief vUsartHal_DMA_Init ����̨ DMA ��ʼ��
	* @param void
	* @return NULL
*/
static void vUsartHal_DMA_Init(void)
{
	USART_DMA_ClockInit();	 

	/* USART_RX Init */  /* USART_RX Init */
	LL_DMA_SetChannelSelection(UsartDMAx, UsartDmaRxStream, UsartDmaRxCHx);
	LL_DMA_SetDataTransferDirection(UsartDMAx, UsartDmaRxStream, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
	LL_DMA_SetStreamPriorityLevel(UsartDMAx, UsartDmaRxStream, LL_DMA_PRIORITY_MEDIUM);
	LL_DMA_SetMode(UsartDMAx, UsartDmaRxStream, LL_DMA_MODE_NORMAL);
	LL_DMA_SetPeriphIncMode(UsartDMAx, UsartDmaRxStream, LL_DMA_PERIPH_NOINCREMENT);
	LL_DMA_SetMemoryIncMode(UsartDMAx, UsartDmaRxStream, LL_DMA_MEMORY_INCREMENT);
	LL_DMA_SetPeriphSize(UsartDMAx, UsartDmaRxStream, LL_DMA_PDATAALIGN_BYTE);
	LL_DMA_SetMemorySize(UsartDMAx, UsartDmaRxStream, LL_DMA_MDATAALIGN_BYTE);
	LL_DMA_SetPeriphAddress(UsartDMAx,UsartDmaRxStream,LL_USART_DMA_GetRegAddr(USARTx)); 
	LL_DMA_DisableFifoMode(UsartDMAx, UsartDmaRxStream);

	/* USART_TX Init */
	LL_DMA_SetChannelSelection(UsartDMAx, UsartDmaTxStream, UsartDmaTxCHx);
	LL_DMA_SetDataTransferDirection(UsartDMAx, UsartDmaTxStream, LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
	LL_DMA_SetStreamPriorityLevel(UsartDMAx, UsartDmaTxStream, LL_DMA_PRIORITY_MEDIUM);
	LL_DMA_SetMode(UsartDMAx, UsartDmaTxStream, LL_DMA_MODE_NORMAL);
	LL_DMA_SetPeriphIncMode(UsartDMAx, UsartDmaTxStream, LL_DMA_PERIPH_NOINCREMENT);
	LL_DMA_SetMemoryIncMode(UsartDMAx, UsartDmaTxStream, LL_DMA_MEMORY_INCREMENT);
	LL_DMA_SetPeriphSize(UsartDMAx, UsartDmaTxStream, LL_DMA_PDATAALIGN_BYTE);
	LL_DMA_SetMemorySize(UsartDMAx, UsartDmaTxStream, LL_DMA_MDATAALIGN_BYTE);
	LL_DMA_SetPeriphAddress(UsartDMAx,UsartDmaTxStream,LL_USART_DMA_GetRegAddr(USARTx));
	LL_DMA_DisableFifoMode(UsartDMAx, UsartDmaTxStream);	
  
	UsartDmaTxClearFlag();
	UsartDmaRxClearFlag();
	
	  /* DMA interrupt init �ж�*/
	NVIC_SetPriority(UsartDmaTxIRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),6, 0));
	NVIC_EnableIRQ(UsartDmaTxIRQn);
	  
	NVIC_SetPriority(UsartDmaRxIRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),6, 0));
	NVIC_EnableIRQ(UsartDmaRxIRQn);
	
	LL_DMA_EnableIT_TC(UsartDMAx,UsartDmaTxStream);
	LL_DMA_EnableIT_TC(UsartDMAx,UsartDmaRxStream);
}




/** 
	* @brief vUsartHal_UART_Init ����̨���ڲ�����ʼ��
	* @param void
	* @return NULL
*/
static void vUsartHal_UART_Init(void)
{
	LL_USART_InitTypeDef USART_InitStruct;

	USART_InitStruct.BaudRate = UsartBaudRate;
	USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
	USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
	USART_InitStruct.Parity = LL_USART_PARITY_NONE;
	USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
	USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;

	LL_USART_Init(USARTx, &USART_InitStruct);
	LL_USART_ConfigAsyncMode(USARTx);

	NVIC_SetPriority(UsartIRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),7, 0));
	NVIC_EnableIRQ(UsartIRQn);

	LL_USART_DisableIT_RXNE(USARTx);
	LL_USART_DisableIT_PE(USARTx);
	LL_USART_EnableIT_IDLE(USARTx);
	
	LL_USART_EnableDMAReq_RX(USARTx);
	LL_USART_EnableDMAReq_TX(USARTx);

	LL_USART_Enable(USARTx);
}

/**
	* @brief    ���� console Ӳ�����ͻ�������ͬʱ��������ձ�־λ
	* @param    ��
	* @return   
*/
static inline void vUsartHal_DMA_SetTxBuffer( uint32_t MemoryAddress ,uint16_t TxMaxLen)
{
//	LL_DMA_DisableIT_TC(UsartDMAx,UsartDmaTxStream);
	LL_DMA_DisableStream(UsartDMAx,UsartDmaTxStream);//�����ݲ�ʹ��
	
	UsartDmaTxClearFlag();
	
	LL_DMA_SetMemoryAddress(UsartDMAx,UsartDmaTxStream,MemoryAddress);
	LL_DMA_SetDataLength(UsartDMAx,UsartDmaTxStream,TxMaxLen);

	LL_DMA_EnableStream(UsartDMAx,UsartDmaTxStream);
//	LL_DMA_EnableIT_TC(UsartDMAx,UsartDmaTxStream);
}



/**
	* @brief    ���� console Ӳ�����ջ�������ͬʱ��������ձ�־λ
	* @param    ��
	* @return   
*/
static inline void vUsartHal_DMA_SetRxBuffer( uint32_t MemoryAddress ,uint16_t RxMaxLen)
{
	LL_DMA_DisableIT_TC(UsartDMAx,UsartDmaRxStream);
	LL_DMA_DisableStream(UsartDMAx,UsartDmaRxStream);//�����ݲ�ʹ��
	
	UsartDmaRxClearFlag();
	
	LL_DMA_SetMemoryAddress(UsartDMAx,UsartDmaRxStream,MemoryAddress);
	LL_DMA_SetDataLength(UsartDMAx,UsartDmaRxStream,RxMaxLen);

	LL_DMA_EnableStream(UsartDMAx,UsartDmaRxStream);
	LL_DMA_EnableIT_TC(UsartDMAx,UsartDmaRxStream);
}


/**
  * @brief    console �������͵�ǰ��
  * @param    ��
  * @retval   ��
  */
static inline void vUsartHal_SendThisPacket(void)
{
	uint16_t PktSize ;
	uint16_t Head ;
	PktSize = stUartTx.PktSize ;
	stUartTx.PktSize = 0;
	Head = stUartTx.Tail - PktSize  ;
	vUsartHal_DMA_SetTxBuffer((uint32_t)(&stUartTx.buf[Head]),PktSize);
}



/**
  * @brief    vUsartHal_RxPktMaxLen ����Ӳ����������
  * @param    ��
  * @retval   ��
  */
void vUsartHal_RxPktMaxLen(uint16_t MaxLen)
{
	stUartRx.MaxLen = MaxLen;
	stUartRx.Tail = 0;
	
	stUartRxQueue.Tail = 0;
	stUartRxQueue.Head = 0;
		
	vUsartHal_DMA_SetRxBuffer((uint32_t)(&stUartRx.buf[0]),MaxLen);
}


int iUsartHal_TxBusy(void)
{
	return (LL_DMA_IsEnabledStream(UsartDMAx,UsartDmaTxStream));
}



/**
	* @brief    vUsartHal_RxPktIn console ���ڽ������ݰ���������
	* @param    
	* @return   ��
*/
static inline void vUsartHal_RxPktIn(char * pkt ,uint16_t len)
{
	stUartRxQueue.Tail = (stUartRxQueue.Tail + 1) % HAL_RX_PACKET_SIZE;
	stUartRxQueue.Pkt[stUartRxQueue.Tail] = pkt;
	stUartRxQueue.PktLen[stUartRxQueue.Tail] = len;
}


/**
	* @brief    iUsartHal_RxPktOut console ���ڶ��г���
	* @param    
	* @return   ��
*/
int iUsartHal_RxPktOut(char ** data,uint16_t * len)
{
	if (stUartRxQueue.Tail != stUartRxQueue.Head)
	{
		stUartRxQueue.Head = (stUartRxQueue.Head + 1) % HAL_RX_PACKET_SIZE;
		*data = stUartRxQueue.Pkt[stUartRxQueue.Head];
		*len  = stUartRxQueue.PktLen[stUartRxQueue.Head];
		return 1;
	}
	else
	{
		*len = 0;
		return 0;
	}
}



/**
	* @brief    vUsartHal_Output console Ӳ�������
	* @param    ��
	* @return   ��
*/
void vUsartHal_Output(char * buf,uint16_t len)
{
	while(len)
	{
		uint16_t remain  = HAL_TX_BUF_SIZE - stUartTx.Tail - 1;
		uint16_t PktSize = (remain > len) ? len : remain;
		uint16_t Tail = stUartTx.Tail;              //�Ȼ�ȡ��ǰβ����ַ
		
		memcpy(&stUartTx.buf[Tail] , buf , PktSize);//�����ݰ�������������
		Tail += PktSize;
		buf  += PktSize;
		len  -= PktSize; 
		
		stUartTx.Tail = Tail;       //����β��
		stUartTx.PktSize += PktSize;//���õ�ǰ����С
		
		//��ʼ����
		if (!LL_DMA_IsEnabledStream(UsartDMAx,UsartDmaTxStream))
			vUsartHal_SendThisPacket();
		
		if (len) 
			while(LL_DMA_IsEnabledStream(UsartDMAx,UsartDmaTxStream)) ;//δ������ȴ�
	}
}



//------------------------------���� IAP ���------------------------------


/**
	* @brief    iUsartHal_IAP_Erase console ���� flash ĳ������
	* @param    ��
	* @return   ��
*/
int iUsartHal_IAP_Erase(uint32_t SECTOR)
{
	uint32_t SectorError;
    FLASH_EraseInitTypeDef FlashEraseInit;
	HAL_StatusTypeDef HAL_Status;
	
	FlashEraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS;       //�������ͣ��������� 
	FlashEraseInit.Sector       = SECTOR;                        //����
	FlashEraseInit.NbSectors    = 1;                             //һ��ֻ����һ������
	FlashEraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;      //��ѹ��Χ��VCC=2.7~3.6V֮��!!
	
	HAL_Status = HAL_FLASHEx_Erase(&FlashEraseInit,&SectorError);
	
	return HAL_Status;
}



/**
	* @brief    vUsartHal_IAP_Write console д flash
	* @param    ��
	* @return   ��
*/
void vUsartHal_IAP_Write(uint32_t FlashAddr,uint32_t FlashData)
{
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,FlashAddr,FlashData);
}



/**
	* @brief    vUsartHal_LockFlash console ���� flash
	* @param    ��
	* @return   ��
*/
void vUsartHal_LockFlash(void)
{
	HAL_FLASH_Lock();
}



/**
	* @brief    vUsartHal_UnlockFlash console ���� flash
	* @param    ��
	* @return   ��
*/
void vUsartHal_UnlockFlash(void)
{
	HAL_FLASH_Unlock();
}



/**
	* @brief    vSystemReboot Ӳ������
	* @param    ��
	* @return  
*/
void vShell_RebootSystem(void * arg)
{
	NVIC_SystemReset();
}



/**
	* @brief    vShell_JumpToAppCmd console ���ڷ���һ����������ж�
	* @param    ��
	* @return   ��
*/
void vShell_JumpCmd(void * arg)
{
	uint32_t UPDATE_ADDR = (SCB->VTOR == FLASH_BASE) ? (APP_ADDR):(IAP_ADDR);
	uint32_t SpInitVal = *(uint32_t *)(UPDATE_ADDR);    
	uint32_t JumpAddr = *(uint32_t *)(UPDATE_ADDR + 4); 
	void (*pAppFun)(void) = (void (*)(void))JumpAddr;    
	__set_BASEPRI(0); 	      
	__set_FAULTMASK(0);       
	__disable_irq();          
	__set_MSP(SpInitVal);     
	__set_PSP(SpInitVal);     
	__set_CONTROL(0);         
	(*pAppFun) ();            
}



//------------------------------����ΪһЩ�жϴ���------------------------------
//#include "cmsis_os.h"//����freertos ��

#ifdef _CMSIS_OS_H
extern osSemaphoreId osSerialRxSemHandle;

#else
#include "AtomRos.h"
extern ros_semaphore_t rosSerialRxSem;
#endif

/**
	* @brief    UsartDmaTxIRQFunc console ���ڷ���һ����������ж�
	* @param    ��
	* @return   ��
*/
void UsartDmaTxIRQFunc(void) 
{
	if (stUartTx.PktSize == 0) //������˰��������ݣ���λ������
	{
		stUartTx.Tail = 0;
		LL_DMA_DisableStream(UsartDMAx,UsartDmaTxStream);
		UsartDmaTxClearFlag();
	}
	else
	{
		vUsartHal_SendThisPacket(); //�����������������
	}
}


/**
	* @brief    UsartDmaRxIRQFunc console ���ڽ������ж�
	* @param    ��
	* @return   ��
*/
void UsartDmaRxIRQFunc(void) 
{
	vUsartHal_RxPktIn(&(stUartRx.buf[stUartRx.Tail]),stUartRx.MaxLen); //�ѵ�ǰ����ַ�ʹ�С���뻺�����
	
	stUartRx.Tail += stUartRx.MaxLen ; //���»����ַ
	
	if (stUartRx.Tail + stUartRx.MaxLen > HAL_RX_BUF_SIZE) //���ʣ��ռ䲻���Ի����������ȣ��� 0 ��ʼ
		stUartRx.Tail = 0;
	
	UsartDmaRxClearFlag();
	vUsartHal_DMA_SetRxBuffer((uint32_t)&(stUartRx.buf[stUartRx.Tail]),stUartRx.MaxLen);//���û����ַ����������

	#ifdef _CMSIS_OS_H	
		osSemaphoreRelease(osSerialRxSemHandle);// �ͷ��ź���
	#else
		task_semaphore_release(&rosSerialRxSem);
	#endif
}



/**
	* @brief    UsartIRQnFunc �����жϺ�����ֻ�п����ж�
	* @param    ��
	* @return   ��
*/
void UsartIRQnFunc(void) 
{
	uint16_t PktLen ;
	
	LL_USART_ClearFlag_IDLE(USARTx); //��������ж�
	
	PktLen = stUartRx.MaxLen - LL_DMA_GetDataLength(UsartDMAx,UsartDmaRxStream);//�õ���ǰ���ĳ���
	
	if (PktLen)
	{
		vUsartHal_RxPktIn(&(stUartRx.buf[stUartRx.Tail]),PktLen); //�ѵ�ǰ�����뻺����У�����Ӧ�ò㴦��
	
		stUartRx.Tail += PktLen ;	 //���»����ַ
		if (stUartRx.Tail + stUartRx.MaxLen > HAL_RX_BUF_SIZE)//���ʣ��ռ䲻���Ի����������ȣ��� 0 ��ʼ
			stUartRx.Tail = 0;

		vUsartHal_DMA_SetRxBuffer((uint32_t)&(stUartRx.buf[stUartRx.Tail]),stUartRx.MaxLen);//���û����ַ����������

		#ifdef _CMSIS_OS_H
			osSemaphoreRelease(osSerialRxSemHandle);// �ͷ��ź���	
		#else
			task_semaphore_release(&rosSerialRxSem);
		#endif
	}
}


//------------------------------�����ķָ���------------------------------
/**
	* @brief    vUsartHal_Init console Ӳ�����ʼ��
	* @param    ��
	* @return   ��
*/
void vUsartHal_Init(void)
{
	//���ų�ʼ��
	#if   (xUSART == 1) 
		vUsartHal_USART1_GPIO_Init();
	#elif (xUSART == 3) 
		vUsartHal_USART3_GPIO_Init();	
	#endif

	vUsartHal_UART_Init();
	vUsartHal_DMA_Init();
	
	stUartTx.Tail = 0;
	stUartTx.PktSize = 0;
	
	vUsartHal_RxPktMaxLen(COMMANDLINE_MAX_LEN);
}


void vUsartHal_Deinit(void)
{
	NVIC_DisableIRQ(UsartDmaTxIRQn);
	NVIC_DisableIRQ(UsartDmaRxIRQn);
	NVIC_DisableIRQ(UsartIRQn);
	
	LL_DMA_DisableIT_TC(UsartDMAx,UsartDmaTxStream);
	LL_DMA_DisableIT_TC(UsartDMAx,UsartDmaRxStream);	
	
	LL_USART_DisableDMAReq_RX(USARTx);
	LL_USART_DisableDMAReq_TX(USARTx);

	LL_USART_Disable(USARTx);	
	
}
