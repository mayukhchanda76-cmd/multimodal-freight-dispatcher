/*
 * module3_yard.c - Module 3: Yard Bay Allocation & Constraint Checker
 *
 * yard.dat holds exactly YARD_BAY_COUNT fixed-size records, bay N at offset
 * (N - 1) * sizeof(YardBay).
 *   Bays 1-5   : hazmat-rated, 30 t   (labels H1..H5)
 *   Bays 6-20  : standard,     15 t   (labels G1..G15)
 */
#include <stdio.h>
#include <string.h>
#include "mfyd.h"

#define GRID_COLUMNS 5

/* Read the whole (small) yard file into bays[]. */
static int read_all_bays(YardBay bays[YARD_BAY_COUNT])
{
    FILE *fp = fopen(YARD_FILE, "rb");
    size_t n;

    if (fp == NULL)
        return MFYD_ERR_IO;
    n = fread(bays, sizeof(YardBay), YARD_BAY_COUNT, fp);
    fclose(fp);
    return n == YARD_BAY_COUNT ? MFYD_OK : MFYD_ERR_IO;
}

/* Overwrite one bay record in place. */
static int save_bay(const YardBay *b)
{
    FILE *fp = fopen(YARD_FILE, "rb+");
    long off = ((long)b->bay_id - 1) * (long)sizeof(YardBay);

    if (fp == NULL)
        return MFYD_ERR_IO;
    if (b->bay_id < 1 || b->bay_id > YARD_BAY_COUNT ||
        fseek(fp, off, SEEK_SET) != 0 ||
        fwrite(b, sizeof *b, 1, fp) != 1) {
        fclose(fp);
        return MFYD_ERR_IO;
    }
    return fclose(fp) == 0 ? MFYD_OK : MFYD_ERR_IO;
}

/*
 * Layout written by the standalone railway_yard.c prototype. It has no
 * `label` field, so its yard.dat is smaller than ours and was rejected as
 * "wrong layout". We convert it once instead of making the user start over.
 */
typedef struct {
    int   bay_id;
    int   is_hazardous_rated;
    float max_weight_tons;
    int   is_occupied;
    int   consignment_id;
    float current_weight;
} LegacyYardBay;

#define LEGACY_BACKUP_FILE YARD_FILE ".bak"

/* Rewrite a legacy yard.dat in the current layout, keeping a raw backup of
 * the original at yard.dat.bak. Occupied bays stay occupied. */
static int migrate_legacy_yard(void)
{
    LegacyYardBay old[YARD_BAY_COUNT];
    YardBay cur[YARD_BAY_COUNT];
    FILE *fp = fopen(YARD_FILE, "rb");
    int i;

    if (fp == NULL)
        return MFYD_ERR_IO;
    if (fread(old, sizeof(LegacyYardBay), YARD_BAY_COUNT, fp) != YARD_BAY_COUNT) {
        fclose(fp);
        return MFYD_ERR_IO;
    }
    fclose(fp);

    for (i = 0; i < YARD_BAY_COUNT; i++) {
        if (old[i].bay_id != i + 1)             /* not a recognisable yard file */
            return MFYD_ERR_IO;

        memset(&cur[i], 0, sizeof cur[i]);
        cur[i].bay_id            = old[i].bay_id;
        cur[i].is_hazmat_rated   = old[i].is_hazardous_rated ? 1 : 0;
        cur[i].max_weight_tons   = old[i].max_weight_tons;
        cur[i].occupied          = old[i].is_occupied ? 1 : 0;
        cur[i].consignment_id    = cur[i].occupied ? old[i].consignment_id : -1;
        cur[i].current_load_tons = cur[i].occupied ? old[i].current_weight : 0.0f;
        if (cur[i].is_hazmat_rated)
            snprintf(cur[i].label, sizeof cur[i].label, "H%d", i + 1);
        else
            snprintf(cur[i].label, sizeof cur[i].label, "G%d", i + 1 - HAZMAT_BAY_COUNT);
    }

    fp = fopen(LEGACY_BACKUP_FILE, "wb");       /* keep the original just in case */
    if (fp == NULL)
        return MFYD_ERR_IO;
    if (fwrite(old, sizeof(LegacyYardBay), YARD_BAY_COUNT, fp) != YARD_BAY_COUNT) {
        fclose(fp);
        return MFYD_ERR_IO;
    }
    if (fclose(fp) != 0)
        return MFYD_ERR_IO;

    fp = fopen(YARD_FILE, "wb");
    if (fp == NULL)
        return MFYD_ERR_IO;
    if (fwrite(cur, sizeof(YardBay), YARD_BAY_COUNT, fp) != YARD_BAY_COUNT) {
        fclose(fp);
        return MFYD_ERR_IO;
    }
    return fclose(fp) == 0 ? MFYD_OK : MFYD_ERR_IO;
}

