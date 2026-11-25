/*
 * main.c - MFYD terminal front end (menu per module)
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mfyd.h"

#define MAX_LIST 256

/* ------------------------------------------------------------------ */
/* input helpers                                                       */
/* ------------------------------------------------------------------ */
static void read_line(const char *prompt, char *buf, size_t n)
{
    if (prompt != NULL) {
        printf("%s", prompt);
        fflush(stdout);
    }
    if (fgets(buf, (int)n, stdin) == NULL) {
        printf("\n");
        exit(EXIT_SUCCESS);                     /* stdin closed */
    }
    if (strchr(buf, '\n') == NULL) {            /* line was longer than buffer: drop the rest */
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF)
            ;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
}

static int prompt_int(const char *prompt)
{
    char buf[64];
    char *end;
    long v;

    for (;;) {
        read_line(prompt, buf, sizeof buf);
        errno = 0;
        v = strtol(buf, &end, 10);
        if (end != buf && errno == 0 && v >= INT_MIN && v <= INT_MAX) {
            while (isspace((unsigned char)*end))
                end++;
            if (*end == '\0')
                return (int)v;
        }
        puts("  Please enter a whole number.");
    }
}

static float prompt_float(const char *prompt)
{
    char buf[64];
    char *end;
    double v;

    for (;;) {
        read_line(prompt, buf, sizeof buf);
        errno = 0;
        v = strtod(buf, &end);
        if (end != buf && errno == 0 && v == v && v > -1e9 && v < 1e9) {
            while (isspace((unsigned char)*end))
                end++;
            if (*end == '\0')
                return (float)v;
        }
        puts("  Please enter a number.");
    }
}

static void prompt_text(const char *prompt, char *dst, size_t n)
{
    char buf[128];

    for (;;) {
        read_line(prompt, buf, sizeof buf);
        if (buf[0] != '\0') {
            size_t len = strlen(buf);
            if (len >= n)
                len = n - 1;                    /* truncate to fit the field */
            memcpy(dst, buf, len);
            dst[len] = '\0';
            return;
        }
        puts("  This field cannot be empty.");
    }
}

static int prompt_yes_no(const char *prompt)
{
    char buf[16];

    for (;;) {
        read_line(prompt, buf, sizeof buf);
        if (buf[0] == 'y' || buf[0] == 'Y') return 1;
        if (buf[0] == 'n' || buf[0] == 'N') return 0;
        puts("  Please answer y or n.");
    }
}

static int prompt_status(ShipmentStatus *out)
{
    int i, v;

    for (i = 0; i < STATUS_COUNT; i++)
        printf("    %d = %s\n", i, status_to_string((ShipmentStatus)i));
    v = prompt_int("  Status number: ");
    if (v < 0 || v >= STATUS_COUNT) {
        puts("  Unknown status number.");
        return 0;
    }
    *out = (ShipmentStatus)v;
    return 1;
}

static void report(const char *what, int rc)
{
    if (rc == MFYD_OK)
        printf("  OK: %s.\n", what);
    else
        printf("  FAILED: %s (%s).\n", what, mfyd_strerror(rc));
}

static void print_consignment(const Consignment *c)
{
    char route[16];

    if (c->assigned_route_id > 0)
        snprintf(route, sizeof route, "#%d", c->assigned_route_id);
    else
        snprintf(route, sizeof route, "-");

    printf("  #%d | %s | %s -> %s | %.2f t | %s | Hazmat: %s | Route: %s | %s\n",
           c->id, c->description, c->origin, c->destination, c->weight_tons,
           priority_to_string(c->priority), c->is_hazardous ? "yes" : "no",
           route, status_to_string(c->status));
}

