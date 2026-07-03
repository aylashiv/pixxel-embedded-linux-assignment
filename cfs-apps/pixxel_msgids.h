#ifndef PIXXEL_MSGIDS_H
#define PIXXEL_MSGIDS_H

/*
 * pixxel_msgids.h — Software Bus message IDs for the pixxel app pair.
 *
 * Raw numeric values live in the mission-wide cpu1_msgids.h so that the
 * full ID namespace is visible in one place.  This header wraps them in
 * the cFE type-safe CFE_SB_MsgId_t form that the SB API requires.
 */

#include "cfe.h"
#include "cpu1_msgids.h"

#define PIXXEL_CMD_MID  CFE_SB_ValueToMsgId(PIXXEL_CMD_MID_RAW)
#define PIXXEL_TLM_MID  CFE_SB_ValueToMsgId(PIXXEL_TLM_MID_RAW)

/* Command function codes dispatched by pixxel_controller */
#define PIXXEL_ENABLE_CC      0   /* write 1 to /dev/pixxel  */
#define PIXXEL_GET_STATUS_CC  1   /* read  status from /dev/pixxel */

#endif /* PIXXEL_MSGIDS_H */