int init_yard_layout_file(void)
{
    const long expected = (long)YARD_BAY_COUNT * (long)sizeof(YardBay);
    const long legacy_expected = (long)YARD_BAY_COUNT * (long)sizeof(LegacyYardBay);
    FILE *fp = fopen(YARD_FILE, "rb");
    int i;

    if (fp != NULL) {
        long size;
        if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0) {
            fclose(fp);
            return MFYD_ERR_IO;
        }
        fclose(fp);
        if (size == expected)
            return MFYD_OK;                 /* already seeded */
        if (size == legacy_expected)
            return migrate_legacy_yard();   /* file from railway_yard.c */
        if (size != 0)
            return MFYD_ERR_IO;             /* corrupt / wrong layout */
        /* size == 0: empty leftover, fall through and seed it */
    }

    fp = fopen(YARD_FILE, "wb");
    if (fp == NULL)
        return MFYD_ERR_IO;

    for (i = 1; i <= YARD_BAY_COUNT; i++) {
        YardBay b;
        memset(&b, 0, sizeof b);
        b.bay_id         = i;
        b.consignment_id = -1;
        if (i <= HAZMAT_BAY_COUNT) {
            b.is_hazmat_rated = 1;
            b.max_weight_tons = HAZMAT_BAY_CAPACITY_TONS;
            snprintf(b.label, sizeof b.label, "H%d", i);
        } else {
            b.is_hazmat_rated = 0;
            b.max_weight_tons = STANDARD_BAY_CAPACITY_TONS;
            snprintf(b.label, sizeof b.label, "G%d", i - HAZMAT_BAY_COUNT);
        }
        if (fwrite(&b, sizeof b, 1, fp) != 1) {
            fclose(fp);
            return MFYD_ERR_IO;
        }
    }
    return fclose(fp) == 0 ? MFYD_OK : MFYD_ERR_IO;
}

int lookup_bay_by_consignment(int consignment_id, YardBay *dest)
{
    YardBay bays[YARD_BAY_COUNT];
    int i, rc = read_all_bays(bays);

    if (rc != MFYD_OK)
        return rc;
    for (i = 0; i < YARD_BAY_COUNT; i++) {
        if (bays[i].occupied && bays[i].consignment_id == consignment_id) {
            if (dest != NULL)
                *dest = bays[i];
            return MFYD_OK;
        }
    }
    return MFYD_ERR_NOT_FOUND;
}

/*
 * Best-fit: among free bays that satisfy the constraints, choose the one
 * with the smallest max_weight_tons (ties -> lowest bay id), so small loads
 * don't burn big hazmat bays.
 *
 * Constraints:  hazardous cargo -> hazmat-rated bays only
 *               weight <= bay.max_weight_tons
 *
 * Safety-conservative: if the caller's flag/weight disagree with the stored
 * consignment, the stricter value (hazardous / heavier) is used.
 *
 * Returns the allocated bay_id (> 0), or a negative MfydResult code.
 */
