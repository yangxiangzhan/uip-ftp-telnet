/**
  ******************************************************************************
  * @file           iap_hal.c
  * @author         ��ô��
  * @brief          serial_console file
                    ������������Ӳ�����󲿷�
  ******************************************************************************
  *
  * COPYRIGHT(c) 2018 GoodMorning
  *
  ******************************************************************************
  */
/* Includes ---------------------------------------------------*/


#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "iap_hal.h"


//------------------------------���� IAP ���------------------------------


/**
	* @brief    iap_erase_flash console ���� flash ĳ������
	* @param    ��
	* @return   ��
*/
int iap_erase_flash(uint32_t SECTOR)
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
void iap_write_flash(uint32_t FlashAddr,uint32_t FlashData)
{
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,FlashAddr,FlashData);
}



/**
	* @brief    iap_lock_flash console ���� flash
	* @param    ��
	* @return   ��
*/
void iap_lock_flash(void)
{
	HAL_FLASH_Lock();
}



/**
	* @brief    iap_unlock_flash console ���� flash
	* @param    ��
	* @return   ��
*/
void iap_unlock_flash(void)
{
	HAL_FLASH_Unlock();
}



/**
	* @brief    vSystemReboot Ӳ������
	* @param    ��
	* @return  
*/
void shell_reboot_command(void * arg)
{
	NVIC_SystemReset();
}



/**
	* @brief    vShell_JumpToAppCmd console ���ڷ���һ����������ж�
	* @param    ��
	* @return   ��
*/
void shell_jump_command(void * arg)
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




