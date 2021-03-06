#include "can.h"
#include "led.h"
#include "stm32f0xx_hal.h"

// CAN control modes
//#define CAN_CTRLMODE_NORMAL             0x00
#define USB_8DEV_CAN_MODE_SILENT        0x01
#define USB_8DEV_CAN_MODE_LOOPBACK      0x02
#define USB_8DEV_MODE_ONESHOT           0x04

// Not supported
//#define CAN_CTRLMODE_3_SAMPLES          0x04
//#define CAN_CTRLMODE_BERR_REPORTING     0x10
//#define CAN_CTRLMODE_FD                 0x20
//#define CAN_CTRLMODE_PRESUME_ACK        0x40
//#define CAN_CTRLMODE_FD_NON_ISO         0x80

CAN_HandleTypeDef can_handle;

static uint8_t enabled; /*< Indicates if CAN interface in enabled. */
static CAN_FilterConfTypeDef sFilterConfig;

static void can_interrupts_enable();
static void can_interrupts_disable();

/**
 * Initialize CAN interface.
 *
 * @return 0 if OK
 */
uint8_t can_init() {
    enabled = 0;
    return 0;
}

/**
 * Request to open the CAN interface.
 *
 * Set up a request and corresponding initialization data to start the CAN
 * interface.
 *
 * @param can_bittiming CAN bit timings to configure CAN.
 * @param ctrlmode flag setting CAN control modes e.g. @see
 * USB_8DEV_CAN_MODE_SILENT
 */
void can_open_req(Can_BitTimingTypeDef* can_bittiming, uint8_t ctrlmode) {
    /* See datasheet p836 on bit timings for SJW, BS1 and BS2
     * tq = (BRP+1).tpclk
     * tsjw = tq.(SJW+1)
     * tbs1 = tq.(TS1+1)
     * tbs2 = tq.(TS2+1)
     * baud = 1/(tsjw+tbs1+tbs2) = 1/(tq.((SJW+1)+(TS1+1)+(TS2+1)))
     */
    static CanTxMsgTypeDef TxMessage;
    static CanRxMsgTypeDef RxMessage;

    // Configure the CAN peripheral
    can_handle.Instance = CANx;
    can_handle.pTxMsg = &TxMessage;
    can_handle.pRxMsg = &RxMessage;

    can_handle.Init.TTCM = DISABLE;
    can_handle.Init.ABOM = DISABLE;
    can_handle.Init.AWUM = DISABLE;
    can_handle.Init.NART = DISABLE;
    if (ctrlmode & USB_8DEV_MODE_ONESHOT) {
        can_handle.Init.NART = ENABLE;
    }
    can_handle.Init.RFLM = DISABLE;
    can_handle.Init.TXFP = DISABLE;
    can_handle.Init.Mode = CAN_MODE_NORMAL;
    if (ctrlmode & USB_8DEV_CAN_MODE_SILENT) {
        can_handle.Init.Mode |= CAN_MODE_SILENT;
    }
    if (ctrlmode & USB_8DEV_CAN_MODE_LOOPBACK) {
        can_handle.Init.Mode |= CAN_MODE_LOOPBACK;
    }
    // The shift is needed because that's how CAN_SJW_xTQ,CAN_BS1_xTQ and
    // CAN_BS2_xTQ are defined
    can_handle.Init.SJW = can_bittiming->sjw << 6*4;
    can_handle.Init.BS1 = can_bittiming->ts1 << 4*4;
    can_handle.Init.BS2 = can_bittiming->ts2 << 5*4;
    can_handle.Init.Prescaler = can_bittiming->brp;

    // Configure the CAN Filter, needed to receive CAN data
    sFilterConfig.FilterNumber = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.BankNumber = 14;
}

/**
 * Open the CAN interface.
 *
 * Initialize and configure the filters for the CAN interface.
 *
 * @return 0 if OK
 */
uint8_t can_open() {
    if (HAL_CAN_Init(&can_handle)) {
        return 1;
    }
    if (HAL_CAN_ConfigFilter(&can_handle, &sFilterConfig)) {
        return 2;
    }
    /*if (HAL_CAN_Receive_IT(&can_handle, CAN_FIFO0)) {*/
        /*return 3;*/
    /*}*/
    can_interrupts_enable();
    enabled = 1;
    return 0;
}

/**
 * Close the CAN interface
 */
uint8_t can_close() {
    can_interrupts_disable();
    if (HAL_CAN_DeInit(&can_handle)) {
        return 1;
    }
    enabled = 0;
    return 0;
}

/**
 * Transmit data over CAN.
 *
 * @param[in] timeout Time in ms to attempt transmit.
 * @return 0 if success
 */
uint8_t can_tx(uint32_t timeout) {
    // If a timeout occurs, the CAN frame is already in the transmit mailbox
    // and the CAN controller will still attempt to send it even after the
    // timeout
    //occurs.
    return HAL_CAN_Transmit(&can_handle, timeout);
}

/**
 * Receive data over CAN.
 *
 * @param[in] timeout Time in ms to attempt receive.
 * @return 0 if success
 */
uint8_t can_rx(uint32_t timeout) {
    return HAL_CAN_Receive(&can_handle, CAN_FIFO0, timeout);
}

/**
 * Check if there are CAN messages pending in receive FIFO.
 *
 * @return Number of messages pending.
 */
uint8_t can_msg_pending() {
    if (!enabled) {
        return 0;
    } else {
        return __HAL_CAN_MSG_PENDING(&can_handle, CAN_FIFO0);
    }
}

static void can_interrupts_enable() {
    /* Enable FIFO0 overrun interrupt */
    // TODO not easily handled by HAL
    //__HAL_CAN_ENABLE_IT(&can_handle, CAN_IT_FOV0);

    /* Enable error warning interrupt */
    __HAL_CAN_ENABLE_IT(&can_handle, CAN_IT_EWG);

    /* Enable error passive interrupt */
    __HAL_CAN_ENABLE_IT(&can_handle, CAN_IT_EPV);

    /* Enable bus-off interrupt */
    __HAL_CAN_ENABLE_IT(&can_handle, CAN_IT_BOF);

    /* Enable last error code interrupt */
    __HAL_CAN_ENABLE_IT(&can_handle, CAN_IT_LEC);

    /* Enable error interrupt */
    __HAL_CAN_ENABLE_IT(&can_handle, CAN_IT_ERR);
}

static void can_interrupts_disable() {
    /* Disable FIFO0 overrun interrupt */
    //__HAL_CAN_ENABLE_IT(&can_handle, CAN_IT_FOV0);

    /* Disable error warning interrupt */
    __HAL_CAN_DISABLE_IT(&can_handle, CAN_IT_EWG);

    /* Disable error passive interrupt */
    __HAL_CAN_DISABLE_IT(&can_handle, CAN_IT_EPV);

    /* Disable bus-off interrupt */
    __HAL_CAN_DISABLE_IT(&can_handle, CAN_IT_BOF);

    /* Disable last error code interrupt */
    __HAL_CAN_DISABLE_IT(&can_handle, CAN_IT_LEC);

    /* Disable error interrupt */
    __HAL_CAN_DISABLE_IT(&can_handle, CAN_IT_ERR);
}
