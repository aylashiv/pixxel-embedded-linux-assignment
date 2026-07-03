/*
 * pixxel_controller.c
 *
 * cFS application that:
 *   1. Opens /dev/pixxel to control the platform driver.
 *   2. Subscribes to PIXXEL_CMD_MID on the Software Bus.
 *   3. On PIXXEL_ENABLE_CC  — writes "1" to /dev/pixxel (enables device).
 *   4. On PIXXEL_GET_STATUS_CC — reads /dev/pixxel, sends status telemetry.
 *   5. Acknowledges every command back to pixxel_main via PIXXEL_TLM_MID.
 *
 * pixxel_controller must be listed before pixxel_main in the cFS startup
 * script so it is running before pixxel_main calls CFE_ES_WaitForStartupSync.
 */

#include "cfe.h"
#include "pixxel_controller_events.h"
#include "pixxel_controller_msg.h"
#include "pixxel_msgids.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define PIXXEL_CTRL_PIPE_DEPTH  10
#define PIXXEL_CTRL_PIPE_NAME   "PIXXEL_CTRL_PIPE"
#define PIXXEL_DEV_PATH         "/dev/pixxel"

/* ------------------------------------------------------------------ */
static int pixxel_ctrl_enable_device(int fd)
{
    ssize_t n = write(fd, "1", 1);
    if (n < 0) {
        CFE_EVS_SendEvent(PIXXEL_CTRL_ENABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "PIXXEL_CTRL: write to %s failed", PIXXEL_DEV_PATH);
        return -1;
    }
    CFE_EVS_SendEvent(PIXXEL_CTRL_ENABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "PIXXEL_CTRL: device enabled via %s", PIXXEL_DEV_PATH);
    return 0;
}

static uint8_t pixxel_ctrl_read_status(int fd)
{
    char buf[4] = {0};
    uint8_t status = 0;

    /* rewind so each read starts at offset 0 */
    lseek(fd, 0, SEEK_SET);

    if (read(fd, buf, sizeof(buf) - 1) < 0) {
        CFE_EVS_SendEvent(PIXXEL_CTRL_STATUS_ERR_EID, CFE_EVS_EventType_ERROR,
                          "PIXXEL_CTRL: read from %s failed", PIXXEL_DEV_PATH);
        return 0;
    }

    status = (uint8_t)atoi(buf);
    CFE_EVS_SendEvent(PIXXEL_CTRL_STATUS_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "PIXXEL_CTRL: device status = %u", status);
    return status;
}

static void pixxel_ctrl_send_tlm(uint8_t acked_cc, uint8_t dev_status)
{
    PIXXEL_CTRL_Tlm_t tlm;

    CFE_MSG_Init(CFE_MSG_PTR(tlm.TlmHeader), PIXXEL_TLM_MID, sizeof(tlm));
    tlm.AckedCmdCode = acked_cc;
    tlm.DeviceStatus = dev_status;
    tlm.Spare[0]     = 0;
    tlm.Spare[1]     = 0;

    CFE_SB_TransmitMsg(CFE_MSG_PTR(tlm.TlmHeader), true);
}

/* ------------------------------------------------------------------ */
void PIXXEL_CTRL_AppMain(void)
{
    CFE_SB_PipeId_t  cmd_pipe;
    CFE_SB_Buffer_t *buf;
    CFE_MSG_FcnCode_t fc;
    CFE_Status_t      status;
    int               dev_fd;
    uint32_t          run_status = CFE_ES_RunStatus_APP_RUN;

    CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY);

    /* Open the driver char device */
    dev_fd = open(PIXXEL_DEV_PATH, O_RDWR);
    if (dev_fd < 0) {
        CFE_EVS_SendEvent(PIXXEL_CTRL_DEV_OPEN_ERR_EID, CFE_EVS_EventType_CRITICAL,
                          "PIXXEL_CTRL: cannot open %s — driver loaded?", PIXXEL_DEV_PATH);
        CFE_ES_WriteToSysLog("PIXXEL_CTRL: fatal — cannot open %s\n", PIXXEL_DEV_PATH);
        CFE_ES_ExitApp(CFE_ES_RunStatus_APP_ERROR);
        return;
    }

    /* Create command pipe and subscribe */
    CFE_SB_CreatePipe(&cmd_pipe, PIXXEL_CTRL_PIPE_DEPTH, PIXXEL_CTRL_PIPE_NAME);
    CFE_SB_Subscribe(PIXXEL_CMD_MID, cmd_pipe);

    CFE_EVS_SendEvent(PIXXEL_CTRL_STARTUP_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "PIXXEL_CTRL: started, listening on SB");

    /* Signal cFS that initialisation is complete */
    CFE_ES_WaitForStartupSync(0);

    /* Main command dispatch loop */
    while (CFE_ES_RunLoop(&run_status)) {
        status = CFE_SB_ReceiveBuffer(&buf, cmd_pipe, CFE_SB_PEND_FOREVER);
        if (status != CFE_SUCCESS) {
            CFE_EVS_SendEvent(PIXXEL_CTRL_PIPE_ERR_EID, CFE_EVS_EventType_ERROR,
                              "PIXXEL_CTRL: SB receive error 0x%08X", (unsigned)status);
            continue;
        }

        CFE_MSG_GetFcnCode(&buf->Msg, &fc);

        switch (fc) {
        case PIXXEL_ENABLE_CC:
            pixxel_ctrl_enable_device(dev_fd);
            pixxel_ctrl_send_tlm(PIXXEL_ENABLE_CC, 0);
            break;

        case PIXXEL_GET_STATUS_CC: {
            uint8_t s = pixxel_ctrl_read_status(dev_fd);
            pixxel_ctrl_send_tlm(PIXXEL_GET_STATUS_CC, s);
            break;
        }

        default:
            CFE_EVS_SendEvent(PIXXEL_CTRL_UNKNOWN_CMD_EID, CFE_EVS_EventType_ERROR,
                              "PIXXEL_CTRL: unknown command code %u", (unsigned)fc);
            break;
        }
    }

    close(dev_fd);
    CFE_ES_ExitApp(run_status);
}
