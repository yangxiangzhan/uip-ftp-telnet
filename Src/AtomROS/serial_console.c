/**
  ******************************************************************************
  * @file           serial_console.c
  * @author         杨翔湛
  * @brief          serial_console file
                    串口控制台文件。文件不直接操作硬件，依赖 serial_hal
  ******************************************************************************
  *
  * COPYRIGHT(c) 2018 GoodMorning
  *
  ******************************************************************************
  */
/* Includes ---------------------------------------------------*/
#include <string.h>
#include <stdarg.h>
#include <stdint.h> //定义了很多数据类型

#include "AtomROS.h"

#include "shell.h"
#include "serial_hal.h"
#include "serial_console.h"

#include "stm32f429xx.h" //for SCB->VTOR

//--------------------相关宏定义及结构体定义--------------------


#define UASRT_IAP_BUF_SIZE  1024

const static char acIAPtip[]=
"\r\n\
 ____   ___   ____\r\n\
|_  _| / _ \\ |  _ \\\r\n\
 _||_ |  _  ||  __/don't press any key now\r\n\
|____||_| |_||_|   ";



static struct st_console_iap
{
	uint32_t iFlashAddr;
	uint32_t iTimeOut;
}
stUsartIAP;

ros_task_t stSerialConsoleTask;
ros_task_t stUsartIapTask;
ros_task_t stUsartIapTimeoutTask;
ros_semaphore_t rosSerialRxSem;



struct shell_buf stUsartShellBuf;
//------------------------------相关函数声明------------------------------




//------------------------------华丽的分割线------------------------------



/**
	* @brief task_UsartIapCompletePro iap 升级超时判断任务
	* @param void
	* @return NULL
*/
int task_UsartIapCompletePro(void * arg)
{
	TASK_BEGIN();//任务开始
	
	printk("loading");
	
	task_cond_wait(OS_current_time - stUsartIAP.iTimeOut > 2000) ;//超时 2.5 s
	
	task_cancel(&stUsartIapTask); // 删除 iap 任务
	
	vUsartHal_LockFlash();   //由于要写完最后一包数据才能上锁，所以上锁放在 task_UsartIapCompletePro 中
	
	vUsartHal_RxPktMaxLen(COMMANDLINE_MAX_LEN);
	
	uint32_t filesize = (SCB->VTOR == FLASH_BASE) ? (stUsartIAP.iFlashAddr-APP_ADDR):(stUsartIAP.iFlashAddr-IAP_ADDR);
	printk("\r\nupdate completed!\r\nupdate package size:%d byte\r\n",filesize);

	TASK_END();
}


/** 
	* @brief task_UsartIapPro  iap 升级任务
	* @param void
	* @return NULL
*/
int task_UsartIapPro(void * arg)
{
	uint16_t pktsize;
	char   * pktdata;

	uint32_t * piData;
	
	TASK_BEGIN();//任务开始
	
	vUsartHal_UnlockFlash();//由于要写完最后一包数据才能上锁，所以上锁放在 task_UsartIapCompletePro 中
	
	if (SCB->VTOR == FLASH_BASE)//如果是 iap 模式，擦除 app 区域
	{
		stUsartIAP.iFlashAddr = APP_ADDR;
		if(iUsartHal_IAP_Erase(5)) //app 地址在 0x8020000,删除扇区5数据
		{
			Error_Here();//发生错误了	
			task_exit();
		}
	}	
 	else
 	{
		stUsartIAP.iFlashAddr = IAP_ADDR;
		if(iUsartHal_IAP_Erase(0)) //iap 地址在 0x8000000,删除扇区0数据
		{
			Error_Here();//发生错误了	
			task_exit();
		}
		
		if(iUsartHal_IAP_Erase(1)) //扇区1数据
		{
			Error_Here();//发生错误了	
			task_exit();
		}
	
		if(iUsartHal_IAP_Erase(2)) //扇区2数据
		{
			Error_Here();//发生错误了	
			task_exit();
		}	
 	}
	
	color_printk(light_green,"\033[2J\033[%d;%dH%s",0,0,acIAPtip);//清屏
	
	while(1)
	{
		//task_cond_wait(iUsartHal_RxPktOut(&pktdata,&pktsize));
		task_semaphore_wait(&rosSerialRxSem);//等待接收到一包数据
		
		while (iUsartHal_RxPktOut(&pktdata,&pktsize))
		{
			piData = (uint32_t*)pktdata;
			
			for (uint32_t iCnt = 0;iCnt < pktsize ; iCnt += 4) // f4 可以以 word 写入
			{
				vUsartHal_IAP_Write(stUsartIAP.iFlashAddr,*piData);
				stUsartIAP.iFlashAddr += 4;
				++piData;
			}
			
			stUsartIAP.iTimeOut = OS_current_time;//更新时间戳
			
			if (task_is_exited(&stUsartIapTimeoutTask))
				task_create(&stUsartIapTimeoutTask,NULL,task_UsartIapCompletePro,NULL);
			else
				printk(".");
		}
	}
	
	TASK_END();
}




/** 
	* @brief task_SerialConsole  串口控制台处理
	* @param void
	* @return NULL
*/
int task_SerialConsole(void * arg)
{
	uint16_t ucLen ;
	char * HalPkt;

	TASK_BEGIN();//任务开始

	task_semaphore_init(&rosSerialRxSem);
	
	while(1)
	{
		task_semaphore_wait(&rosSerialRxSem);
		//task_cond_wait(iUsartHal_RxPktOut(&HalPkt,&ucLen));

		while (iUsartHal_RxPktOut(&HalPkt,&ucLen))
			vShell_Input(&stUsartShellBuf,HalPkt,ucLen);//数据帧传入应用层
		
		task_join(&stUsartIapTask); //在线升级时数据流往 iap 任务走
	}
	
	TASK_END();
}




/** 
	* @brief vShell_UpdateCmd  iap 升级命令
	* @param void
	* @return NULL
*/
void vShell_UpdateCmd(void * arg)
{
	task_create(&stUsartIapTask,NULL,task_UsartIapPro,NULL);
	vUsartHal_RxPktMaxLen(UASRT_IAP_BUF_SIZE);
}





void vSerialConsole_Init(char * info)
{
	vUsartHal_Init(); //先初始化硬件层
	
	vShell_InitBuf(&stUsartShellBuf,vUsartHal_Output);


	if (SCB->VTOR != FLASH_BASE)
	{
		vShell_RegisterCommand("update-iap",vShell_UpdateCmd);	
	}
	else
	{
		vShell_RegisterCommand("update-app",vShell_UpdateCmd);	
		vShell_RegisterCommand("jump-app",vShell_JumpCmd);
	}

	vShell_RegisterCommand("reboot"  ,vShell_RebootSystem);
	
	task_create(&stSerialConsoleTask,NULL,task_SerialConsole,NULL);
	
	color_printk(purple,"%s",info);//打印开机信息或者控制台信息
	
	while(iUsartHal_TxBusy()); //等待打印结束
}






