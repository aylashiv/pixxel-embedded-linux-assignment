#ifndef CPU1_MSGIDS_H
#define CPU1_MSGIDS_H

/*
 * cpu1_msgids.h — Mission-wide Software Bus message ID table for cpu1.
 *
 * Convention:
 *   Commands   (sent TO an app)  → 0x1xxx range
 *   Telemetry  (sent FROM an app) → 0x0xxx range
 *
 * Each app pair that communicates on the SB owns a slice of this namespace.
 * All IDs must be unique across the mission.
 */

/* ---- cFE core services (reserved; do not reuse) ---- */
#define CFE_EVS_CMD_MID       0x1801
#define CFE_SB_CMD_MID        0x1803
#define CFE_TBL_CMD_MID       0x1804
#define CFE_TIME_CMD_MID      0x1805
#define CFE_ES_CMD_MID        0x1806

#define CFE_EVS_HK_TLM_MID   0x0801
#define CFE_ES_HK_TLM_MID    0x0800

/* ---- Pixxel application messages ---- */
/* pixxel_main → pixxel_controller */
#define PIXXEL_CMD_MID_RAW    0x1900

/* pixxel_controller → pixxel_main */
#define PIXXEL_TLM_MID_RAW    0x0900

#endif /* CPU1_MSGIDS_H */
