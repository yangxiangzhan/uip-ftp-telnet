

#include "uip.h"
#include "telnet_server.h"
#include "ustdio.h"

#include <string.h>
#include "shell.h"
#include "gpio.h"
#include "uip_arp.h"
#include "tapdev.h"

#include "fatfs.h"

#define TELNET_WILL  251
#define TELNET_WONT  252
#define TELNET_DO    253
#define TELNET_DONT  254
#define TELNET_IAC   255


#define TELNET_NORMAL	   0
#define TELNET_BIN_TRAN    1
#define TELNET_BIN_ERROR   2




/*---------------------------------------------------------------------------*/


static struct shell_buf telent_shellbuf;
static struct uip_conn * telnet_bind = NULL;
static uint8_t	 telnet_state = TELNET_NORMAL;
static __align(4) char telnet_buf[UIP_TCP_MSS+4] = {0};
static volatile int telnet_buftail = 0;

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
	
	FlashEraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS; //擦除类型，扇区擦除 
	FlashEraseInit.Sector       = 7;                       //0x8060000 在 F429 扇区7，擦除
	FlashEraseInit.NbSectors    = 1;                       //一次只擦除一个扇区
	FlashEraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;   //电压范围，VCC=2.7~3.6V之间!!
	
	HAL_FLASH_Unlock();
	HAL_FLASHEx_Erase(&FlashEraseInit,&SectorError);
	HAL_FLASH_Lock();
	printk("done\r\n");
}



void telnet_server_init(void)
{
	SHELL_MALLOC(&telent_shellbuf ,telnet_puts);	
	shell_register_command("telnet-erase",erase_telnet_flash);	
	uip_listen(HTONS(23));
}


#if 0  //从 telnet 接收文件并生成为 telnet.bin

void telnet_input(uint8_t * srt,uint16_t len)
{
	static FRESULT cFileState = FR_NO_FILE;
	static FIL stTelnetFile = {0};		 /* File object */
	static uint16_t pkt_tail = 0;
	static uint8_t pkt_remix = 0;
	
	uint8_t * copy;
	uint8_t * pkt_end;

	uint32_t iByteWritten;

	if (TELNET_NORMAL != telnet_state) 
		goto TransmitBin;
	
Normal:
	while(len > 2)
	{
		uint8_t telnet_opt = srt[1] ;
		uint8_t telnet_val = srt[2] ;
		if( *srt == TELNET_IAC && telnet_opt > 250 ) 
		{
			srt += 3;
			len -= 3;
			
			if (0 == telnet_val) //只回复二进制命令
			{
				if (TELNET_WILL == telnet_opt)
					telnet_state = TELNET_BIN_TRAN;
				else
					telnet_option(telnet_opt, telnet_val);
			}
		}
		else
			break;
	}

	if ( len )
		shell_input(&telent_shellbuf,(char*)srt,len);

	return ;
	
TransmitBin:
	
	if( srt[0] == TELNET_IAC && 0 == srt[2] && srt[1] > 250) 
	{
		if (FR_OK == cFileState)
		{
			if (pkt_tail)
			{
				cFileState = f_write(&stTelnetFile,(void*)telnet_buf, pkt_tail, &iByteWritten);
				if ((iByteWritten != pkt_tail) || (cFileState != FR_OK))
					Errors("telnet write reamain data error\r\n");
				pkt_tail = 0;
			}
			sprintf(&telnet_buf[4],"\r\nGet file,size:%d byte\r\n",f_size(&stTelnetFile));
			telnet_puts(&telnet_buf[4],strlen(&telnet_buf[4]));
			f_close(&stTelnetFile);
			cFileState = FR_NO_FILE;
		}
		telnet_state = TELNET_NORMAL;
		goto Normal;
	}

	if (TELNET_BIN_TRAN != telnet_state) return;

	if (cFileState) //(0)//文件未打开
	{ 
		pkt_tail = 0;
		pkt_remix = 0;
		cFileState = f_open(&stTelnetFile, "telnet.bin", FA_CREATE_ALWAYS | FA_WRITE);
		if (FR_OK != cFileState)
			telnet_state = TELNET_BIN_ERROR;
	}
	
	copy = srt+pkt_remix;//0xff 0xff  被分包的情况，跳过第一个 ff
	pkt_end = srt+len ;
	pkt_remix = (srt[len-1] == 0xff && srt[len-2] != 0xff);//0xff 0xff  被分包的情况

	for(uint8_t * pcBuf = (uint8_t*)&telnet_buf[pkt_tail];  copy < pkt_end ;++pkt_tail)
	{
		*pcBuf++ = *copy++ ;
		if (*copy == 0xff) ++copy;//如果文件中存在 0xff ，在 SecureCRT 会发两个 0xff ，需要剔除一个
	}
	
	uint16_t remain = pkt_tail & 0x03;//取4的余数
	pkt_tail -= remain;                //fatfs 以4的整数倍写入，否则容易出错
	cFileState = f_write(&stTelnetFile,(void*)telnet_buf, pkt_tail, &iByteWritten);
	if ((cFileState != FR_OK)||(iByteWritten != pkt_tail))
	{
		f_close(&stTelnetFile);
		telnet_state = TELNET_BIN_ERROR;
		Errors("telnet write file error\r\n");	
	}

	if (remain) //此次没写完的留到下次写
		memcpy(telnet_buf,&telnet_buf[pkt_tail],remain);
	pkt_tail = remain;
	
	return ;
}

