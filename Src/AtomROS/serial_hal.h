#ifndef __CONSOLE_HAL_H__
#define __CONSOLE_HAL_H__
#ifdef __cplusplus
 extern "C" {
#endif

#define IAP_ADDR 0x08000000
#define APP_ADDR 0x08020000


// stm32f103vet6 ram起始地址为 0x20000000 ，大小 0x10000
// 直接用 ram 区高字段的地址作为进 iap 模式的标志位
// 在 app 中，先对标志位进行置位提示，然后跳转到 iap 
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

//------------------------------串口 IAP 相关------------------------------
int  iap_erase_flash(uint32_t SECTOR);	 
	 
void iap_write_flash(uint32_t FlashAddr,uint32_t FlashData);

void iap_unlock_flash(void);

void iap_lock_flash(void);

//------------------------------控制台命令------------------------------
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
