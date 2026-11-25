/*
 * consignment.c - Module 1: Consignment Intake & Lifecycle State Machine
 *
 * Records live in consignments.dat as fixed-size structs. IDs are sequential
 * (CONSIGNMENT_ID_BASE + record index), so record N is always at byte offset
 * (id - CONSIGNMENT_ID_BASE) * sizeof(Consignment). That lets us jump straight
 * to a record with fseek() and patch a single field in place.
 */
#include <stdio.h>
#include <string.h>
#include "mfyd.h"

/* Position fp at the start of the record for `id` and read it into dest.
 * On success the file position is left at the START of that record, ready
 * for an in-place fwrite(). Returns MFYD_OK or MFYD_ERR_NOT_FOUND. */
static int seek_consignment(FILE *fp, int id, Consignment *dest)
{
    long idx = (long)id - CONSIGNMENT_ID_BASE;
    long off;

    if (idx < 0)
        return MFYD_ERR_NOT_FOUND;
    off = idx * (long)sizeof(Consignment);

    if (fseek(fp, off, SEEK_SET) != 0)
        return MFYD_ERR_NOT_FOUND;
    if (fread(dest, sizeof(Consignment), 1, fp) != 1)
        return MFYD_ERR_NOT_FOUND;
    if (dest->id != id)
        return MFYD_ERR_NOT_FOUND;
    if (fseek(fp, off, SEEK_SET) != 0)      /* rewind to record start */
        return MFYD_ERR_IO;
    return MFYD_OK;
}

/* Legal moves of the lifecycle state machine. */
int is_valid_transition(ShipmentStatus from, ShipmentStatus to)
{
    switch (from) {
    case STATUS_REGISTERED:     return to == STATUS_STAGED_IN_YARD;
    case STATUS_STAGED_IN_YARD: return to == STATUS_DISPATCHED;
    case STATUS_DISPATCHED:     return to == STATUS_IN_TRANSIT ||
                                       to == STATUS_STAGED_IN_YARD;
    case STATUS_IN_TRANSIT:     return to == STATUS_DELIVERED;
    default:                    return 0;
    }
}

int register_consignment(Consignment *c)
{
    FILE *fp;
    long size;

    if (c == NULL)
        return MFYD_ERR_INVALID;
    if (!(c->weight_tons > 0.0f))               /* also rejects NaN */
        return MFYD_ERR_INVALID;
    if (c->priority != PRIORITY_EXPRESS && c->priority != PRIORITY_STANDARD)
        return MFYD_ERR_INVALID;
    if (c->origin[0] == '\0' || c->destination[0] == '\0')
        return MFYD_ERR_INVALID;

    c->is_hazardous = c->is_hazardous ? 1 : 0;

    fp = fopen(CONSIGNMENTS_FILE, "ab+");
    if (fp == NULL)
        return MFYD_ERR_IO;

    /* Auto-ID from file size: number of existing records + base. */
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0) {
        fclose(fp);
        return MFYD_ERR_IO;
    }
    c->id                = CONSIGNMENT_ID_BASE + (int)(size / (long)sizeof(Consignment));
    c->assigned_route_id = -1;
    c->status            = STATUS_REGISTERED;

    if (fwrite(c, sizeof(Consignment), 1, fp) != 1) {
        fclose(fp);
        return MFYD_ERR_IO;
    }
    if (fclose(fp) != 0)
        return MFYD_ERR_IO;

    log_audit_trail("REGISTER", c->id, "registered");
    return MFYD_OK;
}

int find_consignment_by_id(int id, Consignment *dest)
{
    FILE *fp = fopen(CONSIGNMENTS_FILE, "rb");
    Consignment tmp;

    if (fp == NULL)
        return MFYD_ERR_NOT_FOUND;          /* no file yet = nothing registered */

    while (fread(&tmp, sizeof tmp, 1, fp) == 1) {
        if (tmp.id == id) {
            if (dest != NULL)
                *dest = tmp;
            fclose(fp);
            return MFYD_OK;
        }
    }
    fclose(fp);
    return MFYD_ERR_NOT_FOUND;
}

