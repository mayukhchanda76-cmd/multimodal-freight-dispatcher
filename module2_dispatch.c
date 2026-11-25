/*
 * module2_dispatch.c - Module 2: Capacity Dispatch & Route Optimization
 *
 * Route records live in routes.dat as fixed-size structs starting at
 * ROUTE_ID_BASE, so route N is at byte offset (N - ROUTE_ID_BASE) * sizeof(FreightRoute).
 */
#include <stdio.h>
#include <string.h>
#include "mfyd.h"

/* Position fp at the start of route `route_id` and read it into dest.
 * Leaves fp rewound to the record start for in-place fwrite(). */
static int seek_route(FILE *fp, int route_id, FreightRoute *dest)
{
    long idx = (long)route_id - ROUTE_ID_BASE;
    long off;

    if (idx < 0)
        return MFYD_ERR_NOT_FOUND;
    off = idx * (long)sizeof(FreightRoute);

    if (fseek(fp, off, SEEK_SET) != 0)
        return MFYD_ERR_NOT_FOUND;
    if (fread(dest, sizeof(FreightRoute), 1, fp) != 1)
        return MFYD_ERR_NOT_FOUND;
    if (dest->route_id != route_id)
        return MFYD_ERR_NOT_FOUND;
    if (fseek(fp, off, SEEK_SET) != 0)
        return MFYD_ERR_IO;
    return MFYD_OK;
}

/* Overwrite an existing route record in place. */
static int save_route(const FreightRoute *r)
{
    FILE *fp;
    FreightRoute cur;
    int rc;

    if (r == NULL)
        return MFYD_ERR_INVALID;

    fp = fopen(ROUTES_FILE, "rb+");
    if (fp == NULL)
        return MFYD_ERR_NOT_FOUND;

    rc = seek_route(fp, r->route_id, &cur);
    if (rc != MFYD_OK) {
        fclose(fp);
        return rc;
    }
    if (fwrite(r, sizeof(FreightRoute), 1, fp) != 1) {
        fclose(fp);
        return MFYD_ERR_IO;
    }
    return fclose(fp) == 0 ? MFYD_OK : MFYD_ERR_IO;
}

int add_freight_route(FreightRoute *r)
{
    FILE *fp;
    long size;
    char details[96];

    if (r == NULL)
        return MFYD_ERR_INVALID;
    if (r->origin[0] == '\0' || r->destination[0] == '\0')
        return MFYD_ERR_INVALID;
    if (r->mode != MODE_ROAD && r->mode != MODE_RAIL)
        return MFYD_ERR_INVALID;
    if (!(r->max_capacity_tons > 0.0f) || !(r->rate_per_ton > 0.0f))
        return MFYD_ERR_INVALID;

    fp = fopen(ROUTES_FILE, "ab+");
    if (fp == NULL)
        return MFYD_ERR_IO;

    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0) {
        fclose(fp);
        return MFYD_ERR_IO;
    }

    r->route_id           = ROUTE_ID_BASE + (int)(size / (long)sizeof(FreightRoute));
    r->booked_weight_tons = 0.0f;

    if (fwrite(r, sizeof(FreightRoute), 1, fp) != 1) {
        fclose(fp);
        return MFYD_ERR_IO;
    }
    if (fclose(fp) != 0)
        return MFYD_ERR_IO;

    snprintf(details, sizeof details, "%s -> %s (%s, cap %.1f t)",
             r->origin, r->destination, mode_to_string(r->mode),
             r->max_capacity_tons);
    log_audit_trail("ADD_ROUTE", r->route_id, details);
    return MFYD_OK;
}

int find_route_by_id(int route_id, FreightRoute *dest)
{
    FILE *fp;
    FreightRoute tmp;
    int rc;

    if (route_id < ROUTE_ID_BASE)
        return MFYD_ERR_NOT_FOUND;

    fp = fopen(ROUTES_FILE, "rb");
    if (fp == NULL)
        return MFYD_ERR_NOT_FOUND;

    rc = seek_route(fp, route_id, &tmp);
    fclose(fp);
    if (rc == MFYD_OK && dest != NULL)
        *dest = tmp;
    return rc;
}

int recommend_transit_mode(float weight, int priority, int is_hazardous)
{
    (void)is_hazardous;

    /* Express cargo or loads <= 15 t -> Road; heavy standard loads -> Rail. */
    if (priority == PRIORITY_EXPRESS || !(weight > RAIL_WEIGHT_THRESHOLD_TONS))
        return MODE_ROAD;
    return MODE_RAIL;
}

