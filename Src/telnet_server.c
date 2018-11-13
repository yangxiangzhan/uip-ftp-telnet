
#include <string.h>
#include <stdint.h>
#include "shell.h"
#include "uip_arp.h"
#include "tapdev.h"
#include "uip.h"
#include "telnet_server.h"
#include "ustdio.h"

#include "gpio.h"

#include "fatfs.h"

#define TELNET_WILL  251
#define TELNET_WONT  252
#define TELNET_DO    253
#define TELNET_DONT  254
#define TELNET_IAC   255


#define TELNET_NORMAL	   0
#define TELNET_BIN_TRAN    1
#define TELNET_BIN_ERROR   2


#define TELNET_FILE_ADDR 0x8060000
/*
* secureCRT telnet �����ļ�������̣�
* CRT   : will bin tran ; do bin tran
* server: do bin tran
* CRT   : will bin tran 
* CRT   : <file data>
* CRT   : won't bin tran ; don't bin tran
* server: won't bin tran ; don't bin tran
* CRT   : won't bin tran 
* server: won't bin tran 
* CRT   : <string mode>
*/
	



/*---------------------------------------------------------------------------*/


static __align(4) char telnet_buf[UIP_TCP_MSS+4] = {0};


static struct shell_buf telnet_shell;
static struct uip_conn * telnet_bind = NULL;
static uint8_t	 telnet_state = TELNET_NORMAL;
static volatile uint16_t telnet_buftail = 0;



static struct telnetfile
{
//	char * const buf;
	uint16_t skip0xff;
	uint16_t remain ;
	uint32_t addr ;
}
telnet_file ;//= {.buf = telnet_buf};



void telnet_puts(char * buf,uint16_t len)
{
	if (telnet_state == TELNET_NORMAL && len + telnet_buftail < UIP_TCP_MSS) 
	{
		memcpy(&telnet_buf[telnet_buftail],buf,len);
		telnet_buftail += len;
	}
	else
		telnet_buftail = 0;
}




static void telnet_option(uint8_t option, uint8_t value) //telnet_option(TELNET_DONT,1)
{
	uint32_t new_tail = 3 + telnet_buftail;
	if (new_tail < UIP_TCP_MSS) 
	{
		char * pBuf = &telnet_buf[telnet_buftail];
		*pBuf = (char)TELNET_IAC;
		*++pBuf = (char)option;
		*++pBuf = (char)value;
		telnet_buftail = new_tail;
	}
}


void erase_telnet_flash(void * arg)
{
	uint32_t SectorError;
    FLASH_EraseInitTypeDef FlashEraseInit;
	
	FlashEraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS; //�������ͣ��������� 
	FlashEraseInit.Sector       = 7;                       //TELNET_FILE_ADDR �� F429 ����7������
	FlashEraseInit.NbSectors    = 1;                       //һ��ֻ����һ������
	FlashEraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;   //��ѹ��Χ��VCC=2.7~3.6V֮��!!
	
	HAL_FLASH_Unlock();
	HAL_FLASHEx_Erase(&FlashEraseInit,&SectorError);
	HAL_FLASH_Lock();
	printk("done\r\n");
}



void telnet_server_init(void)
{
	SHELL_MALLOC(&telnet_shell ,telnet_puts);	
	shell_register_command("telnet-erase",erase_telnet_flash);	
	uip_listen(HTONS(23));
}


/**
	* @brief    telnet_check_option 
	*           telnet ����ʱ��Ҫ���ظ��ͻ��˵�ѡ������
	* @param    arg �������
	* @return   void
*/
void telnet_check_option(char ** telnetbuf , uint16_t * buflen ,uint8_t * telnetstate)
{
	uint8_t iac = (uint8_t)((*telnetbuf)[0]);
	uint8_t opt = (uint8_t)((*telnetbuf)[1]);
	uint8_t val = (uint8_t)((*telnetbuf)[2]);

	if (TELNET_NORMAL == *telnetstate)
	{
		while(iac == TELNET_IAC && opt > 250 )
		{
			if (0 == val) //ֻ�ظ�����������
			{
				if (TELNET_WILL == opt)
				{
					*telnetstate = TELNET_BIN_TRAN;
					telnet_file.addr = TELNET_FILE_ADDR;
					telnet_file.remain = 0;
					telnet_file.skip0xff = 0;
					HAL_FLASH_Unlock();
				}
				else
					telnet_option(opt, val);
			}
			
			*telnetbuf += 3;
			*buflen -= 3;
			iac = (uint8_t)((*telnetbuf)[0]);
			opt = (uint8_t)((*telnetbuf)[1]);
			val = (uint8_t)((*telnetbuf)[2]);
		}
	}
	else
	{
		while(iac == TELNET_IAC && val == 0  && opt > 250 )
		{
			if (TELNET_WONT == opt) //ֻ�ظ�����������
			{
				iac = (uint8_t)((*telnetbuf)[3]);
				opt = (uint8_t)((*telnetbuf)[4]);
				val = (uint8_t)((*telnetbuf)[5]);

				if ( iac == TELNET_IAC  && opt == TELNET_DONT  && val == 0 )
				{
					HAL_FLASH_Lock();
					telnet_option(TELNET_WONT, 0);//�˳������ƴ���ģʽ
					telnet_option(TELNET_DONT, 0);//�˳������ƴ���ģʽ
					char * msg = & telnet_buf[telnet_buftail];
					sprintf(msg,"\r\nGet file,size=%d bytes\r\n",telnet_file.addr-TELNET_FILE_ADDR);
					telnet_buftail += strlen(msg);
					*telnetbuf += 3;
					*buflen -= 3;
					*telnetstate = TELNET_NORMAL;
				}
				else
					return ;
			}
			
			*telnetbuf += 3;
			*buflen -= 3;
			iac = (uint8_t)((*telnetbuf)[0]);
			opt = (uint8_t)((*telnetbuf)[1]);
			val = (uint8_t)((*telnetbuf)[2]);
		}
	}
}


