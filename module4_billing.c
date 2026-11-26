/*
 * module4_billing.c - Module 4: Commercial Rating, Invoicing & Daily Ledger
 *
 * Invoice records live in invoices.dat as fixed-size Waybill structs
 * starting at INVOICE_ID_BASE. Audit events are appended to audit.log.
 */
#include <stdio.h>
#include <string.h>
#include "mfyd.h"

/* Check whether an invoice record still corresponds to an active dispatch. */
static int is_invoice_active(const Waybill *w)
{
    Consignment c;

    if (w == NULL || !w->is_active)
        return 0;
    if (find_consignment_by_id(w->consignment_id, &c) != MFYD_OK)
        return 0;
    if (c.assigned_route_id != w->route_id)
        return 0;
    if (c.status != STATUS_DISPATCHED &&
        c.status != STATUS_IN_TRANSIT &&
        c.status != STATUS_DELIVERED)
        return 0;
    return 1;
}

void log_audit_trail(const char *action_tag, int entity_id, const char *details)
{
    FILE *fp = fopen(AUDIT_FILE, "a");
    char stamp[DATE_STR_LEN];

    if (fp == NULL)
        return;

    mfyd_timestamp(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S");
    fprintf(fp, "[%s] %-15s - %s (Entity #%d)\n",
            stamp,
            action_tag != NULL ? action_tag : "EVENT",
            details != NULL ? details : "-",
            entity_id);
    fclose(fp);
}

int search_invoice_by_consignment(int consignment_id, Waybill *dest)
{
    FILE *fp = fopen(INVOICES_FILE, "rb");
    Waybill tmp;
    int found = 0;

    if (fp == NULL)
        return MFYD_ERR_NOT_FOUND;

    /* Scan for the latest active invoice matching this consignment. */
    while (fread(&tmp, sizeof(Waybill), 1, fp) == 1) {
        if (tmp.consignment_id == consignment_id && is_invoice_active(&tmp)) {
            if (dest != NULL)
                *dest = tmp;
            found = 1;
        }
    }

    fclose(fp);
    return found ? MFYD_OK : MFYD_ERR_NOT_FOUND;
}

int generate_waybill_invoice(int consignment_id, Waybill *wb)
{
    Consignment c;
    FreightRoute r;
    Waybill out;
    FILE *fp;
    long size;
    char details[128];
    int rc;

    if (wb == NULL)
        return MFYD_ERR_INVALID;

    rc = find_consignment_by_id(consignment_id, &c);
    if (rc != MFYD_OK)
        return rc;

    /* Only dispatched (or in-transit / delivered) cargo can be invoiced. */
    if ((c.status != STATUS_DISPATCHED &&
         c.status != STATUS_IN_TRANSIT &&
         c.status != STATUS_DELIVERED) || c.assigned_route_id <= 0)
        return MFYD_ERR_STATE;

    /* If an active invoice already exists, populate *wb so main.c can display it. */
    if (search_invoice_by_consignment(consignment_id, wb) == MFYD_OK)
        return MFYD_ERR_DUPLICATE;

    rc = find_route_by_id(c.assigned_route_id, &r);
    if (rc != MFYD_OK)
        return rc;

    fp = fopen(INVOICES_FILE, "ab+");
    if (fp == NULL)
        return MFYD_ERR_IO;

    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0) {
        fclose(fp);
        return MFYD_ERR_IO;
    }

    memset(&out, 0, sizeof out);
    out.invoice_id         = INVOICE_ID_BASE + (int)(size / (long)sizeof(Waybill));
    out.consignment_id     = c.id;
    out.route_id           = r.route_id;
    out.weight_tons        = c.weight_tons;
    out.rate_per_ton       = r.rate_per_ton;
    out.base_freight       = c.weight_tons * r.rate_per_ton;
    out.priority_surcharge = (c.priority == PRIORITY_EXPRESS)
                             ? (out.base_freight * PRIORITY_SURCHARGE_RATE)
                             : 0.0f;
    out.hazmat_fee         = c.is_hazardous ? HAZMAT_SURCHARGE_FEE : 0.0f;
    out.total_amount       = out.base_freight + out.priority_surcharge + out.hazmat_fee;
    out.is_hazardous       = c.is_hazardous ? 1 : 0;
    out.is_active          = 1;
    mfyd_timestamp(out.issued_at, sizeof out.issued_at, "%Y-%m-%d %H:%M:%S");

    if (fwrite(&out, sizeof(Waybill), 1, fp) != 1) {
        fclose(fp);
        return MFYD_ERR_IO;
    }
    if (fclose(fp) != 0)
        return MFYD_ERR_IO;

    *wb = out;

    snprintf(details, sizeof details, "consignment #%d billed $%.2f",
             consignment_id, out.total_amount);
    log_audit_trail("INVOICE", out.invoice_id, details);
    return MFYD_OK;
}

