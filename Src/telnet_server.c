

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


struct shell_buf stTelnetShell;
struct uip_conn *telnet_bind = NULL;
static uint8_t	 cTelnetState = TELNET_NORMAL;

__align(4) char acTelnetBuf[UIP_TCP_MSS+4] = {0};

volatile int buftail = 0;

void vTelnetOut(char * buf,uint16_t len)
{
	if (cTelnetState == TELNET_NORMAL && len + buftail < UIP_TCP_MSS) 
	{
		memcpy(&acTelnetBuf[buftail],buf,len);
		buftail += len;
	}
	else
		buftail = 0;
}




static void
vTelnetOpt(uint8_t option, uint8_t value) //vTelnetOpt(TELNET_DONT,1)
{
	uint32_t iNewTail = 3 + buftail;
	if (iNewTail < UIP_TCP_MSS) 
	{
		char * pBuf = &acTelnetBuf[buftail];
		*pBuf = (char)TELNET_IAC;
		*++pBuf = (char)option;
		*++pBuf = (char)value;
		buftail = iNewTail;
	}
}


void vShell_EraseBuf(void * arg)
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
	SHELL_MALLOC(&stTelnetShell ,vTelnetOut);	
	shell_register_command("telnet-erase",vShell_EraseBuf);	
	uip_listen(HTONS(23));
}


#if 0  //从 telnet 接收文件并生成为 telnet.bin

void vTelnet_Input(uint8_t * pcInput,uint16_t len)
{
	static FRESULT cFileState = FR_NO_FILE;
	static FIL stTelnetFile = {0};		 /* File object */
	static uint16_t sPktTail = 0;
	static uint8_t cLastPktDeal = 0;
	
	uint8_t * pcCopy;
	uint8_t * pcPktEnd;

	uint32_t iByteWritten;

	if (TELNET_NORMAL != cTelnetState) 
		goto TransmitBin;
	
Normal:
	while(len > 2)
	{
		uint8_t telnet_opt = pcInput[1] ;
		uint8_t telnet_val = pcInput[2] ;
		if( *pcInput == TELNET_IAC && telnet_opt > 250 ) 
		{
			pcInput += 3;
			len -= 3;
			
			if (0 == telnet_val) //只回复二进制命令
			{
				if (TELNET_WILL == telnet_opt)
					cTelnetState = TELNET_BIN_TRAN;
				else
					vTelnetOpt(telnet_opt, telnet_val);
			}
		}
		else
			break;
	}

	if ( len )
		shell_input(&stTelnetShell,(char*)pcInput,len);

	return ;
	
TransmitBin:
	
	if( pcInput[0] == TELNET_IAC && 0 == pcInput[2] && pcInput[1] > 250) 
	{
		if (FR_OK == cFileState)
		{
			if (sPktTail)
			{
				cFileState = f_write(&stTelnetFile,(void*)acTelnetBuf, sPktTail, &iByteWritten);
				if ((iByteWritten != sPktTail) || (cFileState != FR_OK))
					Errors("telnet write reamain data error\r\n");
				sPktTail = 0;
			}
			sprintf(&acTelnetBuf[4],"\r\nGet file,size:%d byte\r\n",f_size(&stTelnetFile));
			vTelnetOut(&acTelnetBuf[4],strlen(&acTelnetBuf[4]));
			f_close(&stTelnetFile);
			cFileState = FR_NO_FILE;
		}
		cTelnetState = TELNET_NORMAL;
		goto Normal;
	}

	if (TELNET_BIN_TRAN != cTelnetState) return;

	if (cFileState) //(0)//文件未打开
	{ 
		sPktTail = 0;
		cLastPktDeal = 0;
		cFileState = f_open(&stTelnetFile, "telnet.bin", FA_CREATE_ALWAYS | FA_WRITE);
		if (FR_OK != cFileState)
			cTelnetState = TELNET_BIN_ERROR;
	}
	
	pcCopy = pcInput+cLastPktDeal;//0xff 0xff  被分包的情况，跳过第一个 ff
	pcPktEnd = pcInput+len ;
	cLastPktDeal = (pcInput[len-1] == 0xff && pcInput[len-2] != 0xff);//0xff 0xff  被分包的情况

	for(uint8_t * pcBuf = (uint8_t*)&acTelnetBuf[sPktTail];  pcCopy < pcPktEnd ;++sPktTail)
	{
		*pcBuf++ = *pcCopy++ ;
		if (*pcCopy == 0xff) ++pcCopy;//如果文件中存在 0xff ，在 SecureCRT 会发两个 0xff ，需要剔除一个
	}
	
	uint16_t sRemain = sPktTail & 0x03;//取4的余数
	sPktTail -= sRemain;                //fatfs 以4的整数倍写入，否则容易出错
	cFileState = f_write(&stTelnetFile,(void*)acTelnetBuf, sPktTail, &iByteWritten);
	if ((cFileState != FR_OK)||(iByteWritten != sPktTail))
	{
		f_close(&stTelnetFile);
		cTelnetState = TELNET_BIN_ERROR;
		Errors("telnet write file error\r\n");	
	}

	if (sRemain) //此次没写完的留到下次写
		memcpy(acTelnetBuf,&acTelnetBuf[sPktTail],sRemain);
	sPktTail = sRemain;
	
	return ;
}

