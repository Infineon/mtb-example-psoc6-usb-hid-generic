/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the USB HID Generic Example
*              for ModusToolbox.
*
* Related Document: See Readme.md
*
*
*******************************************************************************
* Copyright 2019-2021, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

#include "cy_pdl.h"
#include "cyhal.h"
#include "cybsp.h"
#include "cycfg.h"
#include "cy_usb_dev.h"
#include "cycfg_usbdev.h"


/*******************************************************************************
* Macros
********************************************************************************/
#define USB_IN_ENDPOINT     0x01
#define USB_OUT_ENDPOINT    0x02
#define MAX_NUM_BYTES       64u


/*******************************************************************************
* Function Prototypes
********************************************************************************/
static void usb_high_isr(void);
static void usb_medium_isr(void);
static void usb_low_isr(void);


/*******************************************************************************
* Global Variables
********************************************************************************/
/* USB Interrupt Configuration */
const cy_stc_sysint_t usb_high_interrupt_cfg =
{
    .intrSrc = (IRQn_Type) usb_interrupt_hi_IRQn,
    .intrPriority = 5U,
};
const cy_stc_sysint_t usb_medium_interrupt_cfg =
{
    .intrSrc = (IRQn_Type) usb_interrupt_med_IRQn,
    .intrPriority = 6U,
};
const cy_stc_sysint_t usb_low_interrupt_cfg =
{
    .intrSrc = (IRQn_Type) usb_interrupt_lo_IRQn,
    .intrPriority = 7U,
};

/* USBDEV context variables */
cy_stc_usbfs_dev_drv_context_t  usb_drvContext;
cy_stc_usb_dev_context_t        usb_devContext;
cy_stc_usb_dev_hid_context_t    usb_hidContext;

/* Allocates static buffer for the USB data endpoint. The allocated buffer is
 * aligned on a 2 byte boundary
 */
CY_USB_DEV_ALLOC_ENDPOINT_BUFFER(usb_buffer, MAX_NUM_BYTES);


/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function for CM4 CPU. It initializes the USB device block and
* enumerates as a HID Generic device. It checks if any data is received from
* host and sends back the same data.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    cy_en_usb_dev_ep_state_t ep_state;
    uint32_t count;
    uint32_t read_count;

    /* Initialize the device and board peripherals */
    result = cybsp_init() ;
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize the User LED */
    cyhal_gpio_init((cyhal_gpio_t) CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);

    /* Initialize the USB device */
    Cy_USB_Dev_Init(CYBSP_USBDEV_HW, &CYBSP_USBDEV_config, &usb_drvContext, 
                    &usb_devices[0], &usb_devConfig, &usb_devContext);

    /* Initialize the HID Class */
    Cy_USB_Dev_HID_Init(&usb_hidConfig, &usb_hidContext, &usb_devContext);

    /* Initialize the USB interrupts */
    Cy_SysInt_Init(&usb_high_interrupt_cfg,   &usb_high_isr);
    Cy_SysInt_Init(&usb_medium_interrupt_cfg, &usb_medium_isr);
    Cy_SysInt_Init(&usb_low_interrupt_cfg,    &usb_low_isr);   

    /* Enable the USB interrupts */
    NVIC_EnableIRQ(usb_high_interrupt_cfg.intrSrc);
    NVIC_EnableIRQ(usb_medium_interrupt_cfg.intrSrc);
    NVIC_EnableIRQ(usb_low_interrupt_cfg.intrSrc);

    /* Make device appear on the bus. This function call is blocking, 
     * it waits till the device enumerates 
     */
    Cy_USB_Dev_Connect(true, CY_USB_DEV_WAIT_FOREVER, &usb_devContext);

    /* Turn on User LED after enumeration */
    cyhal_gpio_write(CYBSP_USER_LED1, CYBSP_LED_STATE_ON);

    /* Enable OUT endpoint to receive data from host */
    Cy_USB_Dev_StartReadEp(USB_OUT_ENDPOINT, &usb_devContext);

    for(;;)
    {
        cyhal_system_sleep();

        /* Read the OUT Endpoint state */
        ep_state = Cy_USBFS_Dev_Drv_GetEndpointState(CYBSP_USBDEV_HW, 
                                                     USB_OUT_ENDPOINT, 
                                                     &usb_drvContext);

        /* Check if any data from USB host is ready to be read */
        if (ep_state == CY_USB_DEV_EP_COMPLETED)
        {
            /* Turn off User LED when data is received */
            cyhal_gpio_write(CYBSP_USER_LED1, CYBSP_LED_STATE_OFF);

            /* Check how many bytes are available to read */
            count = Cy_USB_Dev_GetEpNumToRead(USB_OUT_ENDPOINT, &usb_devContext);

            /* Copy data to the application usb buffer */
            Cy_USB_Dev_ReadEpBlocking(USB_OUT_ENDPOINT, usb_buffer, 
                                      count, &read_count, 
                                      CY_USB_DEV_WAIT_FOREVER, &usb_devContext);

            /* Write data back to the host */
            Cy_USB_Dev_WriteEpBlocking(USB_IN_ENDPOINT, usb_buffer, read_count, 
                                       CY_USB_DEV_WAIT_FOREVER, &usb_devContext);

            /* Enable OUT endpoint to receive data from host again */
            Cy_USB_Dev_StartReadEp(USB_OUT_ENDPOINT, &usb_devContext);

            /* Turn on User LED after data is sent */
            cyhal_gpio_write(CYBSP_USER_LED1, CYBSP_LED_STATE_ON);
        }
    }
}


/***************************************************************************
* Function Name: usb_high_isr
********************************************************************************
* Summary:
*  This function process the high priority USB interrupts.
*
***************************************************************************/
static void usb_high_isr(void)
{
    /* Call interrupt processing */
    Cy_USBFS_Dev_Drv_Interrupt(CYBSP_USBDEV_HW, 
                               Cy_USBFS_Dev_Drv_GetInterruptCauseHi(CYBSP_USBDEV_HW), 
                               &usb_drvContext);
}


/***************************************************************************
* Function Name: usb_medium_isr
********************************************************************************
* Summary:
*  This function process the medium priority USB interrupts.
*
***************************************************************************/
static void usb_medium_isr(void)
{
    /* Call interrupt processing */
    Cy_USBFS_Dev_Drv_Interrupt(CYBSP_USBDEV_HW, 
                               Cy_USBFS_Dev_Drv_GetInterruptCauseMed(CYBSP_USBDEV_HW), 
                               &usb_drvContext);
}


/***************************************************************************
* Function Name: usb_low_isr
********************************************************************************
* Summary:
*  This function process the low priority USB interrupts.
*
**************************************************************************/
static void usb_low_isr(void)
{
    /* Call interrupt processing */
    Cy_USBFS_Dev_Drv_Interrupt(CYBSP_USBDEV_HW, 
                               Cy_USBFS_Dev_Drv_GetInterruptCauseLo(CYBSP_USBDEV_HW), 
                               &usb_drvContext);
}


/* [] END OF FILE */