int allocate_best_fit_bay(int consignment_id, float weight, int is_hazardous)
{
    YardBay bays[YARD_BAY_COUNT];
    YardBay existing;
    Consignment c;
    char details[48];
    float w;
    int hazardous, best = -1, i, rc;

    if (!(weight > 0.0f))
        return MFYD_ERR_INVALID;

    rc = init_yard_layout_file();
    if (rc != MFYD_OK)
        return rc;
    if (find_consignment_by_id(consignment_id, &c) != MFYD_OK)
        return MFYD_ERR_NOT_FOUND;
    if (c.status != STATUS_REGISTERED)          /* only fresh cargo can be staged */
        return MFYD_ERR_STATE;
    if (lookup_bay_by_consignment(consignment_id, &existing) == MFYD_OK)
        return MFYD_ERR_DUPLICATE;

    w         = weight > c.weight_tons ? weight : c.weight_tons;
    hazardous = (is_hazardous || c.is_hazardous) ? 1 : 0;
    if (!(w > 0.0f))
        return MFYD_ERR_INVALID;

    rc = read_all_bays(bays);
    if (rc != MFYD_OK)
        return rc;

    for (i = 0; i < YARD_BAY_COUNT; i++) {
        if (bays[i].occupied)
            continue;
        if (hazardous && !bays[i].is_hazmat_rated)
            continue;
        if (w > bays[i].max_weight_tons)
            continue;
        if (best < 0 || bays[i].max_weight_tons < bays[best].max_weight_tons)
            best = i;
    }
    if (best < 0)
        return MFYD_ERR_CAPACITY;

    bays[best].occupied          = 1;
    bays[best].consignment_id    = consignment_id;
    bays[best].current_load_tons = w;
    rc = save_bay(&bays[best]);
    if (rc != MFYD_OK)
        return rc;

    rc = update_consignment_status(consignment_id, STATUS_STAGED_IN_YARD);
    if (rc != MFYD_OK) {                        /* undo the bay write */
        bays[best].occupied          = 0;
        bays[best].consignment_id    = -1;
        bays[best].current_load_tons = 0.0f;
        save_bay(&bays[best]);
        return rc;
    }

    snprintf(details, sizeof details, "staged in bay %s", bays[best].label);
    log_audit_trail("STAGE", consignment_id, details);
    return bays[best].bay_id;
}

int vacate_yard_bay(int bay_id)
{
    YardBay bays[YARD_BAY_COUNT];
    YardBay prev;
    Consignment c;
    char details[64];
    int released, rc;

    if (bay_id < 1 || bay_id > YARD_BAY_COUNT)
        return MFYD_ERR_INVALID;
    rc = read_all_bays(bays);
    if (rc != MFYD_OK)
        return rc;

    if (!bays[bay_id - 1].occupied)
        return MFYD_ERR_STATE;                  /* already free */

    prev     = bays[bay_id - 1];
    released = prev.consignment_id;

    bays[bay_id - 1].occupied          = 0;
    bays[bay_id - 1].consignment_id    = -1;
    bays[bay_id - 1].current_load_tons = 0.0f;
    rc = save_bay(&bays[bay_id - 1]);
    if (rc != MFYD_OK)
        return rc;

    /*
     * Keep consignment status in sync with yard occupancy:
     * - Manual vacate while STAGED_IN_YARD -> revert to REGISTERED so the
     *   consignment is not stuck in a phantom staged state.
     * - Called during dispatch -> status is already DISPATCHED, leave untouched.
     */
    if (find_consignment_by_id(released, &c) == MFYD_OK &&
        c.status == STATUS_STAGED_IN_YARD) {
        c.status = STATUS_REGISTERED;
        rc = save_consignment(&c);
        if (rc != MFYD_OK) {                    /* roll back bay vacate */
            save_bay(&prev);
            return rc;
        }
    }

    snprintf(details, sizeof details, "released consignment #%d", released);
    log_audit_trail("VACATE", bay_id, details);
    return MFYD_OK;
}

void print_yard_2d_grid(void)
{
    YardBay bays[YARD_BAY_COUNT];
    int i, busy = 0;

    if (init_yard_layout_file() != MFYD_OK || read_all_bays(bays) != MFYD_OK) {
        puts("  Yard layout file is unavailable.");
        return;
    }

    puts("  Yard layout (H = hazmat-rated 30 t, G = general 15 t)");
    for (i = 0; i < YARD_BAY_COUNT; i++) {
        if (i % GRID_COLUMNS == 0)
            printf("  ");
        printf("[%s: %s] ", bays[i].label, bays[i].occupied ? "BUSY" : "FREE");
        if (bays[i].occupied)
            busy++;
        if (i % GRID_COLUMNS == GRID_COLUMNS - 1)
            putchar('\n');
    }
    if (YARD_BAY_COUNT % GRID_COLUMNS != 0)
        putchar('\n');
    printf("  Occupancy: %d / %d bays busy\n", busy, YARD_BAY_COUNT);

    for (i = 0; i < YARD_BAY_COUNT; i++) {
        if (bays[i].occupied)
            printf("    %-4s holds consignment #%d (%.2f t)\n", bays[i].label,
                   bays[i].consignment_id, bays[i].current_load_tons);
    }
}