/* ------------------------------------------------------------------ */
/* Module 1 menu                                                       */
/* ------------------------------------------------------------------ */
static void menu_consignments(void)
{
    for (;;) {
        int choice;

        puts("\n--- Consignment Intake & Lifecycle ---");
        puts(" 1. Register consignment");
        puts(" 2. Find consignment by ID");
        puts(" 3. Update consignment status");
        puts(" 4. List consignments by status");
        puts(" 5. Export manifest report (manifest.txt)");
        puts(" 0. Back");
        choice = prompt_int("Choice: ");

        switch (choice) {
        case 1: {
            Consignment c;
            int rc;
            memset(&c, 0, sizeof c);
            prompt_text("  Description : ", c.description, sizeof c.description);
            prompt_text("  Origin      : ", c.origin, sizeof c.origin);
            prompt_text("  Destination : ", c.destination, sizeof c.destination);
            c.weight_tons  = prompt_float("  Weight (tons): ");
            c.priority     = prompt_int("  Priority (1 = Express, 2 = Standard): ");
            c.is_hazardous = prompt_yes_no("  Hazardous? (y/n): ");
            rc = register_consignment(&c);
            if (rc == MFYD_OK) {
                printf("  OK: registered as consignment #%d.\n", c.id);
                printf("  Suggested mode: %s\n",
                       mode_to_string((TransitMode)recommend_transit_mode(
                           c.weight_tons, c.priority, c.is_hazardous)));
            } else {
                report("registration", rc);
                if (rc == MFYD_ERR_INVALID)
                    puts("  (weight must be > 0 and priority must be 1 or 2)");
            }
            break;
        }
        case 2: {
            Consignment c;
            int id = prompt_int("  Consignment ID: ");
            if (find_consignment_by_id(id, &c) == MFYD_OK)
                print_consignment(&c);
            else
                printf("  Consignment #%d not found.\n", id);
            break;
        }
        case 3: {
            int id = prompt_int("  Consignment ID: ");
            ShipmentStatus st;
            if (prompt_status(&st))
                report("status update", update_consignment_status(id, st));
            break;
        }
        case 4: {
            static Consignment list[MAX_LIST];
            ShipmentStatus st;
            if (prompt_status(&st)) {
                int i, n = filter_consignments_by_status(st, list, MAX_LIST);
                if (n < 0) {
                    report("filter", n);
                } else {
                    printf("  %d consignment(s) with status %s:\n", n, status_to_string(st));
                    for (i = 0; i < n; i++)
                        print_consignment(&list[i]);
                    if (n == MAX_LIST)
                        printf("  (list capped at %d entries)\n", MAX_LIST);
                }
            }
            break;
        }
        case 5:
            export_manifest_report(MANIFEST_FILE);
            printf("  Manifest written to %s\n", MANIFEST_FILE);
            break;
        case 0:
            return;
        default:
            puts("  Invalid choice.");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Module 2 menu                                                       */
/* ------------------------------------------------------------------ */
static void seed_demo_routes(void)
{
    static const struct {
        const char *from, *to;
        TransitMode mode;
        float cap, rate;
    } demo[] = {
        { "Mundra Port",  "Delhi ICD",   MODE_RAIL, 600.0f, 38.50f },
        { "JNPT Mumbai",  "Pune",        MODE_ROAD,  60.0f, 72.00f },
        { "Kolkata",      "Delhi",       MODE_RAIL, 400.0f, 44.00f },
        { "Chennai Port", "Bengaluru",   MODE_ROAD,  45.0f, 65.00f }
    };
    size_t i;
    FreightRoute probe;

    if (find_route_by_id(ROUTE_ID_BASE, &probe) == MFYD_OK) {
        puts("  Routes already exist - demo seeding skipped.");
        return;
    }
    for (i = 0; i < sizeof demo / sizeof demo[0]; i++) {
        FreightRoute r;
        memset(&r, 0, sizeof r);
        snprintf(r.origin, sizeof r.origin, "%s", demo[i].from);
        snprintf(r.destination, sizeof r.destination, "%s", demo[i].to);
        r.mode              = demo[i].mode;
        r.max_capacity_tons = demo[i].cap;
        r.rate_per_ton      = demo[i].rate;
        add_freight_route(&r);
    }
    puts("  Seeded 4 demo routes.");
}

static void menu_routes(void)
{
    for (;;) {
        int choice;

        puts("\n--- Capacity Dispatch & Routes ---");
        puts(" 1. Add freight route");
        puts(" 2. Seed demo routes");
        puts(" 3. Recommend transit mode");
        puts(" 4. Dispatch consignment to route");
        puts(" 5. Cancel dispatch");
        puts(" 6. List routes with utilization");
        puts(" 0. Back");
        choice = prompt_int("Choice: ");

        switch (choice) {
        case 1: {
            FreightRoute r;
            int m, rc;
            memset(&r, 0, sizeof r);
            prompt_text("  Origin      : ", r.origin, sizeof r.origin);
            prompt_text("  Destination : ", r.destination, sizeof r.destination);
            m = prompt_int("  Mode (1 = Road, 2 = Rail): ");
            r.mode              = (TransitMode)m;
            r.max_capacity_tons = prompt_float("  Max capacity (tons): ");
            r.rate_per_ton      = prompt_float("  Rate ($ per ton): ");
            rc = add_freight_route(&r);
            if (rc == MFYD_OK)
                printf("  OK: route #%d added.\n", r.route_id);
            else
                report("add route", rc);
            break;
        }
        case 2:
            seed_demo_routes();
            break;
        case 3: {
            float w = prompt_float("  Weight (tons): ");
            int pr  = prompt_int("  Priority (1 = Express, 2 = Standard): ");
            int hz  = prompt_yes_no("  Hazardous? (y/n): ");
            printf("  Recommended mode: %s\n",
                   mode_to_string((TransitMode)recommend_transit_mode(w, pr, hz)));
            break;
        }
        case 4: {
            Consignment c;
            FreightRoute r;
            int cid = prompt_int("  Consignment ID: ");
            int rid = prompt_int("  Route ID: ");
            if (find_consignment_by_id(cid, &c) == MFYD_OK &&
                find_route_by_id(rid, &r) == MFYD_OK) {
                TransitMode rec = (TransitMode)recommend_transit_mode(
                    c.weight_tons, c.priority, c.is_hazardous);
                if (rec != r.mode)
                    printf("  Note: recommended mode is %s, this route is %s.\n",
                           mode_to_string(rec), mode_to_string(r.mode));
            }
            report("dispatch", dispatch_consignment_to_route(cid, rid));
            break;
        }
        case 5:
            report("cancel dispatch", cancel_dispatch(prompt_int("  Consignment ID: ")));
            break;
        case 6:
            list_routes_with_utilization();
            break;
        case 0:
            return;
        default:
            puts("  Invalid choice.");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Module 3 menu                                                       */
/* ------------------------------------------------------------------ */
static void menu_yard(void)
{
    for (;;) {
        int choice;

        puts("\n--- Yard Bay Management ---");
        puts(" 1. Allocate best-fit bay for a consignment");
        puts(" 2. Vacate a bay");
        puts(" 3. Look up bay by consignment");
        puts(" 4. Show yard grid");
        puts(" 0. Back");
        choice = prompt_int("Choice: ");

        switch (choice) {
        case 1: {
            Consignment c;
            int id = prompt_int("  Consignment ID: ");
            if (find_consignment_by_id(id, &c) != MFYD_OK) {
                printf("  Consignment #%d not found.\n", id);
            } else {
                int rc = allocate_best_fit_bay(id, c.weight_tons, c.is_hazardous);
                if (rc > 0) {
                    YardBay b;
                    if (lookup_bay_by_consignment(id, &b) == MFYD_OK)
                        printf("  OK: consignment #%d staged in bay %s (cap %.0f t).\n",
                               id, b.label, b.max_weight_tons);
                } else {
                    report("bay allocation", rc);
                    if (rc == MFYD_ERR_CAPACITY)
                        puts("  (no free bay satisfies the hazmat and weight limits)");
                    if (rc == MFYD_ERR_STATE)
                        puts("  (only REGISTERED consignments can be staged)");
                }
            }
            break;
        }
        case 2:
            report("vacate bay", vacate_yard_bay(prompt_int("  Bay number (1-20): ")));
            break;
        case 3: {
            YardBay b;
            int id = prompt_int("  Consignment ID: ");
            if (lookup_bay_by_consignment(id, &b) == MFYD_OK)
                printf("  Consignment #%d is in bay %s (bay %d, %.2f t loaded).\n",
                       id, b.label, b.bay_id, b.current_load_tons);
            else
                printf("  Consignment #%d is not in any yard bay.\n", id);
            break;
        }
        case 4:
            print_yard_2d_grid();
            break;
        case 0:
            return;
        default:
            puts("  Invalid choice.");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Module 4 menu                                                       */
/* ------------------------------------------------------------------ */
static void show_audit_log(void)
{
    FILE *fp = fopen(AUDIT_FILE, "r");
    char line[256];

    if (fp == NULL) {
        puts("  Audit log is empty.");
        return;
    }
    while (fgets(line, sizeof line, fp) != NULL)
        printf("  %s", line);
    fclose(fp);
}

static void menu_billing(void)
{
    for (;;) {
        int choice;

        puts("\n--- Billing & Ledger ---");
        puts(" 1. Generate waybill invoice");
        puts(" 2. Print waybill slip (waybill_<id>.txt)");
        puts(" 3. Search invoice by consignment");
        puts(" 4. Daily financial ledger");
        puts(" 5. View audit log");
        puts(" 0. Back");
        choice = prompt_int("Choice: ");

        switch (choice) {
        case 1: {
            Waybill w;
            int id = prompt_int("  Consignment ID: ");
            int rc = generate_waybill_invoice(id, &w);
            if (rc == MFYD_OK)
                printf("  OK: invoice #%d issued, total $%.2f.\n", w.invoice_id, w.total_amount);
            else if (rc == MFYD_ERR_DUPLICATE)
                printf("  Already invoiced: invoice #%d, total $%.2f.\n", w.invoice_id, w.total_amount);
            else {
                report("invoice generation", rc);
                if (rc == MFYD_ERR_STATE)
                    puts("  (consignment must be dispatched before it can be billed)");
            }
            break;
        }
        case 2:
            print_waybill_slip(prompt_int("  Consignment ID: "));
            break;
        case 3: {
            Waybill w;
            int id = prompt_int("  Consignment ID: ");
            if (search_invoice_by_consignment(id, &w) == MFYD_OK) {
                printf("  Invoice #%d issued %s (route #%d)\n", w.invoice_id, w.issued_at, w.route_id);
                printf("    Base freight     : $%10.2f  (%.2f t x $%.2f)\n",
                       w.base_freight, w.weight_tons, w.rate_per_ton);
                printf("    Express surcharge: $%10.2f\n", w.priority_surcharge);
                printf("    Hazmat fee       : $%10.2f\n", w.hazmat_fee);
                printf("    TOTAL            : $%10.2f\n", w.total_amount);
            } else {
                printf("  No active invoice for consignment #%d.\n", id);
            }
            break;
        }
        case 4: {
            float total, avg;
            int haz;
            compute_daily_financial_ledger(&total, &avg, &haz);
            puts("  Daily financial ledger (today's active invoices)");
            printf("    Total revenue        : $%.2f\n", total);
            printf("    Average shipment cost: $%.2f\n", avg);
            printf("    Hazmat shipments     : %d\n", haz);
            break;
        }
        case 5:
            show_audit_log();
            break;
        case 0:
            return;
        default:
            puts("  Invalid choice.");
        }
    }
}

/* ------------------------------------------------------------------ */
int main(void)
{
    int rc = init_yard_layout_file();

    puts("=== Multimodal Freight & Freight Yard Dispatcher (MFYD) ===");
    if (rc != MFYD_OK)
        printf("Warning: yard layout could not be initialised (%s).\n", mfyd_strerror(rc));

    for (;;) {
        int choice;

        puts("\n=========== MAIN MENU ===========");
        puts(" 1. Consignment Intake & Lifecycle");
        puts(" 2. Capacity Dispatch & Routes");
        puts(" 3. Yard Bay Management");
        puts(" 4. Billing & Ledger");
        puts(" 0. Exit");
        choice = prompt_int("Choice: ");

        switch (choice) {
        case 1: menu_consignments(); break;
        case 2: menu_routes();       break;
        case 3: menu_yard();         break;
        case 4: menu_billing();      break;
        case 0: puts("Goodbye."); return EXIT_SUCCESS;
        default: puts("Invalid choice.");
        }
    }
}