#else

static void telnet_input(uint8_t * srt,uint16_t len)
{
	static uint32_t  flash_addr ;
	static uint32_t* value ;	
	static uint16_t  pkt_tail = 0;
	static uint8_t   pkt_remix = 0;
	uint8_t        * pkt_end;
	uint8_t        * copy;

	if (TELNET_NORMAL != telnet_state) 
		goto TransmitBin;
	
Normal:
	while(len > 2)
	{
		uint8_t telnet_opt = srt[1] ;
		uint8_t telnet_val = srt[2] ;
		if( *srt == TELNET_IAC && telnet_opt > 250 ) 
		{
			srt += 3;
			len -= 3;
			
			if (0 == telnet_val) //只回复二进制命令
			{
				if (TELNET_WILL == telnet_opt)
					telnet_state = TELNET_BIN_TRAN;
				else
					telnet_option(telnet_opt, telnet_val);
			}
		}
		else
			break;
	}

	if ( len )
		shell_input(&telent_shellbuf,(char*)srt,len);

	return ;
	
TransmitBin:
	
	if( srt[0] == TELNET_IAC && 0 == srt[2] && srt[1] > 250) 
	{
		telnet_state = TELNET_NORMAL;
		if (flash_addr)
		{
			if (pkt_tail)
			{
				HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,flash_addr,*value);
				flash_addr += 4;
				pkt_tail = 0;
			}
			sprintf(&telnet_buf[4],"\r\nGet file,size:%d byte\r\n",flash_addr - 0x8060000);
			telnet_puts(&telnet_buf[4],strlen(&telnet_buf[4]));
			HAL_FLASH_Lock();
			flash_addr = 0;
		}
		goto Normal;
	}

	if (TELNET_BIN_TRAN != telnet_state) 
		return;

	if (!flash_addr) //(0)//文件未打开
	{
		pkt_tail = 0;
		pkt_remix = 0;
		flash_addr = 0x8060000;
		HAL_FLASH_Unlock();     //解锁 flash
		//iUsartHal_IAP_Erase(6);//一般情况下 0x8060000 已擦除
	}
	
	copy = srt + pkt_remix;//0xff 0xff  被分包的情况，跳过第一个 ff
	pkt_end = srt + len ;
	pkt_remix = (srt[len-1] == 0xff && srt[len-2] != 0xff);//0xff 0xff  被分包的情况

	for(uint8_t * buf = (uint8_t*)&telnet_buf[pkt_tail];  copy < pkt_end ;++pkt_tail)
	{
		*buf++ = *copy++ ;
		if (*copy == 0xff) ++copy;//如果文件中存在 0xff ，在 SecureCRT 会发两个 0xff ，需要剔除一个
	}
	
	//flash 以4的整数倍写入，不足4字节留到下一包写入  
	uint16_t remain = pkt_tail & 0x03;
	pkt_tail >>= 2;                   // 除于 4
	value = (uint32_t*)telnet_buf;

	for(uint32_t i = 0;i < pkt_tail ; ++i,++value)
	{
		if (HAL_OK != HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,flash_addr,*value))
		{
			telnet_state = TELNET_BIN_ERROR;
			flash_addr = 0;
			Errors("write data error\r\n");
			return ;
		}
		else
			flash_addr += 4;
	}
	
	if (remain) //此次没写完的留到下次写
		memcpy(telnet_buf,&telnet_buf[pkt_tail<<2],remain);
	
	pkt_tail = remain;
	
	return ;
}


#endif






//extern void hal_usart_puts(char * buf, uint16_t len);


void telnet_port_call(void)
{
	if(uip_connected()) 
	{
		telnet_option(TELNET_DO,1);   //客户端开启回显
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
	  telnet_input((uint8_t*)uip_appdata,uip_datalen());
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
		if ( default_puts != telnet_puts ) //解绑端口
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