#else

void vTelnet_Input(uint8_t * pcInput,uint16_t len)
{
	static uint16_t  sPktTail = 0;
	static uint32_t  iFlashAddr ;
	static uint32_t* piData ;	
	static uint8_t   cLastPktDeal = 0;
	
	uint8_t * pcCopy;
	uint8_t * pcPktEnd;

	if (TELNET_NORMAL != cTelnetState) 
		goto TransmitBin;
	
Normal:
	while(len > 2)
	{
		uint8_t telnet_opt = pcInput[1] ;
		uint8_t telnet_val = pcInput[2] ;
		if( *pcInput == TELNET_IAC && telnet_opt > 250 ) 
		{
			pcInput += 3;
			len -= 3;
			
			if (0 == telnet_val) //只回复二进制命令
			{
				if (TELNET_WILL == telnet_opt)
					cTelnetState = TELNET_BIN_TRAN;
				else
					vTelnetOpt(telnet_opt, telnet_val);
			}
		}
		else
			break;
	}

	if ( len )
		shell_input(&stTelnetShell,(char*)pcInput,len);

	return ;
	
TransmitBin:
	
	if( pcInput[0] == TELNET_IAC && 0 == pcInput[2] && pcInput[1] > 250) 
	{
		cTelnetState = TELNET_NORMAL;
		if (iFlashAddr)
		{
			if (sPktTail)
			{
				HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,iFlashAddr,*piData);
				iFlashAddr += 4;
				sPktTail = 0;
			}
			sprintf(&acTelnetBuf[4],"\r\nGet file,size:%d byte\r\n",iFlashAddr - 0x8060000);
			vTelnetOut(&acTelnetBuf[4],strlen(&acTelnetBuf[4]));
			HAL_FLASH_Lock();
			iFlashAddr = 0;
		}
		goto Normal;
	}

	if (TELNET_BIN_TRAN != cTelnetState) return;

	if (!iFlashAddr) //(0)//文件未打开
	{
		sPktTail = 0;
		cLastPktDeal = 0;
		iFlashAddr = 0x8060000;
		HAL_FLASH_Unlock();     //解锁 flash
		//iUsartHal_IAP_Erase(6);//一般情况下 0x8060000 已擦除
	}
	
	pcCopy = pcInput + cLastPktDeal;//0xff 0xff  被分包的情况，跳过第一个 ff
	pcPktEnd = pcInput + len ;
	cLastPktDeal = (pcInput[len-1] == 0xff && pcInput[len-2] != 0xff);//0xff 0xff  被分包的情况

	for(uint8_t * pcBuf = (uint8_t*)&acTelnetBuf[sPktTail];  pcCopy < pcPktEnd ;++sPktTail)
	{
		*pcBuf++ = *pcCopy++ ;
		if (*pcCopy == 0xff) ++pcCopy;//如果文件中存在 0xff ，在 SecureCRT 会发两个 0xff ，需要剔除一个
	}
	
	//flash 以4的整数倍写入，不足4字节留到下一包写入  
	uint16_t sRemain = sPktTail & 0x03;
	sPktTail >>= 2;                   // 除于 4
	piData = (uint32_t*)acTelnetBuf;

	for(uint32_t iCnt = 0;iCnt < sPktTail ; ++iCnt,++piData)
	{
		if (HAL_OK != HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,iFlashAddr,*piData))
		{
			cTelnetState = TELNET_BIN_ERROR;
			iFlashAddr = 0;
			Errors("write data error\r\n");
			return ;
		}
		else
			iFlashAddr += 4;
	}
	
	if (sRemain) //此次没写完的留到下次写
		memcpy(acTelnetBuf,&acTelnetBuf[sPktTail<<2],sRemain);
	sPktTail = sRemain;
	
	return ;
}


#endif






extern void vUsartHal_Output(char * buf, uint16_t len);


void telnet_port_call(void)
{
	if(uip_connected()) 
	{
		vTelnetOpt(TELNET_DO,1);   //客户端开启回显
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
	  vTelnet_Input((uint8_t*)uip_appdata,uip_datalen());
	  if ( !telnet_bind )
	  {
		  if ( default_puts == vTelnetOut )
			  telnet_bind = uip_conn;
	  }
	  else
	  {
		  if ( default_puts != vTelnetOut )
			  telnet_bind =  NULL;
	  }
  }

  if (uip_poll()) vLED2_Loop();
  
  if (buftail)
  {
	  uip_send(acTelnetBuf,buftail);
	  buftail = 0;
  }
}








void telnet_server_pro(void)
{  
	if (buftail)
	{
		if ( default_puts != vTelnetOut ) //解绑端口
		  	telnet_bind = NULL;

		if ( telnet_bind )  
		{
			uip_poll_conn(telnet_bind);
			uip_arp_out();
			tapdev_send();
		}

		buftail = 0;
	}
}