int dispatch_consignment_to_route(int consignment_id, int route_id)
{
    Consignment c;
    FreightRoute r;
    YardBay bay;
    float orig_booked;
    int has_bay = 0;
    int bay_rc, rc;
    char details[128];

    rc = find_consignment_by_id(consignment_id, &c);
    if (rc != MFYD_OK)
        return rc;

    /* Enforce lifecycle state machine (must be staged and not already assigned). */
    if (!is_valid_transition(c.status, STATUS_DISPATCHED) || c.assigned_route_id > 0)
        return MFYD_ERR_STATE;

    rc = find_route_by_id(route_id, &r);
    if (rc != MFYD_OK)
        return rc;

    /* Capacity constraint check. */
    if (r.booked_weight_tons + c.weight_tons > r.max_capacity_tons)
        return MFYD_ERR_CAPACITY;

    /* Check whether the consignment currently occupies a yard bay. */
    bay_rc = lookup_bay_by_consignment(consignment_id, &bay);
    if (bay_rc == MFYD_OK)
        has_bay = 1;

    /* 1. Book capacity on the route. */
    orig_booked = r.booked_weight_tons;
    r.booked_weight_tons += c.weight_tons;
    rc = save_route(&r);
    if (rc != MFYD_OK)
        return rc;

    /* 2. Update consignment to DISPATCHED before vacating bay so vacate_yard_bay()
     *    knows this is a dispatch release rather than a manual yard pull-out. */
    c.assigned_route_id = route_id;
    c.status            = STATUS_DISPATCHED;
    rc = save_consignment(&c);
    if (rc != MFYD_OK) {
        r.booked_weight_tons = orig_booked;
        save_route(&r);
        return rc;
    }

    /* 3. Release yard bay if one was occupied; roll back route & consignment on I/O failure. */
    if (has_bay) {
        rc = vacate_yard_bay(bay.bay_id);
        if (rc != MFYD_OK) {
            c.assigned_route_id = -1;
            c.status            = STATUS_STAGED_IN_YARD;
            save_consignment(&c);
            r.booked_weight_tons = orig_booked;
            save_route(&r);
            return rc;
        }
    }

    snprintf(details, sizeof details, "dispatched on route #%d (%.2f t)",
             route_id, c.weight_tons);
    log_audit_trail("DISPATCH", consignment_id, details);
    return MFYD_OK;
}

void list_routes_with_utilization(void)
{
    FILE *fp = fopen(ROUTES_FILE, "rb");
    FreightRoute r;
    int count = 0;

    if (fp == NULL) {
        puts("  No routes registered yet.");
        return;
    }

    puts("\n============================================================================================");
    printf("| %-5s | %-12s | %-12s | %-6s | %-15s | %-9s | %-18s |\n",
           "ID", "Origin", "Destination", "Mode", "Capacity (t)", "$/ton", "Utilization");
    puts("--------------------------------------------------------------------------------------------");

    while (fread(&r, sizeof(FreightRoute), 1, fp) == 1) {
        float pct = 0.0f;
        char bar[13];
        char cap_str[32];
        int bar_width = 10;
        int filled, i;

        if (r.max_capacity_tons > 0.0f)
            pct = (r.booked_weight_tons / r.max_capacity_tons) * 100.0f;
        if (pct < 0.0f)   pct = 0.0f;
        if (pct > 100.0f) pct = 100.0f;

        filled = (int)((pct / 100.0f) * (float)bar_width);
        bar[0] = '[';
        for (i = 0; i < bar_width; i++)
            bar[i + 1] = (i < filled) ? '|' : '.';
        bar[bar_width + 1] = ']';
        bar[bar_width + 2] = '\0';

        snprintf(cap_str, sizeof cap_str, "%.1f / %.1f",
                 r.booked_weight_tons, r.max_capacity_tons);

        printf("| %-5d | %-12.12s | %-12.12s | %-6s | %-15s | %9.2f | %s %5.1f%% |\n",
               r.route_id, r.origin, r.destination, mode_to_string(r.mode),
               cap_str, r.rate_per_ton, bar, pct);
        count++;
    }

    if (count == 0)
        puts("|  (no routes registered)                                                                  |");

    puts("============================================================================================");
    fclose(fp);
}

int cancel_dispatch(int consignment_id)
{
    Consignment c;
    FreightRoute r;
    float orig_booked;
    int old_route_id, bay_rc, rc;
    char details[128];

    rc = find_consignment_by_id(consignment_id, &c);
    if (rc != MFYD_OK)
        return rc;

    if (c.status != STATUS_DISPATCHED || c.assigned_route_id <= 0)
        return MFYD_ERR_STATE;

    old_route_id = c.assigned_route_id;
    rc = find_route_by_id(old_route_id, &r);
    if (rc != MFYD_OK)
        return rc;

    /* 1. Restore booked capacity on the route. */
    orig_booked = r.booked_weight_tons;
    r.booked_weight_tons -= c.weight_tons;
    if (r.booked_weight_tons < 1e-4f)
        r.booked_weight_tons = 0.0f;

    rc = save_route(&r);
    if (rc != MFYD_OK)
        return rc;

    /* 2. Clear route assignment and revert to REGISTERED first. */
    c.assigned_route_id = -1;
    c.status            = STATUS_REGISTERED;
    rc = save_consignment(&c);
    if (rc != MFYD_OK) {
        r.booked_weight_tons = orig_booked;
        save_route(&r);
        return rc;
    }

    /* 3. Attempt to re-stage into a physical yard bay so yard.dat and
     *    consignments.dat stay strictly synchronized. If the yard is full,
     *    the consignment remains cleanly in STATUS_REGISTERED. */
    bay_rc = allocate_best_fit_bay(c.id, c.weight_tons, c.is_hazardous);
    if (bay_rc > 0) {
        snprintf(details, sizeof details,
                 "cancelled route #%d, restored %.2f t, re-staged in bay #%d",
                 old_route_id, c.weight_tons, bay_rc);
    } else {
        snprintf(details, sizeof details,
                 "cancelled route #%d, restored %.2f t, returned to REGISTERED (yard full)",
                 old_route_id, c.weight_tons);
    }
    log_audit_trail("CANCEL_DISPATCH", consignment_id, details);
    return MFYD_OK;
}