/**
	* @brief    telnet_recv_file 
	*           telnet �����ļ������� flash ��
	* @param    arg �������
	* @return   void
*/
void telnet_recv_file(char * data , uint16_t len)
{
	uint8_t  * copyfrom = (uint8_t*)data ;//+ telnet_file.skip0xff;//0xff 0xff ���ְ��������������һ�� ff
	uint8_t	 * copyend = copyfrom + len ;
	uint8_t  * copyto = (uint8_t*)(&telnet_buf[telnet_file.remain]);
	uint32_t * value = (uint32_t*)(&telnet_buf[0]);
	uint32_t   size = 0;
	
	//telnet_file.skip0xff = ((uint8_t)data[len-1] == 0xff && (uint8_t)data[len-2] != 0xff);//0xff 0xff ���ְ������

	//����ļ��д��� 0xff ���� SecureCRT �ᷢ���� 0xff ����Ҫ�޳�һ��
	while(copyfrom < copyend)
	{
		*copyto++ = *copyfrom++ ;
		if (*copyfrom == 0xff) 
			++copyfrom;
	}

	size = copyto - (uint8_t*)(&telnet_buf[0]);
	telnet_file.remain = size & 0x03 ;//stm32f429 �� flash ��4��������д�룬����4�ֽ�������һ��д�� 
	size >>= 2; 	                  // ���� 4
	
	for(uint32_t i = 0;i < size ; ++i)
	{
		if (HAL_OK != HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,telnet_file.addr,*value))
		{
			Errors("write data error\r\n");
		}
		else
		{
			++value;
			telnet_file.addr += 4;
		}
	}
	
	if (telnet_file.remain) //�˴�ûд��������´�д
		memcpy(telnet_buf,&telnet_buf[size<<2],telnet_file.remain);
}


//extern void hal_usart_puts(char * buf, uint16_t len);


void telnet_port_call(void)
{
	if(uip_connected()) 
	{
		telnet_option(TELNET_DO,1);   //�ͻ��˿�������
		printk("telnet connect\r\n");
	}

	if(uip_closed() ||uip_aborted() ||uip_timedout()) 
	{
		if ( telnet_bind == uip_conn )
		{
			telnet_bind = NULL;
			print_DefaultOut(NULL);
		}
		printk("telnet disconnect\r\n");
	}
  

	if(uip_newdata()) 
	{
  		char * recvdata = uip_appdata;
		uint16_t datalen = uip_datalen();
		
		telnet_check_option(&recvdata,&datalen,&telnet_state);

		if (datalen)
		{
			if (TELNET_NORMAL == telnet_state)
				shell_input(&telnet_shell,recvdata,datalen);//�������� shell ����
			else
				telnet_recv_file(recvdata,datalen);
		}
		
		if ( !telnet_bind )
		{
			if ( default_puts == telnet_puts )
				telnet_bind = uip_conn;
		}
		else
		{
			if ( default_puts != telnet_puts )
				telnet_bind =  NULL;
		}
	}

  if (uip_poll()) vLED2_Loop();
  
  if (telnet_buftail)
  {
	  uip_send(telnet_buf,telnet_buftail);
	  telnet_buftail = 0;
  }
}








void telnet_server_pro(void)
{  
	if (telnet_buftail)
	{
		if ( default_puts != telnet_puts ) //���˿�
		  	telnet_bind = NULL;

		if ( telnet_bind )  
		{
			uip_poll_conn(telnet_bind);
			uip_arp_out();
			tapdev_send();
		}

		telnet_buftail = 0;
	}
}