void print_waybill_slip(int consignment_id)
{
    Waybill w;
    Consignment c;
    FreightRoute r;
    FILE *fp;
    char filename[64];
    int has_c, has_r;

    if (search_invoice_by_consignment(consignment_id, &w) != MFYD_OK) {
        printf("  No active invoice for consignment #%d.\n", consignment_id);
        return;
    }

    has_c = (find_consignment_by_id(consignment_id, &c) == MFYD_OK);
    has_r = (find_route_by_id(w.route_id, &r) == MFYD_OK);

    snprintf(filename, sizeof filename, "waybill_%d.txt", consignment_id);
    fp = fopen(filename, "w");
    if (fp == NULL) {
        printf("  FAILED: could not write %s.\n", filename);
        return;
    }

    fprintf(fp, "============================================================\n");
    fprintf(fp, "             MFYD COMMERCIAL FREIGHT WAYBILL                \n");
    fprintf(fp, "============================================================\n");
    fprintf(fp, "Invoice ID      : #%d\n", w.invoice_id);
    fprintf(fp, "Issued At       : %s\n", w.issued_at);
    fprintf(fp, "Consignment ID  : #%d\n", w.consignment_id);
    if (has_c) {
        fprintf(fp, "Description     : %s\n", c.description);
        fprintf(fp, "Shipment Origin : %s\n", c.origin);
        fprintf(fp, "Destination     : %s\n", c.destination);
        fprintf(fp, "Service Level   : %s\n", priority_to_string(c.priority));
        fprintf(fp, "Shipment Status : %s\n", status_to_string(c.status));
    }
    fprintf(fp, "Hazmat Cargo    : %s\n", w.is_hazardous ? "YES" : "NO");
    fprintf(fp, "------------------------------------------------------------\n");
    fprintf(fp, "Route Corridor  : #%d\n", w.route_id);
    if (has_r) {
        fprintf(fp, "Corridor Path   : %s -> %s (%s)\n",
                r.origin, r.destination, mode_to_string(r.mode));
    }
    fprintf(fp, "------------------------------------------------------------\n");
    fprintf(fp, "CHARGE BREAKDOWN\n");
    fprintf(fp, "  Base Freight      (%7.2f t x $%7.2f/t) : $%10.2f\n",
            w.weight_tons, w.rate_per_ton, w.base_freight);
    fprintf(fp, "  Express Surcharge (20%%)                    : $%10.2f\n",
            w.priority_surcharge);
    fprintf(fp, "  Hazmat Handling Fee                        : $%10.2f\n",
            w.hazmat_fee);
    fprintf(fp, "------------------------------------------------------------\n");
    fprintf(fp, "  TOTAL AMOUNT DUE                           : $%10.2f\n",
            w.total_amount);
    fprintf(fp, "============================================================\n");

    fclose(fp);
    printf("  OK: waybill slip written to %s.\n", filename);
}

void compute_daily_financial_ledger(float *total_revenue,
                                    float *avg_shipment_cost,
                                    int *hazmat_count)
{
    FILE *fp;
    Waybill w;
    char today[16];
    float sum = 0.0f;
    int count = 0;
    int haz   = 0;

    if (total_revenue != NULL)     *total_revenue     = 0.0f;
    if (avg_shipment_cost != NULL) *avg_shipment_cost = 0.0f;
    if (hazmat_count != NULL)      *hazmat_count      = 0;

    mfyd_timestamp(today, sizeof today, "%Y-%m-%d");

    fp = fopen(INVOICES_FILE, "rb");
    if (fp == NULL)
        return;

    while (fread(&w, sizeof(Waybill), 1, fp) == 1) {
        if (!is_invoice_active(&w))
            continue;
        if (strncmp(w.issued_at, today, 10) != 0)
            continue;

        sum += w.total_amount;
        count++;
        if (w.is_hazardous)
            haz++;
    }
    fclose(fp);

    if (total_revenue != NULL)
        *total_revenue = sum;
    if (avg_shipment_cost != NULL && count > 0)
        *avg_shipment_cost = sum / (float)count;
    if (hazmat_count != NULL)
        *hazmat_count = haz;
}