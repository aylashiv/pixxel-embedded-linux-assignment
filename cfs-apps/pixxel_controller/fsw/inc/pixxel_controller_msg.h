#ifndef PIXXEL_CONTROLLER_MSG_H
#define PIXXEL_CONTROLLER_MSG_H

#include "cfe.h"

/* Command sent by pixxel_main → pixxel_controller (no payload beyond header) */
typedef struct {
    CFE_MSG_CommandHeader_t CmdHeader;
} PIXXEL_CTRL_NoArgsCmd_t;

/* Telemetry sent by pixxel_controller → pixxel_main */
typedef struct {
    CFE_MSG_TelemetryHeader_t TlmHeader;
    uint8_t AckedCmdCode;  /* command code being acknowledged */
    uint8_t DeviceStatus;  /* 0 = disabled, 1 = enabled (valid on GET_STATUS ack) */
    uint8_t Spare[2];
} PIXXEL_CTRL_Tlm_t;

#endif /* PIXXEL_CONTROLLER_MSG_H */
