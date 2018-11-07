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
#define USART_IRQ              USARTx_IRQHandler(xUSART) //�жϺ�����

#define UsartDmaTxCHx              DMA_Channelx(xDMATxCH)    //���ڷ��� dma ͨ��
#define UsartDmaTxStream           DMA_Streamx(xDMATxStream)
#define UsartDmaTxIRQn             DMAx_Streamy_IRQn(xDMA,xDMATxStream)
#define USART_DMA_TX_IRQ           DMAx_Streamy_IRQHandler(xDMA,xDMATxStream) //�жϺ�����
#define UsartDmaTxClearFlag()      DMAx_ClearFlag_TCy(xDMA,xDMATxStream)
#define UsartDmaTxCompleteFlag()   DMAx_IsActiveFlag_TCy(xDMA,xDMATxStream)

#define UsartDmaRxCHx              DMA_Channelx(xDMARxCH)
#define UsartDmaRxStream           DMA_Streamx(xDMARxStream)
#define UsartDmaRxIRQn             DMAx_Streamy_IRQn(xDMA,xDMARxStream)
#define USART_DMA_RX_IRQ          DMAx_Streamy_IRQHandler(xDMA,xDMARxStream) //�жϺ�����
#define UsartDmaRxClearFlag()      DMAx_ClearFlag_TCy(xDMA,xDMARxStream)
#define UsartDmaRxCompleteFlag()   DMAx_IsActiveFlag_TCy(xDMA,xDMARxStream)

//---------------------------------------------------------

#define HAL_RX_PACKET_SIZE 4     //Ӳ�����յ��Ļ�����У������ݰ�Ϊ��λ
#define HAL_RX_BUF_SIZE    (1024*2+1)  //Ӳ�����ջ�����
//#define HAL_RX_BUF_SIZE    (FLASH_PAGE_SIZE * 2 + 1)//Ӳ�����ջ�����
#define HAL_TX_BUF_SIZE    1024  //Ӳ�����ͻ�����

static struct _serial_tx
{
	uint16_t pkttail ;
	uint16_t pktsize ;
	char buf[HAL_TX_BUF_SIZE];
}
serial_tx = {0};


static struct _serial_rx
{
	uint16_t pkttail;
	uint16_t pktmax;
	char buf[HAL_RX_BUF_SIZE];
}
serial_rx = {0};


static struct _serial_queue
{
	uint16_t tail ;
	uint16_t head ;
	
	uint16_t  pktlen[HAL_RX_PACKET_SIZE];
	char    * pktbuf[HAL_RX_PACKET_SIZE];
}
serial_rxpkt_queue  = {0};




#if   (xUSART == 1)
static void usart1_gpio_init(void)
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
	* @brief usart_dma_init ����̨ DMA ��ʼ��
	* @param void
	* @return NULL
*/
static void usart_dma_init(void)
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
	* @brief usart_base_init ����̨���ڲ�����ʼ��
	* @param void
	* @return NULL
*/
static void usart_base_init(void)
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

static inline void serial_dma_send( uint32_t memory_addr ,uint16_t buf_len)
{
//	LL_DMA_DisableIT_TC(UsartDMAx,UsartDmaTxStream);
	LL_DMA_DisableStream(UsartDMAx,UsartDmaTxStream);//�����ݲ�ʹ��
	
	UsartDmaTxClearFlag();
	
	LL_DMA_SetMemoryAddress(UsartDMAx,UsartDmaTxStream,memory_addr);
	LL_DMA_SetDataLength(UsartDMAx,UsartDmaTxStream,buf_len);

	LL_DMA_EnableStream(UsartDMAx,UsartDmaTxStream);
//	LL_DMA_EnableIT_TC(UsartDMAx,UsartDmaTxStream);
}
*/


