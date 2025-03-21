/**
  ******************************************************************************
  * @file    usbh_hid_touch.c
  * @author  MCD Application Team
  * @brief   This file is the application layer for USB Host HID Touch Handling.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                      www.st.com/SLA0044
  *
  ******************************************************************************
  */

/* BSPDependencies
- "stm32xxxxx_{eval}{discovery}{nucleo_144}.c"
- "stm32xxxxx_{eval}{discovery}_io.c"
- "stm32xxxxx_{eval}{discovery}{adafruit}_lcd.c"
- "stm32xxxxx_{eval}{discovery}_sdram.c"
EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/
#include "../Inc/usbh_hid_touch.h"
#include "../Inc/usbh_hid_parser.h"


/** @addtogroup USBH_LIB
  * @{
  */

/** @addtogroup USBH_CLASS
  * @{
  */

/** @addtogroup USBH_HID_CLASS
  * @{
  */

/** @defgroup USBH_HID_TOUCH
  * @brief    This file includes HID Layer Handlers for USB Host HID class.
  * @{
  */

/** @defgroup USBH_HID_TOUCH_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup USBH_HID_TOUCH_Private_Defines
  * @{
  */
/**
  * @}
  */


/** @defgroup USBH_HID_TOUCH_Private_Macros
  * @{
  */
/**
  * @}
  */

/** @defgroup USBH_HID_TOUCH_Private_FunctionPrototypes
  * @{
  */
static USBH_StatusTypeDef USBH_HID_TouchDecode(USBH_HandleTypeDef *phost);

/**
  * @}
  */


/** @defgroup USBH_HID_TOUCH_Private_Variables
  * @{
  */
HID_TOUCH_Info_TypeDef    touch_info;
static __ALIGN_BEGIN uint8_t touch_report_data [64] __ALIGN_END;
static __ALIGN_BEGIN uint8_t touch_rx_report_buf [64] __ALIGN_END;

/* Structures defining how to access items in a HID touch report */
/* Access button 1 state. */
static const HID_Report_ItemTypedef prop_b1 =
{
  touch_report_data + 0, /*data*/
  1,     /*size*/
  0,     /*shift*/
  0,     /*count (only for array items)*/
  0,     /*signed?*/
  0,     /*min value read can return*/
  1,     /*max value read can return*/
  0,     /*min value device can report*/
  1,     /*max value device can report*/
  1      /*resolution*/
};

/* Access button 2 state. */
static const HID_Report_ItemTypedef prop_b2 =
{
  touch_report_data + 0, /*data*/
  1,     /*size*/
  1,     /*shift*/
  0,     /*count (only for array items)*/
  0,     /*signed?*/
  0,     /*min value read can return*/
  1,     /*max value read can return*/
  0,     /*min value device can report*/
  1,     /*max value device can report*/
  1      /*resolution*/
};

/* Access button 3 state. */
static const HID_Report_ItemTypedef prop_b3 =
{
  touch_report_data + 0, /*data*/
  1,     /*size*/
  2,     /*shift*/
  0,     /*count (only for array items)*/
  0,     /*signed?*/
  0,     /*min value read can return*/
  1,     /*max value read can return*/
  0,     /*min vale device can report*/
  1,     /*max value device can report*/
  1      /*resolution*/
};

/* Access x coordinate change. */
static const HID_Report_ItemTypedef prop_x =
{
  touch_report_data + 4, /*data*/
  16,     /*size*/
  0,     /*shift*/
  0,     /*count (only for array items)*/
  0,     /*signed?*/
  0,     /*min value read can return*/
  0xFFFF,/*max value read can return*/
  0,     /*min vale device can report*/
  0xFFFF,/*max value device can report*/
  1      /*resolution*/
};

/* Access y coordinate change. */
static const HID_Report_ItemTypedef prop_y =
{
  touch_report_data + 6, /*data*/
  16,     /*size*/
  0,     /*shift*/
  0,     /*count (only for array items)*/
  0,     /*signed?*/
  0,     /*min value read can return*/
  0xFFFF,/*max value read can return*/
  0,     /*min vale device can report*/
  0xFFFF,/*max value device can report*/
  1      /*resolution*/
};


/**
  * @}
  */


/** @defgroup USBH_HID_TOUCH_Private_Functions
  * @{
  */

/**
  * @brief  USBH_HID_TouchInit
  *         The function init the HID touch.
  * @param  phost: Host handle
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_TouchInit(USBH_HandleTypeDef *phost)
{
  uint32_t i;
  HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

  touch_info.x = 0U;
  touch_info.y = 0U;
  touch_info.buttons[0] = 0U;
  touch_info.buttons[1] = 0U;
  touch_info.buttons[2] = 0U;

  for (i = 0; i < (sizeof touch_report_data / sizeof touch_report_data [0]); i++)
  {
    touch_report_data[i] = 0U;
  }
  for (i = 0; i < (sizeof touch_rx_report_buf / sizeof touch_rx_report_buf [0]); i++)
  {
    touch_rx_report_buf[i] = 0U;
  }

  if (HID_Handle->length > sizeof(touch_report_data))
  {
    HID_Handle->length = (uint16_t)sizeof(touch_report_data);
  }
  HID_Handle->pHidReportData = (void *)touch_rx_report_buf;
  USBH_HID_FifoInit(&HID_Handle->fifo, phost->device.Data, (uint16_t)(HID_QUEUE_SIZE * sizeof(touch_report_data)));

  return USBH_OK;
}

/**
  * @brief  USBH_HID_GetTouchInfo
  *         The function return touch information.
  * @param  phost: Host handle
  * @retval touch information
  */
HID_TOUCH_Info_TypeDef *USBH_HID_GetTouchInfo(USBH_HandleTypeDef *phost)
{
  if (USBH_HID_TouchDecode(phost) == USBH_OK)
  {
    return &touch_info;
  }
  else
  {
    return NULL;
  }
}

/**
  * @brief  USBH_HID_TouchDecode
  *         The function decode touch data.
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_TouchDecode(USBH_HandleTypeDef *phost)
{
  HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

  if (HID_Handle->length == 0U)
  {
    return USBH_FAIL;
  }
  /*Fill report */
  if (USBH_HID_FifoRead(&HID_Handle->fifo, &touch_report_data, HID_Handle->length) ==  HID_Handle->length)
  {
	  //printhex(0, touch_report_data, HID_Handle->length);
    /*Decode report */
    touch_info.x = HID_ReadItem(& prop_x, 0U);
    touch_info.y = HID_ReadItem(& prop_y, 0U);
    touch_info.buttons[0] = HID_ReadItem(& prop_b1, 0U);

    return USBH_OK;
  }
  return   USBH_FAIL;
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */


/**
  * @}
  */
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
