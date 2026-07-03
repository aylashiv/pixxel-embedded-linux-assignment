/*
 * pixxel_main.c
 *
 * cFS application that orchestrates the pixxel device test sequence:
 *
 *   1. Wait for pixxel_controller to finish initialisation.
 *   2. Send ENABLE command to pixxel_controller via the Software Bus.
 *   3. Wait for the acknowledgement.
 *   4. Sleep 50 ms (allow the driver Status register to propagate).
 *   5. Send GET_STATUS command to pixxel_controller.
 *   6. Wait for status telemetry; print PASS or FAIL.
 *
 * All inter-app communication goes exclusively through the cFS Software Bus.
 */

#include "cfe.h"
#include "pixxel_main_events.h"
#include "pixxel_controller_msg.h"
#include "pixxel_msgids.h"

#define PIXXEL_MAIN_PIPE_DEPTH  10
#define PIXXEL_MAIN_PIPE_NAME   "PIXXEL_MAIN_PIPE"
#define PIXXEL_MAIN_TLM_TIMEOUT 5000  /* ms — how long to wait for controller ack */
#define PIXXEL_STATUS_WAIT_MS   50    /* delay after enable before reading status */

/* ------------------------------------------------------------------ */
static void pixxel_main_send_cmd(CFE_MSG_FcnCode_t fc)
{
    PIXXEL_CTRL_NoArgsCmd_t cmd;

    CFE_MSG_Init(CFE_MSG_PTR(cmd.CmdHeader), PIXXEL_CMD_MID, sizeof(cmd));
    CFE_MSG_SetFcnCode(CFE_MSG_PTR(cmd.CmdHeader), fc);
    CFE_SB_TransmitMsg(CFE_MSG_PTR(cmd.CmdHeader), true);
}

/*
 * Block on the telemetry pipe until the controller sends an acknowledgement
 * for the expected command code.  Returns the telemetry packet or NULL on
 * timeout / error.
 */
static const PIXXEL_CTRL_Tlm_t *
pixxel_main_await_tlm(CFE_SB_PipeId_t pipe, uint8_t expected_cc)
{
    CFE_SB_Buffer_t  *buf;
    CFE_Status_t      rc;
    CFE_MSG_FcnCode_t fc;

    rc = CFE_SB_ReceiveBuffer(&buf, pipe, PIXXEL_MAIN_TLM_TIMEOUT);
    if (rc != CFE_SUCCESS) {
        CFE_EVS_SendEvent(PIXXEL_MAIN_PIPE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "PIXXEL_MAIN: SB receive error 0x%08X waiting for cc=%u",
                          (unsigned)rc, expected_cc);
        return NULL;
    }

    /* tolerate stale messages by re-checking the acked command code */
    CFE_MSG_GetFcnCode(&buf->Msg, &fc);
    (void)fc; /* MID already routed correctly; use AckedCmdCode from payload */

    return (const PIXXEL_CTRL_Tlm_t *)buf;
}

/* ------------------------------------------------------------------ */
void PIXXEL_MAIN_AppMain(void)
{
    CFE_SB_PipeId_t          tlm_pipe;
    const PIXXEL_CTRL_Tlm_t *tlm;
    uint32_t                  run_status = CFE_ES_RunStatus_APP_RUN;

    CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY);

    /* Subscribe to controller telemetry before sending any command */
    CFE_SB_CreatePipe(&tlm_pipe, PIXXEL_MAIN_PIPE_DEPTH, PIXXEL_MAIN_PIPE_NAME);
    CFE_SB_Subscribe(PIXXEL_TLM_MID, tlm_pipe);

    /*
     * Block until all cFS apps (including pixxel_controller) have completed
     * their initialisation.  A 10-second timeout guards against hangs.
     */
    CFE_ES_WaitForStartupSync(10000);

    CFE_EVS_SendEvent(PIXXEL_MAIN_STARTUP_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "PIXXEL_MAIN: started — pixxel_controller is ready");

    /* ---- Step 1: command the controller to enable the device ---- */
    pixxel_main_send_cmd(PIXXEL_ENABLE_CC);
    CFE_EVS_SendEvent(PIXXEL_MAIN_ENABLE_SENT_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "PIXXEL_MAIN: ENABLE command sent to pixxel_controller");

    /* ---- Step 2: wait for acknowledgement ---- */
    tlm = pixxel_main_await_tlm(tlm_pipe, PIXXEL_ENABLE_CC);
    if (!tlm || tlm->AckedCmdCode != PIXXEL_ENABLE_CC) {
        CFE_EVS_SendEvent(PIXXEL_MAIN_PIPE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "PIXXEL_MAIN: did not receive ENABLE ack — aborting");
        CFE_ES_ExitApp(CFE_ES_RunStatus_APP_ERROR);
        return;
    }
    CFE_EVS_SendEvent(PIXXEL_MAIN_ACK_RCVD_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "PIXXEL_MAIN: ENABLE acknowledged by pixxel_controller");

    /* ---- Step 3: wait 50 ms for driver Status register to propagate ---- */
    OS_TaskDelay(PIXXEL_STATUS_WAIT_MS);

    /* ---- Step 4: command the controller to read back device status ---- */
    pixxel_main_send_cmd(PIXXEL_GET_STATUS_CC);
    CFE_EVS_SendEvent(PIXXEL_MAIN_STATUS_SENT_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "PIXXEL_MAIN: GET_STATUS command sent to pixxel_controller");

    /* ---- Step 5: wait for status telemetry ---- */
    tlm = pixxel_main_await_tlm(tlm_pipe, PIXXEL_GET_STATUS_CC);
    if (!tlm || tlm->AckedCmdCode != PIXXEL_GET_STATUS_CC) {
        CFE_EVS_SendEvent(PIXXEL_MAIN_PIPE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "PIXXEL_MAIN: did not receive STATUS telemetry — aborting");
        CFE_ES_ExitApp(CFE_ES_RunStatus_APP_ERROR);
        return;
    }

    /* ---- Step 6: evaluate and print result ---- */
    if (tlm->DeviceStatus == 1) {
        CFE_EVS_SendEvent(PIXXEL_MAIN_STATUS_OK_INF_EID, CFE_EVS_EventType_INFORMATION,
                          "PIXXEL_MAIN: [PASS] device status = %u — device is ENABLED",
                          tlm->DeviceStatus);
        OS_printf("PIXXEL_MAIN: [PASS] device is ENABLED (status=%u)\n",
                  tlm->DeviceStatus);
    } else {
        CFE_EVS_SendEvent(PIXXEL_MAIN_STATUS_FAIL_ERR_EID, CFE_EVS_EventType_ERROR,
                          "PIXXEL_MAIN: [FAIL] device status = %u — expected 1",
                          tlm->DeviceStatus);
        OS_printf("PIXXEL_MAIN: [FAIL] device status = %u (expected 1)\n",
                  tlm->DeviceStatus);
    }

    /* Sequence complete — exit cleanly */
    CFE_ES_ExitApp(run_status);
}