/**
	* @brief    ���� console Ӳ�����ջ�������ͬʱ��������ձ�־λ
	* @param    ��
	* @return   
*/
static inline void serial_dma_recv( uint32_t memory_addr ,uint16_t dma_max_len)
{
	LL_DMA_DisableIT_TC(UsartDMAx,UsartDmaRxStream);
	LL_DMA_DisableStream(UsartDMAx,UsartDmaRxStream);//�����ݲ�ʹ��
	
	UsartDmaRxClearFlag();
	
	LL_DMA_SetMemoryAddress(UsartDMAx,UsartDmaRxStream,memory_addr);
	LL_DMA_SetDataLength(UsartDMAx,UsartDmaRxStream,dma_max_len);

	LL_DMA_EnableStream(UsartDMAx,UsartDmaRxStream);
	LL_DMA_EnableIT_TC(UsartDMAx,UsartDmaRxStream);
}


/**
  * @brief    console �������͵�ǰ��
  * @param    ��
  * @retval   ��
  */
static inline void serial_send_pkt(void)
{
	uint32_t pkt_size = serial_tx.pktsize ;
	uint32_t pkt_head  = serial_tx.pkttail - pkt_size ;
	
	serial_tx.pktsize = 0;
	
//	serial_dma_send((uint32_t)(&serial_tx.buf[pkt_head]),pkt_size);
	LL_DMA_DisableStream(UsartDMAx,UsartDmaTxStream);//�����ݲ�ʹ��
	
	UsartDmaTxClearFlag();
	
	LL_DMA_SetMemoryAddress(UsartDMAx,UsartDmaTxStream,(uint32_t)(&serial_tx.buf[pkt_head]));
	LL_DMA_SetDataLength(UsartDMAx,UsartDmaTxStream,pkt_size);

	LL_DMA_EnableStream(UsartDMAx,UsartDmaTxStream);

}



/**
  * @brief    serial_rxpkt_max_len ����Ӳ����������
  * @param    ��
  * @retval   ��
  */
void serial_rxpkt_max_len(uint16_t pktmax)
{
	serial_rx.pktmax = pktmax;
	serial_rx.pkttail = 0;
	
	serial_rxpkt_queue.tail = 0;
	serial_rxpkt_queue.head = 0;

	serial_dma_recv((uint32_t)(&serial_rx.buf[0]),pktmax);
}


int serial_busy(void)
{
	return (LL_DMA_IsEnabledStream(UsartDMAx,UsartDmaTxStream));
}



/**
	* @brief    serial_rxpkt_queue_in console ���ڽ������ݰ���������
	* @param    
	* @return   ��
*/
static inline void serial_rxpkt_queue_in(char * pkt ,uint16_t len)
{
	serial_rxpkt_queue.tail = (serial_rxpkt_queue.tail + 1) % HAL_RX_PACKET_SIZE;
	serial_rxpkt_queue.pktbuf[serial_rxpkt_queue.tail] = pkt;
	serial_rxpkt_queue.pktlen[serial_rxpkt_queue.tail] = len;
}


/**
	* @brief    serial_rxpkt_queue_out console ���ڶ��г���
	* @param    
	* @return   ��
*/
int serial_rxpkt_queue_out(char ** data,uint16_t * len)
{
	if (serial_rxpkt_queue.tail != serial_rxpkt_queue.head)
	{
		serial_rxpkt_queue.head = (serial_rxpkt_queue.head + 1) % HAL_RX_PACKET_SIZE;
		*data = serial_rxpkt_queue.pktbuf[serial_rxpkt_queue.head];
		*len  = serial_rxpkt_queue.pktlen[serial_rxpkt_queue.head];
		return 1;
	}
	else
	{
		*len = 0;
		return 0;
	}
}


