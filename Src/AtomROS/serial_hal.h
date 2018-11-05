#ifndef __CONSOLE_HAL_H__
#define __CONSOLE_HAL_H__
#ifdef __cplusplus
 extern "C" {
#endif

#define IAP_ADDR 0x08000000
#define APP_ADDR 0x08020000


// stm32f103vet6 ram��ʼ��ַΪ 0x20000000 ����С 0x10000
// ֱ���� ram �����ֶεĵ�ַ��Ϊ�� iap ģʽ�ı�־λ
// �� app �У��ȶԱ�־λ������λ��ʾ��Ȼ����ת�� iap 
#define IAP_FLAG_ADDR       0x2000FF00 

#define IAP_UPDATE_CMD_FLAG 0x1234ABCD

#define IAP_SUCCESS_FLAG      0x54329876

#define iIAP_GetUpdateCmd() (*(__IO uint32_t *) IAP_FLAG_ADDR == IAP_UPDATE_CMD_FLAG)

#define vIAP_SetUpdateCmd() (*(__IO uint32_t *) IAP_FLAG_ADDR = IAP_UPDATE_CMD_FLAG)

#define iIAP_GetSuccessCmd() (*(__IO uint32_t *) IAP_FLAG_ADDR == IAP_SUCCESS_FLAG)

#define vIAP_SetSuccessCmd() (*(__IO uint32_t *) IAP_FLAG_ADDR = IAP_SUCCESS_FLAG)

#define vIAP_ClearFlag()       (*(__IO uint32_t *) IAP_FLAG_ADDR = 0)




void serial_puts(char * buf,uint16_t len);

int  serial_pkt_queue_out(char ** data,uint16_t * len);

void serial_rxpkt_max_len(uint16_t MaxLen);

int  serial_busy(void);

void hal_serial_init(void);

void hal_serial_deinit(void);

//------------------------------���� IAP ���------------------------------
int  iap_erase_flash(uint32_t SECTOR);	 
	 
void iap_write_flash(uint32_t FlashAddr,uint32_t FlashData);

void iap_unlock_flash(void);

void iap_lock_flash(void);

//------------------------------����̨����------------------------------
void shell_jump_command(void * arg);
void shell_reboot_command(void * arg);

	 
#ifdef __cplusplus
}
#endif
#endif /* __CONSOLE_HAL_H__ */

/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
