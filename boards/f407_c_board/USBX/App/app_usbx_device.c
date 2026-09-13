/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_usbx_device.c
  * @author  MCD Application Team
  * @brief   USBX Device applicative file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_usbx_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ux_device_cdc_acm.h"
#include "ux_device_class_cdc_acm.h"
#include "ux_device_descriptors.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define PNX_USBX_MEMORY_SIZE (10U * 1024U)
#define PNX_USB_CDC_CONFIGURATION_NUMBER 1U
#define PNX_USB_CDC_INTERFACE_NUMBER 0U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */
/**
  * @brief  Application USBX Device Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT MX_USBX_Device_Init(VOID *memory_ptr)
{
  UINT ret = UX_SUCCESS;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;

  /* USER CODE BEGIN MX_USBX_Device_MEM_POOL */
  if (byte_pool == UX_NULL)
  {
    return UX_INVALID_PARAMETER;
  }
  /* USER CODE END MX_USBX_Device_MEM_POOL */

  /* USER CODE BEGIN MX_USBX_Device_Init */
  ret = tx_byte_allocate(byte_pool, (VOID **)&usbx_memory,
                         PNX_USBX_MEMORY_SIZE, TX_NO_WAIT);
  if (ret != TX_SUCCESS)
  {
    return ret;
  }

  ret = ux_system_initialize(usbx_memory, PNX_USBX_MEMORY_SIZE,
                             UX_NULL, 0U);
  if (ret != UX_SUCCESS)
  {
    return ret;
  }

  device_framework_high_speed =
    USBD_Get_Device_Framework_Speed(USBD_HIGH_SPEED,
                                    &device_framework_hs_length);
  device_framework_full_speed =
    USBD_Get_Device_Framework_Speed(USBD_FULL_SPEED,
                                    &device_framework_fs_length);
  string_framework = USBD_Get_String_Framework(&string_framework_length);
  language_id_framework =
    USBD_Get_Language_Id_Framework(&language_id_framework_length);

  ret = ux_device_stack_initialize(
    device_framework_high_speed, device_framework_hs_length,
    device_framework_full_speed, device_framework_fs_length,
    string_framework, string_framework_length,
    language_id_framework, language_id_framework_length, UX_NULL);
  if (ret != UX_SUCCESS)
  {
    return ret;
  }

  cdc_parameter.ux_slave_class_cdc_acm_instance_activate =
    USBD_CDC_ACM_Activate;
  cdc_parameter.ux_slave_class_cdc_acm_instance_deactivate =
    USBD_CDC_ACM_Deactivate;
  cdc_parameter.ux_slave_class_cdc_acm_parameter_change =
    USBD_CDC_ACM_ParameterChange;

  ret = ux_device_stack_class_register(
    _ux_system_slave_class_cdc_acm_name,
    ux_device_class_cdc_acm_entry,
    PNX_USB_CDC_CONFIGURATION_NUMBER,
    PNX_USB_CDC_INTERFACE_NUMBER,
    &cdc_parameter);
  if (ret != UX_SUCCESS)
  {
    return ret;
  }

  /* USER CODE END MX_USBX_Device_Init */

  return ret;
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