/**
	* @brief    hal_usart_puts console Ӳ�������
	* @param    ��
	* @return   ��
*/
void serial_puts(char * buf,uint16_t len)
{
	while(len)
	{
		uint32_t pkttail = serial_tx.pkttail;              //�Ȼ�ȡ��ǰβ����ַ
		uint32_t remain  = HAL_TX_BUF_SIZE - pkttail - 1;
		uint32_t pktsize = (remain > len) ? len : remain;
		
		memcpy(&serial_tx.buf[pkttail] , buf , pktsize);//�����ݰ�������������
		
		pkttail += pktsize;
		buf  += pktsize;
		len  -= pktsize; 
		
		serial_tx.pkttail = pkttail;	   //����β��
		serial_tx.pktsize += pktsize;//���õ�ǰ����С

		//��ʼ����
		if (!LL_DMA_IsEnabledStream(UsartDMAx,UsartDmaTxStream))
			serial_send_pkt();

		if (len) 
			while(LL_DMA_IsEnabledStream(UsartDMAx,UsartDmaTxStream)) ;//δ������ȴ�
	}
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
	* @brief    USART_DMA_TX_IRQ console ���ڷ���һ����������ж�
	* @param    ��
	* @return   ��
*/
void USART_DMA_TX_IRQ(void) 
{
	if (serial_tx.pktsize == 0) //������˰��������ݣ���λ������
	{
		serial_tx.pkttail = 0;
		LL_DMA_DisableStream(UsartDMAx,UsartDmaTxStream);
		UsartDmaTxClearFlag();
	}
	else
	{
		serial_send_pkt(); //�����������������
	}
}


/**
	* @brief    USART_DMA_RX_IRQ console ���ڽ������ж�
	* @param    ��
	* @return   ��
*/
void USART_DMA_RX_IRQ(void) 
{
	serial_rxpkt_queue_in(&(serial_rx.buf[serial_rx.pkttail]),serial_rx.pktmax); //�ѵ�ǰ����ַ�ʹ�С���뻺�����
	
	serial_rx.pkttail += serial_rx.pktmax ; //���»����ַ
	
	if (serial_rx.pkttail + serial_rx.pktmax > HAL_RX_BUF_SIZE) //���ʣ��ռ䲻���Ի����������ȣ��� 0 ��ʼ
		serial_rx.pkttail = 0;
	
	UsartDmaRxClearFlag();
	serial_dma_recv((uint32_t)&(serial_rx.buf[serial_rx.pkttail]),serial_rx.pktmax);//���û����ַ����������

	#ifdef _CMSIS_OS_H	
		osSemaphoreRelease(osSerialRxSemHandle);// �ͷ��ź���
	#else
		task_semaphore_release(&rosSerialRxSem);
	#endif
}



/**
	* @brief    USART_IRQ �����жϺ�����ֻ�п����ж�
	* @param    ��
	* @return   ��
*/
void USART_IRQ(void) 
{
	uint16_t pkt_len ;
	
	LL_USART_ClearFlag_IDLE(USARTx); //��������ж�
	
	pkt_len = serial_rx.pktmax - LL_DMA_GetDataLength(UsartDMAx,UsartDmaRxStream);//�õ���ǰ���ĳ���
	
	if (pkt_len)
	{
		serial_rxpkt_queue_in(&(serial_rx.buf[serial_rx.pkttail]),pkt_len); //�ѵ�ǰ�����뻺����У�����Ӧ�ò㴦��
	
		serial_rx.pkttail += pkt_len ;	 //���»����ַ
		if (serial_rx.pkttail + serial_rx.pktmax > HAL_RX_BUF_SIZE)//���ʣ��ռ䲻���Ի����������ȣ��� 0 ��ʼ
			serial_rx.pkttail = 0;

		serial_dma_recv((uint32_t)&(serial_rx.buf[serial_rx.pkttail]),serial_rx.pktmax);//���û����ַ����������

		#ifdef _CMSIS_OS_H
			osSemaphoreRelease(osSerialRxSemHandle);// �ͷ��ź���	
		#else
			task_semaphore_release(&rosSerialRxSem);
		#endif
	}
}


//------------------------------�����ķָ���------------------------------
/**
	* @brief    hal_serial_init console Ӳ�����ʼ��
	* @param    ��
	* @return   ��
*/
void hal_serial_init(void)
{
	//���ų�ʼ��
	#if   (xUSART == 1) 
		usart1_gpio_init();
	#elif (xUSART == 3) 
		vUsartHal_USART3_GPIO_Init();	
	#endif

	usart_base_init();
	usart_dma_init();
	
	serial_tx.pkttail = 0;
	serial_tx.pktsize = 0;
	
	serial_rxpkt_max_len(COMMANDLINE_MAX_LEN);
}


void hal_serial_deinit(void)
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
