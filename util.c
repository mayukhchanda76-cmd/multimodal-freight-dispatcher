/* util.c - small shared helpers */
#include <stdio.h>
#include <time.h>
#include "mfyd.h"

const char *mfyd_strerror(int code)
{
    switch (code) {
    case MFYD_OK:            return "success";
    case MFYD_ERR_IO:        return "file I/O error";
    case MFYD_ERR_NOT_FOUND: return "record not found";
    case MFYD_ERR_INVALID:   return "invalid input";
    case MFYD_ERR_STATE:     return "not allowed in the record's current state";
    case MFYD_ERR_CAPACITY:  return "insufficient capacity / no suitable bay";
    case MFYD_ERR_DUPLICATE: return "already exists";
    default:                 return "unknown error";
    }
}

void mfyd_timestamp(char *buf, size_t n, const char *fmt)
{
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);

    if (n == 0)
        return;
    if (tm_info == NULL || strftime(buf, n, fmt, tm_info) == 0)
        buf[0] = '\0';
}

const char *status_to_string(ShipmentStatus s)
{
    switch (s) {
    case STATUS_REGISTERED:     return "REGISTERED";
    case STATUS_STAGED_IN_YARD: return "STAGED_IN_YARD";
    case STATUS_DISPATCHED:     return "DISPATCHED";
    case STATUS_IN_TRANSIT:     return "IN_TRANSIT";
    case STATUS_DELIVERED:      return "DELIVERED";
    default:                    return "UNKNOWN";
    }
}

const char *mode_to_string(TransitMode m)
{
    switch (m) {
    case MODE_ROAD: return "ROAD";
    case MODE_RAIL: return "RAIL";
    default:        return "UNKNOWN";
    }
}

const char *priority_to_string(int priority)
{
    switch (priority) {
    case PRIORITY_EXPRESS:  return "EXPRESS";
    case PRIORITY_STANDARD: return "STANDARD";
    default:                return "UNKNOWN";
    }
}