int update_consignment_status(int id, ShipmentStatus new_status)
{
    FILE *fp;
    Consignment c;
    long record_off;
    char details[96];
    int rc;

    if ((int)new_status < 0 || new_status >= STATUS_COUNT)
        return MFYD_ERR_INVALID;

    fp = fopen(CONSIGNMENTS_FILE, "rb+");
    if (fp == NULL)
        return MFYD_ERR_NOT_FOUND;

    rc = seek_consignment(fp, id, &c);
    if (rc != MFYD_OK) {
        fclose(fp);
        return rc;
    }
    if (!is_valid_transition(c.status, new_status)) {
        fclose(fp);
        return MFYD_ERR_STATE;
    }

    /* Patch ONLY the status field: record start + offsetof(status). */
    record_off = ftell(fp);
    if (record_off < 0 ||
        fseek(fp, record_off + (long)offsetof(Consignment, status), SEEK_SET) != 0 ||
        fwrite(&new_status, sizeof new_status, 1, fp) != 1) {
        fclose(fp);
        return MFYD_ERR_IO;
    }
    if (fclose(fp) != 0)
        return MFYD_ERR_IO;

    snprintf(details, sizeof details, "status %s -> %s",
             status_to_string(c.status), status_to_string(new_status));
    log_audit_trail("STATUS", id, details);
    return MFYD_OK;
}

/* Overwrite a whole record in place (used when several fields change at once). */
int save_consignment(const Consignment *c)
{
    FILE *fp;
    Consignment cur;
    int rc;

    if (c == NULL)
        return MFYD_ERR_INVALID;
    fp = fopen(CONSIGNMENTS_FILE, "rb+");
    if (fp == NULL)
        return MFYD_ERR_NOT_FOUND;

    rc = seek_consignment(fp, c->id, &cur);
    if (rc != MFYD_OK) {
        fclose(fp);
        return rc;
    }
    if (fwrite(c, sizeof(Consignment), 1, fp) != 1) {
        fclose(fp);
        return MFYD_ERR_IO;
    }
    return fclose(fp) == 0 ? MFYD_OK : MFYD_ERR_IO;
}

int filter_consignments_by_status(ShipmentStatus status,
                                  Consignment list[], int max_len)
{
    FILE *fp = fopen(CONSIGNMENTS_FILE, "rb");
    Consignment tmp;
    int count = 0;

    if (list == NULL || max_len <= 0)
        return MFYD_ERR_INVALID;
    if (fp == NULL)
        return 0;                           /* nothing registered yet */

    while (count < max_len && fread(&tmp, sizeof tmp, 1, fp) == 1) {
        if (tmp.status == status)
            list[count++] = tmp;
    }
    fclose(fp);
    return count;
}

void export_manifest_report(const char *filename)
{
    FILE *in = fopen(CONSIGNMENTS_FILE, "rb");
    FILE *out;
    Consignment c;
    char stamp[24];
    char route[12];
    int rows = 0;
    double total_tons = 0.0;

    if (filename == NULL)
        filename = MANIFEST_FILE;

    out = fopen(filename, "w");
    if (out == NULL) {
        fprintf(stderr, "Cannot write %s\n", filename);
        if (in != NULL)
            fclose(in);
        return;
    }

    mfyd_timestamp(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S");
    fprintf(out, "MFYD CONSIGNMENT MANIFEST\n");
    fprintf(out, "Generated: %s\n\n", stamp);
    fprintf(out, "%-6s %-20s %-14s %-14s %9s %-9s %-4s %-6s %-14s\n",
            "ID", "DESCRIPTION", "ORIGIN", "DESTINATION", "WT (t)",
            "PRIORITY", "HAZ", "ROUTE", "STATUS");
    fprintf(out, "------ -------------------- -------------- -------------- "
                 "--------- --------- ---- ------ --------------\n");

    while (in != NULL && fread(&c, sizeof c, 1, in) == 1) {
        if (c.assigned_route_id > 0)
            snprintf(route, sizeof route, "#%d", c.assigned_route_id);
        else
            snprintf(route, sizeof route, "-");

        fprintf(out, "%-6d %-20.20s %-14.14s %-14.14s %9.2f %-9s %-4s %-6s %-14s\n",
                c.id, c.description, c.origin, c.destination, c.weight_tons,
                priority_to_string(c.priority), c.is_hazardous ? "YES" : "no",
                route, status_to_string(c.status));
        rows++;
        total_tons += c.weight_tons;
    }

    fprintf(out, "------ -------------------- -------------- -------------- "
                 "--------- --------- ---- ------ --------------\n");
    fprintf(out, "Total consignments: %d    Total weight: %.2f t\n", rows, total_tons);

    if (in != NULL)
        fclose(in);
    fclose(out);
}
