#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_HOST | OPT_MODE_FULL_SPEED)

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

#define CFG_TUH_ENUMERATION_BUFSIZE 512

#define CFG_TUH_HUB 1
#define CFG_TUH_CDC 0
#define CFG_TUH_HID 24
#define CFG_TUH_MSC 0
#define CFG_TUH_VENDOR 0

#define CFG_TUH_DEVICE_MAX 16
#define CFG_TUH_INTERFACE_MAX 16

#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

// Keep the verified host scheduling by default. Controlled queue/SOF
// experiments are selected through firmware CMake options.
#ifndef CFG_TUH_TASK_QUEUE_SZ
#define CFG_TUH_TASK_QUEUE_SZ 16
#endif
#ifndef CFG_TUH_SOF_QUEUE_COALESCE
#define CFG_TUH_SOF_QUEUE_COALESCE 0
#endif
#ifndef CFG_TUH_ONE_HOT_FAIRNESS
#define CFG_TUH_ONE_HOT_FAIRNESS 0
#endif

// Output and feature reports use HID control transfers in HID Remapper.  Do
// not spend one of RP2040's 15 host endpoint slots on unused interrupt OUT.
#define CFG_TUH_HID_OPEN_OUT_ENDPOINT 0
#define CFG_TUH_HID_DEFER_INPUT_ENDPOINT 1

#endif
