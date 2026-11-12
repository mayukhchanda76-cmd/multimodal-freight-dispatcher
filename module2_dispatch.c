#include "mfyd.h"

/* ========================================================================= */
/*                          1. ADD FREIGHT ROUTE                             */
/* ========================================================================= */
/**
 * Appends a new route corridor to routes.dat.
 * Auto-generates the route_id based on file size.
 */
int add_freight_route(FreightRoute *r) {
    /* Basic validation */
    if (r == NULL || r->max_capacity_tons <= 0.0f || r->rate_per_ton <= 0.0f) {
        printf("[-] Error: Capacity and rate must be greater than zero.\n");
        return -1;
    }

    FILE *fp = fopen(ROUTES_FILE, "ab+");
    if (fp == NULL) {
        printf("[-] Error: Unable to open routes file.\n");
        return -1;
    }

    /* Auto-generate sequential route ID: total existing records + 1 */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    int next_id = (int)(file_size / sizeof(FreightRoute)) + 1;

    r->route_id = next_id;
    r->booked_weight_tons = 0.0f; /* Initially 0 tons booked */

    /* Append to end of file */
    if (fwrite(r, sizeof(FreightRoute), 1, fp) != 1) {
        printf("[-] Error writing route record.\n");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    printf("[+] Route corridor added successfully with ID #%d.\n", r->route_id);
    return r->route_id;
}

/* ========================================================================= */
/*                          2. FIND ROUTE BY ID                              */
/* ========================================================================= */
/**
 * Reads routes.dat sequentially to find a record matching route_id.
 */
int find_route_by_id(int route_id, FreightRoute *dest) {
    if (dest == NULL || route_id <= 0) {
        return 0;
    }

    FILE *fp = fopen(ROUTES_FILE, "rb");
    if (fp == NULL) {
        return 0;
    }

    FreightRoute temp;
    int found = 0;

    /* Read record by record until match is found */
    while (fread(&temp, sizeof(FreightRoute), 1, fp) == 1) {
        if (temp.route_id == route_id) {
            *dest = temp; /* Copy data into destination pointer */
            found = 1;
            break;
        }
    }

    fclose(fp);
    return found;
}

/* ========================================================================= */
/*                   3. RECOMMEND TRANSIT MODE (OPTIMIZATION)                */
/* ========================================================================= */
/**
 * Optimization Heuristic:
 * - Road: Prioritized for Express cargo (priority == 1) or smaller loads (<= 15t).
 * - Rail: Prioritized for bulk cargo (> 15t) that is non-urgent (priority == 0).
 */
int recommend_transit_mode(float weight, int priority, int is_hazardous) {
    (void)is_hazardous; /* Parameter kept for future hazardous road restrictions */

    if (priority == 1 || weight <= 15.0f) {
        /* Road is optimal: faster turnaround for small or urgent shipments */
        return MODE_ROAD;
    } else {
        /* Rail is optimal: lower cost per ton-km for heavy, non-urgent freight */
        return MODE_RAIL;
    }
}

/* ========================================================================= */
/*                 4. DISPATCH CONSIGNMENT TO ROUTE                          */
/* ========================================================================= */
/**
 * Capacity constraint check, in-place route update, and consignment status flip.
 */
int dispatch_consignment_to_route(int consignment_id, int route_id) {
    /* Step 1: Open and check the consignment */
    FILE *cfp = fopen(CONSIGNMENTS_FILE, "rb+");
    if (cfp == NULL) {
        printf("[-] Error: Consignments file not found.\n");
        return 0;
    }

    Consignment c;
    int c_found = 0;

    while (fread(&c, sizeof(Consignment), 1, cfp) == 1) {
        if (c.id == consignment_id) {
            c_found = 1;
            break;
        }
    }

    if (!c_found) {
        printf("[-] Error: Consignment ID #%d not found.\n", consignment_id);
        fclose(cfp);
        return 0;
    }

    if (c.status == STATUS_DISPATCHED) {
        printf("[-] Error: Consignment #%d is already dispatched.\n", consignment_id);
        fclose(cfp);
        return 0;
    }

    /* Step 2: Open and check the route */
    FILE *rfp = fopen(ROUTES_FILE, "rb+");
    if (rfp == NULL) {
        printf("[-] Error: Routes file not found.\n");
        fclose(cfp);
        return 0;
    }

    FreightRoute r;
    int r_found = 0;

    while (fread(&r, sizeof(FreightRoute), 1, rfp) == 1) {
        if (r.route_id == route_id) {
            r_found = 1;
            break;
        }
    }

    if (!r_found) {
        printf("[-] Error: Route ID #%d not found.\n", route_id);
        fclose(cfp);
        fclose(rfp);
        return 0;
    }

    /* Step 3: Capacity Constraint Check */
    if (r.booked_weight_tons + c.weight_tons > r.max_capacity_tons) {
        printf("[-] Error: Capacity Exceeded! Route #%d has only %.2f tons available (Needed: %.2f tons).\n",
               route_id, r.max_capacity_tons - r.booked_weight_tons, c.weight_tons);
        fclose(cfp);
        fclose(rfp);
        return 0;
    }

    /* Step 4: In-place update of route booked capacity */
    r.booked_weight_tons = r.booked_weight_tons + c.weight_tons;
    fseek(rfp, -(long)sizeof(FreightRoute), SEEK_CUR); /* Step back 1 record */
    fwrite(&r, sizeof(FreightRoute), 1, rfp);          /* Overwrite */
    fclose(rfp);

    /* Step 5: If cargo was stored in a yard bay, vacate the bay */
    if (c.assigned_bay_id > 0) {
        vacate_yard_bay(c.assigned_bay_id);
    }

    /* Step 6: In-place update of consignment status & assigned route */
    c.assigned_route_id = route_id;
    c.status = STATUS_DISPATCHED;
    c.assigned_bay_id = -1; /* No longer occupying yard bay */

    fseek(cfp, -(long)sizeof(Consignment), SEEK_CUR);  /* Step back 1 record */
    fwrite(&c, sizeof(Consignment), 1, cfp);           /* Overwrite */
    fclose(cfp);

    /* Step 7: Log to audit trail */
    char log_text[128];
    snprintf(log_text, sizeof(log_text), "Assigned to Route #%d (Weight: %.2ft)", route_id, c.weight_tons);
    log_audit_trail("DISPATCH", consignment_id, log_text);

    printf("[+] Consignment #%d successfully dispatched on Route #%d.\n", consignment_id, route_id);
    return 1;
}

/* ========================================================================= */
/*              5. LIST ROUTES WITH UTILIZATION PROGRESS BAR                 */
/* ========================================================================= */
/**
 * Renders an ASCII utilization progress bar for all corridors.
 */
void list_routes_with_utilization(void) {
    FILE *fp = fopen(ROUTES_FILE, "rb");
    if (fp == NULL) {
        printf("[-] No routes registered yet.\n");
        return;
    }

    printf("\n========================================================================================\n");
    printf("| %-4s | %-12s | %-12s | %-6s | %-15s | %-25s |\n",
           "ID", "Origin", "Destination", "Mode", "Capacity (t)", "Capacity Utilization");
    printf("----------------------------------------------------------------------------------------\n");

    FreightRoute r;
    while (fread(&r, sizeof(FreightRoute), 1, fp) == 1) {
        /* Calculate utilization percentage */
        float pct = 0.0f;
        if (r.max_capacity_tons > 0.0f) {
            pct = (r.booked_weight_tons / r.max_capacity_tons) * 100.0f;
        }
        if (pct > 100.0f) pct = 100.0f;

        /* Create a 10-character progress bar: e.g. [||||||....] */
        char bar[13];
        bar[0] = '[';
        int bar_width = 10;
        int filled_count = (int)((pct / 100.0f) * bar_width);

        for (int i = 0; i < bar_width; i++) {
            if (i < filled_count) {
                bar[i + 1] = '|';
            } else {
                bar[i + 1] = '.';
            }
        }
        bar[bar_width + 1] = ']';
        bar[bar_width + 2] = '\0';

        char cap_str[32];
        snprintf(cap_str, sizeof(cap_str), "%.1f / %.1f", r.booked_weight_tons, r.max_capacity_tons);

        printf("| %-4d | %-12.12s | %-12.12s | %-6s | %-15s | %s %5.1f%% |\n",
               r.route_id,
               r.origin,
               r.destination,
               (r.mode == MODE_RAIL) ? "RAIL" : "ROAD",
               cap_str,
               bar,
               pct);
    }

    printf("========================================================================================\n");
    fclose(fp);
}

/* ========================================================================= */
/*                          6. CANCEL DISPATCH                               */
/* ========================================================================= */
/**
 * Reverses a dispatch:
 * 1. Rolls back booked weight on the route.
 * 2. Sets consignment status back to STATUS_STAGED_IN_YARD.
 */
int cancel_dispatch(int consignment_id) {
    /* Step 1: Find the consignment */
    FILE *cfp = fopen(CONSIGNMENTS_FILE, "rb+");
    if (cfp == NULL) {
        printf("[-] Error: Consignments file not found.\n");
        return 0;
    }

    Consignment c;
    int c_found = 0;

    while (fread(&c, sizeof(Consignment), 1, cfp) == 1) {
        if (c.id == consignment_id) {
            c_found = 1;
            break;
        }
    }

    if (!c_found) {
        printf("[-] Error: Consignment #%d not found.\n", consignment_id);
        fclose(cfp);
        return 0;
    }

    if (c.status != STATUS_DISPATCHED) {
        printf("[-] Error: Consignment #%d is not currently dispatched.\n", consignment_id);
        fclose(cfp);
        return 0;
    }

    int route_id = c.assigned_route_id;
    float cargo_weight = c.weight_tons;

    /* Step 2: Roll back booked capacity on the route */
    FILE *rfp = fopen(ROUTES_FILE, "rb+");
    if (rfp != NULL) {
        FreightRoute r;
        while (fread(&r, sizeof(FreightRoute), 1, rfp) == 1) {
            if (r.route_id == route_id) {
                r.booked_weight_tons = r.booked_weight_tons - cargo_weight;
                if (r.booked_weight_tons < 0.0f) {
                    r.booked_weight_tons = 0.0f;
                }

                fseek(rfp, -(long)sizeof(FreightRoute), SEEK_CUR);
                fwrite(&r, sizeof(FreightRoute), 1, rfp);
                break;
            }
        }
        fclose(rfp);
    }

    /* Step 3: Revert consignment state back to STATUS_STAGED_IN_YARD */
    c.assigned_route_id = -1;
    c.status = STATUS_STAGED_IN_YARD;

    fseek(cfp, -(long)sizeof(Consignment), SEEK_CUR);
    fwrite(&c, sizeof(Consignment), 1, cfp);
    fclose(cfp);

    /* Step 4: Audit log */
    char log_text[128];
    snprintf(log_text, sizeof(log_text), "Cancelled dispatch on Route #%d, restored %.2ft capacity",
             route_id, cargo_weight);
    log_audit_trail("CANCEL_DISPATCH", consignment_id, log_text);

    printf("[+] Dispatch cancelled for Consignment #%d. Returned to STATUS_STAGED_IN_YARD.\n", consignment_id);
    return 1;
}